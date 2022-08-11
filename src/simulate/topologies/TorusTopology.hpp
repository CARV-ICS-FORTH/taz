// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _TORUS_TOPOLOGY_HPP
#define _TORUS_TOPOLOGY_HPP

#include "AbstractTopology.hpp"

class TorusTopology : public AbstractTopology {

  std::vector<resind_t> base_by_dimension;

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

public:
  TorusTopology(char code_, std::vector<Resource> &resources_,
                resind_t sub_count_, std::vector<int> params_,
                uint16_t link_concurrency, simtime_t link_latency_,
                share_t link_capacity, double link_nominal_mtbf_pow10,
                const FaultProfileMap &link_fault_profiles)
      : AbstractTopology(code_, resources_, sub_count_, params_, link_latency_,
                         link_capacity) {
    n_endpoints_per_sub = 1;
    for (auto x : params)
      n_endpoints_per_sub *= x;
    n_links_per_sub = 0;
    for (auto x : params) {
      base_by_dimension.push_back(n_links_per_sub);
      assert(x >= 2);
      if (x > 2)
        n_links_per_sub += 2 * n_endpoints_per_sub;
      else
        n_links_per_sub += n_endpoints_per_sub;
    }

    init_resources(n_links_per_sub, n_endpoints_per_sub, link_concurrency,
                   link_nominal_mtbf_pow10, link_fault_profiles);
    std::vector<int> position;
#ifdef SHOW_DEBUG
    resind_t n_dimensions = (resind_t)params.size();
    for (resind_t s = 0; s < sub_count; s++)
      for (resind_t d = 0; d < n_dimensions; d++) {
        resind_t n_links =
            params[d] > 2 ? 2 * n_endpoints_per_sub : n_endpoints_per_sub;
        for (resind_t i = 0; i < n_links; i++) {
          index_to_position(i, position);
          resind_t resindex = get_global_resindex(s, base_by_dimension[d] + i);
          std::stringstream ss;
          ss << code << ':' << s << ":Dim " << d
             << (i < n_endpoints_per_sub ? '+' : '-') << " from (" << position
             << ")";
          resources[resindex].name = ss.str();
        }
      }

#endif
  }

  virtual void build_topology_graph() {
    assert(sub_count == 1);
    mat.init(n_endpoints_per_sub);
    std::vector<int> position, position2;
    resind_t j;
    resind_t n_dimensions = (resind_t)params.size();
    for (resind_t d = 0; d < n_dimensions; d++) {
      for (resind_t i = 0; i < n_endpoints_per_sub; i++) {
        index_to_position(i, position);
        position2 = position;
        position2[d] = (position[d] + 1) % params[d];
        // If dimension is only two, no need for looping link
        if (params[d] == 2 && (i % d == 1))
          continue;
        j = position_to_index(position2);
        mat.add_weight(i, j, link_capacity);
        mat.add_weight(j, i, link_capacity);
      }
    }
  }

  virtual void get_route(resind_t sub_src, resind_t index_src, resind_t sub_dst,
                         resind_t index_dst, std::vector<resind_t> &route,
                         simtime_t &latency) {
    if (sub_src != sub_dst) {
      // Assume index 0 is the gateway
      get_route(sub_src, index_src, sub_src, 0, route, latency);
      get_route(sub_dst, 0, sub_dst, index_dst, route, latency);
      return;
    }

    std::vector<int> src_pos, dst_pos, mid_pos;
    const size_t n_dims = params.size();
    int hops = 0;
    index_to_position(index_src, src_pos);
    index_to_position(index_dst, dst_pos);
    mid_pos = src_pos;
    LOG_DEBUG << "(" << src_pos << ")";
    for (size_t d = 0; d < n_dims; d++) {
      resind_t dist = (params[d] + dst_pos[d] - src_pos[d]) % params[d];
      if (dist <= (resind_t)params[d] / 2) {
        // Go  up
        while (mid_pos[d] != dst_pos[d]) {
          resind_t link_index = get_global_resindex(
              sub_src, base_by_dimension[d] + position_to_index(mid_pos));
          route.push_back(link_index);
          mid_pos[d] = (mid_pos[d] + 1) % params[d];
          LOG_DEBUG << "+" << link_index << "+>(" << mid_pos << ")";
          hops++;
        }
      } else {
        // Go  down
        while (mid_pos[d] != dst_pos[d]) {
          resind_t link_index = get_global_resindex(
              sub_src, base_by_dimension[d] + n_endpoints_per_sub +
                           position_to_index(mid_pos));
          route.push_back(link_index);
          mid_pos[d] = (params[d] + mid_pos[d] - 1) % params[d];
          LOG_DEBUG << "-" << link_index << "->(" << mid_pos << ")";
          hops++;
        }
      }
    }
    LOG_DEBUG << "=(" << dst_pos << ")" << std::endl;
    latency += hops * link_latency;
  }
};

#endif
