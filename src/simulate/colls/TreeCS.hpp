//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../AbstractCS.hpp"
#include "Tree.hpp"

/**
 * @brief A Collective Sequence based on a tree
 *
 * * level is the level in the tree ( from the root)
 * * i[0] is the rank
 * * i[1] is the segment index
 *
 * If we assume the following tree:
 *
 * 0-->1-->3-->7
 *  \   \->5-->9
 *   ->2-->4-->8
 *      \->6
 *
 * A Breath-first visit means we will visit cores/ranks in this order:
 *  Node   0, 1,2, 3,4,5,6, 7,9,8
 *  Level  0   1      2       3
 *
 * A Depth-first visit means we visit cores/ranks in this order:
 *  Node   0,1,3,7,5,9,2,4,8,6
 *  Level  0,1,2,3,2,3,1,2,3,2
 *
 * Orthogonally to the order that we visit cores/ranks;
 * we need to determine whether to stay on a node until all sub indices (i[1]
 * and i[2]) have been processed (Segments first); or if we do all the tree and
 * then come back (tree first).
 *
 *
 */
struct TreeCS : public AbstractCS {

  typedef Tree<resind_t, NoResource> TreeType;

  enum Priority { TREE_I1_I2, I1_TREE_I2, I1_I2_TREE };
  enum VisitOrder { BFS_FROM_ROOT, BFS_FROM_LEAVES };

private:
  std::shared_ptr<TreeType> tree;
  resind_t root_node;
  VisitOrder order;
  Priority priority;
  resind_t n_i1;
  resind_t n_i2;

  // the level and the node
  typedef std::pair<resind_t, resind_t> Element;
  std::queue<Element> queue;

  void _prepare_bfs_from_leaves() {
    assert(queue.empty());
    // Reset flags and queue leaves
    for (resind_t i = 0; i < tree->get_size(); i++) {
      auto level = tree->get_flags(i) >> 16;
      tree->get_flags(i) = level << 16;
      if (tree->is_leaf(i))
        queue.push(Element(level, i));
    }
  }

  bool move_to_next_rank(resind_t &level, resind_t &rank) {

    if (order == VisitOrder::BFS_FROM_ROOT) {
      // Queue children elements
      for (resind_t child_index = 0;
           child_index < tree->get_children_count(rank); child_index++) {
        queue.emplace(level + 1, tree->get_child(rank, child_index));
      }
      if (queue.empty()) {
        level = 0;
        rank = root_node;
        return true;
      }
      std::tie(level, rank) = queue.front();
      queue.pop();
      return false;
    }
    assert(order == VisitOrder::BFS_FROM_LEAVES);
    // Queue parent element if other children have already been visited
    auto parent = tree->get_parent(rank);
    if (parent != NoResource) {
      auto parent_children = tree->get_children_count(parent);
      auto &parent_flags = tree->get_flags(parent);
      auto parent_counter = parent_flags & ((1 << 16) - 1);
      if (parent_counter + 1 == parent_children) {
        auto parent_level = tree->get_flags(parent) >> 16;
        queue.emplace(parent_level, parent);
      }
      parent_flags++;
    }
    bool visit_complete = (queue.empty());
    if (visit_complete)
      _prepare_bfs_from_leaves();
    std::tie(level, rank) = queue.front();
    queue.pop();
    return visit_complete;
  }

public:
  TreeCS(size_t dependencies_per_rank, CommFunction &&in_f,
         AtomicAction::DebugInfo d, std::shared_ptr<TreeType> tree,
         resind_t root, VisitOrder tree_order = BFS_FROM_ROOT,
         Priority tree_priority = I1_I2_TREE, resind_t i1_size = 0,
         resind_t i2_size = 0)
      : AbstractCS(dependencies_per_rank, in_f, d), tree(tree), root_node(root),
        order(tree_order), priority(tree_priority), n_i1(i1_size),
        n_i2(i2_size) {
    if (tree_order == BFS_FROM_LEAVES)
      tree->compute_levels(root);
  }

  virtual SequenceIndex init_index(int seq) {
    SequenceIndex index;
    index.seq = seq;
    index.i[1] = 0;
    index.i[2] = 0;
    if (order == BFS_FROM_ROOT) {
      index.level = 0;
      index.i[0] = root_node;
      return index;
    }
    _prepare_bfs_from_leaves();
    std::tie(index.level, index.i[0]) = queue.front();
    queue.pop();
    return index;
  }

  virtual bool advance(SequenceIndex &index) {
    if (priority == TREE_I1_I2 &&
        not move_to_next_rank(index.level, index.i[0]))
      return false;

    if (index.i[1] + 1 < n_i1) {
      index.i[1]++;
      return false;
    }
    index.i[1] = 0;
    if (priority == I1_TREE_I2 &&
        not move_to_next_rank(index.level, index.i[0]))
      return false;
    if (index.i[2] + 1 < n_i2) {
      index.i[2]++;
      return false;
    }
    index.i[2] = 0;
    if (priority == I1_I2_TREE &&
        not move_to_next_rank(index.level, index.i[0]))
      return false;
    return true;
  }
};
