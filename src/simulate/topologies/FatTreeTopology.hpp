// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _FAT_TREE_TOPOLOGY_HPP
#define _FAT_TREE_TOPOLOGY_HPP

#include "AbstractTopology.hpp"
/**
 * @brief A topology object describing a fat tree network
 *
 * Params follows the same syntax as Simgrid fat tree netzone:
 * https://simgrid.org/doc/latest/Platform_examples.html#fat-tree-cluster
 *
 * - [0]: Number of switch levels (L)
 * - [1,L]: The number of children of each switch, for each level
 *          (ie. [1]->level 0, [2] -> level 1, etc)
 * - [L+1,2L]: The number of parents for each switch/host, for each level.
 * - [2L+1,3L]: The number of links for each child-parent pair, for each level.
 *              In practice, we just increase capacity of a single link
 */
class FatTreeTopology : public AbstractTopology {

  int n_levels;
  resind_t routing_key_mod;
  std::vector<int> num_children_per_node;
  std::vector<int> num_parents_per_node;
  std::vector<int> num_ports_lower_level;
  std::vector<int> nodes_by_level;
  std::vector<int> links_by_level;
  std::vector<int> base_by_level;

  /*
    void index_to_position(resind_t index, std::vector<int> &position) {
      position.clear();
      for (auto &&d : params) {
        position.push_back(index % d);
        index /= d;
      }
    }

    resind_t position_to_index(const std::vector<int> &position) {
      resind_t index = 0;
      assert(position.size() == params.size());
      for (int i = (int)params.size() - 1; i >= 0; i--) {
        index += position[i];
        if (i)
          index *= params[i - 1];
      }
      return index;
    }
  */

public:
  FatTreeTopology(char code_, std::vector<Resource> &resources_,
                  resind_t sub_count_, std::vector<int> params_,
                  uint16_t link_concurrency, simtime_t link_latency_,
                  share_t link_capacity, double link_nominal_mtbf_pow10,
                  const FaultProfileMap &link_fault_profiles)
      : AbstractTopology(code_, resources_, sub_count_, params_, link_latency_,
                         link_capacity) {
    assert(params.size() > 1);
    n_levels = params[0];
    assert((int)params.size() == 1 + 3 * n_levels);
    num_children_per_node.resize(n_levels);
    num_parents_per_node.resize(n_levels);
    num_ports_lower_level.resize(n_levels);
    nodes_by_level.resize(n_levels + 1);
    links_by_level.resize(n_levels + 1);
    base_by_level.resize(n_levels);
    routing_key_mod = 1;
    for (int i = 0; i < n_levels; i++) {
      num_children_per_node[i] = params[1 + i];
      num_parents_per_node[i] = params[1 + n_levels + i];
      routing_key_mod *= num_parents_per_node[i];
      // I do not understand how to deal with fat trees with redundant switches
      // other than at the trees, so crash..
      assert(num_parents_per_node[i] == 1 || i + 1 == n_levels);
      num_ports_lower_level[i] = params[1 + 2 * n_levels + i];
    }

    n_endpoints_per_sub = 1;
    for (auto x : num_children_per_node)
      n_endpoints_per_sub *= x;
    nodes_by_level[0] = n_endpoints_per_sub;

    for (int i = 0; i < n_levels; i++) {
      int nodesInThisLevel = 1;

      for (int j = 0; j <= i; j++)
        nodesInThisLevel *= num_parents_per_node[j];

      for (int j = i + 1; j < n_levels; j++)
        nodesInThisLevel *= num_children_per_node[j];

      nodes_by_level[i + 1] = nodesInThisLevel;
    }

    n_links_per_sub = 0;
    for (int i = 0; i < n_levels; i++) {
      links_by_level[i] = 2 * nodes_by_level[i] * num_parents_per_node[i];
      n_links_per_sub += links_by_level[i];
    }

    init_resources(n_links_per_sub, n_endpoints_per_sub, link_concurrency,
                   link_nominal_mtbf_pow10, link_fault_profiles);

    std::vector<int> position;
    base_by_level[0] = 0;
    for (resind_t s = 0; s < sub_count; s++) {
      for (int l = 0; l < n_levels; l++) {
        auto np = num_parents_per_node[l];
        for (int i = 0; i < nodes_by_level[l]; i++)
          for (int p = 0; p < 2 * np; p++) {

            resind_t resindex =
                get_global_resindex(s, base_by_level[l] + i * 2 * np + p);
            auto &r = resources[resindex];
            r.slack = r.capacity = link_capacity * num_ports_lower_level[l];
#ifdef SHOW_DEBUG
            std::stringstream ss;
            ss << s << ":L" << l << (l == 0 ? "e" : "s") << i << "p" << p % np;
            ss << ((p < np) ? "-UP->" : "<-DN-");
            // ss << " from (" << position << ")";
            resources[resindex].name = ss.str();
#endif
          }
        if (s == 0 && l + 1 < n_levels)
          base_by_level[l + 1] = base_by_level[l] + nodes_by_level[l] * 2 *
                                                        num_parents_per_node[l];
      }
    }
  }

  virtual void get_route(resind_t sub_src, resind_t index_src, resind_t sub_dst,
                         resind_t index_dst, std::vector<resind_t> &route,
                         simtime_t &latency) {
    (void)sub_dst;
    // Need to define gateway otherwise..
    assert(sub_src == sub_dst);
    if (index_src == index_dst)
      return;
    // resind_t subtree_size = 1;
    std::vector<resind_t> downwards_links;
    // Go UP until destination is in the same subtree
    LOG_DEBUG << "(" << index_src << ")";
    int l = 0;
    resind_t src_node_index = index_src;
    resind_t dst_node_index = index_dst;
    resind_t routing_key = (index_dst ^ index_src) % routing_key_mod;

    while (src_node_index != dst_node_index) {
      assert(l < n_levels);
      assert(src_node_index < (resind_t)nodes_by_level[l]);
      assert(dst_node_index < (resind_t)nodes_by_level[l]);
      resind_t np = num_parents_per_node[l];
      resind_t nc = num_children_per_node[l];
      resind_t port_index = (np > 1 ? routing_key % np : 0);
      routing_key /= np;
      resind_t src_resindex = get_global_resindex(
          sub_src, base_by_level[l] + src_node_index * np * 2 + port_index);
      route.push_back(src_resindex);
      resind_t dst_resindex = get_global_resindex(
          sub_src,
          base_by_level[l] + dst_node_index * np * 2 + np + port_index);
      downwards_links.push_back(dst_resindex);
      auto next_src_node_index = src_node_index / nc + port_index;
      auto next_dst_node_index = dst_node_index / nc + port_index;
#ifdef SHOW_DEBUG
      LOG_DEBUG << "Level " << l << " UP: " << src_node_index << "-{"
                << src_resindex << "=" << resources[src_resindex].name << "}->"
                << next_src_node_index << " DN: " << dst_node_index << "<-{"
                << dst_resindex << "=" << resources[dst_resindex].name << "}-"
                << next_dst_node_index << std::endl;
#endif
      src_node_index = next_src_node_index;
      dst_node_index = next_dst_node_index;
      // subtree_size *= nc;
      l++;
    }

    // Go DOWN to the destination node
    while (not downwards_links.empty()) {
      route.push_back(downwards_links.back());
      downwards_links.pop_back();
    }

    latency += 2 * l * link_latency;
  }

  virtual void build_topology_graph() { NOT_IMPLEMENTED_YET }
};

#endif