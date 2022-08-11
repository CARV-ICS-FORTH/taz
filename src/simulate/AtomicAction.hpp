// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _ATOMIC_ACTION_HPP_
#define _ATOMIC_ACTION_HPP_

#include "GenericPool.hpp"
#include "Resource.hpp"
#include "Statistics.hpp"
#include "taz-simulate.hpp"

enum class CounterType { TO_FAIL, TO_SEND, TO_RECV };

std::ostream &operator<<(std::ostream &out, CounterType x);

struct AALink {
  // Parent and child for this link
  actind_t parent;
  actind_t child;

  // List to other links from the same parent
  linkind_t prev_from_parent;
  linkind_t next_from_parent;

  // List to other links from the same child
  linkind_t prev_from_child;
  linkind_t next_from_child;

  linkind_t next;

  simtime_t transition_delay;
  uint8_t countdown_delta;
  CounterType type;
  enum { INVALID, INITED, PENDING, PROCESSED, CANCELLED } state;

  AALink() : parent(NoAction), child(NoAction), state(INVALID) {}

  std::string get_label_string(int index) const {
    std::stringstream ss;
    if (SnapshotsType < 3) {
      ss << "idx:" << index << " delta:";
      ss << (int)countdown_delta
         << " delay:" << simtime_to_double(transition_delay);
      ss << " type:" << type;
    } else {
      ss << "+" << simtime_to_double(transition_delay);
    }
    return ss.str();
  }
};

struct SequenceIndex {
  resind_t seq;
  resind_t level;
  resind_t i[3];
  SequenceIndex() : seq(0), level(0), i{0, 0, 0} {}
  operator bool() const { return seq != 0; }
  void grow(const SequenceIndex &b) {
    level = std::max(level, b.level);
    for (int j = 0; j < 3; j++)
      i[j] = std::max(i[j], b.i[j]);
  }
};
std::ostream &operator<<(std::ostream &out, const SequenceIndex &x);

struct SourceInfo {
  int rank;
  int line;
  int epoch;
  uint64_t get_hash() {
    return ((uint64_t)rank) << 44 | ((uint64_t)line) << 22 | ((uint64_t)epoch);
  }
};
std::ostream &operator<<(std::ostream &out, const SourceInfo &x);

struct AtomicAction {
  // Last time the share has been changed (only relevant in the CONSUMING state)
  simtime_t prev_time;
  // This has different meanings depending on state:
  //* BLOCKED: Maximum time at which the action will be unblocked
  //* WAITING: Minimum time (without serialization limitations) at which the
  // action will START consuming resources
  //* CONSUMING: Time at which the action will STOP consuming according to
  // current system state
  //* PROPAGATING: Time at which the action did STOP consuming (to base
  // propagation delays upon)
  simtime_t next_time;
  share_t remaining;
  share_t weight;

  struct ResourceElement {
    resind_t resource;

    // Indices to navigate around other elements for the same resource
    actind_t prev;
    uint8_t prev_el;
    actind_t next;
    uint8_t next_el;
  };

  std::vector<ResourceElement> elements;
  bool holding_elements;

#ifdef USE_RESOURCE_PAIRS
  struct ResourcePairElement {
    resind_t hash;

    // Indices to navigate around other elements for the same resource
    actind_t prev;
    uint16_t prev_el;
    actind_t next;
    uint16_t next_el;
  };

  std::vector<ResourcePairElement> pair_elements;
#endif

  linkind_t first_child;
  linkind_t last_child;
  linkind_t first_parent;
  linkind_t last_parent;

  share_t current_share;
  // share_t max_share;

  uint8_t countdown_to_send;
  uint8_t countdown_to_recv;
  uint8_t countdown_to_fail;
  uint8_t processed_recv_children;
  uint8_t processed_send_children;

  uint8_t concurrency_cost;

  enum State {
    INVALID,
    INITIALIZING,
    WAITING_PRECOND,
    WAITING_START,
    CONSUMING,
    PROPAGATING
  } state;

  enum FinalizationType {
    NO_FIN,
    FIN_ON_ALL_PROPAGATED,
    FIN_ON_SENDRECV_PROPAGATED,
    FIN_WHOLE_JOB,
    FIN_ABORT
  } fin_type;

  // Internal book keeping
  actind_t next;
  actind_t index;

  // the first node that is blocking this action
  resind_t first_blocking;

  // the id of the job, only valid for the last finalize action (with
  // fin_type==FIN_WHOLE_JOB)
  int job_id;

  // Track the bottleneck resource
  resind_t bottleneck_resource;
  resind_t btl_hash;
  // Valid only in Consuming state. The corresponding entry is stored in the
  // heap of the bottleneck source
  RemainHeap::handle_type remain_handle;

  // Invalid in Consuming state (use resource eventhandle instead)
  EventHandleRef event_handle;
  resind_t issuer;

  // Debug fields

#ifdef SHOW_DEBUG
  struct DebugInfo {
    // first: rank, second: line
    SourceInfo source[2];
    uint8_t parent_to_send;
    uint8_t parent_to_recv;
    uint64_t dot_base;
    int dot_order;
    std::string desc;
    SequenceIndex index;
    void combine(const DebugInfo &dbg) {
      assert(source[0].rank >= 0);
      assert(source[1].rank < 0);
      assert(dbg.source[1].rank < 0);
      assert(source[0].epoch == dbg.source[0].epoch);
      assert(not index && not dbg.index);
      source[1] = dbg.source[0];
      if (dbg.desc[dbg.desc.size() - 1] == '>')
        desc = dbg.desc + desc;
      else
        desc += dbg.desc;
    }
    void clear() {
      source[0].rank = -1;
      source[1].rank = -1;
      parent_to_recv = 0;
      parent_to_send = 0;
      dot_base = 0;
      dot_order = -1;
      desc.clear();
      index.seq = 0;
    }
    DebugInfo(int rank, int line, int epoch) {
      clear();
      source[0].rank = rank;
      source[0].line = line;
      source[0].epoch = epoch;
    }
    DebugInfo() { clear(); }
  } dbg;
#else
  struct DebugInfo {
    void combine(const DebugInfo &dbg) { (void)dbg; }
    void clear() {}
    DebugInfo(int rank, int line, int epoch) {
      (void)rank;
      (void)line;
      (void)epoch;
    }
    DebugInfo() = default;
  } dbg;
#endif

  AtomicAction() : first_child(NoAction), state(INVALID) {}

  bool is_ending(simtime_t first_event) const {
    // event_handle != NoEvent &&
    return state == AtomicAction::CONSUMING &&
           next_time < first_event + SimtimePrecision;
  }

  std::string get_state_string() const {
    switch (state) {
    case AtomicAction::INVALID:
      return "INVALID";
    case AtomicAction::INITIALIZING:
      return "INITIALIZING";
    case AtomicAction::WAITING_PRECOND:
      return "WAITING_PRECOND";
    case AtomicAction::WAITING_START:
      return "WAITING_START";
    case AtomicAction::CONSUMING:
      return "CONSUMING";
    case AtomicAction::PROPAGATING:
      return "PROPAGATING";
    }
    return "";
  }

  std::string get_color_string() const {
#ifdef SHOW_DEBUG
    if (dbg.dot_order == -3) {
      return "purple";
    }
    if (dbg.dot_order == -4) {
      return "rose";
    }
#endif
    switch (state) {
    case AtomicAction::INVALID:
      return "gray";
    case AtomicAction::INITIALIZING:
      return "gold";
    case AtomicAction::WAITING_PRECOND:
      return "red";
    case AtomicAction::WAITING_START:
      return "orange";
    case AtomicAction::CONSUMING:
      return "forestgreen";
    case AtomicAction::PROPAGATING:
      return "turquoise";
    }
    return "";
  }

  std::string get_label_string(int pool_index) const {
    // Build the label property
    if (SnapshotsType >= 3) {
#ifdef SHOW_DEBUG
      return dbg.desc;
#endif
    }

    std::stringstream ss;
    ss << " idx:" << pool_index << " issuer:";
    if (issuer == NoResource)
      ss << "NO_RES";
    else
      ss << issuer;
#ifdef SHOW_DEBUG
    ss << " ord:" << dbg.dot_order;
    ss << " desc:" << dbg.desc;
    if (dbg.index)
      ss << " index:" << dbg.index;
#endif
    ss << " state:" << get_state_string();
    if (state == AtomicAction::WAITING_PRECOND) {
      ss << " counts:s" << (int)countdown_to_send << "/r"
         << (int)countdown_to_recv << "/f" << (int)countdown_to_fail;
    }
    ss << " @" << simtime_to_double(next_time);
    return ss.str();
  }
};

template <> struct HeapConsumer<AtomicAction> {
  static size_t get_heap_footprint(const AtomicAction &x) {
    return GET_HEAP_FOOTPRINT(x.elements);
  }
};

class Model;
class WorkingSet;

class AAPool : public MemoryConsumer {
public:
private:
  friend class WorkingSet;
  friend class Model;
  friend class EventHandleRef;

  static AAPool *instance;

  GenericPool<EventHandle, actind_t, NoAction, 12> event_handle_pool;
  GenericPool<AtomicAction, actind_t, NoAction, 11> action_pool;
  GenericPool<AALink, linkind_t, NoAction, 12> link_pool;

  std::ofstream trace_file;

  // This is a heap for all actions that are either in WAITING state
  // unblocked and just waiting for initial delay, or actions that are in
  // CONSUMING state
  EventHeap event_heap;

  simtime_t final_time;

  AAPool()
      : MemoryConsumer("AAPool"), event_handle_pool("AAPool::EventHandlePool"),
        action_pool("AAPool::ActionPool"), link_pool("AAPool::LinkPool"),
        final_time(0) {}

  AtomicAction &_action_at(actind_t index) {
    AtomicAction &aa = action_pool.at(index);
    assert(aa.state != AtomicAction::INVALID);
    return aa;
  }

  // Returns whether link was processed
  bool propagate(simtime_t now, simtime_t propagation_time, AALink &link,
                 linkind_t link_index, bool local_only) {
    (void)link_index;
    INCREMENT_STAT(links_propagated)
    assert(link.state != AALink::INVALID);
    assert(link.state != AALink::CANCELLED);
    if (link.state == AALink::PROCESSED)
      return true;

    actind_t child_index = link.child;
    AtomicAction &aa = _action_at(child_index);
    assert(aa.state != AtomicAction::INVALID);

    aa.next_time =
        std::max(aa.next_time, propagation_time + link.transition_delay);
    // bad_assert(aa.next_time>=now);

    if (aa.state == AtomicAction::INITIALIZING) {
      link.state = AALink::PENDING;
      // Place an event to avoid passing this action by mistake
      if (aa.event_handle.check_liveness())
        update_event(aa.event_handle, aa.next_time, child_index,
                     EventHeapElement::ACTION);
      else {
        evind_t handle_index = NoEvent;
        _create_event(handle_index, aa.next_time, child_index,
                      EventHeapElement::ACTION);
        aa.event_handle = EventHandleRef(handle_index);
      }
      return false;
    }

#ifdef REUSE_POOL_ELEMENTS
    detach_link(link, link_index);
#else
    link.state = AALink::PROCESSED;
#endif

    if (link.type == CounterType::TO_FAIL) {
      assert(aa.countdown_to_recv >= link.countdown_delta);
      if (aa.countdown_to_fail == link.countdown_delta)
        aa.countdown_to_fail = 0;
      else
        aa.countdown_to_fail -= link.countdown_delta;
      return true;
    }

    // Those might not hold on non-deterministic cases
    assert(not aa.event_handle.check_liveness());
    assert(aa.state == AtomicAction::WAITING_PRECOND);

    if (link.type == CounterType::TO_SEND) {
      assert(aa.countdown_to_send >= link.countdown_delta);
      if (aa.countdown_to_send == link.countdown_delta) {
        aa.countdown_to_send = 0;
        if (not aa.countdown_to_recv)
          goto unblock;
      } else
        aa.countdown_to_send -= link.countdown_delta;
    }

    if (link.type == CounterType::TO_RECV) {
      assert(aa.countdown_to_recv >= link.countdown_delta);
      if (aa.countdown_to_recv == link.countdown_delta) {
        aa.countdown_to_recv = 0;
        if (not aa.countdown_to_send)
          goto unblock;
      } else
        aa.countdown_to_recv -= link.countdown_delta;
    }

    LOG_DEBUG << "Child " << child_index
              << " is still blocked (to_send:" << (int)aa.countdown_to_send;
    LOG_DEBUG << " to_recv:" << (int)aa.countdown_to_recv;
    LOG_DEBUG << " to_fail:" << (int)aa.countdown_to_fail << ")" << std::endl;
    return true;

  unblock:
    LOG_DEBUG << "Child " << child_index << " will start at "
              << simtime_to_double(aa.next_time) << std::endl;
    if (not local_only)
      advance_state(aa, child_index, now, aa.next_time, false);
    return true;
  }

  bool advance_state(AtomicAction &aa, actind_t index, simtime_t now,
                     simtime_t propagation_time, bool local_only);

public:
  ~AAPool() {
    while (has_events()) {
      pop_top_event();
    }
  }

  void detach_link(AALink &link, resind_t link_index) {

    if (link.parent != NoAction && link.state != AALink::PROCESSED) {
      if (link.type == CounterType::TO_SEND)
        _action_at(link.parent).processed_send_children++;
      else {
        assert(link.type == CounterType::TO_RECV);
        _action_at(link.parent).processed_recv_children++;
      }
    }
    link.state = AALink::PROCESSED;

    if (SnapshotsType >= 2)
      return;

    // Make this link invalid and remove from lists to enable reuse
    if (link.prev_from_child != NoAction)
      link_at(link.prev_from_child).next_from_child = link.next_from_child;
    else if (link.child != NoAction)
      _action_at(link.child).first_parent = link.next_from_child;
    if (link.next_from_child != NoAction)
      link_at(link.next_from_child).prev_from_child = link.prev_from_child;
    else if (link.child != NoAction)
      _action_at(link.child).last_parent = link.prev_from_child;

    if (link.prev_from_parent != NoAction)
      link_at(link.prev_from_parent).next_from_parent = link.next_from_parent;
    else if (link.parent != NoAction)
      _action_at(link.parent).first_child = link.next_from_parent;
    if (link.next_from_parent != NoAction)
      link_at(link.next_from_parent).prev_from_parent = link.prev_from_parent;
    else if (link.parent != NoAction)
      _action_at(link.parent).last_child = link.prev_from_parent;

    link.parent = NoAction;
    link.child = NoAction;
    link_pool.free_element(link_index);
  }

  static void delete_instance() {
    if (instance)
      delete instance;
  }

  static AAPool *get_instance() {
    if (!instance)
      instance = new AAPool();
    return instance;
  }

  simtime_t get_final_time() { return final_time; }

  AtomicAction &init_action(actind_t &index,
                            const AtomicAction::DebugInfo &dbg) {
    INCREMENT_STAT(actions_inited);
    AtomicAction &aa = action_pool.new_element(index);
    aa.state = AtomicAction::INITIALIZING;
    aa.prev_time = 0;
    aa.next_time = 0;
    aa.remaining = 0;
    aa.weight = 1;
    aa.fin_type = AtomicAction::NO_FIN;
    aa.elements.clear();
    aa.holding_elements = false;
#ifdef USE_RESOURCE_PAIRS
    aa.pair_elements.clear();
#endif
    aa.first_child = NoLink;
    aa.last_child = NoLink;
    aa.first_parent = NoLink;
    aa.last_parent = NoLink;
    aa.index = NoAction;
    aa.first_blocking = NoResource;
    aa.dbg = dbg;
    aa.countdown_to_send = 0;
    aa.countdown_to_recv = 0;
    aa.processed_recv_children = 0;
    aa.processed_send_children = 0;
    aa.current_share = 0;
    aa.countdown_to_fail = 1;
    aa.concurrency_cost = 1;
    aa.bottleneck_resource = NoLink;
    aa.btl_hash = 0;
    aa.remain_handle = NoRemain;
    aa.next = NoAction;
    return aa;
  }

  void seal_action(actind_t index, simtime_t time);

  ActGroupLinks add_child(simtime_t now, ActGroup actind_group,
                          actind_t child_index, simtime_t delay, uint8_t count,
                          CounterType type, bool fin_on_propagate = false,
                          bool increase_countdown = false) {

    auto &child_act = _action_at(child_index);
    assert(child_act.state == AtomicAction::INITIALIZING ||
           not increase_countdown);
    if (delay < SoftLatency)
      delay = SoftLatency;

    LOG_DEBUG << "Add parent (" << actind_group << ") -> child (" << child_index
              << ") relation (delay:";
    LOG_DEBUG << simtime_to_double(delay) << " type:" << type << ")"
              << std::endl;

    ActGroupLinks links{NoLink, NoLink};

    FOREACH_ACT_IN_GROUP(index, actind_group) {
      INCREMENT_STAT(links_inited)
      auto &act = _action_at(index);
#ifdef SHOW_DEBUG
      assert(child_act.dbg.source[0].rank >= 0);
      assert(act.dbg.source[0].rank >= 0);
      assert(child_act.dbg.source[0].epoch >= act.dbg.source[0].epoch);
#endif
      bool parent_ready = (act.state == AtomicAction::PROPAGATING);
      linkind_t link_index;
      AALink &link = link_pool.new_element(link_index);
      links[i] = link_index;
      link.parent = index;
      link.child = child_index;
      // Connect to the parent
      link.prev_from_parent = act.last_child;
      if (act.first_child == NoLink)
        act.first_child = link_index;
      else
        link_at(act.last_child).next_from_parent = link_index;
      act.last_child = link_index;
      link.next_from_parent = NoLink;
      // Connect to the child
      link.prev_from_child = child_act.last_parent;
      if (child_act.first_parent == NoLink)
        child_act.first_parent = link_index;
      else
        link_at(child_act.last_parent).next_from_child = link_index;
      child_act.last_parent = link_index;
      link.next_from_child = NoLink;

      link.transition_delay = delay;
      link.countdown_delta = count;
      link.type = type;

      if (fin_on_propagate)
        act.fin_type = AtomicAction::FIN_ON_ALL_PROPAGATED;

#ifdef SHOW_DEBUG
      switch (type) {
      case CounterType::TO_RECV:
        child_act.dbg.parent_to_recv++;
        break;
      case CounterType::TO_SEND:
        child_act.dbg.parent_to_send++;
        break;
      default:
        break;
      }
#endif

      switch (type) {
      case CounterType::TO_RECV:
        if (increase_countdown)
          child_act.countdown_to_recv++;
        break;
      case CounterType::TO_SEND:
        if (increase_countdown)
          child_act.countdown_to_send++;
        break;
      case CounterType::TO_FAIL:
        break;
      }
      link.state = AALink::INITED;
      if (parent_ready)
        this->propagate(now, act.next_time, link, link_index, false);
    }

    return links;
  }

  bool update_action_consumption(actind_t index, share_t new_share,
                                 resind_t new_bottleneck, simtime_t time);

  // Return the action was actually finalized
  bool finalize_action(actind_t index, simtime_t now) {
    auto &aa = _action_at(index);
    aa.remaining = 0;
    advance_state(aa, index, now, aa.next_time, false);
    return aa.state == AtomicAction::INVALID;
  }

  void abort_action(actind_t index, simtime_t now);

  void update_next_time(actind_t index, simtime_t top_time) {
    _action_at(index).next_time = top_time;
  }

  void set_fin_criteria(actind_t index, AtomicAction::FinalizationType criteria,
                        int job_id = -1) {
    assert(criteria == AtomicAction::FIN_ON_ALL_PROPAGATED ||
           criteria == AtomicAction::FIN_WHOLE_JOB ||
           criteria == AtomicAction::FIN_ABORT);
    _action_at(index).fin_type = criteria;
    _action_at(index).job_id = job_id;
  }

private:
  EventHandle &_create_event(evind_t &handle_index, simtime_t time,
                             actind_t index, EventHeapElement::EventType type) {
    auto &handle = event_handle_pool.new_element(handle_index);
    handle.raw_handle = event_heap.emplace(time, index, handle_index, type);
    handle.state = EventHandle::LIVE;
    handle.n_references = 0;
    handle.next = NoEvent;
    LOG_VERBOSE << "Creating event#" << handle_index << " to "
                << EventHeapElement({time, index, handle_index, type})
                << std::endl;
    return handle;
  }

  // Interface to EventHandleRef
  bool _is_event_live(evind_t ref) {
    assert(ref != NoEvent);
    return event_handle_pool.at(ref).state == EventHandle::LIVE;
  }

  void _reference_event(evind_t ref) {
    assert(_is_event_live(ref));
    auto &handle = event_handle_pool.at(ref);
    handle.n_references++;
  }

  void _unreference_event(evind_t ref) {
    assert(ref != NoEvent);
    auto &handle = event_handle_pool.at(ref);
    assert(handle.n_references > 0);
    if (--handle.n_references == 0) {
      if (handle.state == EventHandle::LIVE)
        event_heap.erase(handle.raw_handle);
      LOG_VERBOSE << "Actually freeing event#" << ref << std::endl;
      event_handle_pool.free_element(ref);
    }
  }

public:
  // Wrap calls to event_heap to help with debug
  EventHandleRef push_event(simtime_t time, resind_t index,
                            EventHeapElement::EventType type) {
    assert(index != NoResource);
    evind_t handle_index = NoEvent;
    _create_event(handle_index, time, index, type);
    return EventHandleRef(handle_index);
  }

  EventHandleRef push_event(const EventHeapElement &el) {
    return push_event(el.time, el.rindex, el.type);
  }

  void update_event(const EventHandleRef &handle, simtime_t time,
                    resind_t index, EventHeapElement::EventType type) {
    assert(index != NoAction);
    LOG_VERBOSE << "Updating event#" << handle.ref << " to "
                << EventHeapElement({time, index, handle.ref, type})
                << std::endl;
    event_heap.update(*handle, {time, index, handle.ref, type});
  }

  bool has_events() { return not event_heap.empty(); }
  // We need to be careful here: if a new event has been inserted, the top
  // might have changed (if 2 events have the same date) Thus, it is important
  // to pop soon after getting the top reference.
  EventHeapElement get_top_event() {
    assert(has_events());
    return event_heap.top();
  }
  void pop_top_event() {
    assert(has_events());
    event_handle_pool.at(event_heap.top().handle_index).state =
        EventHandle::PENDING_FINALIZATION;
    event_heap.pop();
  }

  void set_event_handle(actind_t index, const EventHandleRef &event_handle) {
    auto &handle = _action_at(index).event_handle;
    handle.free();
    handle = event_handle;
  }

  void __attribute__((__used__)) show_events() {
    std::vector<EventHeapElement> events_vector;
    auto n_events = event_heap.size();
    events_vector.reserve(n_events);
    for (auto &el : event_heap) {
      events_vector.push_back(el);
    }
    std::sort(events_vector.begin(), events_vector.end(),
              [](const EventHeapElement &a, const EventHeapElement &b) {
                return a.time < b.time;
              });
    LOG_INFO << n_events << " events pending." << std::endl;
    int i = 0;
    for (auto &el : events_vector) {
      LOG_INFO << i << ':' << el << " ";
      if (i % 3 == 2)
        LOG_INFO << std::endl;
      if (i++ == 100)
        break;
      ;
    }
  }

  void check_action_liveness(std::vector<actind_t> &actions) {
    for (auto &x : actions) {
      if (action_pool.at_bare(x).state == AtomicAction::INVALID)
        x = NoAction;
    }
  }

  void set_first_blocking(actind_t act, resind_t core_id, simtime_t now) {
    auto &a = _action_at(act);
    a.first_blocking = core_id;
    if (core_id == NoResource) {
      advance_state(a, act, now, a.next_time, false);
    }
  }

private:
  void _remove_action_from_bottleneck(AtomicAction &a, actind_t index,
                                      Resource &btl, resind_t btl_index,
                                      simtime_t time, bool &pop_top) {
    pop_top = (index == btl.heap.top().index);
    if (not pop_top) {
      btl.heap.erase(a.remain_handle);
    } else {
      btl.heap.pop();
      btl.modified = true;
      btl.event_handle.free();
      // The bottleneck resource can push another action to the event heap
      // instead Could as well push multiple actions if multiple finish at the
      // same time
      if (not btl.heap.empty()) {
        simtime_t end_time = btl.get_top_end_time(time);
        btl.event_handle =
            this->push_event(end_time, btl_index, EventHeapElement::BOTTLENECK);
      }
    }
  }

public:
  void remove_remain_handle(actind_t index, simtime_t time);

  /*
                  AtomicAction* get_first_active() {
                                  if(first_active==NO_ACTION)
                                                  return nullptr;
                                  return &(action_pool[first_active]);
                  }
  */
  // Only for initialization
  AtomicAction &at(actind_t index) {
    AtomicAction &aa = _action_at(index);
    assert(aa.state == AtomicAction::INITIALIZING);
    return aa;
  }

  const AtomicAction &at_const(actind_t index) {
    AtomicAction &aa = _action_at(index);
    return aa;
  }

  AALink &link_at(linkind_t index) {
    AALink &link = link_pool.at(index);
    assert(link.state != AALink::INVALID);
    return link;
  }

  int action_count() const { return action_pool.get_count(); }

  int link_count() const { return link_pool.get_count(); }

private:
  bool _include_in_snapshot(AtomicAction &a) {
    // if (a.state == AtomicAction::PROPAGATING && a.first_child ==
    // NoResource)
    //   return false;
    if (a.state != AtomicAction::INVALID)
      return true;
#ifdef REUSE_POOL_ELEMENTS
    return false;
#else
#ifdef SHOW_DEBUG
    if (SnapshotsType >= 2)
      return not a.dbg.desc.empty();
#endif
    bool skip = true;
    linkind_t link_index = a.first_child;
    while (link_index != NoLink) {
      auto &link = link_at(link_index);
      auto &aa = action_pool.at_bare(link.child);
      if (aa.state != AtomicAction::INVALID)
        skip = false;
      link_index = link.next_from_parent;
    }
    return not skip;
#endif
  }
#ifdef SHOW_DEBUG
  // A map to detect loops. value is the next action being checked last
  typedef std::unordered_map<actind_t, actind_t> VisitMap;
  struct SequenceInfo {
    std::vector<SequenceIndex> indices;
    uint64_t end_dot_base;
    int start_dot_order;
    int depth_dot_order;
  };
  typedef std::map<uint64_t, SequenceInfo> SequenceMap;
  // return how many loops we found
  int _propagate_order(actind_t index, int order, uint64_t base,
                       VisitMap &visits, SequenceMap &seqs);
  int _propagate_seq(SequenceInfo &s, SequenceMap &seqs);
  actind_t _compute_order();
#endif
public:
  void save_snapshot(std::string suffix, simtime_t now);

  size_t get_action_count() {
    return action_pool.get_count();
  }

  size_t get_link_count() {
    return link_pool.get_count();
  }

  virtual size_t get_footprint() {
    return sizeof(AAPool) + GET_HEAP_FOOTPRINT(event_heap);
  }
};

#endif //_ATOMIC_ACTION_HPP_
