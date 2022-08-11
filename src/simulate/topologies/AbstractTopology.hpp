// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef ABSTRACT_TOPOLOGY_HPP
#define ABSTRACT_TOPOLOGY_HPP

#include "../Resource.hpp"

class AbstractTopology : public MemoryConsumer {
protected:
  // A single char to help with debug
  char code;
  // The global vector with all the resources
  std::vector<Resource> &resources;
  // Index of the first resource within this topology instance
  resind_t start;
  // Number of sub-topologies
  resind_t sub_count;
  resind_t n_links_per_sub;
  resind_t n_endpoints_per_sub;
  // Parameters of the topology
  std::vector<int> params;
  simtime_t link_latency;
  share_t link_capacity;
  // A dense matrix to optionally save topology connectivity
  ApproxMatrix mat;

  void init_resources(resind_t n_links_per_sub_, resind_t n_endpoints_per_sub_,
                      uint16_t link_concurrency, double link_nominal_mtbf_log10,
                      const FaultProfileMap &link_fault_profiles) {
    start = (resind_t)resources.size();
    n_links_per_sub = n_links_per_sub_;
    n_endpoints_per_sub = n_endpoints_per_sub_;
    auto n_resources = sub_count * n_links_per_sub;
    resources.resize(start + n_resources);
    std::normal_distribution<>::param_type LinkParamsFaultShape =
        std::normal_distribution<>::param_type(
            1, ctx.vm["mtbf_stdev_shape_log10"].as<double>());
    std::normal_distribution<>::param_type LinkParamsFaultScale;
    if (link_nominal_mtbf_log10 > 0)
      LinkParamsFaultScale = std::normal_distribution<>::param_type(
          link_nominal_mtbf_log10,
          ctx.vm["mtbf_stdev_scale_log10"].as<double>());

    std::normal_distribution<> dist_normal;

    for (resind_t i = 0; i < n_resources; i++) {
      resind_t resindex = start + i;
      Resource &r = resources[resindex];
      // r.total_weights=0;
      // r.total_concurrency=0;
      r.first = NoAction;
      r.first_el = 0;
      r.first_pair = NoResource;
      r.type = Resource::SHARED;
      r.index = NoResource;
      r.outbound_concurrency = link_concurrency;
      r.first_outbound = NoAction;
      r.last_outbound = NoAction;
      r.capacity = r.slack = link_capacity;
      r.el.rindex = resindex;
      r.el.type = EventHeapElement::LINK_RECOVERY;
      // Init link stochastic law if needed
      bool has_stochastic_law = (link_nominal_mtbf_log10 > 0);
      if (has_stochastic_law) {
        double shape = FaultyEntity::pick_normal_value_log10(
            mt19937_gen_fault_links, dist_normal, LinkParamsFaultShape);
        double scale = FaultyEntity::pick_normal_value_log10(
            mt19937_gen_fault_links, dist_normal, LinkParamsFaultScale);
        r.set_fault_params(FaultDistribution::param_type(shape, scale));
      } else
        r.failure_rate = 0;

      auto it = link_fault_profiles.find(resindex);
      bool has_deterministic_profile = (it != link_fault_profiles.end());
      if (has_deterministic_profile)
        r.profile = it->second;

      if (has_stochastic_law || has_deterministic_profile)
        r.push_next_event(0);
      else
        r.fault_handle.free();
    }
  }

  resind_t get_global_resindex(resind_t sub, resind_t index) {
    assert(index < n_links_per_sub);
    return start + sub * n_links_per_sub + index;
  }

protected:
  AbstractTopology(char code_, std::vector<Resource> &resources_,
                   resind_t sub_count_, std::vector<int> params_,
                   simtime_t link_latency_, share_t link_capacity_)
      : MemoryConsumer("Topology_" + code_), code(code_), resources(resources_),
        sub_count(sub_count_), params(params_), link_latency(link_latency_),
        link_capacity(link_capacity_) {}

public:
  virtual ~AbstractTopology() {}
  virtual void get_route(resind_t sub_src, resind_t index_src, resind_t sub_dst,
                         resind_t index_dst, std::vector<resind_t> &route,
                         simtime_t &latency) = 0;

  int &_get_match(bool src_is_endpoint, int src, bool dst_is_endpoint, int dst,
                  std::vector<int> &matches);

  virtual void build_topology_graph() = 0;
  const ApproxMatrix &get_connectivity_matrix() {
    if (mat.empty())
      build_topology_graph();
    return mat;
  }
  const ApproxMatrix &get_connectivity_matrix() const {
    assert(not mat.empty());
    return mat;
  }

  resind_t get_total_endpoints() { return sub_count * n_endpoints_per_sub; }

  static AbstractTopology *
  build_instance(std::string specs, char code_,
                 std::vector<Resource> &resources_, resind_t sub_count_,
                 uint16_t link_concurrency, share_t link_capacity,
                 simtime_t link_latency_, double link_nominal_mtbf,
                 const FaultProfileMap &link_fault_profiles);

  virtual size_t get_footprint() {
    return sizeof(AbstractTopology) + GET_HEAP_FOOTPRINT(mat);
  }
};

#endif
