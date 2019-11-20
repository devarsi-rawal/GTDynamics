/**
 * @file JumpingRobotSimulation.cpp
 * @brief simulatie jumping robot (planner robot with 5 links), actuated on the
 * 2 shoulder joints and 2 elbow joints loop
 * @Author: Yetong Zhang
 */

#include <Simulation.h>
#include <UrdfLink.h>

#include <CppUnitLite/TestHarness.h>
#include <gtsam/base/Testable.h>
#include <gtsam/base/TestableAssertions.h>

#include <fstream>
#include <iostream>

using namespace std;
using namespace gtsam;
using namespace manipulator;

namespace example {

int num_links = 5;

// the robot is symmetrical, and all 5 links have the same length. The 1st link
// and 5th link having the same mass, 2nd link and 4th link have the same mass.
// Following is the parameters for the robot
double m1 = 0.31;
double m2 = 0.28;
double m3 = 0.54;
double link_radius = 0.02;
double l = 0.55;

Arm<UrdfLink> getRobot() {
  Vector link_length = Vector::Zero(num_links);
  Vector link_mass = Vector::Zero(num_links);
  link_length << l, l, l, l, l;
  link_mass << m1, m2, m3, m2, m1;

  // calculate the inertia from the equation I_xx = 1/2 * m * r^2
  // I_yy = I_zz = 1/12 * m * (l^2 + 3 * r^2)
  vector<gtsam::Matrix3> link_inertia;
  for (int i = 0; i < num_links; i++) {
    gtsam::Matrix3 inertia;
    double principal_inertia1 =
        1.0 / 2 * link_mass(i) * std::pow(link_radius, 2);
    double principal_inertia2 =
        1.0 / 12 * link_mass(i) *
        (3 * std::pow(link_radius, 2) + std::pow(link_length(i), 2));
    inertia << principal_inertia1, 0, 0, 0, principal_inertia2, 0, 0, 0,
        principal_inertia2;
    link_inertia.push_back(inertia);
  }

  // the zero configuration is set to:
  //     _
  //    | |
  //    | |
  // TTTTTTTTTT

  vector<UrdfLink> urdf_jump = {
      UrdfLink(Pose3(Rot3::Rz(M_PI / 2), Point3(link_length(2) / 2, 0, 0)),
               Vector3(0, 0, 1), 'R', link_mass(0),
               Pose3(Rot3(), Point3(link_length(0) / 2, 0, 0)), link_inertia[0],
               false, 0, 0),
      UrdfLink(Pose3(Rot3::Rz(0), Point3(link_length(0), 0, 0)),
               Vector3(0, 0, 1), 'R', link_mass(1),
               Pose3(Rot3(), Point3(link_length(1) / 2, 0, 0)), link_inertia[1],
               true, 0, 0),
      UrdfLink(Pose3(Rot3::Rz(M_PI / 2), Point3(link_length(1), 0, 0)),
               Vector3(0, 0, 1), 'R', link_mass(2),
               Pose3(Rot3(), Point3(link_length(2) / 2, 0, 0)), link_inertia[2],
               true, 0, 0),
      UrdfLink(Pose3(Rot3::Rz(M_PI / 2), Point3(link_length(2), 0, 0)),
               Vector3(0, 0, 1), 'R', link_mass(3),
               Pose3(Rot3(), Point3(link_length(3) / 2, 0, 0)), link_inertia[3],
               true, 0, 0),
      UrdfLink(Pose3(Rot3::Rz(0), Point3(link_length(3), 0, 0)),
               Vector3(0, 0, 1), 'R', link_mass(4),
               Pose3(Rot3(), Point3(link_length(4) / 2, 0, 0)), link_inertia[4],
               true, 0, 0)};

  Pose3 base = Pose3();
  Pose3 tool = Pose3(Rot3(), Point3(2, 0, 0));

  // get screw_axis for loop closure, expressed in base frame
  auto screw_axis =
      unit_twist(Vector3(0, 0, 1), Vector3(-link_length(4) / 2, 0, 0));
  return Arm<UrdfLink>(urdf_jump, base, tool, screw_axis, false, 0, 0);
}
auto robot = getRobot();
auto dof = robot.numLinks() + 1;
}  // namespace example

/* ************************************************************************/
// simulate the trajectory with constant torques at the 4 joints. Results will
// be stored into the folder "matlab/dataset/joint_angles"
TEST(Simulation, gravity_y) {
  double time_step = 0.001;
  int total_steps = 1200;
  double torque2 = -2;  // torque at elbow joint
  double torque3 = 2;   // torque at shoulder joint
  double theta = 70.0 / 180.0 * M_PI;

  Vector initialJointAngles = Vector::Zero(example::dof),
         initialJointVelocities = Vector::Zero(example::dof),
         known_torque = Vector::Zero(example::dof);

  Vector3 gravity = (Vector(3) << 0, -9.8, 0).finished();
  initialJointAngles << -theta, 2 * theta, -theta, -theta, 2 * theta, -theta;
  known_torque << 0, torque2, torque3, torque3, torque2, 0;

  Simulation<UrdfLink> FDsim(time_step, example::robot, gravity,
                             initialJointAngles, initialJointVelocities);
  vector<Vector> jointAngles, jointVelocities, jointAccelerations, jointTorques;
  jointAngles.assign(total_steps, Vector::Zero(example::dof));
  jointVelocities.assign(total_steps, Vector::Zero(example::dof));
  jointAccelerations.assign(total_steps, Vector::Zero(example::dof));
  jointTorques.assign(total_steps, Vector::Zero(example::dof));
  ofstream q, qVel, qAccel, qTorque;
  q.open("../../../matlab/dataset/joint_angles/q.txt");
  qVel.open("../../../matlab/dataset/joint_angles/qVel.txt");
  qAccel.open("../../../matlab/dataset/joint_angles/qAccel.txt");
  qTorque.open("../../../matlab/dataset/joint_angles/qTorque.txt");
  for (int i = 0; i < total_steps; ++i) {
    FDsim.integration(known_torque);
    jointAngles.push_back(FDsim.getJointAngles());
    jointVelocities.push_back(FDsim.getJointVelocities());
    jointAccelerations.push_back(FDsim.getJointAccelerations());
    jointTorques.push_back(FDsim.getJointTorques());
    q << std::setprecision(16) << FDsim.getJointAngles().transpose()
      << std::endl;
    qVel << std::setprecision(16) << FDsim.getJointVelocities().transpose()
         << std::endl;
    qAccel << std::setprecision(16) << FDsim.getJointAccelerations().transpose()
           << std::endl;
    qTorque << std::setprecision(16) << FDsim.getJointTorques().transpose()
            << std::endl;
  }
  q.close();
  qVel.close();
  qAccel.close();
  qTorque.close();
}

int main() {
  TestResult tr;
  return TestRegistry::runAllTests(tr);
}