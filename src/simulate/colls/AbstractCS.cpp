//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "IndexedCS.hpp"

std::shared_ptr<AbstractCS>
AbstractCS::create_linear(commind_t, size_t comm_size, share_t message_size,
                          AtomicAction::DebugInfo dbg) {
  SequenceIndex dim;
  dim.level = 1;
  dim.i[0] = (resind_t)comm_size;       // Sender rank
  dim.i[1] = (resind_t)(comm_size - 1); // Receiver rank (-1 after sender )
  dim.i[2] = 0;
  return Ptr(new IndexedCS(
      2,
      [=](const SequenceIndex &index) {
        resind_t snd = index.i[0];
        resind_t rcv = index.i[1];
        size_t rcv_dep, snd_dep = snd + rcv;
        if (rcv < snd) {
          rcv_dep = comm_size + snd - 2;
        } else {
          rcv_dep = snd - 1;
          rcv++;
        }
        // Need to compute the dependency for sender and receiver
        return CommParams(snd, snd_dep, Dependency::SINGLE, rcv, rcv_dep,
                          Dependency::SINGLE, message_size);
      },
      dbg, dim));
}

static AbstractCS::CommParams create_add_ring_params(resind_t snd,
                                                     resind_t comm_size,
                                                     AbstractCS::Direction dir,
                                                     resind_t step) {

  assert(dir != AbstractCS::Direction::TO_RIGHT_AND_LEFT);
  assert(step < comm_size);
  resind_t rcv = (snd + step) % comm_size;
  if (dir == AbstractCS::Direction::TO_LEFT)
    rcv = (snd + comm_size - step) % comm_size;
  size_t snd_offset, rcv_offset;
  Dependency::State snd_type, rcv_type;
  if (snd < step) {
    snd_type = Dependency::FIRST;
    snd_offset = 1;
  } else {
    snd_type = Dependency::LAST;
    snd_offset = 2;
  }
  if (snd < rcv) {
    rcv_type = Dependency::FIRST;
    rcv_offset = 1;
  } else {
    rcv_type = Dependency::LAST;
    rcv_offset = 2;
  }

  return AbstractCS::CommParams(snd, snd_offset, snd_type, rcv, rcv_offset,
                                rcv_type, MaximumShare);
}

static AbstractCS::CommParams create_xor_ring_params(resind_t snd,
                                                     resind_t comm_size,
                                                     AbstractCS::Direction dir,
                                                     resind_t step) {
  (void)dir;
  assert(dir == AbstractCS::TO_RIGHT_AND_LEFT);
  assert(step < comm_size);
  resind_t rcv = (snd ^ step);
  if (rcv >= comm_size)
    return AbstractCS::CommParams(AbstractCS::CommParams::SKIP_COMM);
  size_t snd_offset, rcv_offset;
  Dependency::State snd_type, rcv_type;
  if (snd < rcv) {
    snd_type = rcv_type = Dependency::FIRST;
    snd_offset = rcv_offset = 1;
  } else {
    snd_type = rcv_type = Dependency::LAST;
    snd_offset = rcv_offset = 2;
  }
  return AbstractCS::CommParams(snd, snd_offset, snd_type, rcv, rcv_offset,
                                rcv_type, MaximumShare);
}

AbstractCS::Ptr AbstractCS::create_logical_ring(size_t comm_size, int nlevels,
                                                bool add_not_xor, Direction dir,
                                                StepFunction &&stepf,
                                                CommUpdateFunction &&update_f,
                                                AtomicAction::DebugInfo dbg) {
  SequenceIndex dim;
  dim.level = nlevels;
  dim.i[0] = (resind_t)comm_size;
  dim.i[1] = 0;
  dim.i[2] = 0;
  if (add_not_xor)
    return Ptr(new IndexedCS(
        4,
        [=](const SequenceIndex &index) {
          auto params = create_add_ring_params(index.i[0], (resind_t)comm_size,
                                               dir, stepf(index));
          update_f(index, params);
          return params;
        },
        dbg, dim));
  return Ptr(new IndexedCS(
      4,
      [=](const SequenceIndex &index) {
        auto params = create_xor_ring_params(index.i[0], (resind_t)comm_size,
                                             dir, stepf(index));
        update_f(index, params);
        return params;
      },
      dbg, dim));
}

inline resind_t rank_to_newrank(resind_t rank, resind_t rem) {
  return (rank < 2 * rem ? (rank % 2 == 0 ? NoResource : rank / 2)
                         : rank - rem);
}
inline resind_t newrank_to_rank(resind_t newrank, resind_t rem) {
  return (newrank < rem ? 2 * newrank + 1 : newrank + rem);
}

AbstractCS::Ptr AbstractCS::create_rdb(commind_t, size_t comm_size,
                                       share_t message_size,
                                       AtomicAction::DebugInfo dbg) {
  SequenceIndex dim;
  auto clog = (resind_t)ceil_log2(comm_size);
  auto pof2 = (1ULL << (clog - 1));
  resind_t rem = (resind_t)(comm_size - pof2);
  dim.level = 1 + clog + 1;
  dim.i[0] = (resind_t)comm_size;
  dim.i[1] = 0;
  dim.i[2] = 0;
  return Ptr(new IndexedCS(
      2,
      [=](const SequenceIndex &index) {
        auto rank = index.i[0];
        if (index.level == 0) {
          // Prolog for non-power-of-two cases
          if (rank < 2 * rem && rank % 2 == 0)
            return CommParams(rank, 1, Dependency::SINGLE, rank + 1, 1,
                              Dependency::SINGLE, message_size);
          return CommParams(CommParams::SKIP_SENDER);
        }
        if (index.level <= clog) {
          resind_t newrank = rank_to_newrank(rank, rem);
          if (newrank == NoResource)
            return CommParams(CommParams::SKIP_SENDER);
          resind_t mask = 1ULL << (index.level - 1);
          resind_t dst_rank = newrank_to_rank(newrank ^ mask, rem);
          if (rank < dst_rank)
            return CommParams(rank, 1, Dependency::FIRST, dst_rank, 1,
                              Dependency::FIRST, message_size);
          return CommParams(rank, 2, Dependency::LAST, dst_rank, 2,
                            Dependency::LAST, message_size);
        }
        // Epilog for non-power-of-two case
        if (rank < 2 * rem && rank % 2 == 1)
          return CommParams(rank, 1, Dependency::SINGLE, rank - 1, 1,
                            Dependency::SINGLE, message_size);
        return CommParams(CommParams::SKIP_SENDER);
      },
      dbg, dim));
}

std::ostream &operator<<(std::ostream &out, const Dependency::State &x) {
  switch (x) {
  case Dependency::INVALID:
    out << "INVALID";
    break;
  case Dependency::SINGLE:
    out << "SINGLE";
    break;
  case Dependency::FIRST:
    out << "FIRST";
    break;
  case Dependency::MIDDLE:
    out << "MIDDLE";
    break;
  case Dependency::LAST:
    out << "LAST";
    break;
  }
  return out;
}

std::ostream &operator<<(std::ostream &out, const AbstractCS::CommParams &x) {
  switch (x.type) {
  case AbstractCS::CommParams::CREATE_COMM:
    out << "(+" << x.snd_dep_offset << " " << x.snd_rank << '='
        << x.snd_dep_type << ")->(+" << x.rcv_dep_offset << " " << x.rcv_rank
        << '=' << x.rcv_dep_type << ")";
    break;
  case AbstractCS::CommParams::SKIP_COMM:
    out << "SKIP_COMM";
    break;
  case AbstractCS::CommParams::SKIP_SENDER:
    out << "SKIP_SENDER";
    break;
  default:
    MUST_DIE
  }
  return out;
}

std::ostream &operator<<(std::ostream &out, const Dependency &x) {
  out << x.state << " #" << x.in_act << " ";
  if (x.used)
    out << "U ";
  if (x.other_dep == std::string::npos)
    out << "&!";
  else
    out << '&' << x.other_dep;
  return out;
}
