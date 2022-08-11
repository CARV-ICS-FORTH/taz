// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _RESOURCE_HPP_
#define _RESOURCE_HPP_

#include "taz-simulate.hpp"

struct RemainHeapElement {
  share_t remaining;
  actind_t index;
  bool operator<(const RemainHeapElement &b) const {
    return remaining > b.remaining;
  }
};
typedef boost::heap::fibonacci_heap<RemainHeapElement> RemainHeap;
extern RemainHeap::handle_type NoRemain;

typedef uint32_t evind_t;
extern evind_t NoEvent;

struct EventHeapElement {
  simtime_t time;
  union {
    resind_t rindex;
    actind_t aindex;
  };
  // Index of the handle in pool
  evind_t handle_index;
  enum EventType {
    INVALID,
    HOST_FAILURE,
    HOST_RECOVERY,
    LINK_FAILURE,
    LINK_RECOVERY,
    ACTION,
    BOTTLENECK,
    CORE,
    JOB_SCHEDULE,
    JOB_KILL,
    JOB_COMPLETE,
    SIMULATION_END
  } type;
  EventHeapElement() = default;
  static_assert(sizeof(resind_t) == sizeof(actind_t),
                "Need to define another constructor then..");
  EventHeapElement(simtime_t time, resind_t index, evind_t handle_index,
                   EventType type)
      : time(time), rindex(index), handle_index(handle_index), type(type) {}
  bool operator<(const EventHeapElement &b) const { return time > b.time; }
  bool is_link_event() const {
    return type == LINK_FAILURE || type == LINK_RECOVERY;
  }
  bool is_host_event() const {
    return type == HOST_FAILURE || type == HOST_RECOVERY;
  }
  bool is_bottleneck_event() const { return type == BOTTLENECK; }
  bool is_core_event() const { return type == CORE; }
  bool is_resource_manager_event() const {
    return type == JOB_SCHEDULE || type == JOB_KILL || type == JOB_COMPLETE;
  }
};

std::ostream &operator<<(std::ostream &out, const EventHeapElement &x);

typedef boost::heap::fibonacci_heap<EventHeapElement> EventHeap;

struct EventHandle {
  EventHeap::handle_type raw_handle;
  int n_references;
  evind_t next;
  enum State { LIVE, PENDING_FINALIZATION, INVALID } state;
  EventHandle() : n_references(0), next(NoEvent), state(EventHandle::INVALID) {}
};

class AAPool;
class EventHandleRef {
  friend AAPool;
  evind_t ref;

public:
  EventHandleRef() : ref(NoEvent) {}
  EventHandleRef(const EventHandleRef &b);
  EventHandleRef(const EventHandle &root);
  EventHandleRef(evind_t handle_index);
  const EventHandleRef &operator=(const EventHandleRef &b);
  void free();
  ~EventHandleRef();
  bool check_liveness();
  bool is_live() const;
  EventHeap::handle_type operator*() const;
};

struct FaultyEntity {
  EventHeapElement el;

  FaultDistribution::param_type fault_params;
  double failure_rate;
  void set_fault_params(FaultDistribution::param_type params);

  EventHandleRef fault_handle;
  // A vector of times if we want to follow a deterministic profile
  // The elements are in reverse chronological order to ease up run time
  // Hence, initially, the last time is the time of the first failure, and the
  // element before last is the first recovery
  std::vector<simtime_t> profile;
  bool is_faulty;
  void push_next_event(simtime_t now);
  static double
  pick_normal_value_log10(std::mt19937 &gen, std::normal_distribution<> &dist,
                          std::normal_distribution<>::param_type &params);
  double get_cdf(double x);
};

struct Resource : public FaultyEntity {
  share_t capacity;
  share_t slack;
  // share_t total_weights;
  // share_t total_concurrency;

  actind_t first;
  uint8_t first_el;

  resind_t first_pair;

#ifdef SHOW_DEBUG
  std::string name;
#endif

  enum { FAT_PIPE, SHARED } type;

  // Handle concurrency
  uint16_t outbound_concurrency;
  actind_t first_outbound;
  actind_t last_outbound;

  // Contains the remaining of all actions that are bottlenecked by this link
  RemainHeap heap;
  share_t remain_offset;
  share_t last_core_share;
  simtime_t last_time;
  void update_offset(simtime_t now, share_t new_share) {
    // if (last_time == now)
    //   return;
    assert(new_share > 0 || heap.empty());
    if (new_share == last_core_share)
      return;
    remain_offset += _delta(now);
    assert(remain_offset < (((uint64_t)1) << 63));
    last_core_share = new_share;
    last_time = now;
  }
  // Given remaining consumption of an action, returns the remain value for the
  // heap
  share_t get_heap_remaining(share_t action_remain, simtime_t now) {
    return remain_offset + _delta(now) + action_remain;
  }
  // Given the remain value in the heap, returns the remaining consumption of
  // the action
  share_t get_action_remaining(share_t heap_remain, simtime_t now) {
    share_t progress = remain_offset + _delta(now);
    if (heap_remain <= progress)
      return 0;
    return heap_remain - progress;
  }
  simtime_t get_top_end_time(simtime_t first_event);

  // The handle of the event when the first bottlenecked-here action finishes
  EventHandleRef event_handle;

  // Internal book keeping
  resind_t index;
  bool modified;

  void __attribute__((__used__)) show_remaining_heap() {
    std::vector<RemainHeapElement> v;
    auto n_elements = heap.size();
    v.reserve(n_elements);
    for (auto &el : heap) {
      v.push_back(el);
    }
    std::sort(v.begin(), v.end(),
              [](const RemainHeapElement &a, const RemainHeapElement &b) {
                return a.remaining < b.remaining;
              });
    LOG_INFO << n_elements << " actions pending." << std::endl;
    int i = 0;
    for (auto &el : v) {
      LOG_INFO << i << ":act:" << el.index << ":rem:" << el.remaining << " ";
      if (i % 3 == 2)
        LOG_INFO << std::endl;
      if (i++ == 100)
        break;
      ;
    }
    LOG_INFO << std::endl;
  }

private:
  inline share_t _delta(time_t now) {
    return (last_core_share * (now - last_time) + SimtimePerSec / 2) /
           SimtimePerSec;
  }
};

template <> struct HeapConsumer<Resource> {
  static size_t get_heap_footprint(const Resource &x) {
    return GET_HEAP_FOOTPRINT(x.heap);
  }
};

#endif /* _RESOURCE_HPP_ */