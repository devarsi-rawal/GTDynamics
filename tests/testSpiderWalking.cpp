/* ----------------------------------------------------------------------------
 * GTDynamics Copyright 2020, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * See LICENSE for the license information
 * -------------------------------------------------------------------------- */

/**
 * @file  testSpiderWalking.cpp
 * @brief Test robot trajectory optimization with Phases.
 * @author: Alejandro Escontrela, Stephanie McCormick
 * @author: Disha Das, Tarushree Gandhi
 * @author: Frank Dellaert, Varun Agrawal, Stefanos Charalambous
 */

#include <CppUnitLite/TestHarness.h>
#include <gtdynamics/factors/ObjectiveFactors.h>
#include <gtdynamics/universal_robot/sdf.h>
#include <gtdynamics/utils/Trajectory.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>
#include <gtsam/nonlinear/NonlinearFactorGraph.h>

#define GROUND_HEIGHT -1.75  //-1.75

using gtsam::NonlinearFactorGraph;
using gtsam::Point3;
using gtsam::Pose3;
using gtsam::Rot3;
using gtsam::Values;
using gtsam::Vector3;
using gtsam::noiseModel::Isotropic;
using gtsam::noiseModel::Unit;
using std::string;
using std::vector;

using namespace gtdynamics;

// Returns a Trajectory object for a single robot walk cycle.
Trajectory getTrajectory(const Robot &robot, size_t repeat) {
  vector<string> odd_links{"tarsus_1", "tarsus_3", "tarsus_5", "tarsus_7"};
  vector<string> even_links{"tarsus_2", "tarsus_4", "tarsus_6", "tarsus_8"};
  auto links = odd_links;
  links.insert(links.end(), even_links.begin(), even_links.end());

  Phase stationary(robot, 1);
  stationary.addContactPoints(links, Point3(0, 0.19, 0), GROUND_HEIGHT);

  Phase odd(robot, 2);
  odd.addContactPoints(odd_links, Point3(0, 0.19, 0), GROUND_HEIGHT);

  Phase even(robot, 2);
  even.addContactPoints(even_links, Point3(0, 0.19, 0), GROUND_HEIGHT);

  WalkCycle walk_cycle;
  walk_cycle.addPhase(stationary);
  walk_cycle.addPhase(even);
  walk_cycle.addPhase(stationary);
  walk_cycle.addPhase(odd);

  Trajectory trajectory(walk_cycle, repeat);
  return trajectory;
}

TEST(testSpiderWalking, WholeEnchilada) {
  // Load Stephanie's robot robot (alt version, created by Tarushree/Disha).
  Robot robot =
      CreateRobotFromFile(SDF_PATH + "/test/spider_alt.sdf", "spider");

  double sigma_dynamics = 1e-5;    // std of dynamics constraints.
  double sigma_objectives = 1e-6;  // std of additional objectives.

  // Noise models.
  auto dynamics_model_6 = Isotropic::Sigma(6, sigma_dynamics),
       dynamics_model_1 = Isotropic::Sigma(1, sigma_dynamics),
       objectives_model_6 = Isotropic::Sigma(6, sigma_objectives),
       objectives_model_1 = Isotropic::Sigma(1, sigma_objectives);

  // Env parameters.
  Vector3 gravity(0, 0, -9.8);
  double mu = 1.0;

  OptimizerSetting opt(sigma_dynamics);
  DynamicsGraph graph_builder(opt, gravity);

  // Create the trajectory, consisting of 2 walk phases, each consisting of 4
  // phases: [stationary, odd, stationary, even].
  auto trajectory = getTrajectory(robot, 2);

  // Create multi-phase trajectory factor graph
  auto collocation = DynamicsGraph::CollocationScheme::Euler;
  auto graph = trajectory.multiPhaseFactorGraph(graph_builder, collocation, mu);
  EXPECT_LONGS_EQUAL(3583, graph.size());
  EXPECT_LONGS_EQUAL(3847, graph.keys().size());

  // Build the objective factors.
  NonlinearFactorGraph objectives = trajectory.contactLinkObjectives(
      Isotropic::Sigma(3, 1e-7), GROUND_HEIGHT);
  // Regression test on objective factors
  EXPECT_LONGS_EQUAL(104, objectives.size());
  EXPECT_LONGS_EQUAL(104, objectives.keys().size());

  // Get final time step.
  int K = trajectory.getEndTimeStep(trajectory.numPhases() - 1);

  // Add base goal objectives to the factor graph.
  auto base_link = robot.link("body");
  for (int k = 0; k <= K; k++) {
    add_link_objectives(&objectives, base_link->id(), k)
        .pose(Pose3(Rot3(), Point3(0, 0.0, 0.5)), Isotropic::Sigma(6, 5e-5))
        .twist(gtsam::Z_6x1, Isotropic::Sigma(6, 5e-5));
  }

  // Add link and joint boundary conditions to FG.
  trajectory.addBoundaryConditions(&objectives, robot, dynamics_model_6,
                                   dynamics_model_6, objectives_model_6,
                                   objectives_model_1, objectives_model_1);

  // Constrain all Phase keys to have duration of 1 /240.
  const double desired_dt = 1. / 240;
  trajectory.addIntegrationTimeFactors(&objectives, desired_dt, 1e-30);

  // Add min torque objectives.
  trajectory.addMinimumTorqueFactors(&objectives, robot, Unit::Create(1));

  // Add prior on hip joint angles (spider specific)
  auto prior_model = Isotropic::Sigma(1, 1.85e-4);
  for (auto &&joint : robot.joints())
    if (joint->name().find("hip2") == 0)
      for (int k = 0; k <= K; k++)
        add_joint_objectives(&objectives, joint->id(), k)
            .angle(2.5, prior_model);

  // Regression test on objective factors
  EXPECT_LONGS_EQUAL(918, objectives.size());
  EXPECT_LONGS_EQUAL(907, objectives.keys().size());

  // Add objective factors to the graph
  graph.add(objectives);
  EXPECT_LONGS_EQUAL(3583 + 918, graph.size());
  EXPECT_LONGS_EQUAL(3847, graph.keys().size());

  // Initialize solution.
  double gaussian_noise = 1e-5;
  Values init_vals =
      trajectory.multiPhaseInitialValues(gaussian_noise, desired_dt);
  EXPECT_LONGS_EQUAL(3847, init_vals.size());

  // Optimize!
  gtsam::LevenbergMarquardtOptimizer optimizer(graph, init_vals);
  auto results = optimizer.optimize();

  // TODO(frank): test whether it works
}

int main() {
  TestResult tr;
  return TestRegistry::runAllTests(tr);
}