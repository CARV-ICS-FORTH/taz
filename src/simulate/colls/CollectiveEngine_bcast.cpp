//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../CollectiveEngine.hpp"
#include "IndexedCS.hpp"
#include "TreeCS.hpp"

void CollectiveEngine::bcast_binomial_tree(Communicator &c, int root_index,
                                           share_t send_size,
                                           AtomicAction::DebugInfo &dbg) {
  resind_t comm_size = (resind_t)c.cores.size();
  std::shared_ptr<TreeCS::TreeType> tree(new TreeCS::TreeType);
  tree->build_bmtree(root_index, comm_size, false);
  std::vector<AbstractCS::Ptr> sequences;
  sequences.emplace_back(new TreeCS(
      2,
      [=](const SequenceIndex &index) {
        resind_t rank = index.i[0];
        resind_t child_index = index.i[1];
        if (child_index >= tree->get_children_count(rank))
          return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_SENDER);
        resind_t child = tree->get_child(rank, child_index);
        return AbstractCS::CommParams(rank, 1, Dependency::SINGLE, child, 1,
                                      Dependency::SINGLE, send_size);
      },
      dbg, tree, root_index, TreeCS::BFS_FROM_ROOT, TreeCS::I1_TREE_I2,
      tree->get_max_fanout()));
  _create_collective(c.index, sequences, "bcast_bmtree");
}

void CollectiveEngine::bcast_ompi_split_bintree(Communicator &c, int root_index,
                                                share_t send_size,
                                                AtomicAction::DebugInfo &dbg,
                                                share_t segsize) {
  constexpr int fanout = 2;
  resind_t comm_size = (resind_t)c.cores.size();
  std::shared_ptr<TreeCS::TreeType> tree(new TreeCS::TreeType);
  tree->build_tree(root_index, fanout, comm_size);
  // Otherwise, we would need to call SMP_linear..
  assert(send_size >= 2);
  resind_t n_segments = (resind_t)ceil_div(send_size, 2 * segsize);
  share_t last_segment_size_l = (send_size % (2 * segsize)) / 2;
  share_t last_segment_size_r =
      (send_size % (2 * segsize)) - last_segment_size_l;
  if (send_size % (2 * segsize) == 0)
    last_segment_size_l = last_segment_size_r = segsize;
  share_t total_size_l = n_segments * (segsize - 1) + last_segment_size_l;
  share_t total_size_r = n_segments * (segsize - 1) + last_segment_size_r;

  std::vector<AbstractCS::Ptr> sequences;
  // Split data in half and propagate it to either the Left or the Right
  // sub-trees
  sequences.emplace_back(new TreeCS(
      3 + fanout,
      [=](const SequenceIndex &index) {
        resind_t rank = index.i[0];
        resind_t child_index = index.i[1];
        resind_t segment_index = index.i[2];
        resind_t lr = (diff_mod(rank, (resind_t)root_index, comm_size) + 1) % 2;
        share_t s = segsize;
        if (segment_index + 1 == n_segments)
          s = (lr == 0) ? last_segment_size_l : last_segment_size_r;
        resind_t children_count = tree->get_children_count(rank);
        if (child_index >= children_count)
          return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_COMM);
        resind_t child = tree->get_child(rank, child_index);
        Dependency::State snd_dep = Dependency::FIRST;
        bool no_recv_next =
            (tree->is_root(rank) || segment_index + 1 == n_segments);
        if (children_count == 1 && no_recv_next)
          snd_dep = Dependency::SINGLE;
        else if (child_index == 0)
          snd_dep = Dependency::FIRST;
        else if (child_index + 1 == children_count && no_recv_next)
          snd_dep = Dependency::LAST;
        else
          snd_dep = Dependency::MIDDLE;
        resind_t children2_count = tree->get_children_count(child);
        bool no_send_before = (children2_count == 0 || segment_index == 0);
        Dependency::State rcv_dep =
            no_send_before ? Dependency::SINGLE : Dependency::LAST;
        return AbstractCS::CommParams(rank, child_index + 1, snd_dep, child,
                                      segment_index ? 1 + children2_count : 1,
                                      rcv_dep, s);
      },
      dbg, tree, root_index, TreeCS::BFS_FROM_ROOT, TreeCS::I1_TREE_I2, fanout,
      n_segments));
  // And now exchange missing half across subtrees
  bool comm_size_odd = (comm_size % 2 == 1);
  SequenceIndex dimension;
  dimension.seq = 2;
  dimension.level = 1;
  dimension.i[0] = comm_size;
  sequences.emplace_back(new IndexedCS(
      2,
      [=](const SequenceIndex &index) {
        resind_t rank = index.i[0];
        resind_t lr = (diff_mod(rank, (resind_t)root_index, comm_size) + 1) % 2;
        resind_t pair = (rank + 1) % comm_size;
        if (lr == 1)
          pair = diff_mod<resind_t>(rank, 1, comm_size);
        size_t snd_offset, rcv_offset;
        Dependency::State snd_type, rcv_type;
        if (rank == (resind_t)root_index) {
          snd_type = rcv_type = Dependency::SINGLE;
          snd_offset = rcv_offset = 1;
        } else if (rank < pair) {
          snd_type = rcv_type = Dependency::FIRST;
          snd_offset = rcv_offset = 1;
        } else {
          snd_type = rcv_type = Dependency::LAST;
          snd_offset = rcv_offset = 2;
        }
        // In a magical way, this works both if comm_size is even or odd.
        // If it is odd, root does nothing
        // If it is even, root sends stuff to the last node before it
        if ((comm_size_odd && rank == (resind_t)root_index) ||
            (not comm_size_odd && pair == (resind_t)root_index))
          return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_COMM);
        share_t s = (lr == 0) ? total_size_l : total_size_r;
        return AbstractCS::CommParams(rank, snd_offset, snd_type, pair,
                                      rcv_offset, rcv_type, s);
      },
      dbg, dimension));
  _create_collective(c.index, sequences, "split_bintree");
}
void CollectiveEngine::bcast_ompi_pipeline(Communicator &c, int root_index,
                                           share_t send_size,
                                           AtomicAction::DebugInfo &dbg,
                                           share_t segsize) {
  constexpr int fanout = 1;
  auto comm_size = (resind_t)c.cores.size();
  std::shared_ptr<TreeCS::TreeType> tree(new TreeCS::TreeType);
  tree->build_chain(root_index, fanout, comm_size);
  share_t last_segment_size = send_size % segsize;
  resind_t n_segments = (resind_t)ceil_div(send_size, segsize);
  if (last_segment_size == 0)
    last_segment_size = segsize;
  std::vector<AbstractCS::Ptr> sequences;
  sequences.emplace_back(new TreeCS(
      3 + fanout,
      [=](const SequenceIndex &index) {
        resind_t rank = index.i[0];
        resind_t child_index = index.i[1];
        resind_t segment_index = index.i[2];
        share_t s =
            (segment_index + 1 == n_segments ? last_segment_size : segsize);
        resind_t children_count = tree->get_children_count(rank);
        if (child_index >= children_count)
          return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_COMM);
        resind_t child = tree->get_child(rank, child_index);
        Dependency::State snd_dep = Dependency::FIRST;
        bool no_recv_next =
            (tree->is_root(rank) || segment_index + 1 == n_segments);
        if (children_count == 1 && no_recv_next)
          snd_dep = Dependency::SINGLE;
        else if (child_index == 0)
          snd_dep = Dependency::FIRST;
        else if (child_index + 1 == children_count && no_recv_next)
          snd_dep = Dependency::LAST;
        else
          snd_dep = Dependency::MIDDLE;
        resind_t children2_count = tree->get_children_count(child);
        bool no_send_before = (children2_count == 0 || segment_index == 0);
        Dependency::State rcv_dep =
            no_send_before ? Dependency::SINGLE : Dependency::LAST;
        return AbstractCS::CommParams(rank, child_index + 1, snd_dep, child,
                                      segment_index ? 1 + children2_count : 1,
                                      rcv_dep, s);
      },
      dbg, tree, root_index, TreeCS::BFS_FROM_ROOT, TreeCS::I1_TREE_I2, fanout,
      n_segments));
  _create_collective(c.index, sequences, "bcast_pipeline");
}

void CollectiveEngine::bcast_SMP_linear(Communicator &c, int root_index,
                                        share_t send_size,
                                        AtomicAction::DebugInfo &dbg) {
  (void)c;
  (void)root_index;
  (void)send_size;
  (void)dbg;
  NOT_IMPLEMENTED_YET
}

void CollectiveEngine::bcast_select(Communicator &c, int root_index,
                                    share_t message_size,
                                    AtomicAction::DebugInfo &dbg) {

  /* Decision function based on MX results for
     messages up to 36MB and communicator sizes up to 64 cores */
  const size_t small_message_size = 2048;
  const size_t intermediate_message_size = 370728;
  const double a_p16 = 3.2118e-6; /* [1 / byte] */
  const double b_p16 = 8.7936;
  const double a_p64 = 2.3679e-6; /* [1 / byte] */
  const double b_p64 = 1.1787;
  const double a_p128 = 1.6134e-6; /* [1 / byte] */
  const double b_p128 = 2.1102;

  int communicator_size = (int)c.cores.size();
  assert(root_index < communicator_size);
  int alg = -1;
  share_t segsize = 1 << 10; //(1KB)

  if (ctx.vm.count("bcast_alg")) {
    alg = ctx.vm["bcast_alg"].as<int>();
    goto selection_done;
  }

  /* Handle messages of small and intermediate size, and
     single-element broadcasts */
  if ((message_size < small_message_size)) {
    alg = 1;
  } else if (message_size < intermediate_message_size) {
    // SplittedBinary with 1KB segments
    segsize = 1024;
    alg = 2;
  }
  // Handle large message sizes
  else if (communicator_size < (a_p128 * message_size + b_p128)) {
    // Pipeline with 128KB segments
    segsize = 1024 << 7;
    alg = 3;
  } else if (communicator_size < 13) {
    // Split Binary with 8KB segments
    segsize = 1024 << 3;
    alg = 2;
  } else if (communicator_size < (a_p64 * message_size + b_p64)) {
    // Pipeline with 64KB segments
    segsize = 1024 << 6;
    alg = 3;
  } else if (communicator_size < (a_p16 * message_size + b_p16)) {
    // Pipeline with 16KB segments
    segsize = 1024 << 4;
    alg = 3;
  } else {
    /* Pipeline with 8KB segments */
    segsize = 1024 << 3;
    alg = 3;
  }

selection_done:
  if (ctx.vm.count("forced_segsize"))
    segsize = ctx.vm["forced_segsize"].as<share_t>();

  switch (alg) {
  case 1:
    /* Binomial without segmentation */
    bcast_binomial_tree(c, root_index, message_size, dbg);
    break;
  case 2:
    bcast_ompi_split_bintree(c, root_index, message_size, dbg, segsize);
    break;
  case 3:
    bcast_ompi_pipeline(c, root_index, message_size, dbg, segsize);
    break;
  case 4:
    bcast_SMP_linear(c, root_index, message_size, dbg);
    break;
  default:
    MUST_DIE;
  }
}
