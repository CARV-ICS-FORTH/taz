// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _PARSER_COLLECTION
#define _PARSER_COLLECTION

#include "parsers/AbstractParser.hpp"
#include "parsers/SimgridTIParser.hpp"
#include "parsers/TrafficParser.hpp"
#include "parsers/VEFParser.hpp"
#include "taz-simulate.hpp"

constexpr int MaxGroupIndex = 1 << 24;
constexpr int NParsers = 3;

class ParserCollection {

  SimgridTIParser simgrid;
  VEFParser vef;
  TrafficParser traffic;
  BaseParser *parsers[NParsers];

public:
  ParserCollection() {
    parsers[0] = &simgrid;
    parsers[1] = &vef;
    parsers[2] = &traffic;
  }

  enum TraceType { SIMGRID_TI = 0, VEF, TRAFFIC, INVALID };
  int get_or_create_trace_group(TraceType type, const char *trace_filename,
                                const char *traffic_filename,
                                int &number_of_traces) {
    int trace_type = (int)type;
    assert(trace_type < NParsers);
    int id = parsers[trace_type]->get_or_create_trace_group(
        trace_filename, traffic_filename, number_of_traces);
    assert(id < MaxGroupIndex);
    id += trace_type * MaxGroupIndex;
    return id;
  }

  void init_job(simtime_t start_time, simtime_t wall_time,
                double compute_time_factor, int job_id, size_t group_id,
                const std::vector<resind_t> &cores_mapping, int n_iterations) {
    size_t trace_type = group_id / MaxGroupIndex;
    assert(trace_type < NParsers);
    parsers[trace_type]->init_job(job_id, group_id % MaxGroupIndex, start_time,
                                  wall_time, cores_mapping, compute_time_factor,
                                  n_iterations);
  }

  void finalize_job(simtime_t end_time, int job_id, size_t group_id,
                    JobFinishType type) {
    size_t trace_type = group_id / MaxGroupIndex;
    assert(trace_type < NParsers);
    parsers[trace_type]->finalize_job(job_id, group_id % MaxGroupIndex,
                                      end_time, type);
  }

  bool parse() {
    bool has_more = false;
    for (size_t i = 0; i < NParsers; i++)
      has_more = has_more || parsers[i]->parse();
    return has_more;
  }

  std::string get_group_pretty_string(size_t group_id) {
    std::stringstream ss;
    size_t trace_type = group_id / MaxGroupIndex;
    size_t index = group_id % MaxGroupIndex;
    switch (trace_type) {
    case SIMGRID_TI:
      ss << "SIMGRID_TI";
      break;
    case VEF:
      ss << "VEF";
      break;
    case TRAFFIC:
      ss << "TRAFFIC";
      break;
    case INVALID:
    default:
      MUST_DIE
    }
    ss << "/" << index;
    return ss.str();
  }
};

#endif
