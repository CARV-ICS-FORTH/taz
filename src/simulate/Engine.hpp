// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _ENGINE_HPP_
#define _ENGINE_HPP_

#include "AtomicAction.hpp"
#include "CollectiveEngine.hpp"
#include "Communicator.hpp"
#include "Model.hpp"
#include "taz-simulate.hpp"
#include <array>
#include <unordered_map>

class ResourceManager;
class AbstractTopology;

// * If we have a new link failure, we check all actions currently using the
// link,
//   get their issuer, and abort the job active on that core.
// * If we have a new action using a link already failing, we check the issuer,
//   and abort the job active on that core.
// * If we have a new node failure, we abort the job active on each core
// * If a job is aborted (e.g. because it passed its wall time), just that job
//   is killed.

// When a job is aborted, all actions are terminated instantaneously.
// For this, we check for blocking and non-blocking actions, and propagate from
// there to find all the actions of the job. For actions that are not holding
// resources, we can just remove the actions For actions that are holding the
// resources, we need to free the resources. This means making sure that all
// those actions are appended to the working set, and that the shares are
// updated as expected. We also need to make sure that the event handles both
// from the main event_heap and from the resources remaining_heaps

/// @brief Engine is concerned with managing the lifecycle of actions in time
/// and interfacing with the simulated applications
///
/// Each job has a number of ranks
/// Each rank is executed in a different core
/// Cores are grouped by host region
/// The global identification across all host regions is a core_id

// .first=tag, .second=send_core
typedef std::pair<int, int> MatchKey;
template <> struct std::hash<MatchKey> {
  std::size_t operator()(const MatchKey &s) const noexcept {
    std::size_t h1 = std::hash<int>{}(s.first);
    std::size_t h2 = std::hash<int>{}(s.second);
    return h1 ^ (h2 << 1); // or use boost::hash_combine
  }
};

class Engine : public MemoryConsumer {

  template <typename T> friend class HeapConsumer;

  struct MatchValue {
    // Info to call _create_comm
    ActGroup other_in_act;
    actind_t other_action;
    simtime_t other_latency;
    size_t size;
    // Used to determine which request was inserted first (in case multiple
    // request with same key are live simultaneously)
    size_t insertion_index;
    MatchValue() = default;
    MatchValue(ActGroup other_in_act, actind_t other_action,
               simtime_t other_latency, size_t size, size_t insertion_index)
        : other_in_act(other_in_act), other_action(other_action),
          other_latency(other_latency), size(size),
          insertion_index(insertion_index) {}
  };

  // If we want to support wildcards (MPI_ANY_xxx), we will need something more
  // clever...
  size_t match_insertion_index;
  typedef std::unordered_multimap<MatchKey, MatchValue> MatchContainer;

  std::vector<FaultyEntity> hosts;

  struct CoreInfo {

    int job_id;

    // Delay since last blocking action
    simtime_t delay_from_last;
    // Handle to event heap element that may limit simulation time
    EventHandleRef event_handle;
    // Last action(s) for each node
    ActGroup last_blocking_actions;
    // True if this node is blocking simulation (because the last blocking
    // actions are too much in the past)
    bool is_blocking_simulation;

    // Next and previous node blocked on the same action
    std::array<resind_t, 2> next_core_id;
    resind_t &next(actind_t act_index) {
      if (last_blocking_actions[0] == act_index)
        return next_core_id[0];
      assert(last_blocking_actions[1] == act_index);
      return next_core_id[1];
    }
    std::array<resind_t, 2> prev_core_id;
    resind_t &prev(actind_t act_index) {
      if (last_blocking_actions[0] == act_index)
        return prev_core_id[0];
      assert(last_blocking_actions[1] == act_index);
      return prev_core_id[1];
    }

    // Any non-blocking action that is still in flight
    std::vector<actind_t> ongoing_non_blocking;
    // Any send that is waiting for a matching receive
    MatchContainer waiting_sends;
    // Any receive that is waiting for a matching send
    MatchContainer waiting_receives;

    bool is_busy() { return job_id >= 0; }
  };

  std::vector<CoreInfo> cores;

  struct JobInfo {
    std::vector<resind_t> cores;
  };

  std::unordered_map<int, JobInfo> jobs;

  // Topology stuff
  resind_t n_regions;
  std::vector<AbstractTopology *> topologies;
  std::vector<int> topologies_ratios;

  // The end time of the last run
  simtime_t last_time;

  bool collect_traffic;
  ApproxMatrix traffic;

  bool _update_core_event(actind_t core_id) {
    AAPool *pool = AAPool::get_instance();
    auto &core = cores[core_id];
    if (!core.is_busy())
      return true;
    bool all_propagating = true;
    simtime_t t = END_OF_TIME;
    FOREACH_ACT_IN_GROUP(index, core.last_blocking_actions) {
      if (pool->at_const(index).state != AtomicAction::PROPAGATING) {
        all_propagating = false;
        break;
      }
      if (pool->at_const(index).next_time <= t) {
        t = pool->at_const(index).next_time;
      }
    }

    if (core.event_handle.check_liveness()) {
      assert(all_propagating);
      pool->update_event(core.event_handle, t + core.delay_from_last, core_id,
                         EventHeapElement::CORE);
    } else if (all_propagating) {
      core.event_handle = pool->push_event(t + core.delay_from_last, core_id,
                                           EventHeapElement::CORE);
    }
    return all_propagating;
  }

  actind_t _do_waitall(resind_t core_id, std::vector<actind_t> &actions,
                       int n_requests, const char *prefix, bool move_blocking,
                       AtomicAction::DebugInfo &dbg) {
    AAPool *pool = AAPool::get_instance();
    assert((int)actions.size() >= n_requests);
    actind_t act = NoAction;
    std::vector<actind_t> wait_actions;
    bool is_two_level = (n_requests >= (int)MaximumFaninFanout);
    // Get all cores to reach there
    auto it = actions.begin();
    for (int index = 0; index < n_requests; index++, it++) {
      auto &in_act = *it;
      if (index % MaximumFaninFanout == 0) {
        // Seal previous action
        if (act != NoAction)
          pool->seal_action(act, get_last_time());
#ifdef SHOW_DEBUG
        std::stringstream ss;
        ss << prefix << "_" << CoreId(core_id) << "i" << index;
        dbg.desc = ss.str();
#endif
        AtomicAction &aa = pool->init_action(act, dbg);
        int n = std::min((int)MaximumFaninFanout, n_requests - index);
        assert(n <= 255);
        aa.countdown_to_send = (uint8_t)n;
        aa.countdown_to_recv = 0;
        aa.remaining = 0;
        aa.countdown_to_fail = 1;
        wait_actions.push_back(act);
        aa.issuer = core_id;
        LOG_DEBUG << prefix << " at " << CoreId(core_id) << " will use action "
                  << act << std::endl;
      }
      pool->add_child(get_last_time(), {in_act, NoAction}, act, SoftLatency, 1,
                      CounterType::TO_SEND);
    }
    // And seal the last action
    // Dow we need a second level ?
    if (is_two_level) {
      assert(actions.size() > 1);
      assert(wait_actions.size() <= 255);
      pool->seal_action(act, get_last_time());
#ifdef SHOW_DEBUG
      std::stringstream ss;
      ss << prefix << "_SEC";
      dbg.desc = ss.str();
#endif
      AtomicAction &aa = pool->init_action(act, dbg);
      aa.countdown_to_send = (uint8_t)wait_actions.size();
      aa.countdown_to_recv = 0;
      aa.remaining = 0;
      aa.countdown_to_fail = 1;
      aa.issuer = core_id;
      LOG_DEBUG << prefix << " at " << CoreId(core_id) << " will use action "
                << act << std::endl;
      for (auto in_act : wait_actions)
        pool->add_child(get_last_time(), {in_act, NoAction}, act, SoftLatency,
                        1, CounterType::TO_SEND);
    }
    actions.erase(actions.begin(), it);
    if (move_blocking)
      move_to_next_blocking(core_id, {act, NoAction}, false);
    pool->seal_action(act, get_last_time());
    return act;
  }

  friend CollectiveEngine;
  CollectiveEngine collectives;
  CommPool comm_pool;

  // Data structure used to store data about past state changes (c.f.
  // --collect_state_changes)
  std::vector<std::pair<uint64_t, uint64_t>> state_changes;
  void _append_state_change(simtime_t time, bool is_link, bool is_recovery,
                            resind_t res) {
    if (not ctx.vm["collect_state_changes"].as<bool>())
      return;
    uint64_t x = (uint64_t)res;
    if (is_recovery)
      x |= 1UL << 62;
    if (is_link)
      x |= 1UL << 61;
    uint64_t time_s = time / SimtimePerSec;
    state_changes.emplace_back(time_s, x);
  }

public:
  ApproxMatrix get_state_changes() {
    ApproxMatrix mat;
    mat.init(state_changes.size(), 2);
    int i = 0;
    for (auto &x : state_changes) {
      mat.add_weight(i, 0, x.first);
      mat.add_weight(i, 1, x.second);
      i++;
    }
    return mat;
  }

  // FIXME: THis is a dirty workaround, those errors should not occur
  simtime_t sum_of_errors;
  int error_count;
  WorkingSet working_set;

  simtime_t get_last_time() { return last_time; }

  Engine(const FaultProfileMap &node_fault_profiles,
         const FaultProfileMap &link_fault_profiles);

  ~Engine();

  commind_t add_job(int job_id, simtime_t start_time,
                    const std::vector<resind_t> &corev) {
    AAPool *pool = AAPool::get_instance();
    actind_t anchor_act = NoAction;
    AtomicAction::DebugInfo dbg(1000000, job_id, 0);
    int rank = 0;
    int prev_core_id = -1;
    for (auto core_id : corev) {
      auto &core = this->cores[core_id];
      assert(core.job_id == -1);
      for (resind_t j = 0; j < 2; j++) {
        core.prev_core_id[j] = NoResource;
        core.next_core_id[j] = NoResource;
      }
      if (rank % MaximumFaninFanout == 0) {
        // Push an anchor action to the event heap
#ifdef SHOW_DEBUG
        std::stringstream ss;
        ss << "ANCHOR_" << CoreId(core_id);
        dbg.desc = ss.str();
#endif
        AtomicAction &aa = pool->init_action(anchor_act, dbg);
        aa.countdown_to_send = 0;
        aa.countdown_to_recv = 0;
        aa.remaining = 0;
        aa.next_time = start_time;
        aa.countdown_to_fail = 1;
        aa.first_blocking = core_id;
        aa.fin_type = AtomicAction::FIN_ON_ALL_PROPAGATED;
        aa.issuer = NoResource;
        pool->seal_action(anchor_act, 0);

        // node.waiting_receives.reserve(comm_size);
        // node.waiting_sends.reserve(comm_size);
        if (rank)
          cores[prev_core_id].next_core_id[0] = NoResource;
      } else {
        core.prev_core_id[0] = prev_core_id;
        cores[prev_core_id].next_core_id[0] = core_id;
      }

      core.job_id = job_id;
      core.last_blocking_actions = {anchor_act, NoAction};
      rank++;
      prev_core_id = core_id;
    }
    std::pair<int, JobInfo> x;
    x.first = job_id;
    x.second.cores = corev;
    auto res = jobs.insert(x);
    assert_always(res.second, "Cannot insert job " << job_id);
    commind_t world_comm = comm_pool.create_communicator(corev, job_id);
    return world_comm;
  }

  commind_t add_communicator(int job_id, const std::vector<resind_t> &corev) {
    return comm_pool.create_communicator(corev, job_id);
  }

  void remove_job(std::vector<commind_t> commv, simtime_t end_time) {
    (void)end_time;
    assert(not commv.empty());
    const std::vector<resind_t> &corev = comm_pool.at(commv[0]).cores;
    for (auto core_id : corev) {
      auto &node = this->cores[core_id];
      node.delay_from_last = END_OF_TIME;
      node.job_id = -1;
      node.event_handle.free();
      assert(not node.is_blocking_simulation);
      assert(node.ongoing_non_blocking.empty());
      assert(node.waiting_receives.empty());
      assert(node.waiting_sends.empty());
      node.last_blocking_actions = {NoAction, NoAction};
    }
    for (auto i : commv)
      comm_pool.destroy_communicator(i);
  }

private:
  // Execute the simulation
  void _run_pre();
  bool _run1_waiting(WorkingSet &set, AAPool *pool, simtime_t top_time,
                     const AtomicAction &act, actind_t index);
  bool _run1_propagating(WorkingSet &set, AAPool *pool, simtime_t top_time,
                         const AtomicAction &act, actind_t index);
  void _run1_link_event(WorkingSet &set, const EventHeapElement &top,
                        simtime_t now);
  void _run1_host_event(WorkingSet &set, const EventHeapElement &top,
                        simtime_t now);
  void _run1_bottleneck_event(WorkingSet &set, const EventHeapElement &top,
                              simtime_t now);
  void _run1_core_event(WorkingSet &set, const EventHeapElement &top,
                        simtime_t now);
  void _run1_resource_manager_event(WorkingSet &set,
                                    const EventHeapElement &top, simtime_t now);

  void _run1(WorkingSet &set, AAPool *pool, bool &bailout,
             int &advanced_events);
  void _run2(WorkingSet &set, AAPool *pool);
  void abort_job(int job_id, WorkingSet &set);

public:
  // Execute the simulation
  simtime_t run(int &advancing_events);

  int get_comm_epoch(commind_t index) { return comm_pool.at(index).epoch; }

  void enqueue_compute(resind_t core_id, simtime_t dt);
  void enqueue_compute(commind_t comm, int rank, double time);
  actind_t enqueue_send(commind_t comm, int src_rank, int dst_rank,
                        share_t size, int tag, bool blocking,
                        AtomicAction::DebugInfo &dbg);
  actind_t enqueue_receive(commind_t comm, int src_rank, int dst_rank,
                           share_t size, int tag, bool blocking,
                           AtomicAction::DebugInfo &dbg);
  ActGroup enqueue_send_receive(commind_t comm, int src_rank, int common_rank,
                                int dst_rank, share_t recv_size,
                                share_t send_size, int tag,
                                AtomicAction::DebugInfo &dbg);
  actind_t enqueue_waitall(commind_t comm, int rank, int n_requests,
                           AtomicAction::DebugInfo &dbg);

  bool enqueue_allreduce(commind_t comm, share_t send_size,
                         simtime_t reduce_time, AtomicAction::DebugInfo &dbg);

  bool enqueue_allgather(commind_t comm, share_t send_size,
                         AtomicAction::DebugInfo &dbg);
  bool enqueue_alltoall(commind_t comm, share_t send_size,
                        AtomicAction::DebugInfo &dbg);
  bool enqueue_barrier(commind_t comm, AtomicAction::DebugInfo &dbg);
  bool enqueue_bcast(commind_t comm, int root_index, share_t send_size,
                     AtomicAction::DebugInfo &dbg);
  bool enqueue_reduce(commind_t comm, int root_index, share_t send_size,
                      simtime_t reduce_time, AtomicAction::DebugInfo &dbg);
  bool enqueue_init(commind_t comm, AtomicAction::DebugInfo &dbg);
  bool enqueue_finalize(commind_t comm, int job_id,
                        AtomicAction::DebugInfo &dbg);

  bool is_core_blocking_simulation(resind_t core_id) {
    assert((size_t)core_id < cores.size());
    return cores[core_id].is_blocking_simulation;
  }

  bool is_core_working(resind_t core_id) {
    CoreId id(core_id);
    return not hosts[id.get_host()].is_faulty;
  }

  double get_core_failure_rate(resind_t core_id) {
    CoreId id(core_id);
    return hosts[id.get_host()].failure_rate;
  }

  double get_core_cdf(resind_t core_id, double duration) {
    CoreId id(core_id);
    return hosts[id.get_host()].get_cdf(duration);
  }

  // Returns -1 if no job is currently runnning on this node
  int get_core_job(resind_t core_id) {
    assert(core_id != NoResource);
    return cores[core_id].job_id;
  }
  int get_number_of_hosts() const { return (int)hosts.size(); }
  int get_number_of_regions() const { return n_regions; }
  int get_number_of_cores() const { return n_regions * NumberOfCoresPerRegion; }

private:
  std::vector<resind_t> route;

  // Compute the route from src to dst
  void get_route(resind_t src_core_id, resind_t dst_core_id, share_t size,
                 simtime_t &latency, share_t &remaining);

public:
  /**
   * @brief Create actions depending on size w.r.t. eager/rdv protocol
   *
   * If we use the Rendez-vous protocol (size > EagerThreshold),
   * we need to get both sender and receiver ready before sending data.
   * Hence, we only need 1 transfer action:
   *
   *  (Sender before) -----                 ----> (Sender after)
   *                       \               /
   *                        - >(transfer)--
   *                       /               \
   *  (Receiver before)----                 ----> (Receiver after)
   *
   * If we use the eager protocol, the sender proceeds without waiting
   * for the receiver. Hence, we need 2 actions.
   * The first to actually do the transfer, and the second just to make sure
   * that receiver waits/pays for completion (mostly if it is early):
   *
   *  (Sender before) -->(transfer)--> (Sender after)
   *                                \
   *                                 ->(comm)-
   *                                          \
   *  (Receiver before)--------------------------> (Receiver after)
   *
   * @param send_core
   * @param receive_core
   * @param size If MaximumShare, means we do not have size info yet
   * @param sender_in_act
   * @param receiver_in_act
   * @param sender_latency  Compute latency from the sender
   * @param receiver_latency Compute latency from the receiver
   * @param transfer_base_latency Transfer latency
   * @param transfer_act
   * @param receiver_act needs to be set to NoAction, unless the action has
   * already been allocated
   * @return true
   * @return false
   */
  void enqueue_raw_comm(resind_t send_core, resind_t receive_core, share_t size,
                        ActGroup sender_in_act, ActGroup receiver_in_act,
                        simtime_t sender_latency, simtime_t receiver_latency,
                        actind_t &transfer_act, actind_t &receiver_act,
                        AtomicAction::DebugInfo &dbg) {

    bool add_sender_dep = false;
    bool add_receiver_dep = false;
    if (receiver_act != NoAction) {
      // Receiver-first, sender-second
      ctx.pool->at(receiver_act).dbg.combine(dbg);
      if (size >= EagerThreshold)
        transfer_act = receiver_act;
    } else if (transfer_act != NoAction) {
      // Sender-first, Receiver-second
      ctx.pool->at(transfer_act).dbg.combine(dbg);
      if (size >= EagerThreshold)
        receiver_act = transfer_act;
    }
    if (transfer_act == NoAction) {
      ctx.pool->init_action(transfer_act, dbg);
      add_sender_dep = true;
      if (size >= EagerThreshold) {
        receiver_act = transfer_act;
        add_receiver_dep = true;
      }
    }
    assert(transfer_act != NoAction);
    if (receiver_act == NoAction) {
      ctx.pool->init_action(receiver_act, dbg);
      add_receiver_dep = true;
    }
    assert(receiver_act != NoAction);

    simtime_t transfer_latency;
    share_t remaining;
    get_route(send_core, receive_core, size, transfer_latency, remaining);
    ctx.model->init_action_resources(transfer_act, route);

    if (collect_traffic) {
      CoreId src_core(send_core);
      CoreId dst_core(receive_core);
      traffic.add_weight(src_core.get_host(), dst_core.get_host(), size);
    }

    if (add_sender_dep)
      ctx.pool->add_child(get_last_time(), sender_in_act, transfer_act,
                          sender_latency + transfer_latency, 1,
                          CounterType::TO_SEND, false, true);
    if (add_receiver_dep)
      ctx.pool->add_child(get_last_time(), receiver_in_act, receiver_act,
                          receiver_latency + transfer_latency, 1,
                          CounterType::TO_RECV, false, true);
    auto &a = ctx.pool->at(transfer_act);
    a.remaining = remaining;
    a.countdown_to_fail = 1;
    a.concurrency_cost = 1;
    a.issuer = send_core;
#ifdef SHOW_DEBUG
    std::stringstream ss;
    ss << ":" << CoreId(send_core) << "-(" << size << "/" << remaining << ")->"
       << CoreId(receive_core);
    a.dbg.desc += ss.str();
#endif
    if (transfer_act != receiver_act) {
      ctx.pool->add_child(get_last_time(), {transfer_act, NoAction},
                          receiver_act, SoftLatency, 1, CounterType::TO_SEND,
                          false, true);
      auto &aa = ctx.pool->at(receiver_act);
      aa.remaining = 0;
      aa.countdown_to_fail = 1;
      aa.concurrency_cost = 1;
      aa.issuer = receive_core;
#ifdef SHOW_DEBUG
      a.dbg.desc += "/TRN";
      aa.dbg.desc += ss.str() + "/RCV";
#endif
    }
  }

  void move_to_next_blocking(resind_t core_id, ActGroup act_group,
                             bool ensure_causality = false) {
    INCREMENT_STAT(move_to_next_blocking)
    AAPool *pool = AAPool::get_instance();
    auto &core = cores[core_id];
    ActGroup &last_blocks = core.last_blocking_actions;
    assert(last_blocks[0] != act_group[0]);
    core.is_blocking_simulation = false;
    // copy those to keep around
    auto old_prev = core.prev_core_id;
    auto old_next = core.next_core_id;
    // if( not nodes[node].ongoing_non_blocking.empty())
    //     LOG_INFO<<"Some non-blocking actions cross blocking call,
    //     beware.."<<std::endl;

    FOREACH_ACT_IN_GROUP(index, act_group) {
      if (ensure_causality)
        pool->add_child(last_time, last_blocks, index, core.delay_from_last, 1,
                        CounterType::TO_SEND, false, true);
      // Add new actions to node list
      const auto &aa = pool->at_const(index);
      core.prev_core_id[i] = NoResource;
      if (aa.first_blocking == NoResource) {
        core.next_core_id[i] = NoResource;
      } else {
        assert(aa.first_blocking < cores.size());
        cores[aa.first_blocking].prev(index) = core_id;
        core.next_core_id[i] = aa.first_blocking;
      }
      pool->set_first_blocking(index, core_id, get_last_time());
      if (aa.fin_type != AtomicAction::FIN_ON_SENDRECV_PROPAGATED)
        pool->set_fin_criteria(index, AtomicAction::FIN_ON_ALL_PROPAGATED);
    }

    // Remove old actions from cores list
    FOREACH_ACT_IN_GROUP(index, last_blocks) {
      assert(pool->at_const(index).first_blocking != NoResource);
      actind_t next = old_next[i];
      actind_t prev = old_prev[i];
      assert(next < cores.size() || next == NoAction);
      if (next != NoAction) {
        cores[next].prev(index) = prev;
      }
      if (prev == NoAction) {
        // This call might trigger lots of stuff if next is NO_ACTION (i.e.
        // this was the last blocking node)
        pool->set_first_blocking(index, next, get_last_time());
      } else {
        cores[prev].next(index) = next;
      }
    }

    // Remove event from heap
    core.event_handle.free();
    LOG_DEBUG << "For core " << CoreId(core_id) << ", move last blocking from "
              << last_blocks << " to " << act_group << std::endl;
    last_blocks = act_group;
    core.delay_from_last = 0;
  }

private:
  // Check in the receive node if an action matches (and remove the
  // MatchEntry) or create a new action and add the MatchEntry.
  // The MatchEntry is always on the receiver side in_act is the action that
  // will precede this one. This is useful if recv gets first, and we need to
  // add latency to it latency is an input  if is_send is true, and an output if
  // is_send is false
  // Return a match was found
  bool match_action(bool is_send, share_t size, int tag, int send_core,
                    int receive_core, ActGroup in_act, simtime_t latency,
                    actind_t &out_act, AtomicAction::DebugInfo &dbg) {
    start_profile_region("main_parsesendrecv_match");

    MatchContainer &matches = is_send ? cores[receive_core].waiting_receives
                                      : cores[receive_core].waiting_sends;
    MatchContainer &alt_matches = is_send
                                      ? cores[receive_core].waiting_sends
                                      : cores[receive_core].waiting_receives;
    auto it_range = matches.equal_range({tag, send_core});
    bool match_found = (it_range.first != matches.end());
    actind_t transfer_act = NoAction;
    actind_t receiver_act = NoAction;
    if (not match_found) {
      simtime_t transfer_latency;
      share_t remaining;
      // FIXME: This is not great because a receiver may not need the actual
      // sender (MPI_ANY) or size, but that would require to make deeper
      // changes..
      get_route(send_core, receive_core, size, transfer_latency, remaining);

      // Just create an AtomicAction to attach children if need to
      // Defer everything else when we get a match
      ctx.pool->init_action(out_act, dbg);
      MatchKey k{tag, send_core};
      MatchValue v{in_act, out_act, latency, size, match_insertion_index++};
      alt_matches.emplace(k, v);
      if (is_send) {
        transfer_act = out_act;
        ctx.pool->add_child(get_last_time(), in_act, transfer_act,
                            latency + transfer_latency, 1, CounterType::TO_SEND,
                            false, true);
      } else {
        receiver_act = out_act;
        ctx.pool->add_child(get_last_time(), in_act, receiver_act,
                            latency + transfer_latency, 1, CounterType::TO_RECV,
                            false, true);
      }
      LOG_DEBUG << "Create new match for action ";
    } else {
      // Find the first match in the matching iterator range
      MatchContainer::iterator it = it_range.first;
      for (MatchContainer::iterator it2 = ++it_range.first;
           it2 != it_range.second; it2++) {
        if (it2->second.insertion_index < it->second.insertion_index)
          it = it2;
      }
      if (is_send) {
        receiver_act = it->second.other_action;
        enqueue_raw_comm(
            send_core, receive_core, size, in_act, it->second.other_in_act,
            latency, it->second.other_latency, transfer_act, receiver_act, dbg);
      } else {
        // Receive
        transfer_act = it->second.other_action;
        enqueue_raw_comm(send_core, receive_core, it->second.size,
                         it->second.other_in_act, in_act,
                         it->second.other_latency, latency, transfer_act,
                         receiver_act, dbg);
      }
      matches.erase(it);
      out_act = is_send ? transfer_act : receiver_act;
      actind_t other_act = is_send ? receiver_act : transfer_act;
      if (other_act != out_act &&
          ctx.pool->at_const(other_act).state == AtomicAction::INITIALIZING)
        ctx.pool->seal_action(other_act, get_last_time());
      LOG_DEBUG << "Found matching action ";
    }

    LOG_DEBUG << transfer_act << "/" << receiver_act << " for "
              << (is_send ? "SND" : "RCV") << " tag:" << tag
              << " sender:" << CoreId(send_core)
              << " receiver:" << CoreId(receive_core) << std::endl;

    end_profile_region("main_parsesendrecv_match");
    return match_found;
  }

public:
  void start_traffic_collection() {
    traffic.init(this->get_number_of_hosts());
    collect_traffic = true;
  }

  void stop_traffic_collection() { collect_traffic = false; }

  const ApproxMatrix &get_traffic() const { return traffic; }
  const ApproxMatrix &get_topology_matrix();

  virtual size_t get_footprint() {
    size_t footprint = sizeof(Engine);
    footprint += GET_HEAP_FOOTPRINT(hosts);
    footprint += GET_HEAP_FOOTPRINT(cores);
    footprint += GET_HEAP_FOOTPRINT(jobs);
    footprint += GET_HEAP_FOOTPRINT(traffic);
    footprint += GET_HEAP_FOOTPRINT(state_changes);
    footprint += GET_HEAP_FOOTPRINT(working_set);
    footprint += GET_HEAP_FOOTPRINT(route);
    return footprint;
  }
};

template <> struct HeapConsumer<Engine::CoreInfo> {
  static size_t get_heap_footprint(const Engine::CoreInfo &x) {
    size_t footprint = GET_HEAP_FOOTPRINT(x.ongoing_non_blocking);
    footprint += GET_HEAP_FOOTPRINT(x.waiting_receives);
    footprint += GET_HEAP_FOOTPRINT(x.waiting_sends);
    return footprint;
  }
};
template <> struct HeapConsumer<Engine::JobInfo> {
  static size_t get_heap_footprint(const Engine::JobInfo &x) {
    return GET_HEAP_FOOTPRINT(x.cores);
  }
};

#endif //_ENGINE_HPP_
