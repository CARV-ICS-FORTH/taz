// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "Model.hpp"
#include "Engine.hpp"
#include "ResourceManager.hpp"
#include <unordered_map>

std::unordered_map<resind_t, int> btl_map;

Heap::handle_type WorkingSet::NoHandle;

bool compare_shares(const LightResource *a, const LightResource *b) {
  return a->share < b->share;
}
bool Model::hold_action_resources(actind_t act_index, simtime_t time) {
  (void)time;
  AAPool *pool = AAPool::get_instance();
  auto &act = pool->_action_at(act_index);
  uint8_t el_index = 0;
  act.holding_elements = true;
#ifdef USE_RESOURCE_PAIRS
  actind_t el2_index = 0;
#endif
  assert(not act.elements.empty());
  size_t i = 0;
  for (; i < act.elements.size(); i++) {
    auto &el = act.elements[i];
    auto &res = at(el.resource);
    if (res.is_faulty) {
      // Skip elements that were never held and return
      act.elements.resize(i);
      return false;
    }
    el.prev = NoAction;
    if (res.first != NoAction) {
      auto &first_act = pool->_action_at(res.first);
      auto &first_el = first_act.elements[res.first_el];
      el.next = res.first;
      el.next_el = res.first_el;
      first_el.prev = act_index;
      first_el.prev_el = el_index;
    } else {
      el.next = NoAction;
    }
    res.first = act_index;
    res.first_el = el_index;
    el_index++;
    // Deal with pairs
#ifdef USE_RESOURCE_PAIRS
    for (size_t j = i + 1; j < act.elements.size(); j++) {
      auto &el2 = act.elements[j];
      resind_t hash = _hash(el.resource) ^ _hash(el2.resource);
      auto it = resource_pairs.find(hash);
      if (it != resource_pairs.end()) {
        it->second.weight += act.weight;
        auto &pair_el = pool->_action_at(it->second.first)
                            .pair_elements[it->second.first_el];
        // FIXME: This is wrong, we need to establish lists of pairs, not
        // using el2 el2.next = it->second.first; el2.next_el =
        // it->second.first_el;
        pair_el.prev = act_index;
        pair_el.prev_el = el2_index;
      } else {
        resind_t res_a_index, res_b_index;
        if (el.resource < el2.resource) {
          res_a_index = el.resource;
          res_b_index = el2.resource;
        } else {
          res_b_index = el.resource;
          res_a_index = el2.resource;
        }
        auto &res_a = at(res_a_index);
        auto &res_b = at(res_b_index);
        resource_pairs.emplace(hash,
                               PairElement{res_a_index, res_b_index, act_index,
                                           (uint16_t)el2_index, NO_RESOURCE,
                                           res_a.first_pair, NO_RESOURCE,
                                           res_b.first_pair, act.index});
        res_a.first_pair = hash;
        res_b.first_pair = hash;
      }

      el2_index++;
    }
#endif
  }
  return true;
}

void Model::update_shares(WorkingSet &set) {

  AAPool *pool = AAPool::get_instance();
  actind_t act_index0 = NoAction;

  btl_map.clear();

  /*
  AtomicAction* act=pool->get_first_active();
  assert(act);
  act->index=0;
  set.actions.push_back(act);
  */

  simtime_t now = set.first_event + SimtimePrecision / 2;

  // Propagate actions to figure out what resources are impacted
  // Note that set.actions is augmented with other actions within this loop
  ADD_INDIV_STAT(original_actions_per_iteration, set.actions.size())
  int ended_count = 0;
  start_profile_region("main_runloopshare_prop");
  for (size_t i = 0; i < set.actions.size(); i++) {
    act_index0 = set.actions[i];
    if (act_index0 == NoAction)
      continue;
    auto &act = pool->at_const(act_index0);
    bool is_ending0 = act.is_ending(set.first_event);
    // TODO: Prune propagation using bottleneck information/core info
    int active_resources = 0;
    for (auto &r : act.elements) {
      auto &res = at(r.resource);
      if (is_ending0 && res.slack > 0) {
        // ending actions should be processed first
        // assert(res.index == NO_RESOURCE);
        res.slack += act.current_share;
        continue;
      }
      // TODO: Estimate whether a resource is likely to propagate changes
      //-> need to handle cases where estimation was wrong
      // if(not is_ending0 && res.slack >=  ) {
      //   res.slack+=act.current_share;
      //   assert(res.index == NO_RESOURCE);
      //   continue;
      // }
      if (res.index != NoResource)
        continue;
      active_resources++;
      res.index = (resind_t)set.resources.size();

      set.resources.emplace_back(&res, r.resource, res.capacity);
      LightResource &lr = set.resources.back();
      // Queue other actions using this
      int n_actions = 0;
      actind_t act_index = res.first;
      uint8_t el_index = res.first_el;
      while (act_index != NoAction) {
        auto &act2 = pool->at_const(act_index);
        // We do  not want actions that are still blocked or waiting to start
        assert(act2.state == AtomicAction::CONSUMING ||
               act2.countdown_to_recv + act2.countdown_to_send == 0 ||
               act2.next_time < set.first_event + SimtimePrecision);
        bool is_ending = act2.is_ending(set.first_event);
        if (act2.index == NoAction)
          set.add_action(act_index, true);
        const AtomicAction::ResourceElement &el = act2.elements[el_index];
        if (not is_ending) {
          n_actions++;
          assert(act2.weight == 1);
          lr.weights += act2.weight;
        } else {
          ended_count++;
        }
        act_index = el.next;
        el_index = el.next_el;
      }
      lr.share = lr.weights ? lr.core / lr.weights : 0;
      ADD_INDIV_STAT(actions_per_resource, n_actions)
    }
    ADD_INDIV_STAT(resources_per_action, active_resources);
  }

  end_profile_region("main_runloopshare_prop");
  ADD_INDIV_STAT(total_actions_per_iteration, set.actions.size())
  ADD_INDIV_STAT(ended_actions_per_iteration, ended_count)
  ADD_INDIV_STAT(total_resources_per_iteration, set.resources.size())

  // Remove the ended actions from the resource element lists
  start_profile_region("main_runloopshare_trimend");
  actind_t first_ended = set.first_ended;
  set.first_ended = NoAction;
  actind_t last_not_finalized = NoAction;
  while (first_ended != NoAction) {
    auto &act = pool->at_const(first_ended);
    // Release concurrency slot
    assert(not act.elements.empty());
    resind_t outbound_resindex = act.elements.front().resource;
    release_action_resources(first_ended);
    auto &outbound_res = at(outbound_resindex);
    if (outbound_res.first_outbound != NoAction) {
      actind_t first_outbound = outbound_res.first_outbound;
      // Unlock the first waiting action
      auto &first_act = pool->_action_at(first_outbound);
      outbound_res.first_outbound = first_act.next;
      LOG_DEBUG << "Unqueue action " << first_outbound << " because action "
                << first_ended << " has finished" << std::endl;
      if (first_act.next == NoAction)
        outbound_res.last_outbound = NoAction;
      first_act.next = NoAction;
      first_act.state = AtomicAction::WAITING_START;
      first_act.next_time =
          std::max(first_act.next_time, now + SimtimePrecision);
      pool->advance_state(first_act, first_outbound, now, now, false);
    } else {
      outbound_res.outbound_concurrency++;
    }

    actind_t next = pool->at_const(first_ended).next;
    bool finalized = pool->finalize_action(first_ended, now);
    // Strip the list from finalized actions
    if (not finalized) {
      if (last_not_finalized != NoAction)
        pool->_action_at(last_not_finalized).next = first_ended;
      else {
        last_not_finalized = first_ended;
        set.first_ended = first_ended;
      }
      pool->_action_at(first_ended).next = NoAction;
    }
    first_ended = next;
  }
  end_profile_region("main_runloopshare_trimend");

  // Populate the heap of resources (ordered in increasing share)
  start_profile_region("main_runloopshare_populate");
  for (resind_t i = 0; i < set.resources.size(); i++) {
    auto &lr = set.resources[i];
    if (lr.weights)
      lr.handle = set.resources_heap.push({lr.share, i});
    else
      lr.ptr->index = NoResource;
  }
  end_profile_region("main_runloopshare_populate");

  uint64_t modified_actions = 0;
  start_profile_region("main_runloopshare_update");
  while (not set.resources_heap.empty()) {
    // Note that supposedly, elements already processed do not need to be
    // reordered
    auto &heapEl = set.resources_heap.top();
    auto &lr = set.resources[heapEl.index];
    auto &res = *(lr.ptr);
    res.slack = 0;

    res.index = NoResource;
    set.resources_heap.pop();
    actind_t act_index = res.first;
    uint8_t el_index = res.first_el;
    int first_resource_to_update = -1;
    while (act_index != NoAction) {
      auto &act2 = pool->at_const(act_index);
      const AtomicAction::ResourceElement &el = act2.elements[el_index];
      if (act2.index != NoAction) {
        // Update action progress if needed
        if (pool->update_action_consumption(act_index, lr.share, el.resource,
                                            now)) {

          modified_actions++;
          // Propagate to other not-yet-visited resources
          for (auto &el2 : act2.elements) {
            auto &res2 = at(el2.resource);
            if (res2.index == NoResource)
              continue;
            auto &lr2 = set.resources[res2.index];
            assert(lr2.weights >= act2.weight);
            lr2.weights -= act2.weight;
            assert(lr2.core > lr.share ||
                   (lr2.core == lr.share && lr2.weights == 0));
            lr2.core -= lr.share;
            if (not lr2.weights) {
              lr2.share = 0;
              // assert(res2.heap.empty());
              res2.remain_offset = 0;
              res2.slack = lr2.core;
              res2.index = NoResource;
              set.resources_heap.erase(lr2.handle);
              lr2.handle = WorkingSet::NoHandle;
            } else if (not lr2.need_update) {
              // Defer resource heap update
              lr2.next_resource_to_update = first_resource_to_update;
              lr2.need_update = true;
              first_resource_to_update = res2.index;
            }
          }
        }
        // We are done with this action for this update
        set.remove_action(act_index);
      }
      act_index = el.next;
      el_index = el.next_el;
    } // End of while there are actions not yet saturated

    // Compute resource heap update
    int lr_index = first_resource_to_update;
    while (lr_index >= 0) {
      auto &lr2 = set.resources[lr_index];
      auto &res2 = *(lr2.ptr);
      if (lr2.weights) {
        lr2.share = lr2.core / lr2.weights;
        assert(lr2.handle != WorkingSet::NoHandle);
        set.resources_heap.update(lr2.handle, {lr2.share, res2.index});
      }
      lr2.need_update = false;
      lr_index = lr2.next_resource_to_update;
    }
  } // End of for each resource (in evolving increasing share)
  end_profile_region("main_runloopshare_update");

  uint64_t modified_resources = 0;
  start_profile_region("main_runloopshare_post");
  for (auto &lr : set.resources) {
    auto &res = lr.ptr;
    assert(res->index == NoResource);
    if (not res->modified)
      continue;
    res->modified = false;
    modified_resources++;
    res->update_offset(now, lr.share);
    // Update the event heap entry for this resource
    if (res->heap.empty())
      continue;
    simtime_t top_time = res->get_top_end_time(set.first_event);
    if (res->event_handle.check_liveness()) {
      // assert(pool->at_const(top_action).event_handle == res->event_handle);
      pool->update_event(res->event_handle, top_time, lr.resource_index,
                         EventHeapElement::BOTTLENECK);
    } else {
      res->event_handle = pool->push_event(top_time, lr.resource_index,
                                           EventHeapElement::BOTTLENECK);
    }
  }
  end_profile_region("main_runloopshare_post");

  ADD_INDIV_STAT(modified_actions_per_iteration, modified_actions)
  ADD_INDIV_STAT(modified_resources_per_iteration, modified_resources)

  // Extract info from the bottlneck hash table
  uint64_t count_sum = 0;
  for (auto &kv : btl_map)
    count_sum += kv.second;

  auto map_size = btl_map.size();
  ADD_INDIV_STAT(btl_group_count, map_size)
  ADD_INDIV_STAT(btl_group_size, map_size ? count_sum / map_size : 0)
}
