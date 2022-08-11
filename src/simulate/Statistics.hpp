//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#ifndef _STATISTICS_HPP_
#define _STATISTICS_HPP_

#include "taz-simulate.hpp"

struct Moments {
  uint64_t count;
  uint64_t sum;
  uint64_t sum2;
  void reset() {
    count = 0;
    sum = 0;
    sum2 = 0;
  }
  void add(uint64_t x) {
    count++;
    sum += x;
    sum2 += x * x;
  }
  void accumulate(const Moments &b) {
    count += b.count;
    sum += b.sum;
    sum2 += b.sum2;
  }
  void to_csv(std::ostream &out, bool is_last = false) {
    if (count) {
      double avg = sum / (double)count;
      double stdev = std::sqrt(sum2 / count - avg * avg);
      out << avg << "," << stdev;
    } else
      out << ",";
    if (is_last)
      out << std::endl;
    else
      out << ",";
  }

  void to_pydict(std::ostream &out, bool is_last = false) {
    if (count) {
      double avg = sum / (double)count;
      double stdev = std::sqrt(sum2 / (double)count - avg * avg);
      // Check for NaNs
      assert(stdev == stdev);
      out << "{'count':" << count << ", 'avg':" << avg << ", 'stdev':" << stdev
          << "}";
    } else
      out << "{'count':0, 'avg':None, 'stdev':None}";
    if (not is_last)
      out << ",";
  }
};

struct Statistics {
  time_t real_start_time;
  simtime_t sim_start_time;

  uint64_t actions_inited;
  uint64_t actions_sealed;
  uint64_t actions_started;
  uint64_t actions_ended;
  uint64_t actions_finalized;
  uint64_t actions_aborted;

  uint64_t sendrecv_matched;
  uint64_t move_to_next_blocking;

  uint64_t links_inited;
  uint64_t links_propagated;

  uint64_t parsed_lines;
  uint64_t total_lines_to_parse;

  uint64_t simulation_inner_iterations;
  uint64_t simulation_outer_iterations;

  uint64_t link_failures;
  uint64_t node_failures;
  uint64_t end_sooner_error;
  simtime_t end_sooner_error_amount;

  // Array depending on relative change of share
  // 0: 0%
  // 1: <0.1%
  // 2: <1%
  // 3: <10%
  // 4: >=10%
  static const int BottleneckThresholds = 5;
  uint64_t bottleneck_change[BottleneckThresholds];

  Moments hops;

  Moments original_actions_per_iteration;
  Moments ended_actions_per_iteration;
  Moments modified_actions_per_iteration;
  Moments total_actions_per_iteration;
  Moments total_resources_per_iteration;
  Moments modified_resources_per_iteration;

  Moments actions_per_resource;
  Moments resources_per_action;

  Moments btl_group_count;
  Moments btl_group_size;

  void reset(simtime_t sim_now);
  void accumulate(const Statistics &b);
  static void print_csv_header();
  void to_csv(std::ostream &out, simtime_t sim_now);
  void to_pydict(std::ostream &out, simtime_t sim_now);
  Statistics() { reset(0); }
};

void update_stats(bool final, simtime_t sim_now);

#endif /*_STATISTICS_HPP_*/