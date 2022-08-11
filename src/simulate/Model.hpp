// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _MODEL_HPP_
#define _MODEL_HPP_

#include "AtomicAction.hpp"
#include "taz-simulate.hpp"

/**
 * @brief
 *
 * For each resource:
 * - a list of ongoing actions (completely ordered)
 * - a list of blocked/waiting actions (partially ordered )
 *
 * For each node, store index of the "apex" blocking action, where new actions
 * can start from
 *
 * Associate each resource to one (or more) sensitivity groups.
 * Each sensitivity group has a list of cores that can use it.
 *
 * Add debug strings to actions to help understanding
 * Add parent/predecessor list to actions (will probably be needed to keep track
 * of action times efficiently)
 *
 * Have both a time estimate and a firm bound ?
 *
 * How to have cores progress before matching is complete -> need richer
 * matching structure (start action+time,end action+time ...)
 *
 * Try to keep time estimate as lazy as possible:
 * -> We just need to know until when sharings in the group(s) are true
 *
 *
 *
 *
 *
 *
 */
/// @brief The Model is only concerned with the sharing of resources, and
/// updating the end time of consuming actions. This thing should be as
/// high-performance and simple as possible

struct HeapElement {
  share_t share;
  resind_t index;
  bool operator<(const HeapElement &b) const { return share > b.share; }
};

typedef boost::heap::fibonacci_heap<HeapElement> Heap;

struct LightResource {
  Heap::handle_type handle;
  Resource *ptr;
  share_t weights;
  share_t core;
  share_t share;
  resind_t resource_index;
  bool need_update;
  int next_resource_to_update;

  LightResource(Resource *ptr, resind_t resource_index, share_t core)
      : ptr(ptr), weights(0), core(core), share(0),
        resource_index(resource_index), need_update(false) {}
};

extern resind_t _hash(resind_t x);

class WorkingSet {
  template <typename T> friend class HeapConsumer;
  std::vector<actind_t> starting_actions;

public:
  std::vector<actind_t> actions;
  std::vector<LightResource> resources;
  Heap resources_heap;
  static Heap::handle_type NoHandle;
  actind_t first_ended;
  actind_t last_ended;
  int n_started_actions;
  int n_ended_actions;
  // First event that needs to be updated
  simtime_t first_event;

  void clear() {
    starting_actions.clear();
    actions.clear();
    resources.clear();
    resources_heap.clear();
    first_ended = NoAction;
    last_ended = NoAction;
    n_started_actions = 0;
    n_ended_actions = 0;
    first_event = END_OF_TIME;
  }

  void add_action(actind_t index, bool on_propagation) {
    AAPool *pool = AAPool::get_instance();
    auto &act = pool->_action_at(index);
    assert(not act.elements.empty());
    if (on_propagation) {
      act.index = (actind_t)actions.size();
      actions.push_back(index);
      return;
    }
    switch (act.state) {
    case AtomicAction::WAITING_START:
      starting_actions.push_back(index);
      break;
    case AtomicAction::CONSUMING:
      // This is an action that is going to end now
      act.remaining = 0;
      act.index = (actind_t)actions.size();
      actions.push_back(index);
      if (first_ended == NoAction) {
        last_ended = first_ended = index;
      } else {
        auto &last_act = pool->_action_at(last_ended);
        last_act.next = index;
        last_ended = index;
      }
      break;
    case AtomicAction::PROPAGATING:
    case AtomicAction::WAITING_PRECOND:
    case AtomicAction::INITIALIZING:
    case AtomicAction::INVALID:
      // We should not have such actions in there
      MUST_DIE;
      break;
    }
    act.event_handle.free();
  }

  void merge_actions() {
    actind_t index = (actind_t)actions.size();
    ctx.pool->check_action_liveness(actions);
    ctx.pool->check_action_liveness(starting_actions);
    for (auto i : starting_actions) {
      if (i == NoAction)
        continue;
      ctx.pool->_action_at(i).index = index;
      index++;
    }
    actions.insert(actions.end(), starting_actions.begin(),
                   starting_actions.end());
  }

  void remove_action(actind_t index) {
    AAPool *pool = AAPool::get_instance();
    auto &act = pool->_action_at(index);
    act.index = NoAction;
  }

  WorkingSet() { clear(); }
};

class Model : public MemoryConsumer {
public:
private:
  friend AAPool;
  std::vector<Resource> resources;

#ifdef USE_RESOURCE_PAIRS
  struct PairElement {
    // Have to maintain res_a < res_b
    resind_t res_a, res_b;

    // First action that uses this pair
    actind_t first;
    uint16_t first_el;

    // Indices of the other pairs that correspond to resource a or b
    resind_t prev_a;
    resind_t next_a;

    resind_t prev_b;
    resind_t next_b;

    share_t weight;
  };

  std::unordered_map<resind_t, PairElement> resource_pairs;
#endif
public:
  Model() : MemoryConsumer("Model") {}

  std::vector<Resource> &get_resources() { return resources; }

  Resource &at(resind_t index) {
    assert(index < resources.size());
    return resources[index];
  }

  void init_action_resources(actind_t act_index,
                             const std::vector<resind_t> &resource_vector) {
    AAPool *pool = AAPool::get_instance();
    auto &act = pool->at(act_index);
    act.elements.resize(resource_vector.size());
    actind_t el_index = 0;
    for (auto &res_index : resource_vector) {
      auto &el = act.elements[el_index];
      el.resource = res_index;
      el_index++;
    }
  }

  // Return if holding resources was successful (i.e. there was no faulty
  // resource)
  bool hold_action_resources(actind_t act_index, simtime_t time);

  bool release_action_resources(actind_t act_index) {
    AAPool *pool = AAPool::get_instance();
    auto &act = pool->_action_at(act_index);
    if (not act.holding_elements)
      return false;
    for (auto &el : act.elements) {
      if (el.next != NoAction) {
        auto &next_el = pool->_action_at(el.next).elements[el.next_el];
        next_el.prev = el.prev;
        next_el.prev_el = el.prev_el;
      }
      if (el.prev == NoAction) {
        auto &res = at(el.resource);
        res.first = el.next;
        res.first_el = el.next_el;
      } else {
        auto &prev_el = pool->_action_at(el.prev).elements[el.prev_el];
        prev_el.next = el.next;
        prev_el.next_el = el.next_el;
      }
    }
    act.elements.clear();
    act.holding_elements = false;

#ifdef USE_RESOURCE_PAIRS
    for (auto &el : act.pair_elements) {
      if (el.next != NO_ACTION) {
        auto &next_el = pool->_action_at(el.next).pair_elements[el.next_el];
        next_el.prev = el.prev;
        next_el.prev_el = el.prev_el;
      }
      auto &top = resource_pairs.at(el.hash);
      if (el.prev == NO_ACTION) {
        top.first = el.next;
        top.first_el = el.next_el;
      } else {
        auto &prev_el = pool->_action_at(el.prev).pair_elements[el.prev_el];
        prev_el.next = el.next;
        prev_el.next_el = el.next_el;
      }
      assert(top.weight >= act.weight);
      top.weight -= act.weight;
      if (top.weight == 0) {
        // Erase this element
        resource_pairs.erase(el.hash);
      }
    }
    act.pair_elements.clear();
#endif
    return true;
  }

  /**
   * @brief Given a set of changing actions  (that either start or end),
   * recompute the shares in the resources
   *
   *
   * Prerequisites:
   * * Changing actions are listed in the active action set  (A)
   * * Active resource set (R) is empty
   *
   * 1. Propagate resources
   *  Do
   *   For each new action a in A
   *    For each resource r used by a
   *      if r not in R
   *        add r to R
   *        add r to vec (add index into r, struct{ptr to
   * r,weights,capa,epsilon}) else update vec[r.index] (weights,capa,epsilon)
   *  Endfor
   *  Foreach modified resource r
   *     Foreach action a that uses r
   *       if  a not in A and ( a.share >= epsilon or a is bottlenecked in r )
   *          add a to A
   *          unset a.share
   *  EndFor
   * while there are new a in A
   * 2. Sort resources by increasing epsilon
   *  i=0
   *  while i< vec.size
   *    recompute epsilon where needed and sort vec by epsilon (could be
   * optimized to be iterative) Update r.index accordingly (try to limit noise)
   *    epsilon_min=vec[i].epsilon
   *    for(i2=i;vec[i].epsilon==epsilon_min;i2++);
   *    Foreach action a using resources in index i to i2
   *      if a.share is unset
   *         a.share=epsilon_min
   *      Foreach resource R used by a
   *         if r.index>i2
   *            vec[r.index].weights-=a.weight
   *            vec[r.index].capacity-=epsilon_min
   *            (mark r.index for reordering)
   *    EndFor
   *    i=i2+1
   *
   *
   * We need temporary vectors:
   * * Vector of resources (->R)
   * * Vector of action (->A)
   * * Vectors RtoA and AtoR
   *
   * We have instable actions that eat from the core. In saturated resource,
   * those are bottlenecked actions. In non-saturated, new actions eat from the
   * slack. Prune propagation. For each resource, keep:
   * * core (including share of actions that end)
   * * core_eaters (new actions and bottlenecked actions)
   * * max_share_stable (the largest share that a non-bottlenecked action has )
   *
   * if action ends, for each resource increase slack accordingly (and update
   * eaters if needed, max_share_stable can be outdated safely) if action
   * starts, increment slack_eaters in each used resource
   *
   * we want to detect if changes in an action that uses the resource will
   * impact other actions.
   *
   * for each new marked resource r
   *   if core/core_eaters<max_share_stable
   *       We need to compress all actions
   *   else
   *       Add core eaters with core capacity only
   */
  void update_shares(WorkingSet &set);

  virtual size_t get_footprint() {
    return sizeof(Model) + GET_HEAP_FOOTPRINT(resources);
  }
};

template <> struct HeapConsumer<WorkingSet> {
  static size_t get_heap_footprint(const WorkingSet &x) {
    size_t footprint = GET_HEAP_FOOTPRINT(x.starting_actions);
    footprint += GET_HEAP_FOOTPRINT(x.actions);
    footprint += GET_HEAP_FOOTPRINT(x.resources);
    footprint += GET_HEAP_FOOTPRINT(x.resources_heap);
    return footprint;
  }
};

#endif //_MODEL_HPP_
