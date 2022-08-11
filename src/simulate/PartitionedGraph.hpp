// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include <cstdint>
#include <cstdio>
// Scotch needs cstdio and cstdint to be included before...
#include "scotch.h"
#include "taz-simulate.hpp"

template <typename T> class BijectiveTranslator {
  template <typename T2> friend class HeapConsumer;

  std::vector<T> a_to_b;
  std::vector<T> b_to_a;
  T n;

public:
  void init(size_t n) {
    this->n = (T)n;
    a_to_b.resize(n);
    b_to_a.resize(n);
    for (size_t i = 0; i < n; i++) {
      a_to_b[i] = (T)i;
      b_to_a[i] = (T)i;
    }
  }

  void update_translation(T a, T b) {
    assert(a < n);
    assert(b < n);
    a_to_b[a] = b;
    b_to_a[b] = a;
  }

  T to_b(T a) const {
    assert(a < n);
    return a_to_b[a];
  }

  T to_a(T b) const {
    assert(b < n);
    return b_to_a[b];
  }
};

template <typename T2> struct HeapConsumer<BijectiveTranslator<T2>> {
  static size_t get_heap_footprint(const BijectiveTranslator<T2> &x) {
    size_t footprint = GET_HEAP_FOOTPRINT(x.a_to_b);
    footprint += GET_HEAP_FOOTPRINT(x.b_to_a);
    return footprint;
  }
};

// This class defines a graph that is paritioned in disconnected subgraph (e.g.
// jobs) It is possible to remove and add partitions kind of efficiently to
// match the dynamicity of e.g. jobs
//
// CAVEAT: Strange things will occur if you remove edges and remove partitions
// in the same graph
class PartitionedGraph {

  template <typename T> friend class HeapConsumer;

  /**
   * @brief A partitionned graph can either be a single partition or multiple
   * partitions that can be removed and added dynamically
   *
   */
  enum PartitionMode { SINGLE, DYNAMIC } mode;

  bool dirty;

  typedef SCOTCH_Num ind_t;
  static constexpr uint64_t NoPartitionId = 0xFFFFFFFFFFFFFFFF;
  struct Partition {
    // Space in the edge structures
    size_t start_edindex;
    size_t edsize;
    // Id of the parition
    uint64_t id;
    // Vertices of the partition if valid
    size_t start_vertex;
    size_t end_vertex;
    // The actual number of arcs  used in this partition
    size_t edsize_actual;
  };

  std::vector<Partition> partitions;

  // Indice of the sub arrays, for each vertex
  std::vector<ind_t> verttab;
  // Indice of the end of active edges for each subarray
  std::vector<ind_t> vendtab;
  // Indice of the end of all edges for each subarray
  std::vector<ind_t> vfintab;
  // Weights for the vertices
  std::vector<ind_t> velotab;
  // Sub-arrays containing the adjacency array of each vertex
  std::vector<ind_t> edgetab;
  // Corresponding edge weights
  std::vector<ind_t> edlotab;

  // Translate vertex index in the vectors and their label
  // This is used only in SINGLE mode
  BijectiveTranslator<ind_t> vertex_label_index;

  // The number of edges working
  uint64_t edgecount;
  // The number of vertex working
  uint64_t vertexcount;

  // Optionally have the result vector of allocation embedded,
  // So we can shuffle it when removing partitions
  bool has_alloc;
  std::vector<ind_t> parttab;

  Partition &request_partition(size_t original_size, uint64_t id) {
    assert(id != NoPartitionId);
    for (auto &p : partitions) {
      if (p.id == NoPartitionId && p.edsize >= original_size) {
        p.id = id;
        return p;
      }
    }
    // Get to the next power of two
    size_t size = (1 << ceil_log2(original_size));
    size_t start_edindex = edgetab.size();
    Partition p;
    p.start_edindex = start_edindex;
    p.edsize = size;
    p.id = id;
    partitions.push_back(p);
    // Sort in partitions in increasing size to at least avoid using excessively
    // large partitions if smaller sufficient ones are available
    std::sort(partitions.begin(), partitions.end(),
              [](const Partition &a, const Partition &b) {
                return a.edsize < b.edsize;
              });
    size += start_edindex;
    edgetab.resize(size);
    edlotab.resize(size);
    return *get_partition_by_id(id);
  }

  std::vector<Partition>::iterator get_partition_by_id(uint64_t id) {
    auto it = std::find_if(partitions.begin(), partitions.end(),
                           [id](const Partition &x) { return x.id == id; });
    assert(it != partitions.end());
    return it;
  }

  enum FindType { ACTIVE, DISABLED, BOTH };

  size_t _find_arc(ind_t src, ind_t dst, FindType type) {
    size_t src_end = vendtab[src];
    if (type == ACTIVE || type == BOTH) {
      size_t src_start = verttab[src];
      for (size_t index = src_start; index < src_end; index++) {
        if (edgetab[index] == (ind_t)dst)
          return index;
      }
    }
    if (type == DISABLED || type == BOTH) {
      size_t src_final = vfintab[src];
      for (size_t index = src_end; index < src_final; index++) {
        if (edgetab[index] == (ind_t)dst)
          return index;
      }
    }
    MUST_DIE
  }

  // We do not need to actually move the edges (i.e. edgetab and edlotab)
  // But we need to change the values of incoming arcs
  void _swap_vertices(ind_t a, ind_t b) {
    if (a == b)
      return;
    ind_t a_first_edge = verttab[a];
    ind_t b_first_edge = verttab[b];
    ind_t a_final_edge = vfintab[a];
    ind_t b_final_edge = vfintab[b];

    for (ind_t i = a_first_edge; i < a_final_edge; i++) {
      ind_t target = edgetab[i];
      if (target != b) {
        size_t incoming_arc = _find_arc(target, a, BOTH);
        edgetab[incoming_arc] = b;
      }
    }

    for (ind_t i = b_first_edge; i < b_final_edge; i++) {
      ind_t target = edgetab[i];
      if (target != a) {
        size_t incoming_arc = _find_arc(target, b, BOTH);
        edgetab[incoming_arc] = a;
      }
    }

    verttab[a] = b_first_edge;
    verttab[b] = a_first_edge;
    vfintab[a] = b_final_edge;
    vfintab[b] = a_final_edge;
    std::swap(vendtab[a], vendtab[b]);
    std::swap(velotab[a], velotab[b]);
  }

  void _swap_edges(size_t a, size_t b) {
    if (a == b)
      return;
    std::swap(edgetab[a], edgetab[b]);
    std::swap(edlotab[a], edlotab[b]);
  }

  void remove_arc(size_t src, size_t dst) {
    _swap_edges(_find_arc(src, dst, ACTIVE), vendtab[src] - 1);
    vendtab[src]--;
  }

  void reinstate_arc(size_t src, size_t dst) {
    _swap_edges(_find_arc(src, dst, DISABLED), vendtab[src]);
    vendtab[src]++;
  }

  SCOTCH_Graph *grafdat;

  void _add_partition(const ApproxMatrix &mat, uint32_t granularity,
                      uint64_t id, uint64_t ratio, bool enforce_symmetry);

public:
  PartitionedGraph()
      : mode(DYNAMIC), dirty(false), edgecount(0), vertexcount(0),
        has_alloc(false) {
    grafdat = SCOTCH_graphAlloc();
    SCOTCH_graphInit(grafdat);
  }

  PartitionedGraph(const ApproxMatrix &mat, uint32_t granularity,
                   uint64_t ratio = 1, bool enforce_symmetry = false)
      : PartitionedGraph() {
    mode = SINGLE;
    _add_partition(mat, granularity, 0, ratio, enforce_symmetry);
    vertex_label_index.init(vertexcount);
  }

  ~PartitionedGraph() {
    SCOTCH_graphExit(grafdat);
    SCOTCH_memFree(grafdat);
  }

  void set_has_alloc(bool x) {
    dirty = true;
    has_alloc = x;
    if (x)
      parttab.resize(verttab.size(), -1);
    else
      parttab.clear();
  }

  void add_partition(const ApproxMatrix &mat, uint32_t granularity, uint64_t id,
                     uint64_t ratio = 1, bool enforce_symmetry = false) {
    assert(mode == DYNAMIC);
    _add_partition(mat, granularity, id, ratio, enforce_symmetry);
  }

  void remove_partition(uint64_t id);

  void remove_edge(ind_t src_label, ind_t dst_label) {
    assert(mode == SINGLE);
    dirty = true;
    auto src = vertex_label_index.to_b(src_label);
    auto dst = vertex_label_index.to_b(dst_label);
    remove_arc(src, dst);
    remove_arc(dst, src);
    edgecount--;
  }

  void reinstate_edge(ind_t src_label, ind_t dst_label) {
    assert(mode == SINGLE);
    dirty = true;
    auto src = vertex_label_index.to_b(src_label);
    auto dst = vertex_label_index.to_b(dst_label);
    reinstate_arc(src, dst);
    reinstate_arc(dst, src);
    edgecount++;
  }

  /*
    void change_vertices_weight(std::vector<ind_t> &vertices, ind_t weight) {
      for (auto v : vertices) {
        change_vertex_weight(v, weight);
      }
    }

    void change_vertex_weight(ind_t vert_index, ind_t weight) {
      dirty = true;
      velotab[vert_index] = weight;
    }
  */

  void remove_vertex(ind_t vertex_label) {
    assert(mode == SINGLE);
    dirty = true;
    auto v = vertex_label_index.to_b(vertex_label);
    assert(v < (ind_t)vertexcount);
    _swap_vertices(v, vertexcount - 1);
    ind_t first_edge = verttab[vertexcount - 1];
    ind_t end_edge = vendtab[vertexcount - 1];

    for (ind_t i = first_edge; i < end_edge; i++) {
      ind_t target = edgetab[i];
      remove_arc(target, vertexcount - 1);
    }
    vertexcount--;
  }

  void reinstate_vertex(ind_t vertex_label) {
    assert(mode == SINGLE);
    dirty = true;
    auto v = vertex_label_index.to_b(vertex_label);
    assert(v >= (ind_t)vertexcount);
    _swap_vertices(v, vertexcount);
    ind_t first_edge = verttab[vertexcount];
    ind_t end_edge = vendtab[vertexcount];

    for (ind_t i = first_edge; i < end_edge; i++) {
      ind_t target = edgetab[i];
      reinstate_arc(target, vertexcount);
    }
    vertexcount++;
  }

  typedef std::pair<std::vector<ind_t>::iterator, std::vector<ind_t>::iterator>
      Allocation;

  Allocation get_partition_alloc(uint64_t id) {
    // If we want support for Single we need to translate first...
    assert(mode == DYNAMIC);
    auto part_it = get_partition_by_id(id);
    return std::make_pair(parttab.begin() + part_it->start_vertex,
                          parttab.begin() + part_it->end_vertex);
  }

  ind_t get_vertnbr() { return (ind_t)vertexcount; }
  ind_t get_edgenbr() { return (ind_t)edgecount; }

  void show();

  SCOTCH_Graph *get_graph() {
    if (dirty) {
      SCOTCH_graphFree(grafdat);
      auto vertnbr = get_vertnbr();
      auto edgenbr = get_edgenbr();
      assert(verttab.size() >= (size_t)vertnbr);
      assert(vendtab.size() >= (size_t)vertnbr);
      assert(velotab.size() >= (size_t)vertnbr);
      assert(edgetab.size() >= (size_t)edgenbr);
      assert(edlotab.size() >= (size_t)edgenbr);
      check_result(SCOTCH_graphBuild(grafdat, 0, vertnbr, verttab.data(),
                                     vendtab.data(), velotab.data(), nullptr,
                                     edgenbr, edgetab.data(), edlotab.data()),
                   0);
      if (not InhibitVerboseMessages)
        show();
#ifdef SHOW_DEBUG
      check_result(SCOTCH_graphCheck(grafdat), 0);
#endif
    }
    dirty = false;
    return grafdat;
  }

  std::vector<ind_t> get_raw_partition_vector() {
    assert(has_alloc);
    return parttab;
  }

  ind_t *get_parttab() {
    assert(has_alloc);
    return parttab.data();
  }

  const ind_t *get_velotab() { return velotab.data(); }

  const BijectiveTranslator<ind_t> &get_vertex_label_translator() const {
    return vertex_label_index;
  }

  static void test();
};

template <> struct HeapConsumer<PartitionedGraph> {
  static size_t get_heap_footprint(const PartitionedGraph &x) {
    size_t footprint = GET_HEAP_FOOTPRINT(x.partitions);
    footprint += GET_HEAP_FOOTPRINT(x.verttab);
    footprint += GET_HEAP_FOOTPRINT(x.vendtab);
    footprint += GET_HEAP_FOOTPRINT(x.vfintab);
    footprint += GET_HEAP_FOOTPRINT(x.velotab);
    footprint += GET_HEAP_FOOTPRINT(x.edgetab);
    footprint += GET_HEAP_FOOTPRINT(x.edlotab);
    footprint += GET_HEAP_FOOTPRINT(x.parttab);
    footprint += GET_HEAP_FOOTPRINT(x.vertex_label_index);
    return footprint;
  }
};
