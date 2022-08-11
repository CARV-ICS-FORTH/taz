// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "AtomicAction.hpp"
#include "PartitionedGraph.hpp"
#include "resource_management/AbstractJobHandling.hpp"
#include "taz-simulate.hpp"

class ResourceManager : public MemoryConsumer {
  resind_t n_cores;

  int last_job_id;
  simtime_t last_job_availability;

  JobHeap job_queue;
  OngoingJobsMap ongoing_jobs;

  std::vector<AbstractJobScheduler *> schedulers;
  AbstractJobMapper *mapper;

  EventHandleRef event_handle;
  // For rm_only case
  std::unordered_map<int, EventHandleRef> job_event_handles;

  simtime_t next_schedule_time;

  void _update_event(simtime_t now, simtime_t update_time,
                     bool force_progress = false);

  void _instantiate_handlers();

public:
  ResourceManager()
      : MemoryConsumer("ResourceManager"), last_job_id(0),
        last_job_availability(0), mapper(nullptr),
        next_schedule_time(END_OF_TIME) {
    n_cores = NumberOfHosts * NumberOfRegionsPerHost * NumberOfCoresPerRegion;
    _instantiate_handlers();
  }

  ~ResourceManager();

  int push_job(ParserCollection::TraceType trace_type, double availability_time,
               double interarrival_time, double rel_nominal_time,
               double walltime, int n_iterations, double compute_time_factor,
               int ranks_per_host, std::string job_source,
               std::string traffic_source, std::string mapping_source,
               simtime_t now);

  // Return whether to pop this handle
  bool do_schedule(simtime_t now);

  void on_job_finished(int job_id, simtime_t time, JobFinishType type);

  void on_host_state_change(resind_t host_index, simtime_t now, bool is_faulty);

  bool is_job_running(int job_id) {
    return ongoing_jobs.find(job_id) != ongoing_jobs.end();
  }
  bool is_done() { return job_queue.empty() && ongoing_jobs.empty(); }

  bool job_queue_is_empty() { return job_queue.empty(); }

  std::vector<JobDescriptor> get_available_jobs(simtime_t now) {
    std::vector<JobDescriptor> available_jobs;
    for (JobHeap::ordered_iterator it = job_queue.ordered_begin();
         it != job_queue.ordered_end(); it++) {
      if (it->availability > now)
        break;
      available_jobs.push_back(*it);
    }
    return available_jobs;
  }
  // resind_t get_n_nodes() { return n_cores; }

  virtual size_t get_footprint() {
    size_t footprint = sizeof(ResourceManager);
    footprint += GET_HEAP_FOOTPRINT(job_queue);
    footprint += GET_HEAP_FOOTPRINT(job_event_handles);
    return footprint;
  }
};
