// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "AbstractTopology.hpp"
#include "FatTreeTopology.hpp"
#include "LoopbackTopology.hpp"
#include "TorusTopology.hpp"

constexpr const char *DelimitersList = ",;:x*";

AbstractTopology *AbstractTopology::build_instance(
    std::string specs, char code, std::vector<Resource> &resources,
    resind_t sub_count, uint16_t link_concurrency, share_t link_capacity,
    simtime_t link_latency, double link_nominal_mtbf_log10,
    const FaultProfileMap &link_fault_profiles) {

  GenericParser parser;
  const char *ptr = parser.init_parsing(specs.data(), DelimitersList);
  std::string type(ptr);
  std::vector<int> params;
  while ((ptr = parser.try_next_token(DelimitersList)) != nullptr) {
    params.push_back(atoi(ptr));
  }

  // Handle convenience shortcuts
  if (type == "TINY") {
    type = "TORUS";
    params.assign({4, 4});
  } else if (type == "MEDIUM") {
    type = "TORUS";
    params.assign({4, 4, 4, 4, 4});
  } else if (type == "LARGE") {
    type = "TORUS";
    params.assign({8, 8, 8, 8, 4, 4});
  }

  // Now the real thing
  AbstractTopology *instance = nullptr;
  if (type == "LOOPBACK")
    instance = (AbstractTopology *)new LoopbackTopology(
        code, resources, sub_count, params, link_latency, link_capacity,
        link_fault_profiles);
  if (type == "TORUS")
    instance = (AbstractTopology *)new TorusTopology(
        code, resources, sub_count, params, link_concurrency, link_latency,
        link_capacity, link_nominal_mtbf_log10, link_fault_profiles);
  if (type == "FATTREE")
    instance = (AbstractTopology *)new FatTreeTopology(
        code, resources, sub_count, params, link_concurrency, link_latency,
        link_capacity, link_nominal_mtbf_log10, link_fault_profiles);

  if (instance != nullptr) {
    /*
    resind_t first_link = instance->get_global_resindex(0, 0);
    for (resind_t i = 0; i < instance->n_links_per_sub; i++) {
      LOG_DEBUG << "Link " << i << "\t:" << resources[first_link + i].name
                << std::endl;
    }*/
    return instance;
  }
  LOG_ERROR << "I do not know topology type <" << type << ">" << std::endl;
  MUST_DIE
}

int &AbstractTopology::_get_match(bool src_is_endpoint, int src,
                                  bool dst_is_endpoint, int dst,
                                  std::vector<int> &matches) {
  const resind_t N = n_links_per_sub + n_endpoints_per_sub;
  resind_t src_id = (src_is_endpoint ? src : n_endpoints_per_sub + src);
  resind_t dst_id = (dst_is_endpoint ? dst : n_endpoints_per_sub + dst);
  return matches.at(src_id * N + dst_id);
}

typedef std::queue<std::pair<resind_t, bool>> Queue;

/*
void AbstractTopology::build_topology_graph(std::string filename) {
  std::vector<resind_t> route;
  constexpr resind_t Sub = 0;
  const resind_t N = n_links_per_sub + n_endpoints_per_sub;
  std::vector<int> matches(N * N, 0);
  std::vector<int> link_initiator(n_links_per_sub, -1);
  std::vector<int> link_target(n_links_per_sub, -1);
  resind_t first_link = get_global_resindex(Sub, 0);
  Queue q;
  // Build an adjacency matrix for all pair of sources and destinations
  for (resind_t src = 0; src < n_endpoints_per_sub; src++)
    for (resind_t dst = 0; dst < n_endpoints_per_sub; dst++) {
      route.clear();
      simtime_t latency = 0;
      get_route(Sub, src, Sub, dst, route, latency);
      assert(latency == route.size() * link_latency);
      assert(src == dst || not route.empty());
      if (route.empty())
        continue;
      for (auto &x : route) {
        assert(x >= first_link);
        x -= first_link;
      }
      _get_match(true, src, false, route[0], matches) = -1;
      auto &init = link_initiator[route[0]];
      assert(init == -1 || init == (int)src);
      init = src;
      for (size_t i = 0; i + 1 < route.size(); i++)
        _get_match(false, route[i], false, route[i + 1], matches) = -1;
      _get_match(false, route.back(), true, dst, matches) = -1;
      auto &target = link_target[route.back()];
      assert(target == -1 || target == (int)dst);
      target = dst;
    }


    for (size_t i = 0; i < n_links_per_sub; i++) {
      LOG_DEBUG << "Link " << i << "\tInit:" << link_initiator[i]
                << " Target:" << link_target[i] << std::endl;
    }
    LOG_DEBUG << "Matches before propagation:" << std::endl;
    for (resind_t src = 0; src < N; src++) {
      LOG_DEBUG << src << "\t:";
      for (resind_t dst = 0; dst < N; dst++) {
        auto &v = matches[src * N + dst];
        if (v == 0) {
          LOG_DEBUG << "_";
        } else if (v == -1) {
          LOG_DEBUG << "x";
        } else
          LOG_DEBUG << v % 10;
        if (dst % 10 == 9) {
          LOG_DEBUG << "  ";
        } else if (dst % 5 == 4)
          LOG_DEBUG << " ";
      }
      LOG_DEBUG << std::endl;
    }

  // Infer whether there are switches
  // If second is true, index is out link. If false this is an in link
  int next_switch = n_endpoints_per_sub;
  bool found_new_switch = false;
  for (resind_t src = 0; src < n_links_per_sub; src++) {
    int current_switch = next_switch;
    for (resind_t dst = 0; dst < n_links_per_sub; dst++)
      if (_get_match(false, src, false, dst, matches) == -1) {
        q.emplace(src, false);
        q.emplace(dst, true);
        auto &target = link_target[src];
        auto &init = link_initiator[dst];
        // Did we catch an endpoint here?
        if (target >= 0 && target < (int)n_endpoints_per_sub) {
          assert(current_switch == target || current_switch == next_switch);
          current_switch = target;
          assert(init == -1 || target == init);
        } else if (init >= 0 && init < (int)n_endpoints_per_sub) {
          assert(current_switch == init || current_switch == next_switch);
          current_switch = init;
        } else {
          assert(target == -1 || target == next_switch);
          target = next_switch;
          assert(init == -1 || init == next_switch);
          init = next_switch;
          found_new_switch = true;
        }
      }
    for (resind_t dst = 0; dst < n_links_per_sub; dst++)
      if (_get_match(false, src, false, dst, matches) == -1)
        _get_match(false, src, false, dst, matches) = current_switch;

    // Propagate to all links incomoing or outgoing from this switch
    while (not q.empty()) {
      auto &x = q.front();
      if (x.second) {
        // We have an out link
        for (resind_t src2 = 0; src2 < n_endpoints_per_sub; src2++) {
          auto v = _get_match(true, src2, false, x.first, matches);
          if (v)
            v = current_switch;
        }
        for (resind_t src2 = 0; src2 < n_links_per_sub; src2++) {
          auto &v = _get_match(false, src2, false, x.first, matches);
          if (v == 0)
            continue;
          if (v == -1) {
            v = current_switch;
            q.emplace(src2, false);
          }
          assert(v == current_switch);
        }
        auto &init = link_initiator[x.first];
        assert(init == -1 || init == current_switch);
        init = current_switch;
      } else {
        // We have an in link
        for (resind_t dst = 0; dst < n_endpoints_per_sub; dst++) {
          auto v = _get_match(false, x.first, true, dst, matches);
          if (v)
            v = current_switch;
        }
        for (resind_t dst = 0; dst < n_links_per_sub; dst++) {
          auto &v = _get_match(false, x.first, false, dst, matches);
          if (v == 0)
            continue;
          if (v == -1) {
            v = current_switch;
            q.emplace(dst, true);
          }
          assert(v == current_switch);
        }
        auto &target = link_target[x.first];
        assert(target == -1 || target == current_switch);
        target = current_switch;
      }
      q.pop();
    }
    if (found_new_switch) {
      next_switch++;
      found_new_switch = false;
    }
  }

    LOG_DEBUG << "Matches after propagation:" << std::endl;
    for (resind_t src = 0; src < N; src++) {
      LOG_DEBUG << src << "\t:";
      for (resind_t dst = 0; dst < N; dst++) {
        auto &v = matches[src * N + dst];
        if (v == 0) {
          LOG_DEBUG << "_";
        } else if (v == -1) {
          LOG_DEBUG << "x";
        } else
          LOG_DEBUG << v % 10;
        if (dst % 10 == 9) {
          LOG_DEBUG << "  ";
        } else if (dst % 5 == 4)
          LOG_DEBUG << " ";
      }
      LOG_DEBUG << std::endl;
    }


  //  Preamble for the file
  std::ofstream stream(filename);
  stream << "topology={  'graph': {" << std::endl
         << "    'directed':True," << std::endl
         << "    'metadata': {" << std::endl
         << "      'node_label_size': 14," << std::endl
         << "      'node_label_color': 'green'," << std::endl
         << "      'edge_label_size': 10," << std::endl
         << "      'edge_label_color': 'blue'," << std::endl
         << "    }," << std::endl
         << "    'nodes': {" << std::endl;
  // Add nodes
  bool first_item = true;
  for (size_t i = 0; i < n_endpoints_per_sub; i++) {
    if (not first_item)
      stream << "," << std::endl;
    stream << "      'e" << i << "':{ 'label':\"e" << i << "\", 'metadata':{ "
           << "'color':'green' }}";
    first_item = false;
  }
  // And switches if applicable
  for (int i = n_endpoints_per_sub; i < next_switch; i++) {
    stream << "," << std::endl
           << "      's" << i << "':{ 'label':\"s" << i << "\", 'metadata':{ "
           << "'color':'orange' }}";
  }
  // And now the links...
  stream << std::endl << "    }," << std::endl << "    'edges':[" << std::endl;
  first_item = true;
  for (size_t i = 0; i < n_links_per_sub; i++) {
    if (not first_item)
      stream << "," << std::endl;
    auto init = link_initiator[i];
    auto target = link_target[i];
    stream << "      {'source':'"
           << (init < (int)n_endpoints_per_sub ? 'e' : 's') << init
           << "', 'target':'" << (target < (int)n_endpoints_per_sub ? 'e' : 's')
           << target << "'";
#ifdef SHOW_DEBUG
    auto label = this->resources[get_global_resindex(Sub, (resind_t)i)].name;
    stream << ", 'label':\"" << label << "\", 'metadata':{ "
           << "'hover':\"" << label << "\"}}";
#else
    stream << ", 'label':\"" << i << "\", 'metadata':{ "
           << "'hover':\"" << i << "\"}}";
#endif
    first_item = false;
  }

  // And wrap it up
  stream << std::endl
         << "    ]" << std::endl
         << "  }" << std::endl
         << "}" << std::endl;
}
*/
