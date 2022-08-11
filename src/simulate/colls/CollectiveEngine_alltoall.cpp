//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../CollectiveEngine.hpp"
#include "IndexedCS.hpp"

void CollectiveEngine::alltoall_basic_linear(Communicator &c,
                                             share_t message_size,
                                             AtomicAction::DebugInfo &dbg) {
  resind_t comm_size = (resind_t)c.cores.size();
  std::vector<AbstractCS::Ptr> sequences;
  SequenceIndex dimension;
  dimension.level = 1;
  dimension.i[0] = comm_size - 1;
  dimension.i[1] = comm_size;

  sequences.emplace_back(new IndexedCS(
      comm_size * 2,
      [=](const SequenceIndex &index) {
        resind_t rank = index.i[1];
        resind_t dst = index.i[0];
        if (dst >= rank)
          dst = (dst + 1) % comm_size;
        // Account for all the incoming receives (rank) and the sends we just
        // emitted
        size_t snd_offset = rank + index.i[0] + 1;
        size_t rcv_offset = rank + 1;
        // Account for all the sends (comm_size - 1) and all the receives (rank)
        if (dst < rank)
          rcv_offset = (comm_size - 1) + index.i[1];
        return AbstractCS::CommParams(rank, snd_offset, Dependency::SINGLE, dst,
                                      rcv_offset, Dependency::SINGLE,
                                      message_size);
      },
      dbg, dimension));
  _create_collective(c.index, sequences, "alltoall_basic_linear");
}
void CollectiveEngine::alltoall_bruck(Communicator &c, share_t message_size,
                                      AtomicAction::DebugInfo &dbg) {
  resind_t comm_size = (resind_t)c.cores.size();
  std::vector<AbstractCS::Ptr> sequences;
  // Compute message size for each round
  auto comm_log2 = floor_log2(comm_size);
  auto comm_pof2 = (1 << comm_log2);
  std::vector<share_t> message_count(comm_log2, comm_pof2 / 2);
  for (resind_t i = comm_pof2 + 1; i < comm_size; i++) {
    for (resind_t j = 0; j < comm_log2; j++)
      if (i & (1 << j))
        message_count[j]++;
  }

  LOG_VERBOSE << "Message count per round: ";
  for (resind_t j = 0; j < comm_log2; j++) {
    LOG_VERBOSE << j << ":" << message_count[j] << " ";
  }
  LOG_VERBOSE << std::endl;

  sequences.emplace_back(AbstractCS::create_logical_ring(
      comm_size, comm_log2, true, AbstractCS::Direction::TO_RIGHT,
      [](const SequenceIndex &index) { return 1 << index.level; },
      [=](const SequenceIndex &index, AbstractCS::CommParams &params) {
        params.size = message_size * message_count[index.level];
      },
      dbg));
  _create_collective(c.index, sequences, "alltoall_bruck");
}
void CollectiveEngine::alltoall_ring(Communicator &c, share_t message_size,
                                     AtomicAction::DebugInfo &dbg) {
  resind_t comm_size = (resind_t)c.cores.size();
  std::vector<AbstractCS::Ptr> sequences;
  sequences.emplace_back(AbstractCS::create_logical_ring(
      comm_size, comm_size - 1, true, AbstractCS::Direction::TO_RIGHT,
      [](const SequenceIndex &index) { return index.level + 1; },
      [=](const SequenceIndex &, AbstractCS::CommParams &params) {
        params.size = message_size;
      },
      dbg));
  _create_collective(c.index, sequences, "alltoall_ring");
}

void CollectiveEngine::alltoall_select(Communicator &c, share_t message_size,
                                       AtomicAction::DebugInfo &dbg) {
  auto comm_size = c.cores.size();
  int alg = -1;
  if (ctx.vm.count("alltoall_alg")) {
    alg = ctx.vm["alltoall_alg"].as<int>();
    goto selection_done;
  }

  /* Decision function based on measurement on Grig cluster at
     the University of Tennessee (2GB MX) up to 64 cores.
     Has better performance for messages of intermediate sizes than the old one
   */
  /* determine block size */

  if ((message_size < 200) && (comm_size > 12)) {
    alg = 1;
  } else if (message_size < 3000) {
    alg = 2;
  } else {
    alg = 3;
  }

selection_done:
  switch (alg) {
  case 1:
    alltoall_bruck(c, message_size, dbg);
    ;
    break;
  case 2:
    alltoall_basic_linear(c, message_size, dbg);
    ;
    break;
  case 3:
    alltoall_ring(c, message_size, dbg);
    ;
    break;
  default:
    MUST_DIE
  }
}