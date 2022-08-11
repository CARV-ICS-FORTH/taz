// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "Resource.hpp"
#include "AtomicAction.hpp"
#include "Statistics.hpp"

RemainHeap::handle_type NoRemain;
extern std::uniform_real_distribution<>::param_type NodeParamsRecovery;
extern std::uniform_real_distribution<>::param_type LinkParamsRecovery;

evind_t NoEvent = std::numeric_limits<evind_t>::max();

simtime_t Resource::get_top_end_time(simtime_t first_event) {
  assert(not heap.empty());
  share_t remaining = heap.top().remaining - remain_offset;
  assert(last_core_share > 0);
  simtime_t endtime =
      last_time +
      (remaining * SimtimePerSec + last_core_share / 2) / last_core_share;
  if (endtime < first_event) {
    INCREMENT_STAT(end_sooner_error)
    return first_event;
  }
  return endtime;
}

std::ostream &operator<<(std::ostream &out, const EventHeapElement &x) {
  out << "(@" << simtime_to_double(x.time) << "[s] " << x.rindex << "=";
  switch (x.type) {
  case EventHeapElement::INVALID:
    out << "INVALID";
    break;
  case EventHeapElement::HOST_FAILURE:
    out << "HOST_FAILURE";
    break;
  case EventHeapElement::HOST_RECOVERY:
    out << "HOST_RECOVERY";
    break;
  case EventHeapElement::LINK_FAILURE:
    out << "LINK_FAILURE";
    break;
  case EventHeapElement::LINK_RECOVERY:
    out << "LINK_RECOVERY";
    break;
  case EventHeapElement::ACTION:
    out << "ACTION";
    break;
  case EventHeapElement::BOTTLENECK:
    out << "BOTTLENECK";
    break;
  case EventHeapElement::CORE:
    out << "CORE";
    break;
  case EventHeapElement::JOB_SCHEDULE:
    out << "JOB_SCHEDULE";
    break;
  case EventHeapElement::JOB_KILL:
    out << "JOB_KILL";
    break;
  case EventHeapElement::JOB_COMPLETE:
    out << "JOB_COMPLETE";
    break;
  case EventHeapElement::SIMULATION_END:
    out << "SIMULATION_END";
    break;
  }
  out << " )";
  return out;
}

EventHandleRef::EventHandleRef(const EventHandleRef &b) : ref(b.ref) {
  if (ref != NoEvent) {
    ctx.pool->_reference_event(b.ref);
    LOG_VERBOSE << "Add reference " << this << " for event#" << ref
                << " from copy constructor" << std::endl;
  }
}

EventHandleRef::EventHandleRef(evind_t handle_index) : ref(handle_index) {
  assert(ref != NoEvent);
  ctx.pool->_reference_event(handle_index);
  LOG_VERBOSE << "Add reference " << this << " for event#" << ref
              << " from int constructor" << std::endl;
}

const EventHandleRef &EventHandleRef::operator=(const EventHandleRef &b) {
  assert(ref == NoEvent);
  assert(b.ref != NoEvent);
  ref = b.ref;
  ctx.pool->_reference_event(ref);
  LOG_VERBOSE << "Add reference " << this << " for event#" << ref
              << " from operator=" << std::endl;
  return *this;
}

EventHandleRef::~EventHandleRef() {
  if (ref == NoEvent)
    return;
  ctx.pool->_unreference_event(ref);
  LOG_VERBOSE << "Remove reference " << this << " for event#" << ref
              << " from destructor" << std::endl;
  ref = NoEvent;
}

void EventHandleRef::free() {
  if (ref == NoEvent)
    return;
  ctx.pool->_unreference_event(ref);
  LOG_VERBOSE << "Remove reference " << this << " for event#" << ref
              << " from free()" << std::endl;
  ref = NoEvent;
}

bool EventHandleRef::check_liveness() {
  if (ref == NoEvent)
    return false;
  if (ctx.pool->_is_event_live(ref))
    return true;
  // state=PENDING_FINALIZATION...
  ctx.pool->_unreference_event(ref);
  ref = NoEvent;
  LOG_VERBOSE << "Remove reference " << this << " for event#" << ref
              << " from check_liveness()" << std::endl;
  return false;
}

bool EventHandleRef::is_live() const {
  if (ref == NoEvent)
    return false;
  return ctx.pool->_is_event_live(ref);
}

EventHeap::handle_type EventHandleRef::operator*() const {
  assert(ctx.pool->_is_event_live(ref));
  return ctx.pool->event_handle_pool.at(ref).raw_handle;
}

void FaultyEntity::push_next_event(simtime_t now) {
  switch (el.type) {
  case EventHeapElement::HOST_FAILURE:
    is_faulty = true;
    el.type = EventHeapElement::HOST_RECOVERY;
    break;
  case EventHeapElement::LINK_FAILURE:
    is_faulty = true;
    el.type = EventHeapElement::LINK_RECOVERY;
    break;
  case EventHeapElement::HOST_RECOVERY:
    is_faulty = false;
    el.type = EventHeapElement::HOST_FAILURE;
    break;
  case EventHeapElement::LINK_RECOVERY:
    is_faulty = false;
    el.type = EventHeapElement::LINK_FAILURE;
    break;
  default:
    MUST_DIE;
  }

  if (not profile.empty()) {
    // There are still profile elements to consume
    el.time = profile.back();
    assert(now <= el.time);
    profile.pop_back();
  } else if (failure_rate > 0) {
    // Pick a delta from distribution
    double delta = -1;
    std::uniform_real_distribution<> distOn;
    FaultDistribution distOff;
    switch (el.type) {
    case EventHeapElement::HOST_FAILURE:
      delta = distOff(mt19937_gen_fault_nodes, fault_params);
      break;
    case EventHeapElement::LINK_FAILURE:
      delta = distOff(mt19937_gen_fault_links, fault_params);
      break;
    case EventHeapElement::HOST_RECOVERY:
      delta = distOn(mt19937_gen_fault_nodes, NodeParamsRecovery);
      break;
    case EventHeapElement::LINK_RECOVERY:
      delta = distOn(mt19937_gen_fault_links, LinkParamsRecovery);
      break;
    default:
      MUST_DIE;
    }

    // 2^64=1.8e19
    // 1 century ~= 100*365*24*3600 ~= 3e9 seconds
    //  If SIMTIME_PER_SEC is 1e9 it means simulation will work for more than 3
    //  centturies ... OK?
    // This is safe to disable this event
    if (delta >= 1e19 / (double)SimtimePerSec)
      el.time = END_OF_TIME;
    else
      el.time = now + double_to_simtime(delta);
  } else {
    el.time = END_OF_TIME;
  }

  if (el.time == END_OF_TIME) {
    fault_handle.free();
    return;
  }
  LOG_VERBOSE << "Push fault-related event: " << el << std::endl;

  if (fault_handle.check_liveness())
    ctx.pool->update_event(fault_handle, el.time, el.rindex, el.type);
  else
    fault_handle = ctx.pool->push_event(el);
}

double FaultyEntity::pick_normal_value_log10(
    std::mt19937 &gen, std::normal_distribution<> &dist,
    std::normal_distribution<>::param_type &params_log10) {
  double x = std::pow(10.0, dist(gen, params_log10));
  // Clamp that
  double clamping_factor = ctx.vm["mtbf_clamping_factor"].as<double>();
  double maxX = std::pow(10.0, params_log10.mean() +
                                   clamping_factor * params_log10.stddev());
  double minX = std::pow(10.0, params_log10.mean() -
                                   clamping_factor * params_log10.stddev());
  assert(minX > 0);
  if (x > maxX)
    return maxX;
  if (x < minX)
    return minX;
  return x;
}

void FaultyEntity::set_fault_params(FaultDistribution::param_type params) {
  fault_params = params;
  // Compute failure rate as the mean of the distribution
  // Mean of a weibull function is scale*gamma(1+1/k)
  assert(params.a() > 0.0);
  assert(params.b() > 0.0);
  failure_rate = params.b() * std::tgamma((params.a() + 1.0) / params.a());
}

double FaultyEntity::get_cdf(double x) {
  // params.a = shape
  // params.b = scale
  double res = -std::expm1(-std::pow(x / fault_params.b(), fault_params.a()));
  assert(res >= 0 && res < 1);
  return res;
}
