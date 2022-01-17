/**
 * @file  testRoadMap.cpp
 * @brief test Roadmap
 * @author Frank Dellaert
 * @author Antoni Jubes
 */

#include <CppUnitLite/TestHarness.h>
#include <gtdynamics/pandarobot/roadmap/CanvasSampler.h>
#include <gtsam/base/Testable.h>
#include <gtsam/base/TestableAssertions.h>
#include <gtsam/geometry/Pose3.h>
#include <gtsam/nonlinear/Values.h>

#include <iostream>
#include <vector>

using namespace gtdynamics;
using namespace gtsam;
using gtsam::assert_equal;

TEST(CanvasSampler, uniformSample) {
  // Define points A, B, C being the vertices of the canvas
  // Choose them so that AB x AC has the same sign as your desired normal
  Point3 A, B, C;
  A << 0.5, 0, 0;
  B << 0, 0, 2;
  C << 0.5, 1, 0;

  // Create object
  CanvasSampler canvas(A, B, C);

  // Uniform check if it's the same
  size_t numACsamples = 4, numABsamples = 4;
  std::vector<Pose3> actual_poses =
      canvas.uniformSample(numABsamples, numACsamples);
  Rot3 bRcanvas(-0.5 / 2.061552, 0, -2 / 2.061552, 0, 1, 0, 2 / 2.061552, 0,
                -0.5 / 2.061552);
  std::vector<Pose3> expected_poses = {Pose3(bRcanvas, Point3(0.4, 0.2, 0.4)),
                                       Pose3(bRcanvas, Point3(0.4, 0.4, 0.4)),
                                       Pose3(bRcanvas, Point3(0.4, 0.6, 0.4)),
                                       Pose3(bRcanvas, Point3(0.4, 0.8, 0.4)),
                                       Pose3(bRcanvas, Point3(0.3, 0.2, 0.8)),
                                       Pose3(bRcanvas, Point3(0.3, 0.4, 0.8)),
                                       Pose3(bRcanvas, Point3(0.3, 0.6, 0.8)),
                                       Pose3(bRcanvas, Point3(0.3, 0.8, 0.8)),
                                       Pose3(bRcanvas, Point3(0.2, 0.2, 1.2)),
                                       Pose3(bRcanvas, Point3(0.2, 0.4, 1.2)),
                                       Pose3(bRcanvas, Point3(0.2, 0.6, 1.2)),
                                       Pose3(bRcanvas, Point3(0.2, 0.8, 1.2)),
                                       Pose3(bRcanvas, Point3(0.1, 0.2, 1.6)),
                                       Pose3(bRcanvas, Point3(0.1, 0.4, 1.6)),
                                       Pose3(bRcanvas, Point3(0.1, 0.6, 1.6)),
                                       Pose3(bRcanvas, Point3(0.1, 0.8, 1.6))};

  EXPECT(assert_equal(expected_poses, actual_poses, 1e-5))
}

TEST(CanvasSampler, uniformRelationships) {
  std::vector<std::vector<size_t>> actualrelationships =
      uniformRelationships(2, 2, 1);
  std::vector<std::vector<size_t>> expectedrelationships(4);
  expectedrelationships[0] = {0, 1, 2};
  expectedrelationships[1] = {0, 1, 4};
  expectedrelationships[2] = {0, 2, 4};
  expectedrelationships[3] = {1, 2, 4};
  for (size_t i = 0; i < 4; i++) {
    EXPECT(assert_equal(expectedrelationships[i], actualrelationships[i]))
  }

  // maybe put another example?
}

TEST(CanvasSampler, randomSample) {
  // Define points A, B, C being the vertices of the canvas
  // Choose them so that AB x AC has the same sign as your desired normal
  Point3 A, B, C;
  A << 0.5, 0, 0;
  B << 0, 0, 2;
  C << 0.5, 1, 0;

  // Create object
  CanvasSampler canvas(A, B, C);

  // Uniform check if it's the same
  size_t numsamples = 20;
  std::vector<Pose3> actual_poses = canvas.randomSample(numsamples);
  Rot3 bRcanvas(-0.5 / 2.061552, 0, -2 / 2.061552, 0, 1, 0, 2 / 2.061552, 0,
                -0.5 / 2.061552);

  EXPECT(assert_equal(20, actual_poses.size()))

  for (size_t i = 0; i < actual_poses.size(); ++i) {
    EXPECT(assert_equal(bRcanvas, actual_poses[i].rotation(), 1e-5))
    // Check if inside boundaries
    EXPECT((actual_poses[i].translation() - A).dot((B - A).normalized()) >= 0)
    EXPECT((actual_poses[i].translation() - A).dot((B - A).normalized()) <=
           (B - A).norm())
    EXPECT((actual_poses[i].translation() - A).dot((C - A).normalized()) >= 0)
    EXPECT((actual_poses[i].translation() - A).dot((C - A).normalized()) <=
           (C - A).norm())
  }
}

int main() {
  TestResult tr;
  return TestRegistry::runAllTests(tr);
}