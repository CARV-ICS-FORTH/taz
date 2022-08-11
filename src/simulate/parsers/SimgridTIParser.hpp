// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _SIMGRIDTIPARSER_HPP
#define _SIMGRIDTIPARSER_HPP

#include "AbstractParser.hpp"
#include <memory>
#include <random>

struct StreamEntry {
  int n_lines_read;
  int epoch; // number of collectives since start
  int rank;
  resind_t node;
  int remaining_iterations;
  bool first_iteration;
  std::ifstream::pos_type iterations_start;
  std::shared_ptr<std::istream> file;
};

struct SimgridTIGroup : public AbstractGroup {
  std::vector<StreamEntry> streams;
  int remaining_streams;
  double compute_time_factor; // Multiply parsed values by this number
  double total_compute_time;
  size_t total_lines_to_parse;
  ApproxMatrix mat;
  SimgridTIGroup() : remaining_streams(0) {}

  virtual void init(int job_id_, commind_t world_comm, simtime_t start,
                    simtime_t wall, const std::vector<resind_t> &cores,
                    double compute_time_factor_, int n_iterations) {
    AbstractGroup::init(job_id_, world_comm, start, wall, cores,
                        compute_time_factor_, n_iterations);
    int i = 0;
    for (auto &s : streams) {
      s.n_lines_read = 0;
      s.epoch = 0;
      assert(i == s.rank);
      s.node = cores[i++];
      s.remaining_iterations = n_iterations;
      s.first_iteration = true;
      s.file->clear();
      s.file->seekg(0);
    }
    remaining_streams = nranks;
    total_compute_time = 0;
    this->compute_time_factor = compute_time_factor_;
  }

  virtual void finish(simtime_t time) {
    LOG_INFO << "Job " << job_id << " average compute time per rank "
             << total_compute_time / nranks << std::endl;
    AbstractGroup::finish(time);
  }
};

template <> struct HeapConsumer<SimgridTIGroup> {
  static size_t get_heap_footprint(const SimgridTIGroup &x) {
    size_t footprint = HeapConsumer<AbstractGroup>::get_heap_footprint(x);
    footprint += GET_HEAP_FOOTPRINT(x.streams);
    footprint += GET_HEAP_FOOTPRINT(x.mat);
    return footprint;
  }
};

class SimgridTIParser : public AbstractParser<SimgridTIGroup>,
                        protected GenericParser {
private:
  // std::hash<char *> str_hash;
  char tmp[1024];

  bool is_good;

  std::default_random_engine generator;

  enum class ParseResult { CONTINUE, PAUSE, ENTER_ITERATION, EXIT_ITERATIONS };
  // Parse line in tmp
  ParseResult parse_line(int rank, SimgridTIGroup &group);
  void skip_to_exit_iterations(StreamEntry &stream, int rank);

  bool _open_trace_file(SimgridTIGroup &group, std::string index_path,
                        std::string filename, int rank);
  void _open_inline(SimgridTIGroup &group, const char *source,
                    int &number_of_traces);

public:
  SimgridTIParser() : AbstractParser<SimgridTIGroup>("SimgridTIParser") {}

  bool good() { return is_good; }

  virtual int get_or_create_trace_group(const char *trace_filename,
                                        const char *traffic_filename,
                                        int &number_of_traces);

  // Return if there are more events in the trace
  virtual bool parse();

  virtual const ApproxMatrix &get_traffic_matrix(size_t group_id) {
    assert(group_id < groups.size());
    return groups[group_id].mat;
  }

  virtual size_t get_footprint() {
    size_t footprint =
        AbstractParser<SimgridTIGroup>::get_footprint() + sizeof(GenericParser);
    footprint += sizeof(tmp) + sizeof(generator);
    return footprint;
  }
};

#endif //_TRACEPARSER_HPP
