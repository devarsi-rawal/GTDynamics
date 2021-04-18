"""
 * GTDynamics Copyright 2020, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * See LICENSE for the license information
 *
 * @file  test_jr_simulator.py
 * @brief Unit test for jumping robot simulator.
 * @author Yetong Zhang
"""

import unittest
import gtsam
import gtdynamics as gtd
import numpy as np

import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)

from src.jumping_robot import Actuator, JumpingRobot
from src.jr_visualizer import visualize_jr
from src.robot_graph_builder import RobotGraphBuilder
from src.actuation_graph_builder import ActuationGraphBuilder
from src.jr_graph_builder import JRGraphBuilder
from src.jr_simulator import JRSimulator


class TestJRSimulator(unittest.TestCase):
    def setUp(self):
        """ Set up the simulator. """
        self.yaml_file_path = "examples/example_jumping_robot/yaml/robot_config.yaml"
        self.init_config = JumpingRobot.create_init_config()
        self.jr_simulator = JRSimulator(self.yaml_file_path, self.init_config)

    def cal_jr_accels(self, theta, torque_hip, torque_knee):
        """ Compute groundtruth joint accelerations from virtual work. """
        m1 = self.jr_simulator.jr.robot.link("shank_r").mass()
        m2 = self.jr_simulator.jr.robot.link("thigh_r").mass()
        m3 = self.jr_simulator.jr.robot.link("torso").mass()
        link_radius = self.jr_simulator.jr.params["morphology"]["r_cyl"]
        l_link = self.jr_simulator.jr.params["morphology"]["l_link"][0]

        g = 9.8
        moment = (0.5 * m1 + 1.5 * m2 + 1.0 * m3) * g * l_link * np.sin(theta)
        J1 = (l_link ** 2 + 3 * link_radius ** 2) * 1.0 / 12 * m1
        J2 = (l_link ** 2 + 3 * link_radius ** 2) * 1.0 / 12 * m2
        J = l_link ** 2 * (1.0 / 4 * m1 + (1.0 / 4 + 2 * np.sin(theta) ** 2) * m2 + 2 * np.sin(theta) ** 2 * m3)

        acc = (torque_hip - torque_knee * 2 -  moment) / (J + J1 + J2)
        expected_joint_accels = {"foot_r": acc, "knee_r": -2*acc, "hip_r": acc, "hip_l": acc, "knee_l": -2*acc, "foot_l": acc}
        return expected_joint_accels

    def test_robot_forward_dynamics(self):
        """ Test forward dynamics of robot frame: specify the angles,
            joint vels and torques, check the joint accelerations. """
        # specify joint angles, joint vels, torques
        theta = np.pi/3
        torque_hip = 0
        torque_knee = 0
        qs = [-theta, 2 * theta, -theta, -theta, 2*theta, -theta]
        vs = [0., 0., 0., 0., 0., 0.]
        torques = [0., torque_knee, torque_hip, torque_hip, torque_knee, 0.]

        # construct known values
        values = gtsam.Values()
        k = 0
        for joint in self.jr_simulator.jr.robot.joints():
            j = joint.id()
            gtd.InsertTorqueDouble(values, j, k, torques[j])
            gtd.InsertJointAngleDouble(values, j, k, qs[j])
            gtd.InsertJointVelDouble(values, j, k , vs[j])

        torso_pose = gtsam.Pose3(gtsam.Rot3(), gtsam.Point3(0, 0, 0.55))
        torso_i = self.jr_simulator.jr.robot.link("torso").id()
        gtd.InsertPose(values, torso_i, k, torso_pose)
        gtd.InsertTwist(values, torso_i, k, np.zeros(6))

        # step forward dynamics
        self.jr_simulator.step_robot_dynamics(k, values)

        # check joint accelerations
        joint_accels = gtd.DynamicsGraph.jointAccelsMap(self.jr_simulator.jr.robot, values, k)
        expected_joint_accels = self.cal_jr_accels(theta, torque_hip, torque_knee)
        for joint in self.jr_simulator.jr.robot.joints():
            name = joint.name()
            self.assertAlmostEqual(joint_accels[name], expected_joint_accels[name], places=7)

    def test_actuation_forward_dynamics(self):
        """ Test forward dynamics of actuator: specify mass, time, controls,
            check torques. """

        # create controls
        Tos = [0, 0, 0, 0]
        Tcs = [1, 1, 1, 1]
        P_s_0 = 65 * 6894.76
        controls = JumpingRobot.create_controls(Tos, Tcs, P_s_0)

        # create init values of known variables
        values = self.jr_simulator.init_config_values(controls)
        k = 0
        curr_time = 0.1

        # compute dynamics
        self.jr_simulator.step_actuation_dynamics(k, values, curr_time)

        torques = []
        for actuator in self.jr_simulator.jr.actuators:
            j = actuator.j
            torque = gtd.TorqueDouble(values, j, k)
            torques.append(torque)
            self.assertAlmostEqual(torque, 0, places=7)
        # TODO(yetong): check torques, pressures, etc


if __name__ == "__main__":
    unittest.main()
