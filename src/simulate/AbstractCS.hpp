//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#ifndef _COLLECTIVE_ROUND_HPP
#define _COLLECTIVE_ROUND_HPP
#include "AtomicAction.hpp"
#include "taz-simulate.hpp"
#include <functional>

struct Dependency {
  actind_t in_act;
  size_t other_dep;
  simtime_t compute_delay;
  enum State { INVALID, SINGLE, FIRST, MIDDLE, LAST } state;
  bool used;
};

struct AbstractCS {

  /**
   * @brief In cyclic rounds, describes whether rank send to their right-side or
   * left-side neighbour (or both).
   *
   * Right side means rank r sends to rank r+something
   * Left side means rank r sends to rank r+something
   */
  enum Direction { TO_RIGHT, TO_LEFT, TO_RIGHT_AND_LEFT };

  struct CommParams {
    resind_t snd_rank;
    // Offset of previous dependency in sender queue of dependencies
    size_t snd_dep_offset;
    // Type of the dependency to be created in the sender queue
    Dependency::State snd_dep_type;
    simtime_t snd_delay;
    resind_t rcv_rank;
    size_t rcv_dep_offset;
    Dependency::State rcv_dep_type;
    simtime_t rcv_delay;
    share_t size;
    enum Type {
      CREATE_COMM, // Just create the communication
      SKIP_COMM,   // Do not create comm
      SKIP_SENDER  // copy the in dependency of sender to new act
    } type;
    CommParams(resind_t sender, size_t snd_offset, Dependency::State snd_type,
               resind_t rcv, size_t rcv_offset, Dependency::State rcv_type,
               share_t s)
        : snd_rank(sender), snd_dep_offset(snd_offset), snd_dep_type(snd_type),
          snd_delay(SoftLatency), rcv_rank(rcv), rcv_dep_offset(rcv_offset),
          rcv_dep_type(rcv_type), rcv_delay(SoftLatency), size(s),
          type(CREATE_COMM) {}
    CommParams(Type type)
        : snd_rank(NoResource), snd_dep_offset(0),
          snd_dep_type(Dependency::INVALID), snd_delay(SoftLatency),
          rcv_rank(NoResource), rcv_dep_offset(0),
          rcv_dep_type(Dependency::INVALID), rcv_delay(SoftLatency),
          size(MaximumShare), type(type) {}
    CommParams() : CommParams(SKIP_COMM) {}

    operator bool() const { return type == CREATE_COMM; }
  };

  typedef std::shared_ptr<AbstractCS> Ptr;

  // A function that gives a step size depending on the level
  typedef std::function<resind_t(const SequenceIndex &)> StepFunction;
  // A function that gives a message size depending on the level
  typedef std::function<void(const SequenceIndex &, CommParams &)>
      CommUpdateFunction;
  // A function that creates a CommParam from index
  typedef std::function<CommParams(const SequenceIndex &)> CommFunction;

  size_t deps_per_rank;
  CommFunction f;
  AtomicAction::DebugInfo dbg;

protected:
  AbstractCS() = delete;
  AbstractCS(size_t dependencies_per_rank, CommFunction in_f,
             AtomicAction::DebugInfo d)
      : deps_per_rank(dependencies_per_rank), f(in_f), dbg(d) {}

public:
  virtual ~AbstractCS() {}

  virtual SequenceIndex init_index(int seq) {
    SequenceIndex index;
    index.seq = seq;
    index.level = 0;
    index.i[0] = 0;
    index.i[1] = 0;
    index.i[2] = 0;
    return index;
  }

  // Return if complete
  virtual bool advance(SequenceIndex &index) = 0;

  /**
   * Each rank sends a message to all the other ranks, in parallel.
   *
   */
  static Ptr create_linear(commind_t comm, size_t comm_size,
                           share_t message_size, AtomicAction::DebugInfo dbg);
  /**
   * Each rank sends to the right rank  ( (r+1)%comm_size ).
   * Optionally if symmetrical is set, we also send to the left (
   * (r+comm_size-1)%comm_size ).
   */
  static Ptr create_logical_ring(size_t comm_size, int nlevels,
                                 bool add_not_xor, Direction dir,
                                 StepFunction &&stepf,
                                 CommUpdateFunction &&update_f,
                                 AtomicAction::DebugInfo dbg);

  static Ptr create_rdb(commind_t comm, size_t comm_size, share_t message_size,
                        AtomicAction::DebugInfo dbg);
};

std::ostream &operator<<(std::ostream &out, const Dependency::State &x);
std::ostream &operator<<(std::ostream &out, const AbstractCS::CommParams &x);
std::ostream &operator<<(std::ostream &out, const Dependency &x);

#endif /*_COLLECTIVE_ROUND_HPP*/
