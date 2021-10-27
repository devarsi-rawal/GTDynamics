/* ----------------------------------------------------------------------------
 * GTDynamics Copyright 2020, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * See LICENSE for the license information
 * -------------------------------------------------------------------------- */

/**
 * @file  PenaltyMethodOptimizer.cpp
 * @brief Penalty method optimization routines.
 * @author: Yetong Zhang
 */

#include "gtdynamics/optimizer/PenaltyMethodOptimizer.h"

namespace gtdynamics {

gtsam::Values PenaltyMethodOptimizer::optimize(
    const gtsam::NonlinearFactorGraph& graph,
    const EqualityConstraints& constraints,
    const gtsam::Values& initial_values) const
{
  gtsam::Values values = initial_values;
  double mu = p_->initial_mu;

  // increase the penalty
  for (int i = 0; i < p_->num_iterations; i++) {
    // converting constrained factors to unconstrained factors
    gtsam::NonlinearFactorGraph merit_graph = graph;
    for (auto& constraint : constraints) {
      merit_graph.add(constraint->createFactor(mu));
    }

    // run optimization
    gtsam::LevenbergMarquardtOptimizer optimizer(merit_graph, values,
                                                 p_->lm_parameters);
    auto result = optimizer.optimize();

    // save results and update parameters
    values = result;
    mu *= p_->mu_increase_rate;
  }
  return values;
}

}  // namespace gtdynamics