//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../CollectiveEngine.hpp"
#include "IndexedCS.hpp"

void CollectiveEngine::allgather_pair(Communicator &c, share_t message_size,
                                      AtomicAction::DebugInfo &dbg) {
  auto comm_size = c.cores.size();
  int nlevels = (int)comm_size - 1;
  _create_collective(
      c.index,
      {AbstractCS::create_logical_ring(
          comm_size, nlevels, false, AbstractCS::TO_RIGHT_AND_LEFT,
          [](const SequenceIndex &i) { return i.level + 1; },
          [=](const SequenceIndex &, AbstractCS::CommParams &params) {
            params.size = message_size;
          },
          dbg)},
      "allgather_pair");
}

void CollectiveEngine::allgather_rdb(Communicator &c, share_t message_size,
                                     AtomicAction::DebugInfo &dbg) {
  resind_t comm_size = (resind_t)c.cores.size();
  resind_t comm_size_log2 = floor_log2(comm_size);
  std::vector<AbstractCS::Ptr> sequences;
  SequenceIndex dimension;
  dimension.level = comm_size_log2 + 1;
  dimension.i[0] = comm_size;
  dimension.i[1] = comm_size_log2 + 1;
  std::vector<share_t> current_size(comm_size, message_size);
  sequences.emplace_back(new IndexedCS(
      3 + comm_size_log2,
      [&](const SequenceIndex &index) {
        resind_t rank = index.i[0];
        resind_t mask = (1 << index.level);
        resind_t dst = rank ^ mask;
        if (dst >= comm_size)
          return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_SENDER);
        size_t offset = 1;
        Dependency::State state = Dependency::SINGLE;
        if (index.i[1] == 0) {
          share_t s = current_size[rank];
          current_size[rank] += mask * message_size;
          return AbstractCS::CommParams(rank, offset, state, dst, offset, state,
                                        s);
        }
        // Other indices are used only if we need to build a tree
        resind_t dst_tree_root = trunc_pof2(dst, index.level);
        resind_t rank_tree_root = trunc_pof2(rank, index.level);

        if (dst_tree_root + mask <= comm_size || index.level < index.i[1] + 1)
          return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_SENDER);

        resind_t k = index.level - index.i[1] - 1;
        resind_t num_procs_completed = comm_size - rank_tree_root - mask;
        // num_procs_completed is the number of processes in this
        //   subtree that have all the data. Send data to others
        //   in a tree fashion. First find root of current tree
        //   that is being divided into two. k is the number of
        //   least-significant bits in this process's rank that
        //   must be zeroed out to find the rank of the root

        resind_t tmp_mask = 1 << k;

        dst = rank ^ tmp_mask;
        resind_t tree_root = trunc_pof2(rank, k);

        // send only if this proc has data and destination
        //   doesn't have data. at any step, multiple processes
        //   can send if they have the data
        // FIXME: Need to compute the size of the message properly
        if ((dst > rank) && (rank < tree_root + num_procs_completed) &&
            (dst >= tree_root + num_procs_completed))
          return AbstractCS::CommParams(rank, offset, state, dst, offset, state,
                                        message_size);

        return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_COMM);
      },
      dbg, dimension));
  _create_collective(c.index, sequences, "allgather_rdb");
  // TODO: This RDB implementation is a mess, perhaps pick another using trees
  // properly ?

  /*
    resind_t mask = 1;
    resind_t i = 0;
    share_t curr_size = message_size;
    while (mask < comm_size) {
      resind_t dst = rank ^ mask;
      resind_t dst_tree_root = dst >> i;
      resind_t dst_tree_root <<= i;
      resind_t rank_tree_root = rank >> i;
      resind_t rank_tree_root <<= i;

      if (dst < comm_size) {
        Request::sendrecv(curr_size, dst, mask * message_size, dst);
        curr_size += mask * message_size;
      }

      if (dst_tree_root + mask > comm_size) {
        num_procs_completed = comm_size - rank_tree_root - mask;
        // num_procs_completed is the number of processes in this
        //   subtree that have all the data. Send data to others
        //   in a tree fashion. First find root of current tree
        //   that is being divided into two. k is the number of
        //   least-significant bits in this process's rank that
        //   must be zeroed out to find the rank of the root

        j = mask;
        k = 0;
        while (j) {
          j >>= 1;
          k++;
        }
        k--;

        resind_t tmp_mask = mask >> 1;

        while (tmp_mask) {
          dst = rank ^ tmp_mask;

          tree_root = rank >> k;
          tree_root <<= k;

          // send only if this proc has data and destination
          //   doesn't have data. at any step, multiple processes
          //   can send if they have the data
          if ((dst > rank) && (rank < tree_root + num_procs_completed) &&
              (dst >= tree_root + num_procs_completed)) {
            Request::send(recv_ptr + offset, last_recv_count, recv_type, dst,
    tag, comm);

            // last_recv_cnt was set in the previous
            //   receive. that's the amount of data to be
            //   sent now.
          }
          // recv only if this proc. doesn't have data and sender
          //   has data
          else if ((dst < rank) && (dst < tree_root + num_procs_completed) &&
                   (rank >= tree_root + num_procs_completed)) {
            Request::recv(recv_ptr + offset, recv_count * num_procs_completed,
                          recv_type, dst, tag, comm, &status);
            // num_procs_completed is also equal to the no. of processes
            // whose data we don't have
            last_recv_count = Status::get_count(&status, recv_type);
            curr_count += last_recv_count;
          }
          tmp_mask >>= 1;
          k--;
        }
      }

      mask <<= 1;
      i++;
    }*/
}
void CollectiveEngine::allgather_bruck(Communicator &c, share_t message_size,
                                       AtomicAction::DebugInfo &dbg) {
  resind_t comm_size = (resind_t)c.cores.size();
  auto comm_size_log2 = floor_log2(comm_size - 1);
  auto comm_pof2 = (resind_t)1 << comm_size_log2;
  auto levels = comm_size_log2 + 1;
  resind_t rem = comm_size - comm_pof2;
  std::vector<AbstractCS::Ptr> sequences;
  SequenceIndex dimension;

  sequences.emplace_back(AbstractCS::create_logical_ring(
      comm_size, levels, true, AbstractCS::Direction::TO_RIGHT,
      [](const SequenceIndex &index) { return 1 << index.level; },
      [=](const SequenceIndex &index, AbstractCS::CommParams &params) {
        params.size = message_size * (1ULL << index.level);
        if (index.level + 1 == levels)
          params.size = message_size * rem;
      },
      dbg));
  _create_collective(c.index, sequences, "allgather_bruck");
}

void CollectiveEngine::allgather_ring(Communicator &c, share_t message_size,
                                      AtomicAction::DebugInfo &dbg) {
  auto comm_size = c.cores.size();
  int nlevels = (int)comm_size - 1;
  _create_collective(
      c.index,
      {AbstractCS::create_logical_ring(
          comm_size, nlevels, true, AbstractCS::TO_RIGHT,
          [](const SequenceIndex &i) { return i.level + 1; },
          [=](const SequenceIndex &, AbstractCS::CommParams &params) {
            params.size = message_size;
          },
          dbg)},
      "allgather_ring");
}

void CollectiveEngine::allgather_ompi_neighborexchange(
    Communicator &c, share_t message_size, AtomicAction::DebugInfo &dbg) {
  resind_t comm_size = (resind_t)c.cores.size();
  if (comm_size % 2) {
    LOG_DEBUG << " Change implementation from allgather_ompi_neighborexchange "
                 "to allgather_ring because comm size is odd."
              << std::endl;
    allgather_ring(c, message_size, dbg);
    return;
  }
  int nlevels = (int)comm_size / 2;
  std::vector<AbstractCS::Ptr> sequences;
  SequenceIndex dimension;
  dimension.level = nlevels;
  dimension.i[0] = comm_size;
  sequences.emplace_back(new IndexedCS(
      4,
      [=](const SequenceIndex &index) {
        bool spin = (index.level % 2 == 1);
        resind_t rank = index.i[0];
        bool rank_is_odd = (rank % 2 == 1);
        resind_t pair = (index.i[0] + 1) % comm_size;
        if (spin != rank_is_odd)
          pair = diff_mod<resind_t>(rank, 1, comm_size);
        share_t s = (index.level == 0 ? message_size : message_size * 2);
        size_t offset = 1;
        Dependency::State state = Dependency::FIRST;
        if (pair < rank) {
          offset = 2;
          state = Dependency::LAST;
        }
        return AbstractCS::CommParams(rank, offset, state, pair, offset, state,
                                      s);
      },
      dbg, dimension));
  _create_collective(c.index, sequences, "allgather_ompi_neighborexchange");
}

void CollectiveEngine::allgather_select(Communicator &c, share_t message_size,
                                        AtomicAction::DebugInfo &dbg) {
  auto comm_size = c.cores.size();
  int alg = -1;
  if (ctx.vm.count("allgather_alg")) {
    alg = ctx.vm["allgather_alg"].as<int>();
    goto selection_done;
  }

  /* Special case for 2 processes */
  if (comm_size == 2) {
    alg = 1;
  } else {

    /* Determine complete data size */
    share_t total_size = message_size * comm_size;
    size_t pow2_size;
    for (pow2_size = 1; pow2_size < comm_size; pow2_size <<= 1)
      ;

    /* Decision based on MX 2Gb results from Grig cluster at
       The University of Tennesse, Knoxville
       - if total message size is less than 50KB use either bruck or
       recursive doubling for non-power of two and power of two cores,
       respectively.
       - else use ring and neighbor exchange algorithms for odd and even
       number of cores, respectively.
    */
    if (total_size < 50000) {
      if (pow2_size == comm_size) {
        alg = 2;
      } else {
        alg = 3;
      }
    } else {
      if (comm_size % 2) {
        alg = 4;
      } else {
        alg = 5;
      }
    }
  }

selection_done:
  switch (alg) {
  case 1:
    allgather_pair(c, message_size, dbg);
    break;
  case 2:
    allgather_rdb(c, message_size, dbg);
    break;
  case 3:
    allgather_bruck(c, message_size, dbg);
    break;
  case 4:
    allgather_ring(c, message_size, dbg);
    break;
  case 5:
    allgather_ompi_neighborexchange(c, message_size, dbg);
    break;
  default:
    MUST_DIE
  }
}
