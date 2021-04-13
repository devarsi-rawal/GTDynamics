"""
GTDynamics Copyright 2021, Georgia Tech Research Corporation,
Atlanta, Georgia 30332-0415
All Rights Reserved
See LICENSE for the license information

@file  test_cdpr_controller_ilqr.py
@brief Unit tests for CDPR.
@author Frank Dellaert
@author Gerry Chen
"""

import unittest

import gtdynamics as gtd
import gtsam
from gtsam import Pose3, Rot3
import numpy as np
from cdpr_planar import Cdpr
from cdpr_controller_ilqr import CdprControllerIlqr
from cdpr_planar_sim import CdprSimulator
from gtsam.utils.test_case import GtsamTestCase

class TestCdprControllerIlqr(GtsamTestCase):
    def testTrajFollow(self):
        """Tests trajectory tracking controller
        """
        cdpr = Cdpr()

        x0 = gtsam.Values()
        gtd.InsertPose(x0, cdpr.ee_id(), 0, Pose3(Rot3(), (1.5, 0, 1.5)))
        gtd.InsertTwist(x0, cdpr.ee_id(), 0, np.zeros(6))

        x_des = [Pose3(Rot3(), (1.5+k/20.0, 0, 1.5)) for k in range(9)]
        x_des = x_des[0:1] + x_des
        controller = CdprControllerIlqr(cdpr, x0=x0, pdes=x_des, dt=0.1)

        sim = CdprSimulator(cdpr, x0, controller, dt=0.1)
        result = sim.run(N=10)
        pAct = [gtd.Pose(result, cdpr.ee_id(), k) for k in range(10)]

        if False:
            print()
            for k, (des, act) in enumerate(zip(x_des, pAct)):
                print(('k: {:d}  --  des: {:.3f}, {:.3f}, {:.3f}  --  act: {:.3f}, {:.3f}, {:.3f}' +
                       '  --  u: {:.3e},   {:.3e},   {:.3e},   {:.3e}').format(
                           k, *des.translation(), *act.translation(),
                           *[gtd.TorqueDouble(result, ji, k) for ji in range(4)]))

        for k, (des, act) in enumerate(zip(x_des, pAct)):
            self.gtsamAssertEquals(des, act)

    def testGains(self):
        """Tests locally linear, time-varying feedback gains
        """
        cdpr = Cdpr()
        dt = 0.01

        x0 = gtsam.Values()
        gtd.InsertPose(x0, cdpr.ee_id(), 0, Pose3(Rot3(), (1.5, 0, 1.5)))
        gtd.InsertTwist(x0, cdpr.ee_id(), 0, np.zeros(6))
        x_des = [Pose3(Rot3(), (1.5, 0, 1.5)),
                 Pose3(Rot3(), (1.5, 0, 1.5)),
                 Pose3(Rot3(), (1.5, 0, 1.5))]  # don't move
        controller = CdprControllerIlqr(cdpr, x0=x0, pdes=x_des, dt=dt)
        print(gtd.str(controller.result))
        print(controller.fg.error(controller.result))
        actual_gains = controller.gains[0][0][gtd.internal.PoseKey(
            cdpr.ee_id(), 0).key()]  # time 0, cable 0, pose gain

        # notation: x_K_y means xstar = K * dy
        expected_1v_K_0x = np.diag([0, -1, 0, -1, 0, -1]) / dt  # v at t=1 in response to x at t=0
        expected_0c0_K_1v = np.array([0, 1e9, 0,
                                      -1 / np.sqrt(2), 0, -1 / np.sqrt(2)]).reshape((1, -1)) * \
                            cdpr.params.mass  # cable 0 tension at t=0 in response to v at t=1
        expected_gains = -expected_0c0_K_1v @ expected_1v_K_0x
        self.gtsamAssertEquals(actual_gains[:, 3:], expected_gains[:, 3:])

if __name__ == "__main__":
    unittest.main()
