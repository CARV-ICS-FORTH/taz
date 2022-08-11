// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _NONE_PARSER
#define _NONE_PARSER

#include "AbstractParser.hpp"

struct TrafficGroup : public AbstractGroup {
  ApproxMatrix mat;
};

template <> struct HeapConsumer<TrafficGroup> {
  static size_t get_heap_footprint(const TrafficGroup &x) {
    return HeapConsumer<AbstractGroup>::get_heap_footprint(x) +
           GET_HEAP_FOOTPRINT(x.mat);
  }
};

class TrafficParser : public AbstractParser<TrafficGroup> {

public:
  TrafficParser() : AbstractParser("TrafficParser") {}
  virtual int get_or_create_trace_group(const char *filename,
                                        const char * /*traffic_filename*/,
                                        int &number_of_traces) {
    std::string source(filename);
    int group_id = AbstractParser::get_or_create_trace_group(filename, nullptr,
                                                             number_of_traces);
    if (group_id >= 0)
      return group_id;

    // The last element has been partially initialized
    group_id = groups.size() - 1;
    auto &group = groups.back();

    // Open the traffic matrix and get number of traces
    group.mat.load(filename);
    assert_always(group.mat.is_square(), "The traffic matrix must be square");
    group.nranks = number_of_traces = group.mat.get_nrows();
    return group_id;
  }

  // The ratio between the wall time and the time doing only computations in the
  // nominal case
  static constexpr int ComputationOnlyRatio = 20;
  static constexpr int CommBurstinessFactor = 10;

  virtual void init_job(int job_id, size_t group_id, simtime_t start_time,
                        simtime_t wall_time,
                        const std::vector<resind_t> &nodes_mapping,
                        double compute_time_factor, int n_iterations) {

    AbstractParser<TrafficGroup>::init_job(job_id, group_id, start_time,
                                           wall_time, nodes_mapping,
                                           compute_time_factor, n_iterations);

    assert(group_id < groups.size());
    auto &group = groups[group_id];

    assert(n_iterations >= 1);

    // Create a simple set of actions that roughly models the application
    // performing iterations
    for (int i = 0; i < group.nranks; i++) {
      AtomicAction::DebugInfo dbg_init(i, 0, 0);
      ctx.engine->enqueue_init(group.comms[0], dbg_init);
    }

    simtime_t iteration_compute =
        (wall_time - start_time) / (ComputationOnlyRatio * n_iterations);
    // Generate a vector comm_vec of comms (src,dest,size) or bidirectional map

    for (int i = 0; i < n_iterations; i++) {
      std::vector<int> request_count(group.nranks, 0);
      for (int r = 0; r < group.nranks; r++)
        for (int c = 0; c < group.nranks; c++) {
          auto size = (group.mat(r, c) * CommBurstinessFactor) / n_iterations;
          if (size == 0)
            continue;
          request_count[r]++;
          request_count[c]++;
          AtomicAction::DebugInfo dbg_send(r, 1, i + 1);
          ctx.engine->enqueue_send(group.comms[0], r, c, size, 0, false,
                                   dbg_send);
          AtomicAction::DebugInfo dbg_receive(c, 1, i + 1);
          ctx.engine->enqueue_receive(group.comms[0], r, c, size, 0, false,
                                      dbg_receive);
        }

      for (int r = 0; r < group.nranks; r++) {
        AtomicAction::DebugInfo dbg_wait(r, 2, i + 1);
        ctx.engine->enqueue_waitall(group.comms[0], r, request_count[r],
                                    dbg_wait);
        ctx.engine->enqueue_compute(group.comms[0], r, iteration_compute);
      }
    }

    for (int i = 0; i < group.nranks; i++) {
      AtomicAction::DebugInfo dbg_fini(i, 3, n_iterations + 1);
      ctx.engine->enqueue_finalize(group.comms[0], job_id, dbg_fini);
    }
  }

  // Return if there are more events in the trace
  virtual bool parse() { return false; }

  virtual const ApproxMatrix &get_traffic_matrix(size_t group_id) {
    assert(group_id < groups.size());
    return groups[group_id].mat;
  }
};

#endif //_NONE_PARSER
