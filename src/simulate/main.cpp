// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "Engine.hpp"
#include "Model.hpp"
#include "ResourceManager.hpp"
#include "Statistics.hpp"
#include "colls/Tree.hpp"
#include "taz-simulate.hpp"
#include "topologies/AbstractTopology.hpp"

#ifdef USE_PAPI
#include <papi.h>
#endif

int main(int argc, char *argv[]) {
  /*
    constexpr size_t N = 16;
    constexpr size_t N_LOG2 = 16;

    int results[N][N_LOG2];
    for (size_t i = 0; i < N; i++) {
      if (i > 0)
        for (size_t j = 0; j < N_LOG2; j++)
          results[i][j] = results[i - 1][j];
      else
        for (size_t j = 0; j < 4; j++)
          results[i][j] = 0;
      for (size_t j = 0; j < 4; j++)
        if (i & (1 << j))
          results[i][j]++;
    }
    LOG_DEBUG << "        \t";
    for (size_t i = 0; i < N; i++) {
      LOG_DEBUG << i+1 << "\t";
    }
    LOG_DEBUG << std::endl;
    for (size_t j = 0; j < 4; j++) {
      LOG_DEBUG << "pof2=" << (1 << j) << " :\t";
      for (size_t i = 0; i < N; i++) {
        LOG_DEBUG << results[i][j] << "\t";
      }
      LOG_DEBUG << std::endl;
    }
    return 0;
    */
  /*
  Tree<int, -1> tree;
  LOG_INFO << "Tree root 0 fanout 2 size 10" << std::endl;
  tree.build_tree(0, 2, 10);
  tree.show();
  LOG_INFO << "Tree root 5 fanout 3 size 14" << std::endl;
  tree.build_tree(5, 3, 14);
  tree.show();
  LOG_INFO << "Bintree size 2" << std::endl;
  tree.build_in_order_bintree(2);
  tree.show();
  LOG_INFO << "Bintree size 3" << std::endl;
  tree.build_in_order_bintree(3);
  tree.show();
  LOG_INFO << "Bintree size 4" << std::endl;
  tree.build_in_order_bintree(4);
  tree.show();
  LOG_INFO << "Bintree size 9" << std::endl;
  tree.build_in_order_bintree(9);
  tree.show();
  LOG_INFO << "Bmtree size 2" << std::endl;
  tree.build_bmtree(0, 2, false);
  tree.show();
  LOG_INFO << "Bmtree size 4" << std::endl;
  tree.build_bmtree(0, 4, false);
  tree.show();
  LOG_INFO << "Bmtree size 8" << std::endl;
  tree.build_bmtree(0, 8, false);
  tree.show();
  // LOG_INFO << "Inorder bmtree size 2" << std::endl;
  // tree.build_bmtree(0, 2, true);
  // tree.show();
  // LOG_INFO << "inorder bmtree size 4" << std::endl;
  // tree.build_bmtree(0, 4, true);
  // tree.show();
  // LOG_INFO << "inorder bmtree size 8" << std::endl;
  // tree.build_bmtree(0, 8, true);
  // tree.show();
  LOG_INFO << "kmtree size 10 radix 2" << std::endl;
  tree.build_kmtree(0, 2, 10);
  tree.show();
  LOG_INFO << "kmtree size 10 radix 3" << std::endl;
  tree.build_kmtree(0, 3, 10);
  tree.show();
  LOG_INFO << "kmtree size 10 radix 4" << std::endl;
  tree.build_kmtree(0, 4, 10);
  tree.show();
  LOG_INFO << "chain size 10 fanout 1" << std::endl;
  tree.build_chain(0, 1, 10);
  tree.show();
  LOG_INFO << "chain size 10 fanout 3" << std::endl;
  tree.build_chain(0, 3, 10);
  tree.show();
  LOG_INFO << "chain size 10 fanout 4" << std::endl;
  tree.build_chain(0, 4, 10);
  tree.show();
  return 0;
  */
  /*
    uint64_t x;
    std::cout << "ceil_log2:" << std::endl;
    for (x = 1; x < 10; x++)
      std::cout << x << " : " << ceil_log2(x) << std::endl;
    std::cout << "floor_log2:" << std::endl;
    for (x = 1; x < 10; x++)
      std::cout << x << " : " << floor_log2(x) << std::endl;
    std::cout << "trunc:" << std::endl;
    for (x = 0; x < 10; x++)
      std::cout << x << " : " << trunc_pof2<uint64_t>(x, 2UL) << std::endl;
    std::cout << "diff_mod:" << std::endl;
    for (x = 0; x < 10; x++)
      std::cout << x << " : " << diff_mod<uint64_t>(x, 2UL, 5UL) <<
    std::endl; std::cout << "div_ceil:" << std::endl; for (x = 0; x < 10;
    x++) std::cout << x << " : " << ceil_div<uint64_t>(x, 3UL) << std::endl;
  */
  /*
   InhibitVerboseMessages = false;
   InhibitDebugMessages = false;
 */
  /*
    std::vector<Resource> resources;
    AbstractTopology *t_loop = AbstractTopology::build_instance(
        "LOOPBACK", 'T', resources, 20, 2, 1000, 10, 0);
    t_loop->build_topology_graph("loopback.py");
    AbstractTopology *t_torus = AbstractTopology::build_instance(
        "TORUS:3x2", 'B', resources, 1, 2, 1000, 20, 0);
    t_torus->build_topology_graph("torus.py");
    AbstractTopology *t_fattree = AbstractTopology::build_instance(
        "FATTREE:2;4,4;1,2;1,4", 'W', resources, 1, 2, 1000, 1, 0);
    t_fattree->build_topology_graph("fattree.py");
    AbstractTopology *t_fattree2 = AbstractTopology::build_instance(
        "FATTREE:2;18,3;1,6;1,3", 'V', resources, 1, 2, 1000, 1, 0);
    t_fattree2->build_topology_graph("fattree2.py");
  */

  // PartitionedGraph::test();

  start_profile_region("initialization");
  if (not configure(argc, argv))
    return -2;
  end_profile_region("initialization");

  simtime_t time = 0;
  simtime_t max_simulation_time =
      double_to_simtime(ctx.vm["endtime"].as<double>());

  start_profile_region("main");
  bool abort = false;
  bool parse_only = ctx.vm["parse_only"].as<bool>();
  int advancing_events = 0;
  int count_no_progress = 0;
  if (ctx.vm["collect_traffic"].as<bool>())
    ctx.engine->start_traffic_collection();
  if (parse_only)
    ctx.resource_manager->do_schedule(0);
  while (not ctx.resource_manager->is_done() &&
         ctx.engine->get_last_time() < max_simulation_time) { //|| cont_sim
    start_profile_region("main_parse");
    bool cnt_parsing = ctx.parsers->parse();
    end_profile_region("main_parse");
    if (SnapshotsOccurence & 0x1) {
      std::stringstream ss;
      ss << ctx.total_stats->simulation_outer_iterations << "_pre";
      AAPool::get_instance()->save_snapshot(ss.str(), time);
    }
    if (not cnt_parsing) {
      if (advancing_events) {
        LOG_DEBUG << "Not parsing (" << advancing_events
                  << " advancing events).." << std::endl;
      } else {
        LOG_INFO << "Not advancing" << std::endl;
      }
      count_no_progress++;
      if (parse_only)
        break;
      if (count_no_progress >= 3) {
        LOG_ERROR << "Simulation iterates but no new lines are being parsed "
                     "nor is there actual progress. "
                     "Looks like a livelock, abort!"
                  << std::endl;
        abort = true;
        break;
      }
    } else
      count_no_progress = 0;
    if (not parse_only) {
      advancing_events = 0;
      time = ctx.engine->run(advancing_events);
      if (advancing_events)
        count_no_progress = 0;
      if (SnapshotsOccurence & 0x4) {
        std::stringstream ss2;
        ss2 << ctx.total_stats->simulation_outer_iterations << "_post";
        AAPool::get_instance()->save_snapshot(ss2.str(), time);
      }
      INCREMENT_STAT(simulation_outer_iterations);
    }
    update_stats(false, time);
  }
  end_profile_region("main");

  // simtime_t final_time = ctx.pool->get_final_time();

  update_stats(true, time);
  if (SnapshotsOccurence & 0x8) {
    AAPool::get_instance()->save_snapshot("final", time);
  }

  if (ctx.vm["collect_traffic"].as<bool>()) {
    ctx.engine->stop_traffic_collection();
    auto &traffic = ctx.engine->get_traffic();
    std::stringstream ss;
    ss << ctx.vm["stats_folder"].as<std::string>() << "/traffic.tazm";
    if (not InhibitVerboseMessages)
      traffic.show();
    traffic.save(ss.str(), "traffic", ApproxMatrix::APPROX);
  }

  if (ctx.vm["collect_state_changes"].as<bool>()) {
    auto state_changes = ctx.engine->get_state_changes();
    std::stringstream ss;
    ss << ctx.vm["stats_folder"].as<std::string>() << "/state_changes.tazm";
    if (not InhibitVerboseMessages)
      state_changes.show();
    state_changes.save(ss.str(), "state_changes", ApproxMatrix::EXACT);
  }

  if (abort) {
    LOG_ERROR << "Abort simulation." << std::endl;
    return -5;
  }

  double time_dbl = simtime_to_double(time);
  LOG_INFO << "Finished at time " << time_dbl << std::endl;
  LOG_INFO << "There were " << ctx.engine->error_count
           << " errors amounting to "
           << simtime_to_double(ctx.engine->sum_of_errors) << " seconds"
           << std::endl;
  return 0;
}
