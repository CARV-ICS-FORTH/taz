//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../CollectiveEngine.hpp"
#include "IndexedCS.hpp"

void CollectiveEngine::allreduce_redbcast(Communicator &c, share_t send_size,
                                          simtime_t reduce_time,
                                          AtomicAction::DebugInfo &dbg) {
  reduce_select(c, 0, send_size, reduce_time, dbg);
  bcast_select(c, 0, send_size, dbg);
}

void CollectiveEngine::allreduce_rdb(Communicator &c, share_t send_size,
                                     simtime_t reduce_time,
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
          auto params = AbstractCS::CommParams(
              comm_pof2 + index.i[0], 1, Dependency::SINGLE, index.i[0], 1,
              Dependency::SINGLE, send_size);
          params.rcv_delay = reduce_time;
          return params;
        },
        dbg, dimension));
  }

  sequences.emplace_back(AbstractCS::create_logical_ring(
      comm_pof2, comm_size_log2, false,
      AbstractCS::Direction::TO_RIGHT_AND_LEFT,
      [](const SequenceIndex &index) { return 1 << index.level; },
      [=](const SequenceIndex &index, AbstractCS::CommParams &params) {
        params.size = send_size;
        if (index.level > 0 || params.snd_rank < rem)
          params.snd_delay = reduce_time;
        if (index.level > 0 || params.rcv_rank < rem)
          params.rcv_delay = reduce_time;
      },
      dbg));

  if (rem) {
    sequences.emplace_back(new IndexedCS(
        2,
        [=](const SequenceIndex &index) {
          return AbstractCS::CommParams(comm_pof2 + index.i[0], 1,
                                        Dependency::SINGLE, index.i[0], 1,
                                        Dependency::SINGLE, send_size);
        },
        dbg, dimension));
  }
  _create_collective(c.index, sequences, "allreduce_rdb");
}

void CollectiveEngine::allreduce_lr(Communicator &c, share_t send_size,
                                    simtime_t reduce_time,
                                    AtomicAction::DebugInfo &dbg) {
  auto comm_size = c.cores.size();
  if (send_size < comm_size)
    return allreduce_redbcast(c, send_size, reduce_time, dbg);
  // This includes both the reduce-scatter and the allgather phases
  int nlevels = 2 * ((int)comm_size - 1);
  share_t message_size = send_size / comm_size;
  _create_collective(
      c.index,
      {AbstractCS::create_logical_ring(
          c.cores.size(), nlevels, true, AbstractCS::TO_RIGHT,
          [](const SequenceIndex &) { return 1; },
          [=](const SequenceIndex &, AbstractCS::CommParams &params) {
            params.size = message_size;
          },
          dbg)},
      "allreduce_lr");
  share_t remainder = send_size % comm_size;
  if (remainder != 0) {
    // Deal with the remainder
    allreduce_select(c, remainder, reduce_time, dbg);
  }
}

void CollectiveEngine::allreduce_ompi_ring_segmented(
    Communicator &c, share_t send_size, simtime_t reduce_time,
    AtomicAction::DebugInfo &dbg, share_t segsize) {
  resind_t comm_size = (resind_t)c.cores.size();
  if (send_size < comm_size * segsize) {
    LOG_DEBUG << " Change implementation from allreduce_ompi_ring_segmented "
                 "to allreduce_lr because send_size < comm_size * segsize."
              << std::endl;
    allreduce_lr(c, send_size, reduce_time, dbg);
    return;
  }
  /* Determine the number of phases of the algorithm */
  resind_t num_phases = (resind_t)(send_size / (comm_size * segsize));
  if ((send_size % (comm_size * segsize) >= comm_size) &&
      (send_size % (comm_size * segsize) > ((comm_size * segsize) / 2))) {
    num_phases++;
  }

  share_t phase_message_size = send_size / comm_size;
  std::vector<AbstractCS::Ptr> sequences;

  SequenceIndex dimension;
  dimension.level = num_phases * (comm_size - 1);
  dimension.i[0] = comm_size;
  sequences.emplace_back(new IndexedCS(
      4,
      [=](const SequenceIndex &index) {
        resind_t rank = index.i[0];
        resind_t dst = (rank + 1) % comm_size;
        AbstractCS::CommParams params(rank, 1, Dependency::SINGLE, dst, 1,
                                      Dependency::SINGLE, phase_message_size);
        params.snd_dep_type = params.rcv_dep_type = Dependency::SINGLE;
        if (index.level == 0) {
          if (params.snd_rank != 0)
            params.snd_dep_offset = 2;
          if (params.rcv_rank == 0)
            params.rcv_dep_offset = 2;
        } else {
          params.snd_delay = reduce_time;
          if (params.snd_rank != 0)
            params.snd_dep_offset = 3;
          if (params.rcv_rank == 0)
            params.rcv_dep_offset = 3;
        }
        if (index.level + 1 == dimension.level) {
          params.snd_dep_type =
              (params.snd_rank != 0 ? Dependency::LAST : Dependency::FIRST);
          params.rcv_dep_type =
              (params.rcv_rank == 0 ? Dependency::LAST : Dependency::FIRST);
        }
        return params;
      },
      dbg, dimension));
  // This is the distribution step
  sequences.emplace_back(AbstractCS::create_logical_ring(
      comm_size, comm_size - 1, true, AbstractCS::Direction::TO_RIGHT,
      [](const SequenceIndex &) { return 1; },
      [=](const SequenceIndex &, AbstractCS::CommParams &params) {
        params.size = phase_message_size;
      },
      dbg));
  _create_collective(c.index, sequences, "allreduce_ring_segmented");
}
void CollectiveEngine::allreduce_rab_rdb(Communicator &c, share_t send_size,
                                         simtime_t reduce_time,
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
          auto params = AbstractCS::CommParams(
              comm_pof2 + index.i[0], 1, Dependency::SINGLE, index.i[0], 1,
              Dependency::SINGLE, send_size);
          params.rcv_delay = reduce_time;
          return params;
        },
        dbg, dimension));
  }
  // This is the reduce-scatter step
  sequences.emplace_back(AbstractCS::create_logical_ring(
      comm_pof2, comm_size_log2, false,
      AbstractCS::Direction::TO_RIGHT_AND_LEFT,
      [](const SequenceIndex &index) { return 1 << index.level; },
      [=](const SequenceIndex &index, AbstractCS::CommParams &params) {
        // FIXME: The size here is inexact (but not trivial to reproduce
        // properly)
        params.size = send_size / (1ULL << (index.level + 1));
        if (index.level > 0 || params.snd_rank < rem)
          params.snd_delay = reduce_time;
        if (index.level > 0 || params.rcv_rank < rem)
          params.rcv_delay = reduce_time;
      },
      dbg));
  // This is the allgather step
  sequences.emplace_back(AbstractCS::create_logical_ring(
      comm_pof2, comm_size_log2, false,
      AbstractCS::Direction::TO_RIGHT_AND_LEFT,
      [=](const SequenceIndex &index) {
        return 1 << (comm_size_log2 - index.level - 1);
      },
      [=](const SequenceIndex &index, AbstractCS::CommParams &params) {
        // FIXME: The size here is inexact (but not trivial to reproduce
        // properly)
        params.size = send_size / (1ULL << (comm_size_log2 - index.level + 1));
      },
      dbg));

  if (rem) {
    sequences.emplace_back(new IndexedCS(
        2,
        [=](const SequenceIndex &index) {
          return AbstractCS::CommParams(comm_pof2 + index.i[0], 1,
                                        Dependency::SINGLE, index.i[0], 1,
                                        Dependency::SINGLE, send_size);
        },
        dbg, dimension));
  }
  _create_collective(c.index, sequences, "allreduce_rab_rdb");
}

void CollectiveEngine::allreduce_select(Communicator &c, share_t send_size,
                                        simtime_t reduce_time,
                                        AtomicAction::DebugInfo &dbg) {
  int comm_size = (int)c.cores.size();

  // OMPI/SMPI selection assuming commutative operation
  int alg = -1;
  share_t segsize = 1 << 10; //(1KB)
  if (ctx.vm.count("allreduce_alg")) {
    alg = ctx.vm["allreduce_alg"].as<int>();
    goto selection_done;
  }

  if (comm_size < 4) {
    if (send_size < 8) {
      alg = 4;
    } else if (send_size < 4096) {
      alg = 3;
    } else if (send_size < 8192) {
      alg = 4;
    } else if (send_size < 16384) {
      alg = 3;
    } else if (send_size < 65536) {
      alg = 4;
    } else if (send_size < 262144) {
      alg = 5;
    } else {
      alg = 6;
    }
  } else if (comm_size < 8) {
    if (send_size < 16) {
      alg = 4;
    } else if (send_size < 8192) {
      alg = 3;
    } else {
      alg = 6;
    }
  } else if (comm_size < 16) {
    if (send_size < 8192) {
      alg = 3;
    } else {
      alg = 6;
    }
  } else if (comm_size < 32) {
    if (send_size < 64) {
      alg = 5;
    } else if (send_size < 4096) {
      alg = 3;
    } else {
      alg = 6;
    }
  } else if (comm_size < 64) {
    if (send_size < 128) {
      alg = 5;
    } else {
      alg = 6;
    }
  } else if (comm_size < 128) {
    if (send_size < 262144) {
      alg = 3;
    } else {
      alg = 6;
    }
  } else if (comm_size < 256) {
    if (send_size < 131072) {
      alg = 2;
    } else if (send_size < 262144) {
      alg = 3;
    } else {
      alg = 6;
    }
  } else if (comm_size < 512) {
    if (send_size < 4096) {
      alg = 2;
    } else {
      alg = 6;
    }
  } else if (comm_size < 2048) {
    if (send_size < 2048) {
      alg = 2;
    } else if (send_size < 16384) {
      alg = 3;
    } else {
      alg = 6;
    }
  } else if (comm_size < 4096) {
    if (send_size < 2048) {
      alg = 2;
    } else if (send_size < 4096) {
      alg = 5;
    } else if (send_size < 16384) {
      alg = 3;
    } else {
      alg = 6;
    }
  } else {
    if (send_size < 2048) {
      alg = 2;
    } else if (send_size < 16384) {
      alg = 5;
    } else if (send_size < 32768) {
      alg = 3;
    } else {
      alg = 6;
    }
  }

selection_done:

  if (ctx.vm.count("forced_segsize"))
    segsize = ctx.vm["forced_segsize"].as<share_t>();

  switch (alg) {
  case 1:
  case 2:
    allreduce_redbcast(c, send_size, reduce_time, dbg);
    break;
  case 3:
    allreduce_rdb(c, send_size, reduce_time, dbg);
    break;
  case 4:
    allreduce_lr(c, send_size, reduce_time, dbg);
    break;
  case 5:
    allreduce_ompi_ring_segmented(c, send_size, reduce_time, dbg, segsize);
    break;
  case 6:
    allreduce_rab_rdb(c, send_size, reduce_time, dbg);
    break;
  default:
    MUST_DIE;
    break;
  }
}
