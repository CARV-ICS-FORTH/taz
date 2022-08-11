// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "AbstractJobHandling.hpp"
#include "scotch.h"

class TrafficMapper : public AbstractJobMapper {
  PartitionedGraph topology_graph;
  size_t n_available_hosts; // Used for lazy removal of values
  std::vector<SCOTCH_Num> available_hosts;
  bool available_hosts_needs_sorting;
  static constexpr SCOTCH_Num NoHost = std::numeric_limits<SCOTCH_Num>::max();

  SCOTCH_Context scotch_context;
  SCOTCH_Graph context_graph;
  // The architecture that contains all routers and nodes of the system
  SCOTCH_Arch *system_arch;
  // The architecture that contains only nodes that are available for mapping
  // currently
  SCOTCH_Arch *mapping_arch;

  void _update_arch() {

    _build_system_arch();

    if (mapping_arch != nullptr) {
      SCOTCH_archExit(mapping_arch);
      SCOTCH_memFree(mapping_arch);
    }

    mapping_arch = SCOTCH_archAlloc();
    check_result(SCOTCH_archInit(mapping_arch), 0);

    if (available_hosts_needs_sorting) {
      std::sort(available_hosts.begin(), available_hosts.end());
      available_hosts.resize(n_available_hosts);
      auto last = std::unique(available_hosts.begin(), available_hosts.end());
      if (last != available_hosts.end()) {
        available_hosts.erase(last, available_hosts.end());
        auto n = available_hosts.size();
        LOG_INFO << "Found " << n_available_hosts - n << " duplicates"
                 << std::endl;
        n_available_hosts = n;
      }
      available_hosts_needs_sorting = false;
    }

#ifdef SHOW_DEBUG
#endif

    LOG_DEBUG << "Available hosts before mapping: " << available_hosts
              << std::endl;
    check_result(SCOTCH_archSub(mapping_arch, system_arch, n_available_hosts,
                                available_hosts.data()),
                 0);
  }

  std::vector<SCOTCH_Num>::iterator get_host_iterator(SCOTCH_Num host_index) {
    return std::find(available_hosts.begin(), available_hosts.end(),
                     host_index);
  }

  void _build_system_arch() {
    if (system_arch != nullptr) {
      SCOTCH_archExit(system_arch);
      SCOTCH_memFree(system_arch);
    }
    system_arch = SCOTCH_archAlloc();
    check_result(SCOTCH_archInit(system_arch), 0);
    auto graph = topology_graph.get_graph();
#ifdef SHOW_DEBUG
    SCOTCH_graphCheck(graph);
#endif
    check_result(
        SCOTCH_contextBindGraph(&scotch_context, graph, &context_graph), 0);

    check_result(SCOTCH_archBuild2(system_arch, &context_graph, 0, nullptr), 0);
    // res = SCOTCH_archBuild(system_arch, graph, 0, nullptr,
    // partitionning_strat);
  }

public:
  TrafficMapper()
      : AbstractJobMapper("TrafficMapper"),
        topology_graph(ctx.engine->get_topology_matrix(), 1, 1000000),
        system_arch(nullptr), mapping_arch(nullptr) {

    int nthreads = ctx.vm["rm_traffic_scotch_nthreads"].as<int>();
    check_result(SCOTCH_contextInit(&scotch_context), 0);
    check_result(SCOTCH_contextThreadSpawn(&scotch_context, nthreads, nullptr),
                 0);

    int wants_determinism = 1;
    if (ctx.vm.count("rm_traffic_scotch_random_seed") > 0) {
      // User sets random seed
      int seed = ctx.vm["rm_traffic_scotch_random_seed"].as<int>();
      SCOTCH_contextRandomSeed(&scotch_context, seed);
      wants_determinism = 0;
    }

    check_result(SCOTCH_contextOptionSetNum(&scotch_context,
                                            SCOTCH_OPTIONNUMDETERMINISTIC,
                                            wants_determinism),
                 0);
    check_result(SCOTCH_contextOptionSetNum(&scotch_context,
                                            SCOTCH_OPTIONNUMRANDOMFIXEDSEED,
                                            wants_determinism),
                 0);
    SCOTCH_contextRandomReset(&scotch_context);

    auto vertnbr = topology_graph.get_vertnbr();

    available_hosts.reserve(vertnbr);
    n_available_hosts = 0;
    for (int i = 0; i < vertnbr; i++) {
      // Do not include routers...
      if (topology_graph.get_velotab()[i] > 0) {
        available_hosts.push_back(i);
        n_available_hosts++;
      }
    }
    available_hosts_needs_sorting = false;

    _build_system_arch();
  }

  virtual ~TrafficMapper() {
    SCOTCH_archExit(system_arch);
    SCOTCH_memFree(system_arch);
    if (mapping_arch) {
      SCOTCH_archExit(mapping_arch);
      SCOTCH_memFree(mapping_arch);
    }
    SCOTCH_contextExit(&scotch_context);
  }

  virtual void on_new_job(const JobDescriptor & /*desc*/, simtime_t /*time*/) {}

  virtual void on_host_state_change(resind_t host_index, simtime_t /*now*/,
                                    bool is_faulty) {

    if (is_faulty) {
      auto it = get_host_iterator(host_index);
      *it = NoHost;
      n_available_hosts--;
    } else {
      available_hosts.push_back(host_index);
      n_available_hosts++;
    }
    available_hosts_needs_sorting = true;
  }

  virtual void on_job_finished(int /*job_id*/, const JobDescriptor &desc,
                               simtime_t /*time*/, JobFinishType /*type*/) {
    for (auto &inter : desc.hosts) {
      for (resind_t h = inter.lower(); boost::icl::contains(inter, h); h++) {
        available_hosts.push_back(h);
        n_available_hosts++;
      }
    }
    available_hosts_needs_sorting = true;
  }

  virtual IntervalSet map_job(int job_id, const JobDescriptor &desc,
                              simtime_t /*time*/, resind_t required_hosts,
                              double /*expected_duration*/) {

    IntervalSet alloc;
    // Cannot satisfy mapping, just return empty allocation
    if (required_hosts > n_available_hosts)
      return alloc;

    ApproxMatrix mat;
    PartitionedGraph jobs_graph;
    jobs_graph.set_has_alloc(true);

    if (ctx.vm["resource_mapper"].as<std::string>() == "traffic" &&
        not desc.traffic_source.empty())
      mat.load(desc.traffic_source);
    else {
      mat.init(required_hosts);
      if (required_hosts > 16)
        mat.fill_symmetric_sequence({2, 0, 0, 0, 0, 0, 0});
      else
        mat.fill(1);
      // mat.show(ApproxMatrix::APPROX);
    }
    // Translate matrix into a symmetric graph
    jobs_graph.add_partition(mat, desc.ranks_per_host, job_id, 1, true);
    // jobs_graph.show();
    // jobs_graph.append_unconnected_vertices(NumberOfHosts);
    auto scotch_jobs_graph = jobs_graph.get_graph();
    SCOTCH_Graph context_jobs_graph;

    check_result(SCOTCH_contextBindGraph(&scotch_context, scotch_jobs_graph,
                                         &context_jobs_graph),
                 0);

    _update_arch();

    auto nparts = SCOTCH_archSize(mapping_arch);
    SCOTCH_Strat mapping_strat;
    check_result(SCOTCH_stratInit(&mapping_strat), 0);
    check_result(SCOTCH_stratGraphMapBuild(
                     &mapping_strat, SCOTCH_STRATBALANCE | SCOTCH_STRATSPEED,
                     (SCOTCH_Num)nparts, 0),
                 0);

    // Money time!!!
    LOG_DEBUG << "Partition of the sub-architecture before mapping: "
              << jobs_graph.get_raw_partition_vector() << std::endl;
    check_result(SCOTCH_graphMap(&context_jobs_graph, mapping_arch,
                                 &mapping_strat, jobs_graph.get_parttab()),
                 0);
    LOG_DEBUG << "Partition of the sub-architecture after mapping: "
              << jobs_graph.get_raw_partition_vector() << std::endl;
    SCOTCH_graphExit(&context_jobs_graph);

    SCOTCH_stratExit(&mapping_strat);
    /*
    for (size_t i = 0; i < NumberOfHosts; i++) {
      if (i % 7 != 0)
        jobs_graph.get_raw_partition_vector()[i] = -1;
    }

    #ifdef SHOW_DEBUG
        auto part2 = jobs_graph.get_raw_partition_vector();
        assert(part2.size() == NumberOfHosts);
        std::sort(part2.begin(), part2.end());
        for (resind_t i = 0; i < NumberOfHosts; i++)
          assert((resind_t)part2[i] == i);
    #endif
    */
    auto it_pair = jobs_graph.get_partition_alloc(job_id);

#ifdef SHOW_DEBUG
    // Check that a single index is only used one at most
    std::unordered_map<int, int> used_hosts;
    auto available_hosts_initial = available_hosts;
    int ind = 0;
    for (auto it = it_pair.first; it < it_pair.second; it++) {
      assert(*it < (int)available_hosts.size());
      auto it_used = used_hosts.find(*it);
      if (it_used != used_hosts.end()) {
        for (auto it2 = it_pair.first; it2 < it_pair.second; it2++)
          LOG_ERROR << *it2 << ' ';
        LOG_ERROR << std::endl
                  << "Using twice index " << *it << " in "
                  << available_hosts_initial.size() << ":"
                  << available_hosts_initial << std::endl;
        auto arch_file = std::fopen("bad.arch", "w");
        SCOTCH_archSave(mapping_arch, arch_file);
        auto graph_file = std::fopen("bad.graph", "w");
        SCOTCH_graphSave(&context_jobs_graph, graph_file);
        MUST_DIE
      }
      used_hosts.insert({*it, ind});
      ind++;
    }
#endif

    for (auto it = it_pair.first; it < it_pair.second; it++) {
      assert(*it < (int)available_hosts.size());
      int host_index = available_hosts[*it];
      available_hosts[*it] = NoHost;
      alloc += (resind_t)host_index;
      n_available_hosts--;
    }
    available_hosts_needs_sorting = true;
    return alloc;
  }

  virtual size_t get_footprint() {
    size_t footprint = sizeof(TrafficMapper);
    footprint += GET_HEAP_FOOTPRINT(topology_graph);
    footprint += GET_HEAP_FOOTPRINT(available_hosts);
    auto scotch_footprint = SCOTCH_memCur();
    assert_always(scotch_footprint >= 0,
                  " You need to compile libscotch with COMMON_MEMORY_TRACE "
                  "flag set");
    return footprint + scotch_footprint;
  }
};
