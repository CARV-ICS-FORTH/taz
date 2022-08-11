// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _ABSTRACT_PARSER
#define _ABSTRACT_PARSER

#include "../Engine.hpp"
#include "../PartitionedGraph.hpp"
#include "../taz-simulate.hpp"

constexpr uint64_t MaxLinesByParsingPass = 256;

struct AbstractGroup {
  // Members that should be immutable after creation

  // What ranks are being used by which communicator (and in which order)
  // The world communicator is not listed here (because each and every core is
  // included)
  std::vector<std::vector<resind_t>> ranks;
  std::string job_source;
  simtime_t start_time;
  double expected_duration;
  int nranks;

  // Members that do change upon each job initialization

  bool active;
  // The ID for each communicator
  std::vector<commind_t> comms;
  // The core for each rank
  std::vector<commind_t> cores;

  int job_id;
  EventHandleRef wall_handle;
  AbstractGroup() : start_time(END_OF_TIME), active(false), job_id(-1) {}

  virtual void init(int job_id_, commind_t world_comm, simtime_t start,
                    simtime_t wall, const std::vector<resind_t> &cores_,
                    double /*compute_time_factor */, int /* n_iterations*/) {
    active = true;
    start_time = start;
    this->job_id = job_id_;
    wall_handle = ctx.pool->push_event(wall, (resind_t)job_id_,
                                       EventHeapElement::JOB_KILL);
    cores = cores_;
    comms.push_back(world_comm);

    // Create additional communicators
    std::vector<resind_t> comm_cores;
    for (size_t i = 0; i < ranks.size(); i++) {
      comm_cores.clear();
      for (auto x : ranks[i]) {
        assert(x < (resind_t)nranks);
        comm_cores.push_back(cores[x]);
      }
      ctx.engine->add_communicator(job_id_, comm_cores);
    }
  }

  virtual void finish(simtime_t time) {
    if (expected_duration > 0.0) {
      double actual_duration = simtime_to_double(time - start_time);
      double diff_percent = 100.0 *
                            std::abs(expected_duration - actual_duration) /
                            std::max(expected_duration, actual_duration);
      if (diff_percent < 1e-3)
        LOG_INFO << "Job " << job_id << " completed in expected time"
                 << std::endl;
      else
        LOG_INFO << "Job " << job_id << " actual duration  (" << actual_duration
                 << ") deviates from expected (" << expected_duration << ") by "
                 << diff_percent << " %" << std::endl;
    }
    wall_handle.free();
    comms.clear();
    cores.clear();
    active = false;
  }

  resind_t get_core_id(int comm_index, int rank) {
    if (comm_index == 0) {
      assert(rank < nranks);
      return cores[rank];
    }
    assert(comm_index + 1 < (int)ranks.size());
    assert(rank < (int)ranks[comm_index - 1].size());
    int core_index = ranks[comm_index - 1][rank];
    assert(core_index < nranks);
    return cores[core_index];
  }

  int get_comm_size(int comm_index) {
    if (comm_index == 0)
      return nranks;
    assert(comm_index <= (int)ranks.size());
    return (int)ranks[comm_index - 1].size();
  }
};

template <> struct HeapConsumer<AbstractGroup> {
  static size_t get_heap_footprint(const AbstractGroup &x) {
    size_t footprint = GET_HEAP_FOOTPRINT(x.ranks);
    footprint += GET_HEAP_FOOTPRINT(x.comms);
    footprint += GET_HEAP_FOOTPRINT(x.cores);
    return footprint;
  }
};

class BaseParser : public MemoryConsumer {
public:
  BaseParser(std::string name) : MemoryConsumer(name) {}

  virtual int get_or_create_trace_group(const char *filename,
                                        const char *traffic_filename,
                                        int &number_of_traces) = 0;
  virtual void init_job(int job_id, size_t group_id, simtime_t start_time,
                        simtime_t wall_time,
                        const std::vector<resind_t> &nodes_mapping,
                        double compute_time_factor, int n_iterations) = 0;
  virtual void finalize_job(int job_id, size_t group_id, simtime_t end_time,
                            JobFinishType type) = 0;
  virtual bool parse() = 0;
  virtual const ApproxMatrix &get_traffic_matrix(size_t group_id) = 0;
};

// A parser collects a certain number of groups, which each represent a
// parsing context. The goal is to reuse those context, if parsing the same
// source is requested multiple times (which we expect to happen often) One
// may derive its own group class to add more data to those groups
template <typename GroupType> class AbstractParser : public BaseParser {

protected:
  std::vector<GroupType> groups;

public:
  AbstractParser(std::string name) : BaseParser(name) {}

  virtual int get_or_create_trace_group(const char *filename,
                                        const char *traffic_filename,
                                        int &number_of_traces) {
    int group_id = 0;
    std::string source(filename);
    for (; group_id < (int)groups.size(); group_id++)
      if (groups[group_id].job_source == source && not groups[group_id].active)
        break;
    if (group_id != (int)groups.size()) {
      number_of_traces = groups[group_id].nranks;
      return group_id;
    }

    groups.push_back(GroupType());
    auto &group = groups.back();

    if (traffic_filename != nullptr && *traffic_filename != '\0')
      group.mat.load(traffic_filename);

    group.job_source = source;
    group.expected_duration = -1.0;

    size_t path_end = source.find_last_of("/\\");
    std::string index_path = source.substr(0, path_end + 1);

    std::string expected_time_name = index_path + "expected.txt";
    std::ifstream expected_time_file(expected_time_name.c_str());
    if (expected_time_file.good()) {
      try {
        expected_time_file >> group.expected_duration;
        LOG_INFO << "Expecting completion time to be "
                 << group.expected_duration << " seconds" << std::endl;
      } catch (std::exception &e) {
        LOG_ERROR << "Cannot parse the expected_time file because " << e.what()
                  << std::endl;
        group.expected_duration = -1.0;
      }
    }
    // This is a new group
    // The number of ranks and the additional communicators need to be defined
    // as well
    return -1;
  }

  void init_job(int job_id, size_t group_id, simtime_t start_time,
                simtime_t wall_time, const std::vector<resind_t> &nodes_mapping,
                double compute_time_factor, int n_iterations) {
    assert(group_id < groups.size());
    auto &group = groups[group_id];
    assert(not group.active);
    assert(group.nranks == (int)nodes_mapping.size());
    commind_t world_comm =
        ctx.engine->add_job(job_id, start_time, nodes_mapping);
    group.init(job_id, world_comm, start_time, wall_time, nodes_mapping,
               compute_time_factor, n_iterations);
  }

  virtual void finalize_job(int /*job_id*/, size_t group_id, simtime_t end_time,
                            JobFinishType /*type*/) {
    assert(group_id < groups.size());
    auto &group = groups[group_id];
    assert(group.active);
    ctx.engine->remove_job(group.comms, end_time);
    group.wall_handle.free();
    group.finish(end_time);
  }

  // Return if there are more events in the trace
  virtual bool parse() = 0;

  virtual const ApproxMatrix &get_traffic_matrix(size_t group_id) = 0;

  virtual size_t get_footprint() {
    return sizeof(*this) + GET_HEAP_FOOTPRINT(groups);
  }
};

#endif //_ABSTRACT_PARSER
