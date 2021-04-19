/* ----------------------------------------------------------------------------
 * GTDynamics Copyright 2020, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * See LICENSE for the license information
 * -------------------------------------------------------------------------- */

/**
 * @file  testObjectiveFactors.cpp
 * @brief Test creation of objectives.
 * @author Frank Dellaert
 */

#include <CppUnitLite/TestHarness.h>
#include <gtdynamics/factors/ObjectiveFactors.h>
#include <gtdynamics/utils/values.h>
#include <gtsam/base/Vector.h>
#include <gtsam/geometry/Pose3.h>
#include <gtsam/nonlinear/NonlinearFactorGraph.h>
#include <gtsam/nonlinear/PriorFactor.h>

#include <iostream>

using gtsam::NonlinearFactorGraph;
using gtsam::Pose3;
using gtsam::noiseModel::Unit;

auto kModel = Unit::Create(6);

TEST(ObjectiveFactors, PoseAndTwist) {
  NonlinearFactorGraph graph;
  constexpr int id = 5, k = 777;
  gtdynamics::add_pose_objective(&graph, Pose3(), kModel, id, k);
  EXPECT_LONGS_EQUAL(1, graph.size());
  gtdynamics::add_link_objective(&graph, Pose3(), kModel, gtsam::Z_6x1, kModel,
                                 id, k);
  EXPECT_LONGS_EQUAL(3, graph.size());
}

TEST(ObjectiveFactors, TwistWithDerivatives) {
  NonlinearFactorGraph graph;
  constexpr int id = 5, k = 777;
  gtdynamics::add_twist_objective(&graph, gtsam::Z_6x1, kModel, gtsam::Z_6x1,
                                  kModel, id, k);
  EXPECT_LONGS_EQUAL(2, graph.size());
}

int main() {
  TestResult tr;
  return TestRegistry::runAllTests(tr);
}
