// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _COLLECTIVE_ENGINE_HPP
#define _COLLECTIVE_ENGINE_HPP

#include "AbstractCS.hpp"
#include "Communicator.hpp"
#include "taz-simulate.hpp"

class Engine;
class CollectiveEngine : public MemoryConsumer {

  std::vector<Dependency> deps;

  size_t deps_per_rank;

  struct DependencyIndices {
    size_t start;
    size_t next_offset;
    size_t size;
  };
  std::vector<DependencyIndices> indices;

  void print_deps();

  void _pop_dependency(std::vector<actind_t> &unused_acts, resind_t rank,
                       size_t &offset, int jobid, bool &out_used);
  size_t _push_dependency(resind_t rank, actind_t in_act, size_t other_dep,
                          Dependency::State state, simtime_t compute_delay,
                          int jobid);
  void _use_dependency(std::vector<ActGroup> &out_acts, resind_t rank,
                       size_t offset, simtime_t &out_delay);
  void _seal_if_needed(std::vector<Dependency> &old_act, resind_t rank,
                       int jobid);
  void _do_post_round(std::vector<Dependency> &old_act,
                      std::vector<Dependency> &new_act,
                      const AbstractCS &rounds);
  void
  _create_collective(commind_t comm,
                     const std::vector<std::shared_ptr<AbstractCS>> &rounds,
                     std::string prefix);

  Engine &engine;

public:
  CollectiveEngine(Engine &e)
      : MemoryConsumer("Engine::Collectives"), engine(e) {}

  // Barrier
  void barrier_select(Communicator &c, AtomicAction::DebugInfo &dbg);
  void barrier_ompi_two_procs(Communicator &c, AtomicAction::DebugInfo &dbg);
  void barrier_ompi_bruck(Communicator &c, AtomicAction::DebugInfo &dbg);
  void barrier_ompi_rdb(Communicator &c, AtomicAction::DebugInfo &dbg);

  // Bcast
  void bcast_select(Communicator &c, int root_index, share_t send_size,
                    AtomicAction::DebugInfo &dbg);
  void bcast_binomial_tree(Communicator &c, int root_index, share_t send_size,
                           AtomicAction::DebugInfo &dbg);
  void bcast_ompi_split_bintree(Communicator &c, int root_index,
                                share_t send_size, AtomicAction::DebugInfo &dbg,
                                share_t segsize);
  void bcast_ompi_pipeline(Communicator &c, int root_index, share_t send_size,
                           AtomicAction::DebugInfo &dbg, share_t segsize);
  void bcast_SMP_linear(Communicator &c, int root_index, share_t send_size,
                        AtomicAction::DebugInfo &dbg);

  // Allgather
  void allgather_select(Communicator &c, share_t message_size,
                        AtomicAction::DebugInfo &dbg);
  void allgather_pair(Communicator &c, share_t message_size,
                      AtomicAction::DebugInfo &dbg);
  void allgather_rdb(Communicator &c, share_t message_size,
                     AtomicAction::DebugInfo &dbg);
  void allgather_bruck(Communicator &c, share_t message_size,
                       AtomicAction::DebugInfo &dbg);
  void allgather_ring(Communicator &c, share_t message_size,
                      AtomicAction::DebugInfo &dbg);
  void allgather_ompi_neighborexchange(Communicator &c, share_t message_size,
                                       AtomicAction::DebugInfo &dbg);

  // Alltoall
  void alltoall_select(Communicator &c, share_t message_size,
                       AtomicAction::DebugInfo &dbg);
  void alltoall_basic_linear(Communicator &c, share_t message_size,
                             AtomicAction::DebugInfo &dbg);
  void alltoall_bruck(Communicator &c, share_t message_size,
                      AtomicAction::DebugInfo &dbg);
  void alltoall_ring(Communicator &c, share_t message_size,
                     AtomicAction::DebugInfo &dbg);

  // Reduce
  void reduce_select(Communicator &c, int root_index, share_t send_size,
                     simtime_t reduce_time, AtomicAction::DebugInfo &dbg);
  void reduce_ompi_basic_linear(Communicator &c, int root_index,
                                share_t message_size, simtime_t reduce_time,
                                AtomicAction::DebugInfo &dbg);
  void reduce_ompi_binomial(Communicator &c, int root_index,
                            share_t message_size, simtime_t reduce_time,
                            AtomicAction::DebugInfo &dbg, share_t segsize,
                            int max_outstanding_reqs);
  void reduce_ompi_pipeline(Communicator &c, int root_index,
                            share_t message_size, simtime_t reduce_time,
                            AtomicAction::DebugInfo &dbg, share_t segsize,
                            int max_outstanding_reqs);
  void reduce_ompi_binary(Communicator &c, int root_index, share_t message_size,
                          simtime_t reduce_time, AtomicAction::DebugInfo &dbg,
                          share_t segsize, int max_outstanding_reqs);

  // Allreduce
  void allreduce_select(Communicator &c, share_t send_size,
                        simtime_t reduce_time, AtomicAction::DebugInfo &dbg);
  void allreduce_redbcast(Communicator &c, share_t send_size,
                          simtime_t reduce_time, AtomicAction::DebugInfo &dbg);
  void allreduce_rdb(Communicator &c, share_t send_size, simtime_t reduce_time,
                     AtomicAction::DebugInfo &dbg);
  void allreduce_lr(Communicator &c, share_t send_size, simtime_t reduce_time,
                    AtomicAction::DebugInfo &dbg);
  void allreduce_ompi_ring_segmented(Communicator &c, share_t send_size,
                                     simtime_t reduce_time,
                                     AtomicAction::DebugInfo &dbg,
                                     share_t segsize);
  void allreduce_rab_rdb(Communicator &c, share_t send_size,
                         simtime_t reduce_time, AtomicAction::DebugInfo &dbg);

  virtual size_t get_footprint() {
    size_t footprint = sizeof(CollectiveEngine);
    footprint += GET_HEAP_FOOTPRINT(deps);
    footprint += GET_HEAP_FOOTPRINT(indices);
    return footprint;
  }
};

#endif /*_COLLECTIVE_ENGINE_HPP*/