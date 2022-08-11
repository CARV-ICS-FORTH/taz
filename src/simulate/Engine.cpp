// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "Engine.hpp"
#include "ResourceManager.hpp"
#include "topologies/AbstractTopology.hpp"
#include <cmath>
#include <string>

std::mt19937 mt19937_gen_fault_nodes;
std::mt19937 mt19937_gen_fault_links;

std::uniform_real_distribution<>::param_type NodeParamsRecovery;
std::uniform_real_distribution<>::param_type LinkParamsRecovery;

size_t MaximumFaninFanout;

Engine::Engine(const FaultProfileMap &node_fault_profiles,
               const FaultProfileMap &link_fault_profiles)
    : MemoryConsumer("Engine"), match_insertion_index(0), last_time(0),
      collect_traffic(false), collectives(*this), comm_pool("Engine::CommPool"),
      sum_of_errors(0), error_count() {
  auto &resources = ctx.model->get_resources();

  // Init distributions to generate individual fault distribution parameters
  double node_nominal_mtbf_log10 =
      ctx.vm["node_nominal_mtbf_log10"].as<double>();
  double link_nominal_mtbf_log10 =
      ctx.vm["link_nominal_mtbf_log10"].as<double>();
  double mtbf_scale_log10 = ctx.vm["mtbf_stdev_scale_log10"].as<double>();
  double mtbf_shape_log10 = ctx.vm["mtbf_stdev_shape_log10"].as<double>();
  double node_nominal_mttr =
      std::pow(10.0, ctx.vm["node_nominal_mttr_log10"].as<double>());
  double link_nominal_mttr =
      std::pow(10.0, ctx.vm["link_nominal_mttr_log10"].as<double>());
  // MTTR follows a uniform law. The variance is 1/12*range^2.
  // Hence range=stdev*sqrt(12)
  double mttr_range =
      std::pow(10.0, ctx.vm["mttr_stdev_log10"].as<double>()) * 3.464;
  uint16_t link_concurrency = ctx.vm["link_concurrency"].as<uint16_t>();
  simtime_t link_latency = ctx.vm["link_latency"].as<simtime_t>();
  simtime_t loopback_link_latency = ctx.vm["link_latency"].as<simtime_t>();
  uint64_t link_capacity = ctx.vm["link_capacity"].as<uint64_t>();
  uint64_t loopback_link_capacity =
      ctx.vm["loopback_link_capacity"].as<uint64_t>();

  auto ptr = AbstractTopology::build_instance(
      ctx.vm["topology"].as<std::string>(), 'F', resources, 1, link_concurrency,
      link_capacity, link_latency, link_nominal_mtbf_log10,
      link_fault_profiles);
  ptr->build_topology_graph();
  if (ctx.vm["export_topology"].as<bool>()) {
    std::stringstream topology_filename;
    topology_filename << ctx.vm["stats_folder"].as<std::string>()
                      << "/topology.tazm";
    ptr->get_connectivity_matrix().save(topology_filename.str(), "topology",
                                        ApproxMatrix::APPROX);
  }

  auto n_hosts = n_regions = ptr->get_total_endpoints();
  hosts.resize(n_hosts);
  topologies.push_back(ptr);
  topologies_ratios.push_back(1 << 31);

  std::string intern_topology;

  switch (NumberOfRegionsPerHost) {
  case 1:
    break;
  case 2:
    intern_topology = "TORUS:2";
    break;
  case 4:
    intern_topology = "TORUS:2x2";
    break;
  default:
    MUST_DIE
  }
  if (!intern_topology.empty()) {
    ptr = AbstractTopology::build_instance(
        intern_topology, 'I', resources, n_regions, link_concurrency,
        loopback_link_capacity, loopback_link_latency, 0, link_fault_profiles);
    n_regions *= ptr->get_total_endpoints();
    topologies.push_back(ptr);
    topologies_ratios.push_back(NumberOfRegionsPerHost);
  }

  ptr = AbstractTopology::build_instance(
      "LOOPBACK", 'L', resources, n_regions, link_concurrency,
      loopback_link_capacity, loopback_link_latency, 0, link_fault_profiles);
  topologies.push_back(ptr);
  topologies_ratios.push_back(1);

  resind_t n_cores = n_regions * NumberOfCoresPerRegion;

  NodeParamsRecovery = std::uniform_real_distribution<>::param_type(
      std::max(60.0, node_nominal_mttr - mttr_range / 2),
      node_nominal_mttr + mttr_range / 2);
  LinkParamsRecovery = std::uniform_real_distribution<>::param_type(
      std::max(60.0, link_nominal_mttr - mttr_range / 2),
      link_nominal_mttr + mttr_range / 2);

  std::normal_distribution<>::param_type NodeParamsFaultShape =
      std::normal_distribution<>::param_type(0, mtbf_shape_log10);
  std::normal_distribution<>::param_type NodeParamsFaultScale =
      std::normal_distribution<>::param_type(node_nominal_mtbf_log10,
                                             mtbf_scale_log10);

  std::normal_distribution<> dist_normal;

  // Create the cores
  cores.resize(n_cores);
  for (resind_t i = 0; i < n_cores; i++) {
    for (resind_t j = 0; j < 2; j++) {
      cores[i].prev_core_id[j] = NoResource;
      cores[i].next_core_id[j] = NoResource;
    }
    cores[i].job_id = -1;
    cores[i].last_blocking_actions = {NoAction, NoAction};
    cores[i].is_blocking_simulation = false;
  }

  for (resind_t i = 0; i < n_hosts; i++) {
    double shape = FaultyEntity::pick_normal_value_log10(
        mt19937_gen_fault_nodes, dist_normal, NodeParamsFaultShape);
    double scale = FaultyEntity::pick_normal_value_log10(
        mt19937_gen_fault_nodes, dist_normal, NodeParamsFaultScale);
    hosts[i].el.type = EventHeapElement::HOST_RECOVERY;
    hosts[i].el.rindex = i;
    hosts[i].set_fault_params(FaultDistribution::param_type(shape, scale));
    auto it = node_fault_profiles.find(i);
    if (it != node_fault_profiles.end())
      hosts[i].profile = it->second;
    hosts[i].push_next_event(0);
  }
}

Engine::~Engine() {
  for (auto ptr : topologies) {
    delete ptr;
  }
  topologies.clear();
}

void Engine::get_route(resind_t src_core_id, resind_t dst_core_id, share_t size,
                       simtime_t &latency, share_t &remaining) {
  route.clear();
  latency = 0;
  CoreId src_core(src_core_id);
  CoreId dst_core(dst_core_id);
  remaining = ctx.bw_factor(size);
  // This is OK to have size of 0, but we need bw factor to make remaining non
  // zero
  assert(remaining != 0);
  auto src_sub = src_core.get_host_region_id();
  resind_t src_index = 0;
  auto dst_sub = dst_core.get_host_region_id();
  resind_t dst_index = 0;
  for (int l = (int)topologies.size() - 1; l >= 0; l--) {
    auto ratio = topologies_ratios[l];
    src_index = src_sub % ratio;
    src_sub /= ratio;
    dst_index = dst_sub % ratio;
    dst_sub /= ratio;

    topologies[l]->get_route(src_sub, src_index, dst_sub, dst_index, route,
                             latency);
    if (src_sub == dst_sub)
      break;
  }

  ADD_INDIV_STAT(hops, route.size())
  latency = ctx.lat_factor(size, latency);
  LOG_DEBUG << "=" << dst_core << ", latency:" << simtime_to_double(latency)
            << std::endl;
}

void Engine::enqueue_compute(resind_t core_id, simtime_t dt) {
  auto &core = cores[core_id];
  LOG_DEBUG << "For core " << CoreId(core_id) << ", move time from last block: "
            << simtime_to_double(core.delay_from_last) << " + "
            << simtime_to_double(dt);
  core.delay_from_last += dt;
  core.is_blocking_simulation = false;
  _update_core_event(core_id);
  LOG_DEBUG << "=" << simtime_to_double(core.delay_from_last) << std::endl;
}

void Engine::enqueue_compute(commind_t comm, int rank, double time) {
  // TODO: Check whether compute entries parameter is time or flops
  auto &c = comm_pool.at(comm);
  assert(rank < (int)c.cores.size());
  assert(time > 0.0);
  resind_t core_id = c.cores[rank];
  simtime_t dt = double_to_simtime(time);
  enqueue_compute(core_id, dt);
}

actind_t Engine::enqueue_send(commind_t comm, int src_rank, int dst_rank,
                              share_t size, int tag, bool blocking,
                              AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  assert(src_rank < (int)c.cores.size());
  assert(dst_rank < (int)c.cores.size());
  resind_t src = c.cores[src_rank];
  resind_t dst = c.cores[dst_rank];
  AAPool *pool = AAPool::get_instance();

  actind_t act;
  ActGroup last_blocks = cores[src].last_blocking_actions;

#ifdef SHOW_DEBUG
  if (blocking)
    dbg.desc = "SEND";
  else
    dbg.desc = "ISEND";
#endif

  // Create a new action to represent this transmission
  bool match_found = match_action(true, size, tag, src, dst, last_blocks,
                                  cores[src].delay_from_last, act, dbg);

  LOG_DEBUG << "Send " << src << "-(sz:" << size << ",tag:" << tag
            << ",blk:" << blocking << ")->" << dst << " will use action " << act
            << " match found:" << match_found << std::endl;

  AtomicAction &aa = pool->at(act);
  if (blocking)
    move_to_next_blocking(src, {act, NoAction}, true);
  else {
    aa.fin_type = AtomicAction::FIN_ON_SENDRECV_PROPAGATED;
    cores[src].ongoing_non_blocking.push_back(act);
  }
  if (match_found)
    pool->seal_action(act, get_last_time());
  return act;
}

actind_t Engine::enqueue_receive(commind_t comm, int src_rank, int dst_rank,
                                 share_t size, int tag, bool blocking,
                                 AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  assert(src_rank < (int)c.cores.size());
  assert(dst_rank < (int)c.cores.size());
  resind_t src = c.cores[src_rank];
  resind_t dst = c.cores[dst_rank];
  AAPool *pool = AAPool::get_instance();
  actind_t act;
  ActGroup &last_blocks = cores[dst].last_blocking_actions;
#ifdef SHOW_DEBUG
  if (blocking)
    dbg.desc = ",RECV";
  else
    dbg.desc = ",IRECV";
#endif
  bool match_found = match_action(false, size, tag, src, dst, last_blocks,
                                  cores[dst].delay_from_last, act, dbg);

  LOG_DEBUG << "Receive " << src << "-(sz:" << size << ",tag:" << tag
            << ",blk:" << blocking << ")->" << dst << " will use action " << act
            << " match found:" << match_found << std::endl;

  AtomicAction &aa = pool->at(act);
  if (blocking)
    move_to_next_blocking(dst, {act, NoAction}, true);
  else {
    aa.fin_type = AtomicAction::FIN_ON_SENDRECV_PROPAGATED;
    cores[dst].ongoing_non_blocking.push_back(act);
  }
  if (match_found)
    pool->seal_action(act, get_last_time());
  return act;
}

ActGroup Engine::enqueue_send_receive(commind_t comm, int src_rank,
                                      int common_rank, int dst_rank,
                                      share_t recv_size, share_t send_size,
                                      int tag, AtomicAction::DebugInfo &dbg) {
  actind_t recv_act =
      enqueue_receive(comm, src_rank, common_rank, recv_size, tag, false, dbg);
  actind_t send_act =
      enqueue_send(comm, common_rank, dst_rank, send_size, tag, false, dbg);
  auto &c = comm_pool.at(comm);
  resind_t common = c.cores[common_rank];
  ActGroup res{recv_act, send_act};
  if (recv_act == send_act)
    res[1] = NoAction;
  move_to_next_blocking(common, res);
  return res;
}

actind_t Engine::enqueue_waitall(commind_t comm, int rank, int n_requests,
                                 AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  assert(rank < (int)c.cores.size());
  resind_t node = c.cores[rank];

  assert(node < cores.size());
  return _do_waitall(node, cores[node].ongoing_non_blocking, n_requests,
                     "WAITALL", true, dbg);
}

bool Engine::enqueue_allreduce(commind_t comm, share_t send_size,
                               simtime_t reduce_time,
                               AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  // int comm_size = (int)c.cores.size();
  LOG_DEBUG << "Allreduce (rdb) at " << CoreId(c.cores[0])
            << " countdown: " << c.countdown << std::endl;

  if (not c.try_lock())
    return false;
  collectives.allreduce_select(c, send_size, reduce_time, dbg);
  return true;
}

bool Engine::enqueue_allgather(commind_t comm, share_t send_size,
                               AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  LOG_DEBUG << "Allgather at " << CoreId(c.cores[0])
            << " countdown: " << c.countdown << std::endl;
  if (not c.try_lock())
    return false;
  collectives.allgather_select(c, send_size, dbg);
  return true;
}

bool Engine::enqueue_alltoall(commind_t comm, share_t send_size,
                              AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  LOG_DEBUG << "Alltoall at " << CoreId(c.cores[0])
            << " countdown: " << c.countdown << std::endl;
  if (not c.try_lock())
    return false;
  collectives.alltoall_select(c, send_size, dbg);
  return true;
}

bool Engine::enqueue_barrier(commind_t comm, AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  LOG_DEBUG << "Barrier at " << CoreId(c.cores[0])
            << " countdown: " << c.countdown << std::endl;
  if (not c.try_lock())
    return false;
  collectives.barrier_select(c, dbg);
  return true;
}

bool Engine::enqueue_bcast(commind_t comm, int root_rank, share_t send_size,
                           AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  LOG_DEBUG << "Bcast at " << CoreId(c.cores[0])
            << " countdown: " << c.countdown << std::endl;
  if (not c.try_lock())
    return false;
  collectives.bcast_select(c, root_rank, send_size, dbg);
  return true;
}

bool Engine::enqueue_reduce(commind_t comm, int root_rank, share_t send_size,
                            simtime_t reduce_time,
                            AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  LOG_DEBUG << "Reduce at " << CoreId(c.cores[0])
            << " countdown: " << c.countdown << std::endl;
  if (not c.try_lock())
    return false;
  collectives.reduce_select(c, root_rank, send_size, reduce_time, dbg);
  return true;
}

bool Engine::enqueue_init(commind_t comm, AtomicAction::DebugInfo &dbg) {

  auto &c = comm_pool.at(comm);
  int comm_size = (int)c.cores.size();

  LOG_DEBUG << "Init at " << CoreId(c.cores[0]) << " countdown: " << c.countdown
            << std::endl;
  if (not c.try_lock())
    return false;
  // Get all cores to reach there
  AAPool *pool = AAPool::get_instance();
  actind_t act = NoAction;
  // actind_t prev_act=NO_ACTION;
  for (int i_rank = 0; i_rank < comm_size; i_rank++) {
    int i = c.cores[i_rank];
    if (i_rank % MaximumFaninFanout == 0) {
      // Seal previous action
      if (act != NoAction)
        pool->seal_action(act, get_last_time());
#ifdef SHOW_DEBUG
      std::stringstream ss;
      ss << "INIT_" << i;
      dbg.desc = ss.str();
#endif
      AtomicAction &aa = pool->init_action(act, dbg);
      aa.countdown_to_send = 0;
      aa.countdown_to_recv = 0;
      aa.remaining = 0;
      aa.countdown_to_fail = 1;
      aa.issuer = i;
      LOG_DEBUG << "Init at " << i << " will use action " << act << std::endl;
    }
    move_to_next_blocking(i, {act, NoAction}, true);
  }
  pool->seal_action(act, get_last_time());
  return true;
}

bool Engine::enqueue_finalize(commind_t comm, int job_id,
                              AtomicAction::DebugInfo &dbg) {
  auto &c = comm_pool.at(comm);
  int comm_size = (int)c.cores.size();

  LOG_DEBUG << "Finalize at " << CoreId(c.cores[0])
            << " countdown: " << c.countdown << std::endl;
  if (not c.try_lock())
    return false;
  AAPool *pool = AAPool::get_instance();
  actind_t act = NoAction;
  std::vector<actind_t> finalize_actions;
  bool is_two_level = (comm_size > (int)MaximumFaninFanout);
  // Get all cores to reach there
  for (int i_rank = 0; i_rank < comm_size; i_rank++) {
    int i = c.cores[i_rank];
    if (i_rank % MaximumFaninFanout == 0) {
      // Seal previous action
      if (act != NoAction)
        pool->seal_action(act, get_last_time());
#ifdef SHOW_DEBUG
      std::stringstream ss;
      ss << "FINAL_" << i;
      dbg.desc = ss.str();
#endif
      AtomicAction &aa = pool->init_action(act, dbg);
      aa.countdown_to_send = 0;
      aa.countdown_to_recv = 0;
      aa.remaining = 0;
      aa.countdown_to_fail = 1;
      aa.issuer = i;
      finalize_actions.push_back(act);
      LOG_DEBUG << "Finalize at " << i << " will use action " << act
                << std::endl;
    }
    move_to_next_blocking(i, {act, NoAction}, true);
  }
  if (not is_two_level)
    pool->set_fin_criteria(act, AtomicAction::FIN_WHOLE_JOB, job_id);

  // And seal the last action
  pool->seal_action(act, get_last_time());
  // Dow we need a second level ?
  if (is_two_level) {
    assert(finalize_actions.size() > 1);
    assert(finalize_actions.size() <= 255);
#ifdef SHOW_DEBUG
    std::stringstream ss;
    ss << "FINAL_SEC";
    dbg.desc = ss.str();
#endif
    AtomicAction &aa = pool->init_action(act, dbg);
    aa.countdown_to_send = (uint8_t)finalize_actions.size();
    aa.countdown_to_recv = 0;
    aa.remaining = 0;
    aa.countdown_to_fail = 1;
    aa.issuer = c.cores[0];
    for (auto parent_act : finalize_actions)
      pool->add_child(get_last_time(), {parent_act, NoAction}, act, SoftLatency,
                      1, CounterType::TO_SEND);

    for (int i_rank = 0; i_rank < comm_size; i_rank++) {
      int i = c.cores[i_rank];
      move_to_next_blocking(i, {act, NoAction}, false);
    }

    pool->set_fin_criteria(act, AtomicAction::FIN_WHOLE_JOB, job_id);
    pool->seal_action(act, get_last_time());
    LOG_DEBUG << "Finalize at " << CoreId(c.cores[0]) << " will use action "
              << act << std::endl;
  }

  return true;
}

void Engine::_run_pre() {
  // Make sure that each node has its blocking event if needed
  for (actind_t i = 0; i < (actind_t)cores.size(); i++)
    _update_core_event(i);
}

bool Engine::_run1_waiting(WorkingSet &set, AAPool *pool, simtime_t top_time,
                           const AtomicAction &act, actind_t index) {
  (void)act;
  // assert(top_time==act.next_time);
  pool->update_next_time(index, END_OF_TIME);
  LOG_DEBUG << "Action " << index << " starts at time "
            << simtime_to_double(top_time) << "#" << top_time << std::endl;
  assert(act.current_share == 0);
  if (not ctx.model->hold_action_resources(index, top_time)) {
    int job_id = get_core_job(act.issuer);
    this->abort_job(job_id, set);
    ctx.resource_manager->on_job_finished(job_id, top_time,
                                          JobFinishType::LINK_FAILURE);
    return true;
  }
  set.add_action(index, false);

  set.n_started_actions++;
  return false;
}

bool Engine::_run1_propagating(WorkingSet &set, AAPool * /*pool*/,
                               simtime_t top_time, const AtomicAction &act,
                               actind_t index) {
  // This should happen only for actions that do not consume anything
  // Do update simulation time, but not need to include that in
  // update_shares()
  assert(act.remaining == 0);
  // If this is blocking a node, we need to stop simulation cycle before
  // that
  if (act.first_blocking != NoResource) {
    LOG_DEBUG << "Simulation is blocked by nodes ";
    actind_t core_id = act.first_blocking;
    while (core_id != NoResource) {
      assert(core_id < cores.size());
      auto &core = cores[core_id];
      simtime_t block_time = act.next_time + core.delay_from_last;
      if (top_time + SimtimePrecision > block_time) {
        core.is_blocking_simulation = true;
        LOG_DEBUG << CoreId(core_id) << ":" << core.last_blocking_actions
                  << " (" << simtime_to_double(block_time) << "#" << block_time
                  << ")  ";
      }
      core_id = core.next(index);
    }
    LOG_DEBUG << std::endl;
    last_time = top_time - 1;
    set.first_event = last_time - SimtimePrecision;
    return true;
  }
  return false;
}

void Engine::_run1_link_event(WorkingSet &set, const EventHeapElement &top,
                              simtime_t now) {
  resind_t index = top.rindex;
  auto &res = ctx.model->at(index);
  res.push_next_event(now);
  _append_state_change(now, true, not res.is_faulty, index);
  LOG_DEBUG << "@" << simtime_to_double(now) << ", link " << index
            << (res.is_faulty ? " fails   " : " recovers") << std::endl;
  // Kill any application that has ongoing action using this resource
  assert(res.is_faulty || res.first == NoAction);
  actind_t act_index = res.first;
  uint8_t el_index = res.first_el;
  while (act_index != NoAction) {
    auto &act = ctx.pool->at_const(act_index);
    const AtomicAction::ResourceElement &el2 = act.elements[el_index];
    resind_t node_index = act.issuer;
    assert(node_index != NoResource);
    auto &node = cores[node_index];
    assert(node.is_busy());
    this->abort_job(node.job_id, set);
    ctx.resource_manager->on_job_finished(node.job_id, now,
                                          JobFinishType::LINK_FAILURE);
    act_index = el2.next;
    el_index = el2.next_el;
  }
}

void Engine::_run1_host_event(WorkingSet &set, const EventHeapElement &top,
                              simtime_t now) {
  resind_t index = top.rindex;
  auto &host = this->hosts[index];
  host.push_next_event(now);
  _append_state_change(now, false, not host.is_faulty, index);
  LOG_DEBUG << "@" << simtime_to_double(now) << ", node " << index
            << (host.is_faulty ? " fails   " : " recovers") << std::endl;
  for (resind_t region = 0; region < NumberOfRegionsPerHost; region++)
    for (resind_t core_index = 0; core_index < NumberOfCoresPerRegion;
         core_index++) {
      CoreId id(index, region, core_index);
      auto &core = cores[id.get_core_id()];
      if (core.is_busy()) {
        assert(host.is_faulty);
        this->abort_job(core.job_id, set);
        ctx.resource_manager->on_job_finished(core.job_id, now,
                                              JobFinishType::HOST_FAILURE);
      }
    }
  ctx.resource_manager->on_host_state_change(index, now, host.is_faulty);
}

void Engine::_run1_bottleneck_event(WorkingSet &set,
                                    const EventHeapElement &top,
                                    simtime_t first_event) {
  auto &btl = ctx.model->at(top.rindex);
  while (not btl.heap.empty()) {
    auto top_remain = btl.heap.top();

    // Get any action that is going to be finished withn the simulation
    // precision interval
    simtime_t end_time = btl.get_top_end_time(first_event);
    if (end_time > first_event + SimtimePrecision)
      break;

    // This is an action that is going to end now
    assert(ctx.pool->at_const(top_remain.index).current_share != 0);
    LOG_DEBUG << "Action " << top_remain.index << " ends at time "
              << simtime_to_double(end_time) << "#" << end_time << std::endl;
    assert(ctx.pool->at_const(top_remain.index).remain_handle != NoRemain);
    ctx.pool->update_next_time(top_remain.index, end_time);
    set.n_ended_actions++;
    set.add_action(top_remain.index, false);
    ctx.pool->remove_remain_handle(top_remain.index, set.first_event);
  }
}

void Engine::_run1_core_event(WorkingSet & /*set*/, const EventHeapElement &top,
                              simtime_t /*now*/) {
  // Beware not to insert new event in this function...
  auto core_id = top.aindex;
  LOG_DEBUG << "Simulation is blocked by core " << core_id << std::endl;
}

void Engine::_run1_resource_manager_event(WorkingSet &set,
                                          const EventHeapElement &top,
                                          simtime_t now) {
  if (top.type == EventHeapElement::JOB_KILL) {
    this->abort_job(top.rindex, set);
    ctx.resource_manager->on_job_finished(top.rindex, now,
                                          JobFinishType::TIMEOUT);
  } else if (top.type == EventHeapElement::JOB_COMPLETE) {
    ctx.resource_manager->on_job_finished(top.rindex, now,
                                          JobFinishType::COMPLETION);
  } else {
    ctx.resource_manager->do_schedule(now);
  }
}

void Engine::_run1(WorkingSet &set, AAPool *pool, bool &bailout,
                   int &advanced_events) {
  bailout = false;
  advanced_events = false;
  // Start building the next working set with actions which time is near
  set.clear();
  while (not bailout && pool->has_events()) {
    auto top_el = pool->get_top_event();
    simtime_t top_time = top_el.time;
    // Ensure progress by pushing the first event to next time slot
    // bad_assert(top_time+SimtimePrecision >last_time);
    if (top_time + SimtimePrecision <= last_time) {
      error_count++;
      sum_of_errors += last_time - top_time - SimtimePrecision;
      top_time = last_time - SimtimePrecision;
    }
    if (set.first_event == END_OF_TIME) {
      set.first_event = top_time;
      last_time = set.first_event + SimtimePrecision - 1;
    } else if (top_time >= set.first_event + SimtimePrecision) {
      // This action is too far in the future, leave it for later
      break;
    }

    // These cases assume that the top is not removed (they express that the
    // simulation is blocked)
    if (top_el.is_core_event()) {
      _run1_core_event(set, top_el, set.first_event);
      advanced_events++;
      bailout = true;
      break;
    }

    if (top_el.type == EventHeapElement::ACTION &&
        pool->at_const(top_el.aindex).state == AtomicAction::INITIALIZING) {
      LOG_DEBUG << "Simulation is blocked by initializing action "
                << top_el.aindex << std::endl;
      last_time = top_time - 1;
      set.first_event = last_time - SimtimePrecision;
      bailout = true;
      break;
    }

    pool->pop_top_event();

    LOG_VERBOSE << "@" << simtime_to_double(top_time) << ", popped event#"
                << top_el.handle_index << " = " << top_el << std::endl;

    if (top_el.is_link_event()) {
      _run1_link_event(set, top_el, set.first_event);
      advanced_events++;
      bailout = true;
      break;
    }
    if (top_el.is_host_event()) {
      _run1_host_event(set, top_el, set.first_event);
      advanced_events++;
      bailout = true;
      break;
    }
    if (top_el.is_bottleneck_event()) {
      _run1_bottleneck_event(set, top_el, set.first_event);
      advanced_events++;
      bailout = true;
      break;
    }
    if (top_el.is_resource_manager_event()) {
      _run1_resource_manager_event(set, top_el, set.first_event);
      advanced_events++;
      bailout = true;
      break;
    }
    if (top_el.type == EventHeapElement::SIMULATION_END) {
      last_time = top_time;
      set.first_event = last_time - SimtimePrecision;
      bailout = true;
      continue;
    }
    assert(top_el.type == EventHeapElement::ACTION);
    actind_t index = top_el.aindex;
    const auto &act = pool->at_const(index);
    switch (act.state) {
    case AtomicAction::WAITING_START:
      bailout = _run1_waiting(set, pool, top_time, act, index);
      break;
    case AtomicAction::PROPAGATING:
      bailout = _run1_propagating(set, pool, top_time, act, index);
      break;
    case AtomicAction::CONSUMING:
    case AtomicAction::INITIALIZING:
    case AtomicAction::WAITING_PRECOND:
    case AtomicAction::INVALID:
      // We should not have such actions in there
      MUST_DIE;
      break;
    }
    if (not bailout)
      advanced_events++;
  }
  set.merge_actions();
}

void Engine::_run2(WorkingSet &set, AAPool *pool) {
  actind_t first_ended = set.first_ended;
  while (first_ended != NoAction) {
    const auto &aa = pool->at_const(first_ended);
    // Check whether there are nodes blocked on this action
    actind_t core_id = aa.first_blocking;
    while (core_id != NoResource) {
      assert(core_id < cores.size());
      auto &node = cores[core_id];
      _update_core_event(core_id);
      core_id = node.next(first_ended);
    }
    first_ended = aa.next;
  }
}

void Engine::abort_job(int job_id, WorkingSet &set) {
  assert(job_id >= 0);
  assert(ctx.resource_manager->is_job_running(job_id));
  std::queue<actind_t> queue;

  // First grab all actions registered for this job
  //  (Some actions of this job are not registered in vectors)
  assert(jobs.find(job_id) != jobs.end());
  const std::vector<resind_t> &corev = jobs[job_id].cores;
  for (auto core_id : corev) {
    assert(core_id < cores.size());
    auto &core = cores[core_id];
    for (auto index : core.ongoing_non_blocking)
      queue.push(index);
    FOREACH_ACT_IN_GROUP(index, core.last_blocking_actions) {
      queue.push(index);
    }
    core.ongoing_non_blocking.clear();
    for (auto kv : core.waiting_receives) {
      queue.push(kv.second.other_action);
    }
    core.waiting_receives.clear();
    for (auto kv : core.waiting_sends) {
      queue.push(kv.second.other_action);
    }
    core.waiting_sends.clear();
  }

  std::vector<actind_t> v;
  // Then propagate from those registered actions to find all actions related
  // to the job
  while (not queue.empty()) {
    actind_t index = queue.front();
    queue.pop();
    auto &a = ctx.pool->at_const(index);
    if (a.fin_type == AtomicAction::FIN_ABORT)
      continue;

    // Visit children
    auto link_index = a.first_child;
    while (link_index != NoLink) {
      auto &link = ctx.pool->link_at(link_index);
      actind_t child_index = link.child;
      link_index = link.next_from_parent;
      if (child_index != NoAction) {
        auto &aa = ctx.pool->at_const(child_index);
        if (aa.fin_type != AtomicAction::FIN_ABORT)
          queue.push(child_index);
      }
    }

    // Visit parents
    link_index = a.first_parent;
    while (link_index != NoLink) {
      auto &link = ctx.pool->link_at(link_index);
      actind_t parent_index = link.parent;
      link_index = link.next_from_child;
      if (parent_index != NoAction) {
        auto &aa = ctx.pool->at_const(parent_index);
        if (aa.fin_type != AtomicAction::FIN_ABORT)
          queue.push(parent_index);
      }
    }
    // Change internal state to avoid re-visits
    ctx.pool->set_fin_criteria(index, AtomicAction::FIN_ABORT, job_id);
    v.push_back(index);
  }

  // Finally, actually abort the actions
  for (auto index : v) {
    auto &a = ctx.pool->at_const(index);
    // If the aborted action consumes resources, we need to update the set
    // accordingly (and let it clean up the heap structures)
    if (a.state == AtomicAction::CONSUMING) {
      ctx.pool->update_next_time(index, last_time);
      set.n_ended_actions++;
      ctx.pool->remove_remain_handle(index, set.first_event);
      set.add_action(index, false);
    } else
      ctx.pool->abort_action(index, last_time);
  }
}

simtime_t Engine::run(int &total_advancing_events) {
  AAPool *pool = AAPool::get_instance();
  total_advancing_events = 0;
  int advancing_events;
  // start_profile_region("main_runloop");
  bool bailout = false;
  int iteration = 0;
  while (not bailout && pool->has_events() &&
         not ctx.resource_manager->is_done()) {

    start_profile_region("main_runpre");
    _run_pre();
    end_profile_region("main_runpre");

    start_profile_region("main_runloop1");
    advancing_events = 0;
    _run1(working_set, pool, bailout, advancing_events);
    total_advancing_events += advancing_events;
    end_profile_region("main_runloop1");
    if (SnapshotsOccurence & 0x2) {
      std::stringstream ss;
      ss << ctx.total_stats->simulation_outer_iterations << "_" << iteration;
      pool->save_snapshot(ss.str(), working_set.first_event);
      iteration++;
    }
    INCREMENT_STAT(simulation_inner_iterations);
    update_stats(false, get_last_time());
    if (working_set.actions.empty()) {
      if (advancing_events)
        continue;
      goto done;
    }
    // Compute the new shares in the network
    start_profile_region("main_runloopshare");
    ctx.model->update_shares(working_set);
    end_profile_region("main_runloopshare");
    start_profile_region("main_runloop2");
    _run2(working_set, pool);
    end_profile_region("main_runloop2");
  } // End of the iteration loop
  // end_profile_region("main_runloop");
done:
  return last_time;
}

const ApproxMatrix &Engine::get_topology_matrix() {
  return topologies[0]->get_connectivity_matrix();
}
