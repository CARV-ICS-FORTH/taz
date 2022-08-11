// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "AbstractJobHandling.hpp"

class FixedMapper : public AbstractJobMapper {
public:
  FixedMapper() : AbstractJobMapper("FixedMapper") {}

  virtual void on_new_job(const JobDescriptor & /*desc*/, simtime_t /*time*/) {}
  virtual void on_host_state_change(resind_t /*host_index*/, simtime_t /*now*/,
                                    bool /*is_faulty*/) {}
  virtual void on_job_finished(int /*job_id*/, const JobDescriptor & /*desc*/,
                               simtime_t /*time*/, JobFinishType /*type*/) {}
  virtual IntervalSet map_job(int /*job_id*/, const JobDescriptor &desc,
                              simtime_t /*time*/, resind_t required_hosts,
                              double /*expected_duration*/) {
    assert(not desc.mapping_source.empty());
    ApproxMatrix mat;
    mat.load(desc.mapping_source);
    assert(mat.get_nrows() == 1);
    assert(mat.get_ncols() == required_hosts);
    IntervalSet alloc;
    for (size_t i = 0; i < required_hosts; i++)
      alloc += (resind_t)mat(0, i);
    return alloc;
  }

  virtual size_t get_footprint() { return sizeof(FixedMapper); }
};