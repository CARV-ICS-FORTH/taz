// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "Statistics.hpp"
#include "AtomicAction.hpp"

void Statistics::reset(simtime_t now) {
  real_start_time = time(nullptr);
  sim_start_time = now;

  actions_inited = 0;
  actions_sealed = 0;
  actions_started = 0;
  actions_ended = 0;
  actions_finalized = 0;
  actions_aborted = 0;

  sendrecv_matched = 0;
  move_to_next_blocking = 0;

  links_inited = 0;
  links_propagated = 0;

  parsed_lines = 0;
  total_lines_to_parse = 0;

  simulation_inner_iterations = 0;
  simulation_outer_iterations = 0;

  link_failures = 0;
  node_failures = 0;
  end_sooner_error = 0;
  end_sooner_error_amount = 0;

  for (size_t i = 0; i < BottleneckThresholds; i++)
    bottleneck_change[i] = 0;

  hops.reset();

  original_actions_per_iteration.reset();
  ended_actions_per_iteration.reset();
  modified_actions_per_iteration.reset();
  total_actions_per_iteration.reset();
  total_resources_per_iteration.reset();
  modified_resources_per_iteration.reset();

  actions_per_resource.reset();
  resources_per_action.reset();

  btl_group_count.reset();
  btl_group_size.reset();
}

void Statistics::accumulate(const Statistics &b) {
  actions_inited += b.actions_inited;
  actions_sealed += b.actions_sealed;
  actions_started += b.actions_started;
  actions_ended += b.actions_ended;
  actions_finalized += b.actions_finalized;
  actions_aborted += b.actions_aborted;

  sendrecv_matched += b.sendrecv_matched;
  move_to_next_blocking += b.move_to_next_blocking;

  links_inited += b.links_inited;
  links_propagated += b.links_propagated;

  parsed_lines += b.parsed_lines;
  total_lines_to_parse += b.total_lines_to_parse;

  simulation_inner_iterations += b.simulation_inner_iterations;
  simulation_outer_iterations += b.simulation_outer_iterations;

  link_failures += b.link_failures;
  node_failures += b.node_failures;
  end_sooner_error += b.end_sooner_error;
  end_sooner_error_amount += b.end_sooner_error;

  for (size_t i = 0; i < BottleneckThresholds; i++)
    bottleneck_change[i] += b.bottleneck_change[i];

  hops.accumulate(b.hops);

  original_actions_per_iteration.accumulate(b.original_actions_per_iteration);
  ended_actions_per_iteration.accumulate(b.ended_actions_per_iteration);
  modified_actions_per_iteration.accumulate(b.modified_actions_per_iteration);
  total_actions_per_iteration.accumulate(b.total_actions_per_iteration);
  total_resources_per_iteration.accumulate(b.total_resources_per_iteration);
  modified_resources_per_iteration.accumulate(b.modified_actions_per_iteration);

  resources_per_action.accumulate(b.resources_per_action);
  actions_per_resource.accumulate(b.actions_per_resource);

  btl_group_count.accumulate(b.btl_group_count);
  btl_group_size.accumulate(b.btl_group_size);
}

void Statistics::print_csv_header() {
  std::stringstream l1, l2;
  l1 << "time,,", l2 << "real,sim,";
  l1 << ",,actions,,,,", l2 << "ainit,aseal,astart,aend,afinal,aabort,";
  l1 << "misc.,,", l2 << "match,nexblk,";
  l1 << "links,,", l2 << "linit,lprop,";
  l1 << "parsing,,", l2 << "p_done,p_tot,";
  l1 << "simulation,,", l2 << "inner,outer,";
  l1 << "failures,,", l2 << "link,node,";
  l1 << "sooner_errors,,", l2 << "e1_cnt,e1_sum,";
  l1 << "bottleneck,,,,,,,,,",
      l2 << "0chg,01chg,1chg,10chg,ltchg,cb_avg,cb_stc,sb_avg,sb_std,";
  l1 << "hops_per_msg,,", l2 << "hm_avg,hm_std,";
  l1 << "orig_act/it,,", l2 << "oa_avg,oa_std,";
  l1 << "end_act/it,,", l2 << "ea_avg,ea_std,";
  l1 << "mod_act/it,,", l2 << "ma_avg,ma_std,";
  l1 << "tot_act/it,,", l2 << "ta_avg,ta_std,";
  l1 << "tot_res/it,,", l2 << "tr_avg,tr_std,";
  l1 << "mod_res/it,,", l2 << "mr_avg,mr_std,";
  l1 << "res/act,,", l2 << "ra_avg,ra_std,";
  l1 << "act/res,,", l2 << "ar_avg,ar_std,";
  ctx.stats_file << l1.str() << std::endl << l2.str() << std::endl;

  ctx.jobs_file
      << "id,endtype,availability,nnodes,start,end,nominal,slowdown,accounted"
      << std::endl;
}

// Print a CSV-style output
void Statistics::to_csv(std::ostream &out, simtime_t now) {
  out << difftime(time(nullptr), real_start_time) << ",";
  out << now - sim_start_time << ",";
  out << actions_inited << ",";
  out << actions_sealed << ",";
  out << actions_started << ",";
  out << actions_ended << ",";
  out << actions_finalized << ",";
  out << actions_aborted << ",";
  out << sendrecv_matched << ",";
  out << move_to_next_blocking << ",";
  out << links_inited << ",";
  out << links_propagated << ",";
  out << parsed_lines << ",";
  out << total_lines_to_parse << ",";
  out << simulation_inner_iterations << ",";
  out << simulation_outer_iterations << ",";
  out << link_failures << ",";
  out << node_failures << ",";
  out << end_sooner_error << ",";
  out << end_sooner_error_amount << ",";
  for (size_t i = 0; i < BottleneckThresholds; i++)
    out << bottleneck_change[i] << ",";
  btl_group_count.to_csv(out);
  btl_group_size.to_csv(out);
  hops.to_csv(out);
  original_actions_per_iteration.to_csv(out);
  ended_actions_per_iteration.to_csv(out);
  modified_actions_per_iteration.to_csv(out);
  total_actions_per_iteration.to_csv(out);
  total_resources_per_iteration.to_csv(out);
  modified_resources_per_iteration.to_csv(out);
  resources_per_action.to_csv(out);
  actions_per_resource.to_csv(out, true);
}

void puttab(std::ostream &out, size_t n) {
  for (size_t i = 0; i < n; i++)
    out << '\t';
}
#define PRINT_ATTR(x)                                                          \
  puttab(out, ntabs);                                                          \
  out << '\'' << #x << "':" << x << ',' << std::endl;
#define PRINT_MOMENT(x)                                                        \
  puttab(out, ntabs);                                                          \
  out << '\'' << #x << "':";                                                   \
  x.to_pydict(out, is_last);                                                   \
  out << std::endl;

void Statistics::to_pydict(std::ostream &out, simtime_t now) {
  bool is_last = false;
  constexpr int ntabs = 2;
  double real_duration = difftime(time(nullptr), real_start_time);
  double sim_duration = simtime_to_double(now - sim_start_time);
  PRINT_ATTR(real_duration);
  PRINT_ATTR(sim_duration);

  PRINT_ATTR(actions_inited);
  PRINT_ATTR(actions_sealed);
  PRINT_ATTR(actions_started);
  PRINT_ATTR(actions_ended);
  PRINT_ATTR(actions_finalized);
  PRINT_ATTR(actions_aborted);

  PRINT_ATTR(sendrecv_matched);
  PRINT_ATTR(move_to_next_blocking);

  PRINT_ATTR(links_inited);
  PRINT_ATTR(links_propagated);

  PRINT_ATTR(parsed_lines);
  PRINT_ATTR(total_lines_to_parse);

  PRINT_ATTR(simulation_inner_iterations);
  PRINT_ATTR(simulation_outer_iterations);

  PRINT_ATTR(link_failures);
  PRINT_ATTR(node_failures);
  PRINT_ATTR(end_sooner_error);
  PRINT_ATTR(end_sooner_error_amount);

  puttab(out, ntabs);
  out << "'bottleneck_change':[" << bottleneck_change[0];
  for (size_t i = 1; i < BottleneckThresholds; i++)
    out << ',' << bottleneck_change[i];
  out << "]," << std::endl;

  PRINT_MOMENT(hops);

  PRINT_MOMENT(original_actions_per_iteration);
  PRINT_MOMENT(ended_actions_per_iteration);
  PRINT_MOMENT(modified_actions_per_iteration);
  PRINT_MOMENT(total_actions_per_iteration);
  PRINT_MOMENT(total_resources_per_iteration);
  PRINT_MOMENT(modified_resources_per_iteration);

  PRINT_MOMENT(resources_per_action);
  PRINT_MOMENT(actions_per_resource);

  PRINT_MOMENT(btl_group_count);
  is_last = true;
  PRINT_MOMENT(btl_group_size);
}

std::string pretty_size(size_t x) {
  std::stringstream out;
  if (x < 1000ULL) {
    out << x << "[By]";
    return out.str();
  }
  if (x < 1000000ULL) {
    out << x / 1000ULL << "[KBy]";
    return out.str();
  }
  if (x < 1000000000ULL) {
    out << x / 1000000ULL << "[MBy]";
    return out.str();
  }
  out << x / 1000000000ULL << "[GBy]";
  return out.str();
}

void update_stats(bool final, simtime_t sim_now) {
  // Limit rate
  time_t real_now = time(nullptr);
  double duration = difftime(real_now, ctx.out_stats->real_start_time);
  ctx.total_stats->accumulate(*ctx.tmp_stats);
  ctx.out_stats->accumulate(*ctx.tmp_stats);
  ctx.tmp_stats->reset(sim_now);

  if (not final && duration < ctx.vm["stats_period"].as<int>())
    return;
  // Condensate info on screen
  double total_duration = difftime(real_now, ctx.total_stats->real_start_time);
  // double parsed_percent = ctx.total_stats->parsed_lines * 100 /
  //                         (double)ctx.total_stats->total_lines_to_parse;
  // uint64_t action_steps =
  //     _tmp_stats.actions_sealed + _tmp_stats.actions_started +
  //     _tmp_stats.actions_ended + _tmp_stats.actions_finalized;
  double vm_usage, resident_set;
  process_mem_usage(vm_usage, resident_set);
  auto usage_vector = MemoryConsumer::get_consumers_footprint();
  // Sort usages by decreasing size
  std::sort(usage_vector.begin(), usage_vector.end(),
            [](const MemoryConsumer::MemoryFootprintType &a,
               const MemoryConsumer::MemoryFootprintType &b) {
              return a.second > b.second;
            });

  double sim_delta = simtime_to_double(sim_now - ctx.out_stats->sim_start_time);
  LOG_INFO << "@" << total_duration << " seconds, ran "
           << ctx.total_stats->simulation_outer_iterations << "/"
           << ctx.total_stats->simulation_inner_iterations
           << " simulation iterations = +" << sim_delta << "[s]";
  //  << ", parsed " << parsed_percent
  //  << "% of the trace, action steps: seal:"
  //  << ctx.out_stats->actions_sealed
  //  << " start:" << ctx.out_stats->actions_started
  //  << " update:" << ctx.out_stats->bottleneck_change[0] << "/"
  //  << ctx.out_stats->bottleneck_change[1] << "/"
  //  << ctx.out_stats->bottleneck_change[2] << "/"
  //  << ctx.out_stats->bottleneck_change[3] << "/"
  //  << ctx.out_stats->bottleneck_change[4] << "/"
  //  << " end:" << ctx.out_stats->actions_ended << " fin:"
  //  << ctx.out_stats->actions_finalized
  //<< " pool counts:" << pool->action_count() << " actions  "
  //<< pool->link_count() << " actlinks"
  LOG_INFO << " actions:" <<ctx.pool->get_action_count() << " links:" << ctx.pool->get_link_count();
  LOG_INFO << " footprint: ";
  constexpr size_t UsageShowCount = 5;
  for (size_t i = 0; i < UsageShowCount && i < usage_vector.size(); i++)
    LOG_INFO << usage_vector[i].first << '='
             << pretty_size(usage_vector[i].second) << ' ';
  size_t remainder = 0;
  for (size_t i = UsageShowCount; i < usage_vector.size(); i++)
    remainder += usage_vector[i].second;
  LOG_INFO << "rest=" << pretty_size(remainder)
           << " / resident=" << pretty_size(resident_set*1024)
           << " vm=" << pretty_size(vm_usage*1024) << std::endl;
  // Full information to file
  ctx.out_stats->to_csv(ctx.stats_file, sim_now);
  ctx.out_stats->reset(sim_now);
}
