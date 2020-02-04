/* ----------------------------------------------------------------------------
 * GTDynamics Copyright 2020, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * See LICENSE for the license information
 * -------------------------------------------------------------------------- */

/**
 * @file DynamicsGraphBuilder.h
 * @brief Builds a dynamics graph from a UniversalRobot object.
 * @author Yetong Zhang, Alejandro Escontrela
 */

#include "dynamics/DynamicsGraph.h"

#include <ContactDynamicsMomentFactor.h>
#include <ContactKinematicsAccelFactor.h>
#include <ContactKinematicsPoseFactor.h>
#include <ContactKinematicsTwistFactor.h>
#include <PoseFactor.h>
#include <TorqueFactor.h>
#include <TwistAccelFactor.h>
#include <TwistFactor.h>
#include <WrenchEquivalenceFactor.h>
#include <WrenchFactors.h>
#include <WrenchPlanarFactor.h>
#include <JsonSaver.h>

#include <gtsam/base/numericalDerivative.h>
#include <gtsam/nonlinear/DoglegOptimizer.h>
#include <gtsam/nonlinear/ExpressionFactorGraph.h>
#include <gtsam/nonlinear/GaussNewtonOptimizer.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>
#include <gtsam/nonlinear/expressions.h>
#include <gtsam/slam/PriorFactor.h>

#include <utils.h>

#include <iostream>
#include <vector>

using gtsam::Double_;
using gtsam::NonlinearFactorGraph, gtsam::GaussianFactorGraph;
using gtsam::Values;
using gtsam::Pose3;
using gtsam::PriorFactor;
using gtsam::Vector, gtsam::Vector6;
using gtsam::I_1x1, gtsam::I_6x6;

namespace robot {

gtsam::Matrix36 getPlanarJacobian(const gtsam::Vector3 &planar_axis)
{
    gtsam::Matrix36 H_wrench;
    if (planar_axis[0] == 1)
    { // x axis
        H_wrench << 0, 1, 0, 0, 0, 0,
            0, 0, 1, 0, 0, 0,
            0, 0, 0, 1, 0, 0;
    }
    else if (planar_axis[1] == 1)
    { // y axis
        H_wrench << 1, 0, 0, 0, 0, 0,
            0, 0, 1, 0, 0, 0,
            0, 0, 0, 0, 1, 0;
    }
    else if (planar_axis[2] == 1)
    { // z axis
        H_wrench << 1, 0, 0, 0, 0, 0,
            0, 1, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 1;
    }
    return H_wrench;
}

GaussianFactorGraph DynamicsGraphBuilder::linearDynamicsGraph(const UniversalRobot &robot, const int t,
                                                        const UniversalRobot::JointValues& joint_angles,
                                                        const UniversalRobot::JointValues& joint_vels,
                                                        const UniversalRobot::FKResults &fk_results,
                                                        const boost::optional<gtsam::Vector3> &gravity,
                                                        const boost::optional<gtsam::Vector3> &planar_axis)
{
    GaussianFactorGraph graph;
    auto poses = fk_results.first;
    auto twists = fk_results.second;
    for (RobotLinkSharedPtr link : robot.links())
    {
        int i = link->getID();
        if (link->isFixed())
        {
            // prior on twist acceleration for fixed link
            // A_i = 0
            Vector6 rhs = Vector6::Zero();
            graph.add(TwistAccelKey(i, t), I_6x6, rhs, gtsam::noiseModel::Constrained::All(6));
        }
        else
        {
            // wrench factor
            // G_i * A_i - F_i_j1 - .. - F_i_jn  = ad(V_i)^T * G_i * V*i + m_i * R_i^T * g
            const auto &connected_joints = link->getJoints();
            const gtsam::Matrix6 G_i = link->inertiaMatrix();
            const double m_i = link->mass();
            const Vector6 V_i = twists.at(link->name());
            const Pose3 T_wi = poses.at(link->name());
            Vector6 rhs = Pose3::adjointMap(V_i).transpose() * G_i * V_i;
            if (gravity)
            {
                Vector gravitational_force = T_wi.rotation().transpose() * (*gravity) * m_i;
                for (int i = 3; i < 6; ++i)
                {
                    rhs[i] += gravitational_force[i - 3];
                }
            }
            if (connected_joints.size() == 0)
            {
                graph.add(TwistAccelKey(i, t), G_i,
                          rhs, gtsam::noiseModel::Constrained::All(6));
            }
            else if (connected_joints.size() == 1)
            {
                graph.add(TwistAccelKey(i, t), G_i,
                          WrenchKey(i, connected_joints[0].lock()->getID(), t), -I_6x6,
                          rhs, gtsam::noiseModel::Constrained::All(6));
            }
            else if (connected_joints.size() == 2)
            {
                graph.add(TwistAccelKey(i, t), G_i,
                          WrenchKey(i, connected_joints[0].lock()->getID(), t), -I_6x6,
                          WrenchKey(i, connected_joints[1].lock()->getID(), t), -I_6x6,
                          rhs, gtsam::noiseModel::Constrained::All(6));
            }
        }
    }

    for (RobotJointSharedPtr joint : robot.joints())
    {
        RobotLinkSharedPtr link_1 = joint->parentLink().lock();
        RobotLinkSharedPtr link_2 = joint->childLink().lock();
        int i1 = link_1->getID();
        int i2 = link_2->getID();
        int j = joint->getID();

        const Pose3 T_wi1 = poses.at(link_1->name());
        const Pose3 T_wi2 = poses.at(link_2->name());
        const Pose3 T_i2i1 = T_wi2.inverse() * T_wi1;
        const Vector6 V_i2 = twists.at(link_2->name());
        const Vector6 S_i2_j = joint->screwAxis(link_2);
        const double v_j = joint_vels.at(joint->name());

        // twist acceleration factor
        // A_i2 - Ad(T_21) * A_i1 - S_i2_j * a_j = ad(V_i2) * S_i2_j * v_j
        Vector6 rhs_tw = Pose3::adjointMap(V_i2) * S_i2_j * v_j;
        graph.add(TwistAccelKey(i2, t), I_6x6,
                  TwistAccelKey(i1, t), -T_i2i1.AdjointMap(),
                  JointAccelKey(j, t), -S_i2_j,
                  rhs_tw, gtsam::noiseModel::Constrained::All(6));

        // torque factor
        // S_i_j^T * F_i_j - tau = 0
        gtsam::Vector1 rhs_torque = gtsam::Vector1::Zero();
        graph.add(WrenchKey(i2, j, t), joint->screwAxis(link_2).transpose(), TorqueKey(j, t), -I_1x1, rhs_torque, gtsam::noiseModel::Constrained::All(1));

        // wrench equivalence factor
        // F_i1_j + Ad(T_i2i1)^T F_i2_j = 0
        Vector6 rhs_weq = Vector6::Zero();
        graph.add(WrenchKey(i1, j, t), I_6x6, WrenchKey(i2, j, t), T_i2i1.AdjointMap().transpose(), rhs_weq, gtsam::noiseModel::Constrained::All(6));

        // wrench planar factor
        if (planar_axis)
        {
            gtsam::Matrix36 J_wrench = getPlanarJacobian(*planar_axis);
            graph.add(WrenchKey(i2, j, t), J_wrench, gtsam::Vector3::Zero(), gtsam::noiseModel::Constrained::All(3));
        }
    }
    return graph;
}

GaussianFactorGraph DynamicsGraphBuilder::linearFDPriors(const UniversalRobot &robot,
                                                         const int t, 
                                                         const UniversalRobot::JointValues& torques)
{
    GaussianFactorGraph graph;
    for (auto &&joint : robot.joints())
    {
        int j = joint->getID();
        double torque = torques.at(joint->name());
        gtsam::Vector1 rhs;
        rhs << torque;
        graph.add(TorqueKey(j, t), I_1x1, rhs, gtsam::noiseModel::Constrained::All(1));
    }
    return graph;
}

Values DynamicsGraphBuilder::linearSolveFD(const UniversalRobot &robot, const int t,
                                    const UniversalRobot::JointValues& joint_angles,
                                    const UniversalRobot::JointValues& joint_vels,
                                    const UniversalRobot::JointValues& torques,
                                    const UniversalRobot::FKResults &fk_results,
                                    const boost::optional<gtsam::Vector3> &gravity,
                                    const boost::optional<gtsam::Vector3> &planar_axis)
{
  // construct and solve linear graph
  GaussianFactorGraph graph = linearDynamicsGraph(robot, t, joint_angles, joint_vels, fk_results, gravity, planar_axis);
  GaussianFactorGraph priors = linearFDPriors(robot, t, torques);
  for (auto & factor: priors)
  {
      graph.add(factor);
  }
  gtsam::VectorValues results = graph.optimize();

  // arrange values
  Values values;
  for (RobotJointSharedPtr joint: robot.joints())
  {
    int j = joint -> getID();
    int i1 = joint->parentLink().lock()->getID();
    int i2 = joint->childLink().lock()->getID();
    std::string name = joint ->name();
    values.insert(JointAngleKey(j, t), joint_angles.at(name));
    values.insert(JointVelKey(j, t), joint_vels.at(name));
    values.insert(JointAccelKey(j, t), results.at(JointAccelKey(j, t))[0]);
    values.insert(TorqueKey(j, t), torques.at(name));
    values.insert(WrenchKey(i1, j, t), results.at(WrenchKey(i1, j, t)));
    values.insert(WrenchKey(i2, j, t), results.at(WrenchKey(i2, j, t)));
  }
  const auto& poses = fk_results.first;
  const auto& twists = fk_results.second;
  for (RobotLinkSharedPtr link: robot.links())
  {
    int i = link -> getID();
    std::string name = link -> name();
    values.insert(PoseKey(i, t), poses.at(name));
    values.insert(TwistKey(i, t), twists.at(name));
    values.insert(TwistAccelKey(i, t), results.at(TwistAccelKey(i, t)));
  }
  return values;
}



gtsam::NonlinearFactorGraph DynamicsGraphBuilder::qFactors(
    const UniversalRobot &robot, const int t) const {
  NonlinearFactorGraph graph;
  for (auto &&link : robot.links()) {
    int i = link->getID();
    if (link->isFixed()) {
      graph.add(PriorFactor<Pose3>(PoseKey(i, t), link->getFixedPose(),
                                   opt_.bp_cost_model));
    }
  }

  for (auto &&joint : robot.joints()) {
    const auto &link_1 = joint->parentLink().lock();
    const auto &link_2 = joint->childLink().lock();
    int i1 = link_1->getID();
    int i2 = link_2->getID();
    int j = joint->getID();
    // add pose factor
    graph.add(robot::PoseFactor(PoseKey(i1, t), PoseKey(i2, t),
                                      JointAngleKey(j, t), opt_.p_cost_model,
                                      joint->transformTo(link_2), joint->screwAxis(link_2)));
  }
  return graph;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::vFactors(
    const UniversalRobot &robot, const int t) const {
  NonlinearFactorGraph graph;
  for (auto &&link : robot.links()) {
    int i = link->getID();
    if (link->isFixed()) {
      graph.add(PriorFactor<Vector6>(TwistKey(i, t), Vector6::Zero(),
                                     opt_.bv_cost_model));
    }
  }

  for (auto &&joint : robot.joints()) {
    const auto &link_1 = joint->parentLink().lock();
    const auto &link_2 = joint->childLink().lock();
    int i1 = link_1->getID();
    int i2 = link_2->getID();
    int j = joint->getID();
    // add twist factor
    graph.add(robot::TwistFactor(TwistKey(i1, t), TwistKey(i2, t),
                                       JointAngleKey(j, t), JointVelKey(j, t),
                                       opt_.v_cost_model, joint->transformTo(link_2),
                                       joint->screwAxis(link_2)));
  }
  return graph;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::aFactors(
    const UniversalRobot &robot, const int t) const {
  NonlinearFactorGraph graph;
  for (auto &&link : robot.links()) {
    int i = link->getID();
    if (link->isFixed()) {
      graph.add(PriorFactor<Vector6>(TwistAccelKey(i, t), Vector6::Zero(),
                                     opt_.ba_cost_model));
    }
  }

  for (auto &&joint : robot.joints()) {
    const auto &link_1 = joint->parentLink().lock();
    const auto &link_2 = joint->childLink().lock();
    int i1 = link_1->getID();
    int i2 = link_2->getID();
    int j = joint->getID();
    // add twist acceleration factor
    graph.add(robot::TwistAccelFactor(
        TwistKey(i2, t), TwistAccelKey(i1, t), TwistAccelKey(i2, t),
        JointAngleKey(j, t), JointVelKey(j, t), JointAccelKey(j, t),
        opt_.a_cost_model, joint->transformTo(link_2), joint->screwAxis(link_2)));
  }
  return graph;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::dynamicsFactors(
    const UniversalRobot &robot, const int t,
    const boost::optional<gtsam::Vector3> &gravity,
    const boost::optional<gtsam::Vector3> &planar_axis) const {
  NonlinearFactorGraph graph;
  for (auto &&link : robot.links()) {
    int i = link->getID();
    if (!link->isFixed()) {
      const auto &connected_joints = link->getJoints();
      if (connected_joints.size() == 0) {
        graph.add(WrenchFactor0(TwistKey(i, t), TwistAccelKey(i, t),
                                PoseKey(i, t), opt_.fa_cost_model,
                                link->inertiaMatrix(), gravity));
      } else if (connected_joints.size() == 1) {
        graph.add(WrenchFactor1(TwistKey(i, t), TwistAccelKey(i, t),
                                WrenchKey(i, connected_joints[0].lock()->getID(), t),
                                PoseKey(i, t), opt_.fa_cost_model,
                                link->inertiaMatrix(), gravity));
      } else if (connected_joints.size() == 2) {
        graph.add(WrenchFactor2(TwistKey(i, t), TwistAccelKey(i, t),
                                WrenchKey(i, connected_joints[0].lock()->getID(), t),
                                WrenchKey(i, connected_joints[1].lock()->getID(), t),
                                PoseKey(i, t), opt_.fa_cost_model,
                                link->inertiaMatrix(), gravity));
      } else if (connected_joints.size() == 3) {
        graph.add(WrenchFactor3(TwistKey(i, t), TwistAccelKey(i, t),
                                WrenchKey(i, connected_joints[0].lock()->getID(), t),
                                WrenchKey(i, connected_joints[1].lock()->getID(), t),
                                WrenchKey(i, connected_joints[2].lock()->getID(), t),
                                PoseKey(i, t), opt_.fa_cost_model,
                                link->inertiaMatrix(), gravity));
      } else if (connected_joints.size() == 4) {
        graph.add(WrenchFactor4(TwistKey(i, t), TwistAccelKey(i, t),
                                WrenchKey(i, connected_joints[0].lock()->getID(), t),
                                WrenchKey(i, connected_joints[1].lock()->getID(), t),
                                WrenchKey(i, connected_joints[2].lock()->getID(), t),
                                WrenchKey(i, connected_joints[3].lock()->getID(), t),
                                PoseKey(i, t), opt_.fa_cost_model,
                                link->inertiaMatrix(), gravity));
      } else {
        throw std::runtime_error("Wrench factor not defined");
      }
    }
  }

  for (auto &&joint : robot.joints()) {
    const auto &link_1 = joint->parentLink().lock();
    const auto &link_2 = joint->childLink().lock();
    int i1 = link_1->getID();
    int i2 = link_2->getID();
    int j = joint->getID();
    // add wrench equivalence factor
    // if (!link_1->isFixed() && !link_2->isFixed())
    // {
    graph.add(WrenchEquivalenceFactor(WrenchKey(i1, j, t), WrenchKey(i2, j, t),
                                      JointAngleKey(j, t), opt_.f_cost_model,
                                      joint->transformTo(link_2), joint->screwAxis(link_2)));
    // }

    // add torque factor
    graph.add(robot::TorqueFactor(WrenchKey(i2, j, t), TorqueKey(j, t),
                                        opt_.t_cost_model, joint->screwAxis(link_2)));

    // add planar wrench factor
    if (planar_axis) {
      graph.add(WrenchPlanarFactor(WrenchKey(i2, j, t), opt_.planar_cost_model,
                                   *planar_axis));
    }
  }
  return graph;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::dynamicsFactorGraph(
    const UniversalRobot &robot, const int t,
    const boost::optional<gtsam::Vector3> &gravity,
    const boost::optional<gtsam::Vector3> &planar_axis,
    const boost::optional<std::vector<uint>> &contacts) const {
  NonlinearFactorGraph graph;
  graph.add(qFactors(robot, t));
  graph.add(vFactors(robot, t));
  graph.add(aFactors(robot, t));
  graph.add(dynamicsFactors(robot, t, gravity, planar_axis));
  return graph;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::trajectoryFG(
    const UniversalRobot &robot, const int num_steps, const double dt,
    const DynamicsGraphBuilder::CollocationScheme collocation,
    const boost::optional<gtsam::Vector3> &gravity,
    const boost::optional<gtsam::Vector3> &planar_axis) const {
  NonlinearFactorGraph graph;
  for (int t = 0; t < num_steps + 1; t++) {
    graph.add(dynamicsFactorGraph(robot, t, gravity, planar_axis));
    if (t < num_steps) {
      graph.add(collocationFactors(robot, t, dt, collocation));
    }
  }
  return graph;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::multiPhaseTrajectoryFG(
    const std::vector<UniversalRobot> &robots,
    const std::vector<int> &phase_steps,
    const std::vector<gtsam::NonlinearFactorGraph> &transition_graphs,
    const CollocationScheme collocation,
    const boost::optional<gtsam::Vector3> &gravity,
    const boost::optional<gtsam::Vector3> &planar_axis) const {
  NonlinearFactorGraph graph;
  int num_phases = robots.size();

  // add dynamcis for each step
  int t = 0;
  graph.add(dynamicsFactorGraph(robots[0], t, gravity, planar_axis));

  for (int phase = 0; phase < num_phases; phase++) {
    // in-phase
    for (int phase_step = 0; phase_step < phase_steps[phase] - 1;
         phase_step++) {
      graph.add(dynamicsFactorGraph(robots[phase], ++t, gravity, planar_axis));
    }
    // transition
    if (phase == num_phases - 1) {
      graph.add(dynamicsFactorGraph(robots[phase], ++t, gravity, planar_axis));
    } else {
      t++;
      graph.add(transition_graphs[phase]);
    }
  }

  // add collocation factors
  t = 0;
  for (int phase = 0; phase < num_phases; phase++) {
    for (int phase_step = 0; phase_step < phase_steps[phase]; phase_step++) {
      graph.add(
          multiPhaseCollocationFactors(robots[phase], t++, phase, collocation));
    }
  }
  return graph;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::collocationFactors(
    const UniversalRobot &robot, const int t, const double dt,
    const CollocationScheme collocation) const {
  gtsam::ExpressionFactorGraph graph;
  for (auto &&joint : robot.joints()) {
    int j = joint->getID();
    Double_ q0_expr = Double_(JointAngleKey(j, t));
    Double_ q1_expr = Double_(JointAngleKey(j, t + 1));
    Double_ v0_expr = Double_(JointVelKey(j, t));
    Double_ v1_expr = Double_(JointVelKey(j, t + 1));
    Double_ a0_expr = Double_(JointAccelKey(j, t));
    Double_ a1_expr = Double_(JointAccelKey(j, t + 1));
    switch (collocation) {
      case CollocationScheme::Euler:
        graph.addExpressionFactor(q0_expr + dt * v0_expr - q1_expr, 0.0,
                                  opt_.q_col_cost_model);
        graph.addExpressionFactor(v0_expr + dt * a0_expr - v1_expr, 0.0,
                                  opt_.v_col_cost_model);
        break;
      case CollocationScheme::Trapezoidal:
        graph.addExpressionFactor(
            q0_expr + 0.5 * dt * v0_expr + 0.5 * dt * v1_expr - q1_expr,
            0.0, opt_.q_col_cost_model);
        graph.addExpressionFactor(
            v0_expr + 0.5 * dt * a0_expr + 0.5 * dt * a1_expr - v1_expr,
            0.0, opt_.v_col_cost_model);
        break;
      default:
        throw std::runtime_error(
            "runge-kutta and hermite-simpson not implemented yet");
        break;
    }
  }
  NonlinearFactorGraph nonlinear_graph;
  nonlinear_graph.add(graph);
  return nonlinear_graph;
}

// the * operator for doubles in expression factor does not work well yet
double multDouble(const double &d1, const double &d2,
                  gtsam::OptionalJacobian<1, 1> H1,
                  gtsam::OptionalJacobian<1, 1> H2) {
  if (H1) *H1 = gtsam::I_1x1 * d2;
  if (H2) *H2 = gtsam::I_1x1 * d1;
  return d1 * d2;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::multiPhaseCollocationFactors(
    const UniversalRobot &robot, const int t, const int phase,
    const CollocationScheme collocation) const {
  gtsam::ExpressionFactorGraph graph;
  Double_ phase_expr = Double_(PhaseKey(phase));
  for (auto &&joint : robot.joints()) {
    int j = joint->getID();
    Double_ q0_expr = Double_(JointAngleKey(j, t));
    Double_ q1_expr = Double_(JointAngleKey(j, t + 1));
    Double_ v0_expr = Double_(JointVelKey(j, t));
    Double_ v1_expr = Double_(JointVelKey(j, t + 1));
    Double_ a0_expr = Double_(JointAccelKey(j, t));
    Double_ a1_expr = Double_(JointAccelKey(j, t + 1));

    if (collocation == CollocationScheme::Euler) {
      Double_ v0dt(multDouble, phase_expr, v0_expr);
      Double_ a0dt(multDouble, phase_expr, a0_expr);
      graph.addExpressionFactor(q0_expr + v0dt - q1_expr, 0.0,
                                opt_.q_col_cost_model);
      graph.addExpressionFactor(v0_expr + a0dt - v1_expr, 0.0,
                                opt_.v_col_cost_model);
    } else if (collocation == CollocationScheme::Trapezoidal) {
      Double_ v0dt(multDouble, phase_expr, v0_expr);
      Double_ a0dt(multDouble, phase_expr, a0_expr);
      Double_ v1dt(multDouble, phase_expr, v1_expr);
      Double_ a1dt(multDouble, phase_expr, a1_expr);
      graph.addExpressionFactor(q0_expr + 0.5 * v0dt + 0.5 * v1dt - q1_expr,
                                0.0, opt_.q_col_cost_model);
      graph.addExpressionFactor(v0_expr + 0.5 * a0dt + 0.5 * a1dt - v1_expr,
                                0.0, opt_.v_col_cost_model);
    } else {
      throw std::runtime_error(
          "runge-kutta and hermite-simpson not implemented yet");
    }
  }
  NonlinearFactorGraph nonlinear_graph;
  nonlinear_graph.add(graph);
  return nonlinear_graph;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::forwardDynamicsPriors(
    const UniversalRobot &robot, const int t, const gtsam::Vector &joint_angles,
    const gtsam::Vector &joint_vels, const gtsam::Vector &torques) const {
  gtsam::NonlinearFactorGraph graph;
  auto joints = robot.joints();
  for (int idx = 0; idx < robot.numJoints(); idx++) {
    auto joint = joints[idx];
    int j = joint->getID();
    graph.add(
        gtsam::PriorFactor<double>(JointAngleKey(j, t), joint_angles[idx],
                                   opt_.prior_q_cost_model));
    graph.add(
        gtsam::PriorFactor<double>(JointVelKey(j, t), joint_vels[idx],
                                   opt_.prior_qv_cost_model));
    graph.add(gtsam::PriorFactor<double>(
        TorqueKey(j, t), torques[idx], opt_.prior_t_cost_model));
  }
  return graph;
}

gtsam::NonlinearFactorGraph DynamicsGraphBuilder::trajectoryFDPriors(
    const UniversalRobot &robot, const int num_steps,
    const gtsam::Vector &joint_angles, const gtsam::Vector &joint_vels,
    const std::vector<gtsam::Vector> &torques_seq) const {
  gtsam::NonlinearFactorGraph graph;
  auto joints = robot.joints();
  for (int idx = 0; idx < robot.numJoints(); idx++) {
    int j = joints[idx]->getID();
    graph.add(
        gtsam::PriorFactor<double>(JointAngleKey(j, 0), joint_angles[idx],
                                   opt_.prior_q_cost_model));
    graph.add(
        gtsam::PriorFactor<double>(JointVelKey(j, 0), joint_vels[idx],
                                   opt_.prior_qv_cost_model));
  }
  for (int t = 0; t <= num_steps; t++) {
    for (int idx = 0; idx < robot.numJoints(); idx++) {
      int j = joints[idx]->getID();
      graph.add(
          gtsam::PriorFactor<double>(TorqueKey(j, t), torques_seq[t][idx],
                                     opt_.prior_t_cost_model));
    }
  }

  return graph;
}

gtsam::Vector DynamicsGraphBuilder::jointAccels(const UniversalRobot &robot,
                                                const gtsam::Values &result,
                                                const int t) {
  gtsam::Vector joint_accels = gtsam::Vector::Zero(robot.numJoints());
  auto joints = robot.joints();
  for (int idx = 0; idx < robot.numJoints(); idx++) {
    auto joint = joints[idx];
    int j = joint->getID();
    joint_accels[idx] = result.atDouble(JointAccelKey(j, t));
  }
  return joint_accels;
}

gtsam::Vector DynamicsGraphBuilder::jointVels(const UniversalRobot &robot,
                                              const gtsam::Values &result,
                                              const int t) {
  gtsam::Vector joint_vels = gtsam::Vector::Zero(robot.numJoints());
  auto joints = robot.joints();
  for (int idx = 0; idx < robot.numJoints(); idx++) {
    auto joint = joints[idx];
    int j = joint->getID();
    joint_vels[idx] = result.atDouble(JointVelKey(j, t));
  }
  return joint_vels;
}

gtsam::Vector DynamicsGraphBuilder::jointAngles(const UniversalRobot &robot,
                                                const gtsam::Values &result,
                                                const int t) {
  gtsam::Vector joint_angles = gtsam::Vector::Zero(robot.numJoints());
  auto joints = robot.joints();
  for (int idx = 0; idx < robot.numJoints(); idx++) {
    auto joint = joints[idx];
    int j = joint->getID();
    joint_angles[idx] = result.atDouble(JointAngleKey(j, t));
  }
  return joint_angles;
}

gtsam::Vector DynamicsGraphBuilder::jointTorques(const UniversalRobot &robot,
                                                const gtsam::Values &result,
                                                const int t) {
  gtsam::Vector joint_torques = gtsam::Vector::Zero(robot.numJoints());
  auto joints = robot.joints();
  for (int idx = 0; idx < robot.numJoints(); idx++) {
    auto joint = joints[idx];
    int j = joint->getID();
    joint_torques[idx] = result.atDouble(TorqueKey(j, t));
  }
  return joint_torques;
}

UniversalRobot::JointValues DynamicsGraphBuilder::jointAccelsMap(const UniversalRobot &robot,
                                                const gtsam::Values &result,
                                                const int t) {
  UniversalRobot::JointValues joint_accels;

  for (RobotJointSharedPtr joint: robot.joints()) {
    int j = joint->getID();
    std::string name = joint->name();
    joint_accels[name] = result.atDouble(JointAccelKey(j, t));
  }
  return joint_accels;
}


gtsam::Values DynamicsGraphBuilder::zeroValues(const UniversalRobot &robot,
                                               const int t) {
  gtsam::Vector zero_twists = gtsam::Vector6::Zero(),
                zero_accels = gtsam::Vector6::Zero(),
                zero_wrenches = gtsam::Vector6::Zero(),
                zero_torque = gtsam::Vector1::Zero(),
                zero_q = gtsam::Vector1::Zero(),
                zero_v = gtsam::Vector1::Zero(),
                zero_a = gtsam::Vector1::Zero();
  gtsam::Values zero_values;
  for (auto &link : robot.links()) {
    int i = link->getID();
    zero_values.insert(PoseKey(i, t), link->Twcom());
    zero_values.insert(TwistKey(i, t), zero_twists);
    zero_values.insert(TwistAccelKey(i, t), zero_accels);
  }
  for (auto &joint : robot.joints()) {
    int j = joint->getID();
    auto parent_link = joint->parentLink().lock();
    auto child_link = joint->childLink().lock();
    zero_values.insert(WrenchKey(parent_link->getID(), j, t), zero_wrenches);
    zero_values.insert(WrenchKey(child_link->getID(), j, t), zero_wrenches);
    zero_values.insert(TorqueKey(j, t), zero_torque[0]);
    zero_values.insert(JointAngleKey(j, t), zero_q[0]);
    zero_values.insert(JointVelKey(j, t), zero_v[0]);
    zero_values.insert(JointAccelKey(j, t), zero_a[0]);
  }
  return zero_values;
}

gtsam::Values DynamicsGraphBuilder::zeroValuesTrajectory(
    const UniversalRobot &robot, const int num_steps, const int num_phases) {
  gtsam::Values zero_values;
  for (int t = 0; t <= num_steps; t++) {
    zero_values.insert(zeroValues(robot, t));
  }
  if (num_phases > 0) {
    for (int phase = 0; phase <= num_phases; phase++) {
      zero_values.insert(PhaseKey(phase), 0.0);
    }
  }
  return zero_values;
}

gtsam::Values DynamicsGraphBuilder::optimize(
    const gtsam::NonlinearFactorGraph &graph, const gtsam::Values &init_values,
    OptimizerType optim_type) {
  if (optim_type == OptimizerType::GaussNewton) {
    gtsam::GaussNewtonOptimizer optimizer(graph, init_values);
    optimizer.optimize();
    return optimizer.values();
  } else if (optim_type == OptimizerType::LM) {
    gtsam::LevenbergMarquardtOptimizer optimizer(graph, init_values);
    optimizer.optimize();
    return optimizer.values();
  } else if (optim_type == OptimizerType::PDL) {
    gtsam::DoglegOptimizer optimizer(graph, init_values);
    optimizer.optimize();
    return optimizer.values();
  } else {
    throw std::runtime_error("optimizer not implemented yet");
  }
}

void print_key(const gtsam::Key &key) {
  auto symb = gtsam::LabeledSymbol(key);
  char ch = symb.chr();
  int index = symb.label();
  int t = symb.index();
  if (ch == 'F') {
    std::cout << ch << int(index / 16) << index % 16 << "_" << t;
  } else if (ch == 't') {
    if (index == 0) {  // phase key
      std::cout << "dt" << t;
    } else if (index == 1) {  // time key
      std::cout << "t" << t;
    } else {  // time to open valve
      std::cout << "ti" << t;
    }
  } else {
    std::cout << ch << index << "_" << t;
  }
  std::cout << "\t";
}

// print the factors of the factor graph
void DynamicsGraphBuilder::print_values(const gtsam::Values &values) {
  for (auto &key : values.keys()) {
    print_key(key);
    std::cout << "\n";
    values.at(key).print();
    std::cout << "\n";
  }
}

// print the factors of the factor graph
void DynamicsGraphBuilder::print_graph(
    const gtsam::NonlinearFactorGraph &graph) {
  for (auto &factor : graph) {
    for (auto &key : factor->keys()) {
      print_key(key);
    }
    std::cout << "\n";
  }
}

// using radial location to locate the variables
gtsam::Vector3 radial_location(double r, double i, int n)
{
    double theta = M_PI * 2 / n * i;
    double x = r * cos(theta);
    double y = r * sin(theta);
    return (gtsam::Vector(3) << x, y, 0).finished();
}

// using radial location to locate the variables
gtsam::Vector3 corner_location(double r, double j, int n)
{
    double theta = M_PI * 2 / n * (j + 0.5);
    double x = r * cos(theta);
    double y = r * sin(theta);
    return (gtsam::Vector(3) << x, y, 0).finished();
}

gtsam::JsonSaver::LocationType get_locations(const UniversalRobot &robot, const int t,
                                             bool radial)
{
    gtsam::JsonSaver::LocationType locations;

    if (radial)
    {
        int n = robot.numLinks();
        for (auto &link : robot.links())
        {
            int i = link->getID();
            locations[PoseKey(i, t)] = radial_location(2, i, n);
            locations[TwistKey(i, t)] = radial_location(3, i, n);
            locations[TwistAccelKey(i, t)] = radial_location(4, i, n);
        }

        for (auto &joint : robot.joints())
        {
            int j = joint->getID();
            locations[JointAngleKey(j, t)] = corner_location(2.5, j, n);
            locations[JointVelKey(j, t)] = corner_location(3.5, j, n);
            locations[JointAccelKey(j, t)] = corner_location(4.5, j, n);
            locations[TorqueKey(j, t)] = corner_location(6, j, n);
            int i1 = joint->parentLink().lock()->getID();
            int i2 = joint->childLink().lock()->getID();
            locations[WrenchKey(i1, j, t)] = corner_location(5.5, j - 0.25, n);
            locations[WrenchKey(i2, j, t)] = corner_location(5.5, j + 0.25, n);
        }
    }
    else
    {
        for (auto &link : robot.links())
        {
            int i = link->getID();
            locations[PoseKey(i, t)] = (gtsam::Vector(3) << i, 0, 0).finished();
            locations[TwistKey(i, t)] = (gtsam::Vector(3) << i, 1, 0).finished();
            locations[TwistAccelKey(i, t)] =
                (gtsam::Vector(3) << i, 2, 0).finished();
        }

        for (auto &joint : robot.joints())
        {
            int j = joint->getID();
            locations[JointAngleKey(j, t)] =
                (gtsam::Vector(3) << j + 0.5, 0.5, 0).finished();
            locations[JointVelKey(j, t)] =
                (gtsam::Vector(3) << j + 0.5, 1.5, 0).finished();
            locations[JointAccelKey(j, t)] =
                (gtsam::Vector(3) << j + 0.5, 2.5, 0).finished();
            int i1 = joint->parentLink().lock()->getID();
            int i2 = joint->childLink().lock()->getID();
            locations[WrenchKey(i1, j, t)] =
                (gtsam::Vector(3) << j + 0.25, 3.5, 0).finished();
            locations[WrenchKey(i2, j, t)] =
                (gtsam::Vector(3) << j + 0.75, 3.5, 0).finished();
            locations[TorqueKey(j, t)] =
                (gtsam::Vector(3) << j + 0.5, 4.5, 0).finished();
        }
    }
    return locations;
}

void DynamicsGraphBuilder::saveGraph(const std::string &file_path,
                                     const gtsam::NonlinearFactorGraph &graph,
                                     const gtsam::Values &values,
                                     const UniversalRobot &robot, const int t,
                                     bool radial)
{
    std::ofstream json_file;
    json_file.open(file_path);
    gtsam::JsonSaver::LocationType locations = get_locations(robot, t, radial);
    gtsam::JsonSaver::SaveFactorGraph(graph, json_file, values, locations);
    json_file.close();
}

void DynamicsGraphBuilder::saveGraphMultiSteps(const std::string &file_path,
                                               const gtsam::NonlinearFactorGraph &graph,
                                               const gtsam::Values &values,
                                               const UniversalRobot &robot, const int num_steps,
                                               bool radial)
{
    std::ofstream json_file;
    json_file.open(file_path);
    gtsam::JsonSaver::LocationType locations;

    for (int t = 0; t <= num_steps; t++)
    {
        gtsam::JsonSaver::LocationType locations_t = get_locations(robot, t, radial);
        gtsam::Vector offset = (gtsam::Vector(3) << 20.0 * t, 0, 0).finished();
        for (auto it = locations_t.begin(); it != locations_t.end(); it++)
        {
            auto key = it->first;
            locations_t[key] = locations_t[key] + offset;
        }
        locations.insert(locations_t.begin(), locations_t.end());
    }

    gtsam::JsonSaver::SaveFactorGraph(graph, json_file, values, locations);
    json_file.close();
}

}  // namespace robot
