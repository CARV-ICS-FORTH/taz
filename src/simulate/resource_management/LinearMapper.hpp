// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "AbstractJobHandling.hpp"

class LinearMapper : public AbstractJobMapper {
protected:
  IntervalSet idle_hosts;

  virtual bool _skip_host(resind_t /*i*/, resind_t /*required_hosts*/,
                          double /*expected_duration*/) {
    return false;
  }
  virtual void _on_mapping_fail(const JobDescriptor &desc,
                                resind_t required_hosts,
                                resind_t skipped_hosts) {
    LOG_DEBUG << "Failed to allocated cores for job " << desc.job_id
              << " ( skipped " << skipped_hosts << " hosts , still missing "
              << required_hosts << " hosts)" << std::endl;
  }

public:
  LinearMapper(std::string name = "LinearMapper") : AbstractJobMapper(name) {
    idle_hosts += Interval(0, NumberOfHosts);
  }

  virtual void on_new_job(const JobDescriptor & /*desc*/, simtime_t /*time*/) {}
  virtual void on_host_state_change(resind_t host_index, simtime_t now,
                                    bool is_faulty) {
    (void)now;
    if (is_faulty) {
      idle_hosts -= host_index;
    } else {
      idle_hosts += host_index;
    }
  }
  virtual void on_job_finished(int /*job_id*/, const JobDescriptor &desc,
                               simtime_t /*time*/, JobFinishType /*type*/) {
    idle_hosts += desc.hosts;
  }

  virtual IntervalSet map_job(int /*job_id*/, const JobDescriptor &desc,
                              simtime_t /*time*/, resind_t required_hosts,
                              double expected_duration) {
    resind_t skipped_hosts = 0;
    IntervalSet::iterator it = idle_hosts.begin();
    IntervalSet alloc;

    for (; it != idle_hosts.end(); it++) {
      resind_t itv_start = it->lower();
      resind_t itv_end = it->lower();
      for (resind_t i = it->lower(); required_hosts && i < it->upper(); i++) {
        bool working = ctx.engine->is_core_working(i);
        if (not working)
          continue;
        if (_skip_host(i, required_hosts, expected_duration)) {
          skipped_hosts++;
          continue;
        }
        // Use this host
        required_hosts--;
        if (itv_end == i)
          itv_end++;
        else {
          Interval itv(itv_start, itv_end);
          alloc += itv;
          itv_start = i;
          itv_end = i + 1;
        }
      }
      if (itv_start != itv_end) {
        Interval itv(itv_start, itv_end);
        alloc += itv;
      }
    }

    if (required_hosts) {
      // We could not find a large enough interval set, give up !
      _on_mapping_fail(desc, required_hosts, skipped_hosts);
      return IntervalSet();
    }
    idle_hosts -= alloc;
    return alloc;
  }

  virtual size_t get_footprint() { return sizeof(LinearMapper); }
};