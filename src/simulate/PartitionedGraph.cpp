// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#include "PartitionedGraph.hpp"

void PartitionedGraph::_add_partition(const ApproxMatrix &mat,
                                      uint32_t granularity, uint64_t id,
                                      uint64_t ratio, bool enforce_symmetry) {
  dirty = true;
  assert(mat.is_square());
  const uint32_t N = mat.get_nrows();
  uint64_t vert_count = 0;
  uint64_t arc_count = 0;
  uint32_t vert_count_max = ceil_div(N, granularity);

  size_t start_vert = verttab.size();
  std::vector<ind_t> edgetmp;
  std::vector<ind_t> edlotmp;

  if (not InhibitVerboseMessages)
    mat.show();

  if (enforce_symmetry)
    ratio *= 2;

  for (uint32_t i = 0; i < vert_count_max; i++) {
    uint64_t acc = 0;
    uint32_t i2_max = std::min((i + 1) * granularity, N);

    // Save current offset as start of edges for i
    verttab.push_back(arc_count);
    // bool vertex_is_disconnected = true;
    for (uint32_t j = 0; j < vert_count_max; j++) {
      // Does not compute weights inside a region (not needed for the graph
      // mapping)
      if (i == j)
        continue;
      uint32_t j2_max = std::min((j + 1) * granularity, N);
      acc = 0;
      for (uint32_t i2 = i * granularity; i2 < i2_max; i2++)
        for (uint32_t j2 = j * granularity; j2 < j2_max; j2++) {
          acc += mat(i2, j2);
          if (enforce_symmetry)
            acc += mat(j2, i2);
        }

      if (acc > 0) {
        edgetmp.push_back(start_vert + j);
        if (ratio != 1)
          acc /= ratio;
        edlotmp.push_back(acc);
        arc_count++;
        // vertex_is_disconnected = false;
      }
    }
    // Save current offset as end of edges for i
    // if (vertex_is_disconnected) {
    //  verttab.pop_back();
    //} else {
    vert_count++;
    vendtab.push_back(arc_count);
    vfintab.push_back(arc_count);
    velotab.push_back(1);
    //}
  }
  assert(edlotab.size() == edgetab.size());
  assert(arc_count % 2 == 0);

  if (has_alloc) {
    parttab.resize(vendtab.size());
    for (size_t i = start_vert; i < vendtab.size(); i++)
      parttab[i] = -1;
  }

  // Now that we know how many arcs we have, request a partition
  auto &part = request_partition(arc_count, id);
  part.start_vertex = start_vert;
  part.end_vertex = start_vert + vert_count;
  part.edsize_actual = arc_count;
  edgecount += arc_count;

  // Copy the adjacency matrix and weights
  for (size_t i = 0; i < arc_count; i++) {
    edgetab[part.start_edindex + i] = edgetmp[i];
    edlotab[part.start_edindex + i] = edlotmp[i];
  }
  // Update vertices info
  assert(verttab.size() == vendtab.size() && verttab.size() == vfintab.size() &&
         verttab.size() == velotab.size());
  for (size_t i = start_vert; i < verttab.size(); i++) {
    verttab[i] += part.start_edindex;
    vendtab[i] += part.start_edindex;
    vfintab[i] += part.start_edindex;
  }
  vertexcount += vert_count;
}

void PartitionedGraph::remove_partition(uint64_t id) {
  assert(mode == DYNAMIC);
  auto removed_part_it = get_partition_by_id(id);
  auto edge_shift = removed_part_it->edsize;
  assert(edgecount >= removed_part_it->edsize_actual);
  edgecount -= removed_part_it->edsize_actual;
  // We need to shift the partitions that have larger start vertex
  ind_t vertex_shift =
      removed_part_it->end_vertex - removed_part_it->start_vertex;
  assert((ind_t)vertexcount >= vertex_shift);
  vertexcount -= vertex_shift;

  ind_t last_start_index = 0;
  for (auto &p : partitions) {
    if (p.id != NoPartitionId &&
        p.start_edindex > removed_part_it->start_edindex) {
      // If this does not hold we need to be more careful how we overwrite
      // edgetab elements..
      assert_always((ind_t)p.start_edindex >= last_start_index,
                    "Edge indices need to be ordered");
      for (size_t i = p.start_edindex; i < p.start_edindex + p.edsize_actual;
           i++) {
        assert(edgetab[i] >= vertex_shift);
        edgetab[i - edge_shift] = edgetab[i] - vertex_shift;
      }
      p.start_vertex -= vertex_shift;
      p.end_vertex -= vertex_shift;
      last_start_index = p.start_edindex;
      p.start_edindex -= edge_shift;
    }
  }

  edgetab.resize(edgecount);
  edlotab.resize(edgecount);

  // Shift the vertices vectors
  for (size_t i = removed_part_it->end_vertex; i < verttab.size(); i++) {
    verttab[i - vertex_shift] = verttab[i];
    vendtab[i - vertex_shift] = vendtab[i];
    vfintab[i - vertex_shift] = vfintab[i];
    if (has_alloc)
      parttab[i - vertex_shift] = parttab[i];
  }
  verttab.resize(vertexcount);
  vendtab.resize(vertexcount);
  vfintab.resize(vertexcount);
  velotab.resize(vertexcount);
  if (has_alloc)
    parttab.resize(vertexcount);

  partitions.erase(removed_part_it);
}

void PartitionedGraph::show() {
  auto vertnbr = get_vertnbr();
  auto edgenbr = get_edgenbr();
  LOG_INFO << "Graph of " << verttab.size() << " vertices (" << vertnbr
           << " active) and " << edgenbr << " edges (" << edgenbr << " active)"
           << std::endl;
  LOG_INFO << "Active vertices:" << std::endl;
  for (ind_t i = 0; i < vertnbr; i++) {
    LOG_INFO << i << ":(" << velotab[i] << ") edges:[" << verttab[i] << ","
             << vendtab[i] << "[" << std::endl;
  }
  if (vertnbr != (ind_t)verttab.size()) {
    LOG_INFO << "Disabled vertices:" << std::endl;
    for (size_t i = vertnbr; i < verttab.size(); i++) {
      LOG_INFO << i << ":(" << velotab[i] << ") edges:[" << verttab[i] << ","
               << vendtab[i] << "[" << std::endl;
    }
  }
  LOG_INFO << "Active edges:" << std::endl;
  for (ind_t i = 0; i < edgenbr; i++) {
    LOG_INFO << i << ":(" << edlotab[i] << ")->" << edgetab[i] << std::endl;
  }
  if (edgenbr != (ind_t)edgetab.size()) {
    LOG_INFO << "Disabled edges:" << std::endl;
    for (size_t i = edgenbr; i < edgetab.size(); i++) {
      LOG_INFO << i << ":(" << edlotab[i] << ")->" << edgetab[i] << std::endl;
    }
  }
}

void PartitionedGraph::test() {
  ApproxMatrix mat;
  mat.init(8, 8);

  mat.add_weight(0, 1, 1);
  mat.add_weight(1, 0, 1);

  mat.add_weight(0, 2, 2);
  mat.add_weight(2, 0, 2);

  mat.add_weight(0, 4, 4);
  mat.add_weight(4, 0, 4);

  mat.add_weight(1, 2, 12);
  mat.add_weight(2, 1, 12);

  mat.add_weight(2, 5, 25);
  mat.add_weight(5, 2, 25);

  mat.add_weight(2, 6, 26);
  mat.add_weight(6, 2, 26);

  mat.add_weight(3, 4, 34);
  mat.add_weight(4, 3, 34);

  mat.add_weight(3, 5, 35);
  mat.add_weight(5, 3, 35);

  mat.add_weight(3, 7, 37);
  mat.add_weight(7, 3, 37);

  mat.add_weight(5, 6, 56);
  mat.add_weight(6, 5, 56);

  PartitionedGraph single_1(mat, 1, 1);
  single_1.remove_edge(0, 1);
  single_1.remove_edge(7, 3);
  single_1.remove_edge(5, 6);
  single_1.remove_edge(0, 4);
  single_1.reinstate_edge(7, 3);
  single_1.reinstate_edge(1, 0);
  single_1.reinstate_edge(5, 6);
  single_1.reinstate_edge(0, 4);
  assert(single_1.get_vertnbr() == 8);
  assert(single_1.get_edgenbr() == 20);

  PartitionedGraph single_4(mat, 4, 1);
  LOG_INFO << "After init" << std::endl;
  single_4.show();
  single_4.remove_vertex(0);
  single_4.remove_vertex(0);
  LOG_INFO << "After removes" << std::endl;
  single_4.reinstate_vertex(0);
  single_4.reinstate_vertex(1);
  LOG_INFO << "After reinstate" << std::endl;
  assert(single_4.get_vertnbr() == 2);

  PartitionedGraph dynamic;
  dynamic.add_partition(mat, 1, 0, 1);
  dynamic.add_partition(mat, 1, 1, 1);
  dynamic.add_partition(mat, 2, 2, 1);
  dynamic.add_partition(mat, 4, 3, 1);
  dynamic.add_partition(mat, 1, 4, 1);

  dynamic.remove_partition(2);
  dynamic.remove_partition(0);
  dynamic.remove_partition(1);
  dynamic.remove_partition(3);
  dynamic.remove_partition(4);

  assert(dynamic.get_vertnbr() == 0);
  assert(dynamic.get_edgenbr() == 0);
}
