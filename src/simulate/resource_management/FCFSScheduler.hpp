// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#pragma once

#include "AbstractJobHandling.hpp"

class FCFSScheduler : public AbstractJobScheduler {
public:
  FCFSScheduler() : AbstractJobScheduler("FCFSScheduler") {}

  virtual void on_new_job(const JobDescriptor & /*desc*/, simtime_t /*time*/) {}
  virtual void on_host_state_change(resind_t /*host_index*/, simtime_t /*now*/,
                                    bool /*is_faulty*/) {}
  virtual void on_job_finished(int /*job_id*/, const JobDescriptor & /*desc*/,
                               simtime_t /*time*/, JobFinishType /*type*/) {}
  virtual JobHeap::handle_type
  select_next_job(const JobHeap & /*heap*/, bool &peek_top, simtime_t /*now*/) {
    peek_top = true;
    return JobHeap::handle_type();
  }
  virtual bool on_mapping_failed(JobHeap::handle_type /*handle*/) {
    return true;
  }
  virtual void on_mapping_succeeded(JobHeap::handle_type /*handle*/,
                                    int /*job_id*/, simtime_t /*wall_time*/,
                                    int /*nhosts*/) {}
  virtual size_t get_footprint() { return sizeof(FCFSScheduler); }
};