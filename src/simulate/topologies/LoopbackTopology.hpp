// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _LOOPBACK_TOPOLOGY_HPP
#define _LOOPBACK_TOPOLOGY_HPP

#include "AbstractTopology.hpp"

class LoopbackTopology : public AbstractTopology {

public:
  LoopbackTopology(char code_, std::vector<Resource> &resources_,
                   resind_t sub_count_, std::vector<int> params_,
                   simtime_t link_latency_, share_t link_capacity_,
                   const FaultProfileMap &link_fault_profiles)
      : AbstractTopology(code_, resources_, sub_count_, params_, link_latency_,
                         link_capacity_) {
    assert(params.empty());
    init_resources(1, 1, 0xFFFF, 0, link_fault_profiles);
#ifdef SHOW_DEBUG
    for (resind_t i = 0; i < sub_count; i++) {
      // CoreId from_id((i % n_regions) * NumberOfCoresPerRegion);
      // core_id_to_position(i % n_regions, position);
      std::stringstream ss;
      // ss << "Loop (" << position << ")";
      ss << code << ":Loop (" << i << ")";
      resources[start + i].name = ss.str();
    }
#endif
  }

  virtual void get_route(resind_t sub_src, resind_t, resind_t sub_dst, resind_t,
                         std::vector<resind_t> &route, simtime_t &latency) {
    if (sub_src != sub_dst)
      return;
    route.push_back(start + sub_src);
    latency += link_latency;
  }

  virtual void build_topology_graph() {
    mat.init(sub_count);
    for (resind_t i = 0; i < sub_count; i++)
      mat.add_weight(i, i, link_capacity);
  }
};

#endif