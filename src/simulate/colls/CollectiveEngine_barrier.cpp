//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../CollectiveEngine.hpp"
#include "../Engine.hpp"
#include "IndexedCS.hpp"

void CollectiveEngine::barrier_ompi_two_procs(Communicator &c,
                                              AtomicAction::DebugInfo &dbg) {
  assert(c.cores.size() == 2);
  (void)dbg;
  auto ind_a = c.cores[0];
  auto ind_b = c.cores[1];
  auto &node_a = engine.cores[ind_a];
  auto &node_b = engine.cores[ind_b];
  actind_t transfer_act_a_to_b = NoAction;
  actind_t receiver_act_a_to_b = NoAction;
#ifdef SHOW_DEBUG
  dbg.desc = "barrier_ompi_two_procs";
  dbg.index.seq = 1;
  dbg.index.level = 0;
  dbg.index.i[0] = 0;
  dbg.index.i[1] = 0;
  dbg.index.i[2] = 0;
#endif
  // Create rank 0 (a) to rank 1 (b) message
  engine.enqueue_raw_comm(ind_a, ind_b, 0, node_a.last_blocking_actions,
                          node_b.last_blocking_actions, node_a.delay_from_last,
                          node_a.delay_from_last, transfer_act_a_to_b,
                          receiver_act_a_to_b, dbg);
  ctx.pool->seal_action(transfer_act_a_to_b, engine.get_last_time());
  if (transfer_act_a_to_b != receiver_act_a_to_b)
    ctx.pool->seal_action(receiver_act_a_to_b, engine.get_last_time());

  // Create rank 1 (b) to rank 0 (a) message
  actind_t transfer_act_b_to_a = NoAction;
  actind_t receiver_act_b_to_a = NoAction;
#ifdef SHOW_DEBUG
  dbg.index.i[0] = 1;
#endif
  engine.enqueue_raw_comm(ind_b, ind_a, 0, node_b.last_blocking_actions,
                          node_a.last_blocking_actions, node_b.delay_from_last,
                          node_a.delay_from_last, transfer_act_b_to_a,
                          receiver_act_b_to_a, dbg);
  ctx.pool->seal_action(transfer_act_b_to_a, engine.get_last_time());
  if (transfer_act_b_to_a != receiver_act_b_to_a)
    ctx.pool->seal_action(receiver_act_b_to_a, engine.get_last_time());

  engine.move_to_next_blocking(ind_a,
                               {transfer_act_a_to_b, receiver_act_b_to_a});
  engine.move_to_next_blocking(ind_b,
                               {transfer_act_b_to_a, receiver_act_a_to_b});
}

void CollectiveEngine::barrier_ompi_bruck(Communicator &c,
                                          AtomicAction::DebugInfo &dbg) {
  auto comm_size = c.cores.size();
  auto comm_size_log2 = floor_log2(comm_size - 1);
  std::vector<AbstractCS::Ptr> sequences;
  sequences.emplace_back(AbstractCS::create_logical_ring(
      comm_size, (int)comm_size_log2 + 1, true, AbstractCS::Direction::TO_RIGHT,
      [](const SequenceIndex &index) { return 1 << index.level; },
      [=](const SequenceIndex &, AbstractCS::CommParams &params) {
        params.size = 0;
      },
      dbg));
  _create_collective(c.index, sequences, "barrier_bruck");
}

void CollectiveEngine::barrier_ompi_rdb(Communicator &c,
                                        AtomicAction::DebugInfo &dbg) {
  resind_t comm_size = (resind_t)c.cores.size();
  auto comm_size_log2 = floor_log2(comm_size);
  auto comm_pof2 = (resind_t)1 << comm_size_log2;
  resind_t rem = comm_size - comm_pof2;
  std::vector<AbstractCS::Ptr> sequences;
  SequenceIndex dimension;
  dimension.level = 1;
  dimension.i[0] = rem;
  if (rem) {
    sequences.emplace_back(new IndexedCS(
        2,
        [=](const SequenceIndex &index) {
          return AbstractCS::CommParams(comm_pof2 + index.i[0], 1,
                                        Dependency::SINGLE, index.i[0], 1,
                                        Dependency::SINGLE, 0);
        },
        dbg, dimension));
  }

  sequences.emplace_back(AbstractCS::create_logical_ring(
      comm_pof2, comm_size_log2, false,
      AbstractCS::Direction::TO_RIGHT_AND_LEFT,
      [](const SequenceIndex &index) { return 1 << index.level; },
      [=](const SequenceIndex &, AbstractCS::CommParams &params) {
        params.size = 0;
      },
      dbg));

  if (rem) {
    sequences.emplace_back(new IndexedCS(
        2,
        [=](const SequenceIndex &index) {
          return AbstractCS::CommParams(comm_pof2 + index.i[0], 1,
                                        Dependency::SINGLE, index.i[0], 1,
                                        Dependency::SINGLE, 0);
        },
        dbg, dimension));
  }
  _create_collective(c.index, sequences, "barrier_rdb");
}

void CollectiveEngine::barrier_select(Communicator &c,
                                      AtomicAction::DebugInfo &dbg) {
  auto comm_size = c.cores.size();
  int alg = -1;
  if (ctx.vm.count("barrier_alg")) {
    alg = ctx.vm["barrier_alg"].as<int>();
    goto selection_done;
  }
  if (2 == comm_size)
    alg = 1;
  else {
    /*     * Basic optimisation. If we have a power of 2 number of nodes*/
    /*     * the use the recursive doubling algorithm, otherwise*/
    /*     * bruck is the one we want.*/
    int has_one = 0;
    for (; comm_size > 0; comm_size >>= 1) {
      if (comm_size & 0x1) {
        has_one = 1;
        break;
      }
    }
    if (has_one)
      alg = 2;
    else
      alg = 3;
  }

selection_done:
  switch (alg) {
  case 1:
    barrier_ompi_two_procs(c, dbg);
    break;
  case 2:
    barrier_ompi_bruck(c, dbg);
    break;
  case 3:
    barrier_ompi_rdb(c, dbg);
    break;
  default:
    MUST_DIE
  }
}
