"""
GTDynamics Copyright 2021, Georgia Tech Research Corporation,
Atlanta, Georgia 30332-0415
All Rights Reserved
See LICENSE for the license information

@file  cdpr_controller_ilqr.py
@brief Optimal controller for a cable robot.  Solved by creating a factor graph and adding state
objectives and control costs, then optimizing
@author Frank Dellaert
@author Gerry Chen
"""

import gtsam
import gtdynamics as gtd
import numpy as np
import utils
from cdpr_controller import CdprControllerBase

class CdprControllerIlqr(CdprControllerBase):
    """Precomputes the open-loop trajectory
    then just calls on that for each update.
    """
    def __init__(self, cdpr, x0, pdes=[], dt=0.01, Q=None, R=np.array([1.])):
        """constructor

        Args:
            cdpr (Cdpr): cable robot object
            x0 (gtsam.Values): initial state
            pdes (list, optional): list of desired poses. Defaults to [].
            dt (float, optional): time step duration. Defaults to 0.01.
            Q (np.ndarray, optional): State objective cost (as a vector). Defaults to None, which
            denotes a constrained noise model.
            R (np.ndarray, optional): Control cost (as a 1-vector). Defaults to np.array([1.]).
        """
        self.cdpr = cdpr
        self.pdes = pdes
        self.dt = dt

        # initial guess
        x_guess = utils.zerovalues(cdpr.ee_id(), ts=range(len(pdes)), dt=dt)
        # create iLQR graph
        fg = self.create_ilqr_fg(cdpr, x0, pdes, dt, Q, R)
        # optimize
        self.optimizer = gtsam.LevenbergMarquardtOptimizer(fg, x_guess)
        self.result = self.optimizer.optimize()
        self.fg = fg

    def update(self, values, t):
        """New control: returns the entire results vector, which contains the optimal open-loop
        control from the optimal trajectory.
        """        
        return self.result

    @staticmethod
    def create_ilqr_fg(cdpr, x0, pdes, dt, Q, R):
        """Creates the factor graph for the iLQR problem.  This essentially consists of creating a
        factor graph that describes the CDPR dynamics over all time steps, then adding state
        objective and control cost factors.

        Args:
            cdpr (Cdpr): cable robot object
            x0 (gtsam.Values): initial Pose/Twist of the cable robot
            pdes (List[gtsam.Pose3]): desired poses of the cdpr
            dt (float): time step duration
            Q (Union[np.ndarray, None]): The state objective cost (None for hard constraint)
            R (np.ndarray): The control cost

        Returns:
            gtsam.NonlinearFactorGraph: The factor graph corresponding to the iLQR problem.
        """
        N = len(pdes)
        # initial conditions
        fg = cdpr.priors_ik(ks=[0],
                            Ts=[gtd.Pose(x0, cdpr.ee_id(), 0)],
                            Vs=[gtd.Twist(x0, cdpr.ee_id(), 0)])
        # dynamics
        fg.push_back(cdpr.all_factors(N, dt))
        # control costs
        tmid = (cdpr.params.tmin + cdpr.params.tmax) / 2
        for k in range(N):
            for ji in range(4):
                fg.push_back(
                    gtd.PriorFactorDouble(gtd.internal.TorqueKey(ji, k).key(), tmid,
                                          gtsam.noiseModel.Diagonal.Precisions(R)))
        # state objective costs
        cost_x = gtsam.noiseModel.Constrained.All(6) if Q is None else \
            gtsam.noiseModel.Diagonal.Precisions(Q)
        for k in range(N):
            fg.push_back(
                gtsam.PriorFactorPose3(
                    gtd.internal.PoseKey(cdpr.ee_id(), k).key(), pdes[k], cost_x))
        return fg
