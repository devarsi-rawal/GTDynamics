"""
 * GTDynamics Copyright 2020, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * See LICENSE for the license information
 *
 * @file  test_jr_values.py
 * @brief Unit test for jumping robot.
 * @author Yetong Zhang
"""

import inspect
import os.path as osp
import sys

currentdir = osp.dirname(osp.abspath(inspect.getfile(inspect.currentframe())))
parentdir = osp.dirname(currentdir)
sys.path.insert(0, parentdir)

import unittest

import gtdynamics as gtd
import gtsam
import numpy as np

from src.jr_values import JRValues
from src.jumping_robot import Actuator, JumpingRobot


class TestJRValues(unittest.TestCase):
    """ Tests for jumping robot. """

    def setUp(self):
        """ Set up the jumping robot. """
        self.yaml_file_path = osp.join(parentdir, "yaml", "robot_config.yaml")
        self.init_config = JumpingRobot.create_init_config()
        self.jr = JumpingRobot(self.yaml_file_path, self.init_config)

    def test_compute_mass_flow_convergence(self):
        """ Test computation of air mass flow, which should converge. """
        j = 1
        k = 0

        values = gtsam.Values()
        values.insert(Actuator.ValveOpenTimeKey(j), 0)
        values.insert(Actuator.ValveCloseTimeKey(j), 1)
        P_a_key = Actuator.PressureKey(j, k)
        P_s_key = Actuator.SourcePressureKey(k)
        t_key = gtd.TimeKey(k).key()
        values.insert(P_a_key, 101.325)
        values.insert(P_s_key, 65 * 6894.76/1000)
        values.insert(t_key, 0.0001)

        mdot, mdot_sigma = JRValues.compute_mass_flow(self.jr, values, j, k)


if __name__ == "__main__":
    unittest.main()
