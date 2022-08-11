// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#pragma once

#include "../ParserCollection.hpp"
#include "../taz-simulate.hpp"

#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>
typedef typename boost::icl::interval<resind_t>::type Interval;
typedef typename boost::icl::interval_set<resind_t> IntervalSet;

std::ostream &operator<<(std::ostream &out, JobFinishType x);

struct JobDescriptor {
  ParserCollection::TraceType trace_type;
  std::string job_source;
  std::string traffic_source;
  std::string mapping_source;
  // Time when the job will be available for scheduling
  simtime_t availability;
  simtime_t start_time;
  // The rate between the end of a job instance and the availability of the
  // next one
  double interarrival_rate;
  // The nominal time for a job execution (used to compute system yield)
  // Relative to the job start time
  simtime_t rel_nominal_time;
  // THe maximum time that the job might take (before it gets killed)
  // Relative to the job start time
  simtime_t rel_wall_time;
  int job_id;
  size_t stream_group_index;
  int n_iterations;
  double compute_time_factor;
  int ranks_per_host;
  int nhosts;
  IntervalSet hosts;
  bool operator<(const JobDescriptor &b) const {
    return availability > b.availability;
  }
  JobDescriptor()
      : trace_type(ParserCollection::INVALID), availability(END_OF_TIME),
        start_time(END_OF_TIME), interarrival_rate(0), job_id(-1),
        stream_group_index(std::numeric_limits<size_t>::max()), nhosts(-1) {}
};

std::ostream &operator<<(std::ostream &out, const JobDescriptor &x);

typedef boost::heap::fibonacci_heap<JobDescriptor> JobHeap;
typedef std::map<int, JobDescriptor> OngoingJobsMap;

class AbstractJobHandler : public MemoryConsumer {

public:
  AbstractJobHandler(std::string name) : MemoryConsumer(name) {}
  virtual ~AbstractJobHandler() {}
  // Called when a new job is added to the job heap
  virtual void on_new_job(const JobDescriptor &desc, simtime_t time) = 0;

  // Called when a host becomes faulty or recovers
  virtual void on_host_state_change(resind_t host_index, simtime_t now,
                                    bool is_faulty) = 0;

  // Called when a job has finished
  virtual void on_job_finished(int job_id, const JobDescriptor &desc,
                               simtime_t time, JobFinishType type) = 0;
};

class AbstractJobScheduler : public AbstractJobHandler {

public:
  AbstractJobScheduler(std::string name) : AbstractJobHandler(name) {}

  // Called when there are jobs to be scheduled.
  // If peek_top is set to true, the top of the heap is selected.
  // Otherwise, the returned handle is used
  // Ultimately, the function may return empty
  // handle if it prefers to wait.
  virtual JobHeap::handle_type
  select_next_job(const JobHeap &heap, bool &peek_top, simtime_t now) = 0;

  // Called when mapping for last job selection failed
  // Return whether we should stop trying to schedule jobs
  virtual bool on_mapping_failed(JobHeap::handle_type handle) = 0;
  // Called when mapping for last job selection succeeded
  virtual void on_mapping_succeeded(JobHeap::handle_type handle, int job_id,
                                    simtime_t wall_time, int nhosts) = 0;
};

class AbstractJobMapper : public AbstractJobHandler {

public:
  AbstractJobMapper(std::string name) : AbstractJobHandler(name) {}

  // Either return an empty set if mapping failed, or a mapping with at least
  // the number of processes needed
  virtual IntervalSet map_job(int job_id, const JobDescriptor &desc,
                              simtime_t time, resind_t required_hosts,
                              double expected_duration) = 0;
};
