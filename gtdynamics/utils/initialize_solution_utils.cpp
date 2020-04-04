/* ----------------------------------------------------------------------------
 * GTDynamics Copyright 2020, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * See LICENSE for the license information
 * -------------------------------------------------------------------------- */

/**
 * @file  initialize_solution_utils.cpp
 * @brief Utility methods for initializing trajectory optimization solutions.
 * @Author: Alejandro Escontrela and Yetong Zhang
 */

#include "gtdynamics/utils/initialize_solution_utils.h"

#include <gtdynamics/dynamics/DynamicsGraph.h>
#include <gtdynamics/factors/MinTorqueFactor.h>
#include <gtdynamics/universal_robot/Robot.h>
#include <gtsam/base/Value.h>
#include <gtsam/base/Vector.h>
#include <gtsam/linear/Sampler.h>
#include <gtsam/nonlinear/GaussNewtonOptimizer.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>
#include <gtsam/nonlinear/NonlinearFactorGraph.h>

#include <string>
#include <utility>
#include <vector>

namespace gtdynamics {

/// Add zero-mean gaussian noise to a Pose.
inline gtsam::Pose3 addGaussianNoiseToPose(const gtsam::Pose3& T, double std,
                                           gtsam::Sampler sampler) {
  gtsam::Vector rand_vec = sampler.sample();
  gtsam::Point3 p = gtsam::Point3(T.translation().vector() + rand_vec.head(3));
  gtsam::Rot3 R = gtsam::Rot3::Expmap(gtsam::Rot3::Logmap(T.rotation()) +
                                      rand_vec.tail<3>());
  return gtsam::Pose3(R, p);
}

gtsam::Values InitializeSolutionInterpolation(
    const Robot& robot, const std::string& link_name, const gtsam::Pose3& wTl_i,
    const gtsam::Pose3& wTl_f, const double& T_s, const double& T_f,
    const double& dt, const double& gaussian_noise,
    const boost::optional<std::vector<ContactPoint>>& contact_points) {
  gtsam::Values init_vals;

  gtsam::noiseModel::Diagonal::shared_ptr sampler_noise_model =
      gtsam::noiseModel::Diagonal::Sigmas(
          gtsam::Vector6::Constant(6, gaussian_noise));
  gtsam::Sampler sampler = gtsam::Sampler(sampler_noise_model);

  // Initial and final discretized timesteps.
  int n_steps_init = static_cast<int>(std::round(T_s / dt));
  int n_steps_final = static_cast<int>(std::round(T_f / dt));

  gtsam::Point3 wPl_i = wTl_i.translation(), wPl_f = wTl_f.translation();
  gtsam::Rot3 wRl_i = wTl_i.rotation(), wRl_f = wTl_f.rotation();

  // Initialize joint angles and velocities to 0.
  gtdynamics::Robot::JointValues jangles, jvels;
  for (auto&& joint : robot.joints()) {
    jangles.insert(std::make_pair(joint->name(), sampler.sample()[0]));
    jvels.insert(std::make_pair(joint->name(), sampler.sample()[0]));
  }

  double t_elapsed = T_s;
  for (int t = n_steps_init; t <= n_steps_final; t++) {
    double s = (t_elapsed - T_s) / (T_f - T_s);

    // Compute interpolated pose for link.
    gtsam::Point3 wPl_t = (1 - s) * wPl_i + s * wPl_f;
    gtsam::Rot3 wRl_t = wRl_i.slerp(s, wRl_f);
    gtsam::Pose3 wTl_t = addGaussianNoiseToPose(gtsam::Pose3(wRl_t, wPl_t),
                                                gaussian_noise, sampler);

    // Compute forward dynamics to obtain remaining link poses.
    auto fk_results = robot.forwardKinematics(jangles, jvels, link_name, wTl_t);
    for (auto&& pose_result : fk_results.first)
      init_vals.insert(gtdynamics::PoseKey(
                           robot.getLinkByName(pose_result.first)->getID(), t),
                       pose_result.second);

    // Initialize link dynamics to 0.
    for (auto&& link : robot.links()) {
      init_vals.insert(gtdynamics::TwistKey(link->getID(), t),
                       sampler.sample());
      init_vals.insert(gtdynamics::TwistAccelKey(link->getID(), t),
                       sampler.sample());
    }

    // Initialize joint kinematics/dynamics to 0.
    for (auto&& joint : robot.joints()) {
      int j = joint->getID();
      init_vals.insert(
          gtdynamics::WrenchKey(joint->parentLink()->getID(), j, t),
          sampler.sample());
      init_vals.insert(gtdynamics::WrenchKey(joint->childLink()->getID(), j, t),
                       sampler.sample());
      init_vals.insert(gtdynamics::TorqueKey(j, t), sampler.sample()[0]);
      init_vals.insert(gtdynamics::JointAngleKey(j, t), sampler.sample()[0]);
      init_vals.insert(gtdynamics::JointVelKey(j, t), sampler.sample()[0]);
      init_vals.insert(gtdynamics::JointAccelKey(j, t), sampler.sample()[0]);
    }

    // Initialize contacts to 0.
    if (contact_points) {
      for (auto&& contact_point : *contact_points) {
        int link_id = -1;
        for (auto& link : robot.links()) {
          if (link->name() == contact_point.name) link_id = link->getID();
        }
        if (link_id == -1) throw std::runtime_error("Link not found.");
        init_vals.insert(
            gtdynamics::ContactWrenchKey(link_id, contact_point.contact_id, t),
            sampler.sample());
      }
    }
    t_elapsed += dt;
  }

  return init_vals;
}

gtsam::Values InitializeSolutionInterpolationMultiPhase(
    const Robot& robot, const std::string& link_name, const gtsam::Pose3& wTl_i,
    const std::vector<gtsam::Pose3>& wTl_t, const std::vector<double>& ts,
    const double& dt, const double& gaussian_noise,
    const boost::optional<std::vector<ContactPoint>>& contact_points) {
  gtsam::Values init_vals;
  gtsam::Pose3 pose = wTl_i;
  double curr_t = 0.0;
  for (size_t i = 0; i < wTl_t.size(); i++) {
    gtsam::Values phase_vals = InitializeSolutionInterpolation(
        robot, link_name, pose, wTl_t[i], curr_t, ts[i], dt, gaussian_noise,
        contact_points);
    for (auto&& key_value_pair : phase_vals)
      init_vals.tryInsert(key_value_pair.key, key_value_pair.value);
    pose = wTl_t[i];
    curr_t = ts[i];
  }
  return init_vals;
}

gtsam::Values InitializeSolutionInverseKinematics(
    const Robot& robot, const std::string& link_name, const gtsam::Pose3& wTl_i,
    const std::vector<gtsam::Pose3>& wTl_t, const std::vector<double>& ts,
    const double& dt, const double& gaussian_noise,
    const boost::optional<std::vector<ContactPoint>>& contact_points) {
  gtsam::Point3 wPl_i = wTl_i.translation();  // Initial translation.
  gtsam::Rot3 wRl_i = wTl_i.rotation();       // Initial rotation.
  double t_i = 0.0;                           // Time elapsed.

  gtsam::Vector3 gravity = (gtsam::Vector(3) << 0, 0, -9.8).finished();

  gtsam::noiseModel::Diagonal::shared_ptr sampler_noise_model =
      gtsam::noiseModel::Diagonal::Sigmas(
          gtsam::Vector6::Constant(6, gaussian_noise));
  gtsam::Sampler sampler = gtsam::Sampler(sampler_noise_model);

  // Linearly interpolated pose for link at each discretized timestep.
  std::vector<gtsam::Pose3> wTl_dt;
  for (size_t i = 0; i < ts.size(); i++) {
    gtsam::Point3 wPl_t = wTl_t[i].translation();  // des P.
    gtsam::Rot3 wRl_t = wTl_t[i].rotation();       // des R.
    double t_ti = t_i, t_t = ts[i];                // Initial and final times.

    for (int t = std::lround(t_i / dt); t < std::lround(t_t / dt); t++) {
      double s = (t_i - t_ti) / (t_t - t_ti);  // Normalized phase progress.

      // Compute interpolated pose for link.
      gtsam::Point3 wPl_s = (1 - s) * wPl_i + s * wPl_t;
      gtsam::Rot3 wRl_s = wRl_i.slerp(s, wRl_t);
      gtsam::Pose3 wTl_s = gtsam::Pose3(wRl_s, wPl_s);
      wTl_dt.push_back(wTl_s);
      t_i += dt;
    }

    wPl_i = wPl_t;
    wRl_i = wRl_t;
  }
  wTl_dt.push_back(wTl_t[wTl_t.size() - 1]);  // Add the final pose.

  gtsam::Pose3 wTl_i_processed;
  if (gaussian_noise > 0.0) {
    wTl_i_processed = addGaussianNoiseToPose(wTl_i, gaussian_noise, sampler);
    for (size_t i = 0; i < wTl_dt.size(); i++)
      wTl_dt[i] = addGaussianNoiseToPose(wTl_dt[i], gaussian_noise, sampler);
  } else {
    wTl_i_processed = wTl_i;
  }

  // Iteratively solve the inverse kinematics problem while statisfying
  // the contact pose constraint.
  gtsam::Values init_vals, init_vals_t;

  // Initial pose and joint angles are known a priori.
  gtdynamics::Robot::JointValues jangles, jvels;
  for (auto&& joint : robot.joints()) {
    jangles.insert(std::make_pair(joint->name(), sampler.sample()[0]));
    jvels.insert(std::make_pair(joint->name(), sampler.sample()[0]));
  }
  // Compute forward dynamics to obtain remaining link poses.
  auto fk_results =
      robot.forwardKinematics(jangles, jvels, link_name, wTl_i_processed);
  for (auto&& pose_result : fk_results.first)
    init_vals_t.insert(
        gtdynamics::PoseKey(robot.getLinkByName(pose_result.first)->getID(), 0),
        pose_result.second);
  for (auto&& joint : robot.joints())
    init_vals_t.insert(gtdynamics::JointAngleKey(joint->getID(), 0),
                       sampler.sample()[0]);

  auto dgb = gtdynamics::DynamicsGraph();
  for (int t = 0; t <= std::lround(ts[ts.size() - 1] / dt); t++) {
    gtsam::NonlinearFactorGraph kfg =
        dgb.qFactors(robot, t, gravity, contact_points);
    kfg.add(gtsam::PriorFactor<gtsam::Pose3>(
        gtdynamics::PoseKey(robot.getLinkByName(link_name)->getID(), t),
        wTl_dt[t], gtsam::noiseModel::Isotropic::Sigma(6, 0.001)));

    gtsam::LevenbergMarquardtOptimizer optimizer(kfg, init_vals_t);
    gtsam::Values results = optimizer.optimize();

    init_vals.insert(results);

    // Add zero initial values for remaining variables.
    for (auto&& link : robot.links()) {
      init_vals.insert(gtdynamics::TwistKey(link->getID(), t),
                       sampler.sample());
      init_vals.insert(gtdynamics::TwistAccelKey(link->getID(), t),
                       sampler.sample());
    }
    for (auto&& joint : robot.joints()) {
      int j = joint->getID();
      init_vals.insert(
          gtdynamics::WrenchKey(joint->parentLink()->getID(), j, t),
          sampler.sample());
      init_vals.insert(gtdynamics::WrenchKey(joint->childLink()->getID(), j, t),
                       sampler.sample());
      init_vals.insert(gtdynamics::TorqueKey(j, t), sampler.sample()[0]);
      init_vals.insert(gtdynamics::JointVelKey(j, t), sampler.sample()[0]);
      init_vals.insert(gtdynamics::JointAccelKey(j, t), sampler.sample()[0]);
    }
    if (contact_points) {
      for (auto&& contact_point : *contact_points) {
        int link_id = -1;
        for (auto& link : robot.links()) {
          if (link->name() == contact_point.name) link_id = link->getID();
        }
        if (link_id == -1) throw std::runtime_error("Link not found.");
        init_vals.insert(
            gtdynamics::ContactWrenchKey(link_id, contact_point.contact_id, t),
            sampler.sample());
      }
    }

    // Update initial values for next timestep.
    init_vals_t.clear();
    for (auto&& link : robot.links())
      init_vals_t.insert(
          gtdynamics::PoseKey(link->getID(), t + 1),
          results.at<gtsam::Pose3>(gtdynamics::PoseKey(link->getID(), t)));
    for (auto&& joint : robot.joints())
      init_vals_t.insert(
          gtdynamics::JointAngleKey(joint->getID(), t + 1),
          results.atDouble(gtdynamics::JointAngleKey(joint->getID(), t)));
  }

  return init_vals;
}

gtsam::Values ZeroValues(const Robot& robot, const int t,
                         const double& gaussian_noise,
                         const boost::optional<ContactPoints>& contact_points) {
  gtsam::Values zero_values;

  gtsam::noiseModel::Diagonal::shared_ptr sampler_noise_model =
      gtsam::noiseModel::Diagonal::Sigmas(
          gtsam::Vector6::Constant(6, gaussian_noise));
  gtsam::Sampler sampler = gtsam::Sampler(sampler_noise_model);

  for (auto& link : robot.links()) {
    int i = link->getID();
    zero_values.insert(
        gtdynamics::PoseKey(i, t),
        addGaussianNoiseToPose(link->wTcom(), gaussian_noise, sampler));
    zero_values.insert(gtdynamics::TwistKey(i, t), sampler.sample());
    zero_values.insert(gtdynamics::TwistAccelKey(i, t), sampler.sample());
  }
  for (auto& joint : robot.joints()) {
    int j = joint->getID();
    auto parent_link = joint->parentLink();
    auto child_link = joint->childLink();
    zero_values.insert(gtdynamics::WrenchKey(parent_link->getID(), j, t),
                       sampler.sample());
    zero_values.insert(gtdynamics::WrenchKey(child_link->getID(), j, t),
                       sampler.sample());
    zero_values.insert(gtdynamics::TorqueKey(j, t), sampler.sample()[0]);
    zero_values.insert(gtdynamics::JointAngleKey(j, t), sampler.sample()[0]);
    zero_values.insert(gtdynamics::JointVelKey(j, t), sampler.sample()[0]);
    zero_values.insert(gtdynamics::JointAccelKey(j, t), sampler.sample()[0]);
  }
  if (contact_points) {
    for (auto&& contact_point : *contact_points) {
      int link_id = -1;
      for (auto& link : robot.links()) {
        if (link->name() == contact_point.name) link_id = link->getID();
      }

      if (link_id == -1) throw std::runtime_error("Link not found.");

      zero_values.insert(
          gtdynamics::ContactWrenchKey(link_id, contact_point.contact_id, t),
          sampler.sample());
    }
  }

  return zero_values;
}

gtsam::Values ZeroValuesTrajectory(
    const Robot& robot, const int num_steps, const int num_phases,
    const double& gaussian_noise,
    const boost::optional<ContactPoints>& contact_points) {
  gtsam::Values z_values;
  for (int t = 0; t <= num_steps; t++) {
    z_values.insert(ZeroValues(robot, t, gaussian_noise, contact_points));
  }
  if (num_phases > 0) {
    for (int phase = 0; phase <= num_phases; phase++) {
      z_values.insert(gtdynamics::PhaseKey(phase), 0.0);
    }
  }
  return z_values;
}

}  // namespace gtdynamics
