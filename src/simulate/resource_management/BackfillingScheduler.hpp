// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#pragma once

#include "AbstractJobHandling.hpp"
#include <map>

typedef std::pair<int, simtime_t> KeyType;

/* struct KeyCompare {
  bool operator()(const KeyType &a, const KeyType &b) const {
    if (a.first == b.first)
      return false;
    return a.second < b.second;
  }
};
*/

class BackfillingScheduler : public AbstractJobScheduler {

  // Key is job id and value is walltime
  std::map<int, simtime_t> ongoing_jobs_walltime;

  // Key is .first: job ID .second: wall_time
  // Value is nhosts
  std::map<KeyType, int> ongoing_jobs_nhosts;

  // Note that this can transiently be negative (if a fault hit an allocated
  // host)
  int idle_hosts;

  // The job Ids that can be backfilled
  std::vector<int> candidate_jobs;

  void _find_candidates(const JobHeap &heap, simtime_t now) {
    assert(candidate_jobs.empty());
    auto &top = heap.top();
    // Determine how much laxity we have.
    // This is the time until enough jobs have reached their wall time so that
    // there are enough hosts available hosts for the top job to run.
    simtime_t ready_time = END_OF_TIME;
    auto missing_hosts = top.nhosts;
    assert_always(missing_hosts > idle_hosts,
                  "Should not call backfilling since top job has enough "
                  "resource available already!");
    missing_hosts -= idle_hosts;
    for (auto &kv : ongoing_jobs_walltime) {
      auto it = ongoing_jobs_nhosts.find(kv);
      assert(it != ongoing_jobs_nhosts.end());
      if (it->second >= missing_hosts) {
        ready_time = kv.second;
        break;
      }
      missing_hosts -= it->second;
    }

    // Now find what available job could be backfilled, i.e.:
    //  * availability is in the past
    //  * nranks <= available_hosts
    //  * now + rel_wall_time < ready_time
    auto available_hosts = idle_hosts;
    JobHeap::ordered_iterator visiting_iterator = heap.ordered_begin();
    for (; visiting_iterator != heap.ordered_end(); visiting_iterator++) {
      // We are done with jobs that are currently available
      if (visiting_iterator->availability > now)
        break;
      // We are interrested only in jobs that will finish before top is ready
      if (now + visiting_iterator->rel_wall_time >= ready_time)
        continue;
      if (visiting_iterator->nhosts < 0)
        // We do not know how many hosts are needed but this is OK
        candidate_jobs.push_back(visiting_iterator->job_id);
      else if (visiting_iterator->nhosts < available_hosts) {
        candidate_jobs.push_back(visiting_iterator->job_id);
        available_hosts -= visiting_iterator->nhosts;
      }
    }
    LOG_DEBUG << "Backfilling scheduler found candidate jobs " << candidate_jobs
              << std::endl;
    // Reverse vector order so that we can pop back nicely
    std::reverse(candidate_jobs.begin(), candidate_jobs.end());
  }

public:
  BackfillingScheduler() : AbstractJobScheduler("BackfillingScheduler") {
    idle_hosts = ctx.engine->get_number_of_hosts();
  }
  virtual void on_new_job(const JobDescriptor & /*desc*/, simtime_t /*time*/) {}

  virtual void on_job_finished(int job_id, const JobDescriptor & /*desc*/,
                               simtime_t /*time*/, JobFinishType /*type*/) {
    auto it = ongoing_jobs_walltime.find(job_id);
    assert(it != ongoing_jobs_walltime.end());
    auto it2 = ongoing_jobs_nhosts.find(*it);
    assert(it2 != ongoing_jobs_nhosts.end());
    ongoing_jobs_walltime.erase(it);
    ongoing_jobs_nhosts.erase(it2);
  }

  virtual JobHeap::handle_type
  select_next_job(const JobHeap &heap, bool & /* peek_top*/, simtime_t now) {
    if (candidate_jobs.empty()) {
      _find_candidates(heap, now);
      if (candidate_jobs.empty())
        return JobHeap::handle_type(); // we are done;
    }
    // Find the corresponding job heap node
    for (JobHeap::iterator it = heap.begin(); it != heap.end(); it++) {
      if (it->job_id != candidate_jobs.back())
        continue;
      auto h = JobHeap::s_handle_from_iterator(it);
      candidate_jobs.pop_back();
      return h;
    }
    // We should never get here (meaning job was not presnet in the heap)
    MUST_DIE
  }

  virtual bool on_mapping_failed(JobHeap::handle_type /*handle*/) {
    return false;
  }
  virtual void on_mapping_succeeded(JobHeap::handle_type /*handle*/, int job_id,
                                    simtime_t wall_time, int nhosts) {
    idle_hosts -= nhosts;

    KeyType key{job_id, wall_time};
    assert(ongoing_jobs_walltime.find(job_id) == ongoing_jobs_walltime.end());
    assert(ongoing_jobs_nhosts.find(key) == ongoing_jobs_nhosts.end());
    ongoing_jobs_walltime.emplace(key);
    ongoing_jobs_nhosts.emplace(key, nhosts);
  }

  virtual void on_host_state_change(resind_t /*host_index*/, simtime_t /*now*/,
                                    bool is_faulty) {
    if (is_faulty)
      idle_hosts--;
    else
      idle_hosts++;
  }

  virtual size_t get_footprint() {
    size_t footprint = sizeof(BackfillingScheduler);
    footprint += GET_HEAP_FOOTPRINT(ongoing_jobs_walltime);
    footprint += GET_HEAP_FOOTPRINT(ongoing_jobs_nhosts);
    footprint += GET_HEAP_FOOTPRINT(candidate_jobs);
    return footprint;
  }
};