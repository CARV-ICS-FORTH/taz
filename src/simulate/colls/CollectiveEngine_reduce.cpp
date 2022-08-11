//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../CollectiveEngine.hpp"
#include "../Engine.hpp"
#include "IndexedCS.hpp"
#include "TreeCS.hpp"

void CollectiveEngine::reduce_ompi_basic_linear(Communicator &c, int root_index,
                                                share_t message_size,
                                                simtime_t reduce_time,
                                                AtomicAction::DebugInfo &dbg) {
  resind_t comm_size = (resind_t)c.cores.size();
  std::vector<AbstractCS::Ptr> sequences;
  SequenceIndex dimension;
  dimension.level = 1;
  dimension.i[0] = comm_size;
  dimension.i[1] = 0;

  sequences.emplace_back(new IndexedCS(
      2,
      [=](const SequenceIndex &index) {
        resind_t rank = index.i[0];
        if (rank == (resind_t)root_index)
          return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_COMM);
        auto params =
            AbstractCS::CommParams(rank, 1, Dependency::SINGLE, root_index, 1,
                                   Dependency::SINGLE, message_size);
        params.rcv_delay = reduce_time;
        return params;
      },
      dbg, dimension));
  _create_collective(c.index, sequences, "reduce_basic_linear");
}

resind_t get_counter(resind_t flags) { return flags & ((1 << 16) - 1); }

AbstractCS::Ptr reduce_ompi_generic(int root_index, share_t message_size,
                                    simtime_t reduce_time,
                                    AtomicAction::DebugInfo &dbg,
                                    std::shared_ptr<TreeCS::TreeType> tree,
                                    share_t segsize, int max_outstanding_reqs) {

  if (not segsize)
    segsize = message_size;
  share_t n_segments = ceil_div(message_size, segsize);
  share_t last_segment_size = message_size % segsize;
  if (last_segment_size == 0)
    last_segment_size = segsize;
  return AbstractCS::Ptr(new TreeCS(
      3 + tree->get_max_fanout(),
      [=](const SequenceIndex &index) {
        resind_t rank = index.i[0];
        resind_t segment_index = index.i[1];
        if (tree->is_root(rank))
          return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_COMM);
        resind_t parent = tree->get_parent(rank);
        resind_t parent_counter = get_counter(tree->get_flags(parent));
        bool follows_send = segment_index != 0 && not tree->is_root(parent);
        bool follows_reduce = not follows_send;
        share_t s =
            (segment_index + 1 == n_segments ? last_segment_size : segsize);
        resind_t children_count = tree->get_children_count(rank);
        resind_t parent_children = tree->get_children_count(parent);
        size_t rcv_offset = 2;
        switch (parent_counter) {
        case 0:
          if (segment_index == 0) {
            rcv_offset = 1;
            follows_reduce = false;
            break;
          }
          if (tree->is_root(parent)) {
            rcv_offset = 2;
            follows_reduce = true;
          } else {
            if (parent_children == 1) {
              rcv_offset = 1;
              follows_reduce = false;
            } else {
              rcv_offset = 3;
              follows_reduce = true;
            }
          }
          break;
        case 1:
          if (segment_index == 0) {
            rcv_offset = 2;
            follows_reduce = false;
            break;
          }
          if (tree->is_root(parent)) {
            rcv_offset = 2;
            follows_reduce = true;
          } else {
            rcv_offset = 2;
            follows_reduce = false;
          }
          break;
        default:
          rcv_offset = 2;
          follows_reduce = true;
          break;
        }

        auto params =
            AbstractCS::CommParams(rank, 1, Dependency::SINGLE, parent,
                                   rcv_offset, Dependency::SINGLE, s);
        if (follows_reduce)
          params.rcv_delay = reduce_time;

        if (children_count == 0) {
          // This is a leaf
          // If we have few segments, do a boring blocking send
          // Else we wait on the request before max_outstanding_request
          if (n_segments > (share_t)max_outstanding_reqs)
            params.snd_dep_offset = std::min((size_t)segment_index + 1,
                                             (size_t)max_outstanding_reqs);
        } else {
          // This not a leaf
          assert(get_counter(tree->get_flags(rank)) == children_count);
          params.snd_delay = reduce_time;
        }
        return params;
      },
      dbg, tree, root_index, TreeCS::BFS_FROM_LEAVES, TreeCS::TREE_I1_I2,
      (resind_t)n_segments));
}

void CollectiveEngine::reduce_ompi_binomial(Communicator &c, int root_index,
                                            share_t message_size,
                                            simtime_t reduce_time,
                                            AtomicAction::DebugInfo &dbg,
                                            share_t segsize,
                                            int max_outstanding_reqs) {
  resind_t comm_size = (resind_t)c.cores.size();
  std::shared_ptr<TreeCS::TreeType> tree(new TreeCS::TreeType);
  tree->build_bmtree(root_index, comm_size, false);
  _create_collective(
      c.index,
      {reduce_ompi_generic(root_index, message_size, reduce_time, dbg, tree,
                           segsize, max_outstanding_reqs)},
      "reduce_ompi_binomial");
}

void CollectiveEngine::reduce_ompi_pipeline(Communicator &c, int root_index,
                                            share_t message_size,
                                            simtime_t reduce_time,
                                            AtomicAction::DebugInfo &dbg,
                                            share_t segsize,
                                            int max_outstanding_reqs) {
  resind_t comm_size = (resind_t)c.cores.size();
  std::shared_ptr<TreeCS::TreeType> tree(new TreeCS::TreeType);
  tree->build_chain(root_index, 2, comm_size);
  _create_collective(
      c.index,
      {reduce_ompi_generic(root_index, message_size, reduce_time, dbg, tree,
                           segsize, max_outstanding_reqs)},
      "reduce_ompi_pipeline");
}
void CollectiveEngine::reduce_ompi_binary(Communicator &c, int root_index,
                                          share_t message_size,
                                          simtime_t reduce_time,
                                          AtomicAction::DebugInfo &dbg,
                                          share_t segsize,
                                          int max_outstanding_reqs) {
  resind_t comm_size = (resind_t)c.cores.size();
  std::shared_ptr<TreeCS::TreeType> tree(new TreeCS::TreeType);
  tree->build_in_order_bintree(comm_size);
  std::vector<AbstractCS::Ptr> sequences;
  sequences.emplace_back(reduce_ompi_generic(comm_size - 1, message_size,
                                             reduce_time, dbg, tree, segsize,
                                             max_outstanding_reqs));
  _create_collective(c.index, sequences, "reduce_ompi_binomial");
  if ((resind_t)root_index != comm_size - 1) {
    auto snd_node_index = c.cores[comm_size - 1];
    const auto &snd_node = engine.cores[snd_node_index];
    auto rcv_node_index = c.cores[root_index];
    const auto &rcv_node = engine.cores[rcv_node_index];
    actind_t transfer_act = NoAction;
    actind_t receiver_act = NoAction;
#ifdef SHOW_DEBUG
    dbg.desc = "reduce_ompi_binomial";
    dbg.index.seq = 2;
    dbg.index.level = 0;
    dbg.index.i[0] = root_index;
#endif
    engine.enqueue_raw_comm(
        comm_size - 1, root_index, message_size, snd_node.last_blocking_actions,
        rcv_node.last_blocking_actions, snd_node.delay_from_last,
        rcv_node.delay_from_last, transfer_act, receiver_act, dbg);
    ctx.pool->seal_action(transfer_act, engine.get_last_time());
    if (transfer_act != receiver_act)
      ctx.pool->seal_action(receiver_act, engine.get_last_time());
    engine.move_to_next_blocking(snd_node_index, {transfer_act, NoAction},
                                 false);
    engine.move_to_next_blocking(rcv_node_index, {receiver_act, NoAction},
                                 false);
  }
}

void CollectiveEngine::reduce_select(Communicator &c, int root_index,
                                     share_t message_size,
                                     simtime_t reduce_time,
                                     AtomicAction::DebugInfo &dbg) {
  auto comm_size = c.cores.size();
  int alg = -1;
  // int segsize = 0;
  constexpr double a1 = 0.6016 / 1024.0; /* [1/B] */
  constexpr double b1 = 1.3496;
  constexpr double a2 = 0.0410 / 1024.0; /* [1/B] */
  constexpr double b2 = 9.7128;
  constexpr double a3 = 0.0422 / 1024.0; /* [1/B] */
  constexpr double b3 = 1.1614;
  constexpr double a4 = 0.0033 / 1024.0; /* [1/B] */
  constexpr double b4 = 1.6761;
  share_t segsize = 0;
  int max_outstanding_reqs = 1;

  if (ctx.vm.count("reduce_alg")) {
    alg = ctx.vm["reduce_alg"].as<int>();
    goto selection_done;
  }

  /* no limit on # of outstanding requests */
  // const int max_requests = 0;

  if ((comm_size < 8) && (message_size < 512)) {
    /* Linear_0K */
    alg = 1;
  } else if (((comm_size < 8) && (message_size < 20480)) ||
             (message_size < 2048)) {
    /* Binomial_0K */
    segsize = 0;
    alg = 2;
  } else if (comm_size > (a1 * message_size + b1)) {
    // Binomial_1K
    segsize = 1024;
    alg = 2;
  } else if (comm_size > (a2 * message_size + b2)) {
    // Pipeline_1K
    segsize = 1024;
    alg = 3;
  } else if (comm_size > (a3 * message_size + b3)) {
    // Binary_32K
    segsize = 32 * 1024;
    alg = 4;
  } else {
    if (comm_size > (a4 * message_size + b4)) {
      // Pipeline_32K
      segsize = 32 * 1024;
    } else {
      // Pipeline_64K
      segsize = 64 * 1024;
    }
    alg = 3;
  }

selection_done:

  if (ctx.vm.count("forced_segsize"))
    segsize = ctx.vm["forced_segsize"].as<share_t>();

  if (ctx.vm.count("forced_forced_max_outstanding_reqs"))
    max_outstanding_reqs = ctx.vm["forced_max_outstanding_reqs"].as<int>();

  switch (alg) {
  case 1:
    reduce_ompi_basic_linear(c, root_index, message_size, reduce_time, dbg);
    break;
  case 2:
    reduce_ompi_binomial(c, root_index, message_size, reduce_time, dbg, segsize,
                         max_outstanding_reqs);
    break;
  case 3:
    reduce_ompi_pipeline(c, root_index, message_size, reduce_time, dbg, segsize,
                         max_outstanding_reqs);
    break;
  case 4:
    reduce_ompi_binary(c, root_index, message_size, reduce_time, dbg, segsize,
                       max_outstanding_reqs);
    break;
  default:
    MUST_DIE
  }
}
