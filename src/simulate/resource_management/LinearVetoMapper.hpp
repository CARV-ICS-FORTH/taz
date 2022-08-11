// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "AbstractJobHandling.hpp"

class LinearVetoMapper : public LinearMapper {
  // We use natural logarithm for reliability computations, to reduce
  // rounding errors
  //  Accept only cores which failure rate is lower than target
  double reliability_target_ln;
  double reliability_budget_ln;

  bool _skip_host(resind_t i, resind_t required_hosts,
                  double expected_duration) {
    if (not std::isnan(reliability_target_ln)) {
      double node_reliability_ln =
          std::log1p(-ctx.engine->get_core_cdf(i, expected_duration));
      if (node_reliability_ln <
          reliability_budget_ln / (double)required_hosts) {
        return true;
      }
      reliability_budget_ln -= node_reliability_ln;
    }
    return false;
  }

  virtual void _on_mapping_fail(const JobDescriptor &desc,
                                resind_t required_hosts,
                                resind_t skipped_hosts) {
    LOG_DEBUG << "Failed to allocated cores for job " << desc.job_id
              << " because reliability target could not be satisfied ( skipped "
              << skipped_hosts << " cores, still missing " << required_hosts
              << " with required reliability per core "
              << std::exp(reliability_budget_ln / (double)required_hosts)
              << std::endl;
  }

public:
  LinearVetoMapper() : LinearMapper("LinearVetoMapper") {

    double reliability_target =
        ctx.vm["rm_linear_veto_reliability_target"].as<double>();
    assert(reliability_target >= 0);
    reliability_target_ln =
        reliability_target > 0 ? std::log(reliability_target) : std::nan("");
  }

  virtual IntervalSet map_job(int job_id, const JobDescriptor &desc,
                              simtime_t time, resind_t required_hosts,
                              double expected_duration) {
    reliability_budget_ln = reliability_target_ln;
    return LinearMapper::map_job(job_id, desc, time, required_hosts,
                                 expected_duration);
  }

  virtual size_t get_footprint() { return sizeof(LinearVetoMapper); }
};