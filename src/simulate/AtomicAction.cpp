// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "AtomicAction.hpp"
#include "Model.hpp"
#include "ResourceManager.hpp"

AAPool *AAPool::instance = nullptr;

void AAPool::seal_action(actind_t index, simtime_t now) {
  INCREMENT_STAT(actions_sealed)
  AtomicAction &aa = _action_at(index);
  assert(aa.state == AtomicAction::INITIALIZING);
#ifdef SHOW_DEBUG
  assert(aa.countdown_to_recv <= aa.dbg.parent_to_recv);
  assert(aa.countdown_to_send <= aa.dbg.parent_to_send);
#endif
  LOG_DEBUG << "Seal action " << index << std::endl;
  aa.state = AtomicAction::WAITING_PRECOND;
  start_profile_region("seal_erase_handle");
  if (aa.event_handle.check_liveness())
    aa.event_handle.free();
  end_profile_region("seal_erase_handle");
  start_profile_region("seal_pending_links");
  bool invalid_list = true;
  while (invalid_list) {
    invalid_list = false;
    linkind_t link_index = aa.first_parent;
    while (not invalid_list && link_index != NoLink) {
      auto &link = link_at(link_index);
      link_index = link.next_from_child;
      if (link.state == AALink::PENDING && link.parent != NoAction) {
        auto &aaa = _action_at(link.parent);
        invalid_list =
            advance_state(aaa, link.parent, now, aaa.next_time, true);
      }
    }
  }
  end_profile_region("seal_pending_links");
  start_profile_region("seal_advance");
  advance_state(aa, index, now, aa.next_time, false);
  end_profile_region("seal_advance");
}

void AAPool::abort_action(actind_t index, simtime_t now) {
  LOG_DEBUG << "Abort action " << index << std::endl;
  auto &a = action_pool.at(index);
  assert(a.fin_type == AtomicAction::FIN_ABORT);

  // Detach children
  auto link_index = a.first_child;
  while (link_index != NoLink) {
    auto &link = link_at(link_index);
    // actind_t child_index = link.child;
    detach_link(link, link_index);
    link_index = link.next_from_parent;
  }

  // Visit parents
  link_index = a.first_parent;
  while (link_index != NoLink) {
    auto &link = link_at(link_index);
    // actind_t parent_index = link.parent;
    detach_link(link, link_index);
    link_index = link.next_from_child;
  }

  // Change internal state to avoid re-visits
  if (a.state == AtomicAction::CONSUMING) {
    auto &res = ctx.model->at(a.bottleneck_resource);
    assert(a.remain_handle != NoRemain);
    res.heap.update(a.remain_handle, {res.get_heap_remaining(0, now), index});
    if (res.event_handle.check_liveness())
      update_event(res.event_handle, now, index, EventHeapElement::ACTION);
    a.remaining = 0;
  } else {
    assert(a.state == AtomicAction::WAITING_PRECOND ||
           a.state == AtomicAction::WAITING_START || a.elements.empty());
    ctx.model->release_action_resources(index);
    a.event_handle.free();
    if (SnapshotsType < 2)
      action_pool.free_element(index);
  }
}

bool AAPool::advance_state(AtomicAction &aa, actind_t index, simtime_t now,
                           simtime_t propagation_time, bool local_only) {
  assert(aa.state != AtomicAction::INVALID);
  linkind_t link_index;
  bool all_propagated = true;
  bool requires_share = (aa.remaining != 0);
  int propagated_recv_count = aa.processed_recv_children;
  int propagated_send_count = aa.processed_send_children;
  bool propagated_change = false;
  switch (aa.state) {
  case AtomicAction::INITIALIZING:
    // Inhibit any progres until sealing() is called
    break;
    // aa.state=AtomicAction::WAITING_PRECOND;
    /*no break*/
  case AtomicAction::WAITING_PRECOND:
    if (aa.countdown_to_recv + aa.countdown_to_send != 0) {
      assert(aa.next == NoAction);
      break;
    }
    if (not aa.elements.empty()) {
      resind_t outbound_resindex = aa.elements.front().resource;
      auto &outbound_res = ctx.model->at(outbound_resindex);
      if (outbound_res.outbound_concurrency == 0) {
        if (aa.next == NoAction && outbound_res.last_outbound != index) {
          if (outbound_res.last_outbound != NoAction)
            _action_at(outbound_res.last_outbound).next = index;
        }
        if (outbound_res.first_outbound == NoAction)
          outbound_res.first_outbound = index;
        outbound_res.last_outbound = index;
        LOG_DEBUG << "Queue action " << index
                  << " because of concurrency constraint" << std::endl;
        break;
      } else {
        outbound_res.outbound_concurrency--;
      }
    }
    aa.state = AtomicAction::WAITING_START;
    /*no break*/ [[gnu::fallthrough]];
  case AtomicAction::WAITING_START:
    assert(aa.next_time != END_OF_TIME);
    if (not aa.event_handle.check_liveness() && requires_share) {
      evind_t handle_index = NoEvent;
      _create_event(handle_index, aa.next_time, index,
                    EventHeapElement::ACTION);
      aa.event_handle = EventHandleRef(handle_index);
    }
    if (requires_share)
      break;
    INCREMENT_STAT(actions_started)
    aa.state = AtomicAction::CONSUMING;
    /*no break*/ [[gnu::fallthrough]];
  case AtomicAction::CONSUMING:
    assert(aa.remaining == 0 || aa.next_time == END_OF_TIME);
    if (aa.remaining != 0)
      break;
    INCREMENT_STAT(actions_ended)
    aa.state = AtomicAction::PROPAGATING;
    /*no break*/ [[gnu::fallthrough]];
  case AtomicAction::PROPAGATING:
    assert(aa.next_time != END_OF_TIME);
    final_time = std::max(final_time, aa.next_time);
    assert(not aa.event_handle.check_liveness());
    LOG_DEBUG << "Progagate from " << index << " at "
              << simtime_to_double(propagation_time) << std::endl;
    link_index = aa.first_child;
    while (link_index != NoLink) {
      auto &link = link_at(link_index);
      CounterType type = link.type;
      // assert(aa.next_time+link.transition_delay>now);
      if (propagate(now, aa.next_time, link, link_index, local_only)) {
        propagated_change = true;
        if (type == CounterType::TO_SEND)
          propagated_send_count++;
        else {
          assert(type == CounterType::TO_RECV);
          propagated_recv_count++;
        }
      } else
        all_propagated = false;
      link_index = link.next_from_parent;
    }

    // Check whether to terminate this action or defer that.
    // For the final action, just disregard that
    if (aa.first_blocking != NoResource &&
        aa.fin_type != AtomicAction::FIN_WHOLE_JOB &&
        aa.fin_type != AtomicAction::FIN_ABORT)
      return propagated_change;
    switch (aa.fin_type) {
    case AtomicAction::NO_FIN:
      return propagated_change;
    case AtomicAction::FIN_ON_ALL_PROPAGATED:
      if (not all_propagated)
        return propagated_change;
      break;
    case AtomicAction::FIN_ON_SENDRECV_PROPAGATED:
      if (propagated_recv_count == 0 || propagated_send_count == 0)
        return propagated_change;
      break;
    case AtomicAction::FIN_WHOLE_JOB:
      ctx.resource_manager->on_job_finished(aa.job_id, now,
                                            JobFinishType::COMPLETION);
      //      event_heap.push({propagation_time, (resind_t)aa.job_id,
      //                      EventHeapElement::JOB_COMPLETE});
      break;
    case AtomicAction::FIN_ABORT:
      break;
    }

    aa.event_handle.free();

    INCREMENT_STAT(actions_finalized)
    if (SnapshotsType >= 2)
      break;
    // Detach parent links and free them if possible
    link_index = aa.first_parent;
    while (link_index != NoLink) {
      auto &link = link_at(link_index);
      link.child = NoAction;
      if (link.parent == NoAction)
        detach_link(link, link_index);
      link_index = link.next_from_child;
    }
    aa.first_parent = NoAction;

    // Detach child links and free them if possible
    link_index = aa.first_child;
    while (link_index != NoLink) {
      auto &link = link_at(link_index);
      if (link.state == AALink::INITED)
        link.state = AALink::CANCELLED;
      else
        assert(link.state == AALink::PENDING ||
               link.state == AALink::PROCESSED);
      link.parent = NoAction;
      if (link.child == NoAction)
        detach_link(link, link_index);
      link_index = link.next_from_parent;
    }
    aa.first_child = NoAction;

    // Queue this action to the free list
    action_pool.free_element(index);
    break;
  case AtomicAction::INVALID:
    MUST_DIE;
    break;
  }
  return propagated_change;
}

// Those are used for statistics reasons
extern std::unordered_map<resind_t, int> btl_map;
// https://stackoverflow.com/questions/664014/what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
resind_t _hash(resind_t x) {
  uint64_t xx = x;
  xx = (xx ^ (xx >> 30)) * uint64_t(0xbf58476d1ce4e5b9);
  xx = (xx ^ (xx >> 27)) * uint64_t(0x94d049bb133111eb);
  xx = xx ^ (xx >> 31);
  return (resind_t)xx;
}

constexpr int ShareCodeSignificantBits = 7;
// 64 bits = 2^6 bits -> 6 bits to code leading zeros count
static_assert(sizeof(share_t) == 8,
              "Need to change this part if shares have differnt size!");
#ifndef _WIN32
share_code_t encode_share(share_t share) {
  assert(share != 0);
  // Use _BitScanReverse for MSVC ?
  int lzc = __builtin_clzll(share);
  int shift = std::max(64 - lzc - ShareCodeSignificantBits, 0);
  share_code_t code = (lzc << ShareCodeSignificantBits) | (share >> shift);
  return code;
}

share_t decode_share(share_code_t code) {
  int lzc = code >> ShareCodeSignificantBits;
  share_t share = code & ((1 << ShareCodeSignificantBits) - 1);
  if (lzc < 64 - ShareCodeSignificantBits)
    share <<= 64 - lzc - ShareCodeSignificantBits;
  return share;
}
void test_code(share_t share) {
  share_code_t code = encode_share(share);
  share_t res = decode_share(code);
  share_t allowed_error = share >> ShareCodeSignificantBits;
  std::cout << std::hex << "share:0x" << share << " code:0x" << code
            << " res:0x" << res << std::endl;
  assert(res <= share);
  std::cout << " err:0x" << share - res << " allowed:0x" << allowed_error
            << std::endl;
  assert(share - res <= allowed_error);
}

#endif

// return whether this update actually changes the share of the action
bool AAPool::update_action_consumption(actind_t index, share_t new_share,
                                       resind_t new_bottleneck,
                                       simtime_t time) {
  AtomicAction &aa = _action_at(index);
  assert(aa.next_time == END_OF_TIME);
  assert(not aa.event_handle.is_live());
  // Compute relative share change (for stats only)

  share_t share_diff_ppm = 0;
  if (aa.current_share > new_share)
    share_diff_ppm = (aa.current_share - new_share) * 100000 / aa.current_share;
  else
    share_diff_ppm = (new_share - aa.current_share) * 100000 / new_share;
  if (share_diff_ppm < 100)
    INCREMENT_STAT(bottleneck_change[0])
  else if (share_diff_ppm < 1000)
    INCREMENT_STAT(bottleneck_change[1])
  else if (share_diff_ppm < 10000)
    INCREMENT_STAT(bottleneck_change[2])
  else if (share_diff_ppm < 100000)
    INCREMENT_STAT(bottleneck_change[3])
  else
    INCREMENT_STAT(bottleneck_change[4])

  // Neglect "very small" changes in rate
  share_t margin = (aa.current_share / 128) * ShareChange;
  bool share_change = (aa.current_share > new_share + margin ||
                       aa.current_share + margin < new_share ||
                       ((aa.current_share == 0) != (new_share == 0)));
  auto &new_btl = ctx.model->at(new_bottleneck);
  if (aa.bottleneck_resource != new_bottleneck) {
    share_change = true;
    // new_btl.modified = true;
    //  aa.btl_hash = aa.btl_hash ^ _hash(new_bottleneck);
    //// aa.btl_hash= new_bottleneck;
    //// aa.btl_hash=  _hash(aa.bottleneck_resource) ^_hash(new_bottleneck);
    //// Populate the bottleneck hash
    // auto search = btl_map.find(aa.btl_hash);
    // if (search != btl_map.end())
    //   search->second++;
    // else
    //   btl_map.emplace(std::make_pair(aa.btl_hash, 1));

    if (aa.bottleneck_resource != NoResource) {
      assert(aa.remain_handle != NoRemain);
      bool pop_top = false;
      auto &old_btl = ctx.model->at(aa.bottleneck_resource);
      aa.remaining =
          old_btl.get_action_remaining((*aa.remain_handle).remaining, time);
      _remove_action_from_bottleneck(aa, index, old_btl, aa.bottleneck_resource,
                                     time, pop_top);
      LOG_DEBUG << "Changed bottleneck of action " << index << " from "
                << aa.bottleneck_resource << " to " << new_bottleneck
                << std::endl;
      if (not InhibitDebugMessages) {
        LOG_INFO << "Old bottleneck" << aa.bottleneck_resource << " :"
                 << std::endl;
        old_btl.show_remaining_heap();
      }
    } else {
      INCREMENT_STAT(actions_started)
      aa.next_time = END_OF_TIME;
      aa.event_handle.free();
      aa.state = AtomicAction::CONSUMING;
      LOG_DEBUG << "Set bottleneck of action " << index << " to "
                << new_bottleneck << std::endl;
    }
    new_btl.update_offset(time, new_share);

    auto new_remaining = new_btl.get_heap_remaining(aa.remaining, time);
    aa.remain_handle = new_btl.heap.push({new_remaining, index});
    aa.bottleneck_resource = new_bottleneck;

    if (not InhibitDebugMessages) {
      LOG_INFO << "New bottleneck " << new_bottleneck << " :" << std::endl;
      new_btl.show_remaining_heap();
    }
  }
  if (share_change) {
    LOG_DEBUG << "Bottleneck resource " << new_bottleneck << " is modified "
              << std::endl;
    aa.current_share = new_share;
    new_btl.modified = true;
  }
  return share_change;
}

void AAPool::remove_remain_handle(actind_t index, simtime_t time) {
  auto &a = _action_at(index);
  auto &btl = ctx.model->at(a.bottleneck_resource);
  bool pop_top = false;
  _remove_action_from_bottleneck(a, index, btl, a.bottleneck_resource, time,
                                 pop_top);
  // assert(a.fin_type == AtomicAction::FIN_ABORT || pop_top);
  a.remain_handle = NoRemain;
  if (not InhibitDebugMessages) {
    LOG_INFO << "After removing action " << index << ", bottleneck "
             << a.bottleneck_resource << " is now :" << std::endl;
    btl.show_remaining_heap();
  }
}

std::ostream &operator<<(std::ostream &out, CounterType x) {
  switch (x) {
  case CounterType::TO_FAIL:
    out << "TO_FAIL";
    break;
  case CounterType::TO_SEND:
    out << "TO_SEND";
    break;
  case CounterType::TO_RECV:
    out << "TO_RECV";
    break;
  }
  return out;
}

#ifdef SHOW_DEBUG
static std::vector<actind_t> smallest_loop;
static std::unordered_map<actind_t, int> act_occurences;

constexpr int LoopCutAction = -4;
constexpr int DisconnectedAction = -2;
constexpr int UnvisitedAction = -1;

// Return whether we have violation here
static bool _add_line_info(std::unordered_map<int, int> &trace_last_line,
                           const SourceInfo &source, int &epoch) {
  if (source.rank < 0)
    return false;
  if (source.epoch < epoch) {
    LOG_INFO << "Trace causality violation at rank " << source.rank << ", line "
             << source.line << ", epoch " << source.epoch << " < " << epoch
             << std::endl;
    return true;
  }
  epoch = source.epoch;
  auto it = trace_last_line.find(source.rank);
  if (it == trace_last_line.end())
    trace_last_line.emplace(source.rank, source.line);
  else {
    if (it->second > source.line) {
      LOG_INFO << "Trace causality violation at rank " << source.rank
               << ", line " << source.line << " after " << it->second
               << std::endl;
      return true;
    }
    it->second = source.line;
  }
  return false;
}

int AAPool::_propagate_order(actind_t index, int order, uint64_t base,
                             AAPool::VisitMap &visits, SequenceMap &seqs) {
  auto &a = action_pool.at_bare(index);
  // Cut the loop
  if (a.dbg.dot_order == LoopCutAction)
    return 0;
  assert(a.dbg.dot_order >= UnvisitedAction);
  if (order <= a.dbg.dot_order)
    return 0;
  // Should not have an entry here. If we do, we have a loop
  VisitMap::iterator it = visits.find(index);
  if (it != visits.end()) {
    a.dbg.dot_order = LoopCutAction;
    int epoch = 0;
    std::unordered_map<int, int> trace_last_line;
    std::vector<actind_t> loop_acts;
    assert(a.dbg.source[0].rank >= 0);
    bool violation = _add_line_info(trace_last_line, a.dbg.source[0], epoch) ||
                     _add_line_info(trace_last_line, a.dbg.source[1], epoch);
    loop_acts.push_back(index);
    actind_t i = it->second;
    do {
      AtomicAction &aa = action_pool.at_bare(i);
      assert(aa.dbg.source[0].rank >= 0);
      violation = violation ||
                  _add_line_info(trace_last_line, aa.dbg.source[0], epoch) ||
                  _add_line_info(trace_last_line, aa.dbg.source[1], epoch);
      loop_acts.push_back(i);
      auto it2 = act_occurences.find(i);
      if (it2 != act_occurences.end())
        it2->second++;
      else
        act_occurences.emplace(i, 1);
      i = visits[i];
    } while (i != index);
    violation = _add_line_info(trace_last_line, a.dbg.source[0], epoch) ||
                _add_line_info(trace_last_line, a.dbg.source[1], epoch);
    if (not violation) {
      LOG_ERROR << "Loop without violation ? (epoch=" << epoch << ")"
                << std::endl;
      /* for (auto ii : loop_acts) {
        AtomicAction &aa = action_pool.at_bare(ii);
        LOG_ERROR << aa.dbg.source[0].first << "=" << aa.dbg.source[0].second;
        if (aa.dbg.source[1].first >= 0)
          LOG_ERROR << " " << aa.dbg.source[1].first << "="
                    << aa.dbg.source[1].second;
        LOG_ERROR << "\t" << aa.get_label_string(ii) << std::endl;
      }
      MUST_DIE; */
    }
    if (smallest_loop.empty() || smallest_loop.size() > loop_acts.size())
      smallest_loop.swap(loop_acts);
    // MUST_DIE;
    return 1;
  }
  auto place = visits.emplace(index, NoAction);
  int sequence_order = 0;
  if (not a.dbg.index) {
    a.dbg.dot_base = base;
    a.dbg.dot_order = order;
    order += 10;
    sequence_order = order;
  } else {
    auto newbase = a.dbg.source[0].get_hash();
    auto it2 = seqs.find(newbase);
    assert(it2 != seqs.end());
    auto &info = it2->second;
    if (base != newbase) {
      if (info.end_dot_base < newbase || info.start_dot_order < order)
        info.start_dot_order = order;
      if (info.end_dot_base == 0)
        info.end_dot_base = base;
    }
    a.dbg.dot_order = a.dbg.index.seq << 24 | a.dbg.index.level << 4;
    a.dbg.dot_order = std::max(a.dbg.dot_order, order);
    sequence_order = a.dbg.dot_order + 1;
    a.dbg.dot_base = base = newbase;
    order = 0;
  }
  int loops = 0;
  linkind_t link_index = a.first_child;
  while (link_index != NoLink) {
    auto &link = link_at(link_index);
    if (link.child != NoAction) {
      place.first->second = link.child;
      bool child_in_sequence = action_pool.at_bare(link.child).dbg.index;
      loops += _propagate_order(link.child,
                                child_in_sequence ? sequence_order : order,
                                base, visits, seqs);
    }
    link_index = link.next_from_parent;
  }
  visits.erase(place.first);
  return loops;
}

int AAPool::_propagate_seq(SequenceInfo &s, SequenceMap &seqs) {
  if (s.end_dot_base == 0)
    return s.start_dot_order;
  s.start_dot_order = _propagate_seq(seqs[s.end_dot_base], seqs);
  s.end_dot_base = 0;
  return s.start_dot_order + s.depth_dot_order;
}

actind_t AAPool::_compute_order() {
  // Get ready for computation of orders and positions
  std::vector<actind_t> root_actions;
  actind_t max_unconnected = NoAction;
  SequenceMap seqs;
  for (int i = 0; i < (int)action_pool.get_capacity(); i++) {
    auto &a = action_pool.at_bare(i);
    if (not _include_in_snapshot(a))
      continue;
    // Look for valid parent
    bool has_parent = false;
    linkind_t link_index = a.first_parent;
    while (link_index != NoLink) {
      auto &link = link_at(link_index);
      if (link.parent != NoAction &&
          action_pool.at_bare(link.parent).state != AtomicAction::INVALID) {
        has_parent = true;
        break;
      }
      link_index = link.next_from_child;
    }
    // Look for valid child
    bool has_child = false;
    link_index = a.first_child;
    while (link_index != NoLink) {
      auto &link = link_at(link_index);
      if (link.child != NoAction &&
          action_pool.at_bare(link.child).state != AtomicAction::INVALID) {
        has_child = true;
        break;
      }
      link_index = link.next_from_parent;
    }
    if (not has_parent && has_child)
      root_actions.push_back(i);
    if (not has_parent && not has_child) {
      a.dbg.dot_order = DisconnectedAction;
      max_unconnected = i;
    } else {
      a.dbg.dot_order = UnvisitedAction;
    }
    a.dbg.dot_base = 0;
    if (a.dbg.index) {
      auto src_hash = a.dbg.source[0].get_hash();
      auto it = seqs.find(src_hash);
      if (it == seqs.end()) {
        LOG_VERBOSE << "Found sequence " << (src_hash % 1000) << " from "
                    << a.dbg.desc << " @ " << a.dbg.source[0] << std::endl;
        auto res = seqs.emplace(src_hash, SequenceInfo());
        it = res.first;
        it->second.end_dot_base = 0;
        it->second.depth_dot_order = 0;
        it->second.start_dot_order = UnvisitedAction;
        assert(res.second);
      }
      assert(a.dbg.index.seq > 0);
      if (a.dbg.index.seq > it->second.indices.size())
        it->second.indices.resize(a.dbg.index.seq);
      it->second.indices[a.dbg.index.seq - 1].grow(a.dbg.index);
    }
  }
  // Compute the depth of each Sequence
  for (auto &x : seqs) {
    for (const auto &y : x.second.indices)
      x.second.depth_dot_order += (y.level + 1) * 10 + 5;
  }

  // Now get order to compute
  VisitMap visits;
  int loops = 0;
  for (auto i : root_actions) {
    loops += _propagate_order(i, 0, 0, visits, seqs);
    assert(visits.empty());
  }

  if (loops > 0) {
    LOG_INFO << "We found " << loops << " loops!" << std::endl;
    LOG_INFO << "The smallest has " << smallest_loop.size() << " actions"
             << std::endl;
    for (auto i : smallest_loop) {
      LOG_INFO << action_pool.at_bare(i).get_label_string(i) << "->"
               << std::endl;
    }
    smallest_loop.clear();
    typedef std::pair<actind_t, int> Pair;
    std::vector<Pair> sorted_occurences;
    sorted_occurences.reserve(act_occurences.size());
    for (auto p : act_occurences)
      sorted_occurences.emplace_back(p);
    std::sort(sorted_occurences.begin(), sorted_occurences.end(),
              [](const Pair &a, const Pair &b) { return a.second < b.second; });
    LOG_INFO << std::endl
             << "Action Most often involved in loops:" << std::endl;
    for (size_t i = 0; i < 10; i++) {
      auto &p = sorted_occurences[i];
      LOG_INFO << p.second << " x "
               << action_pool.at_bare(p.first).get_label_string(p.first)
               << std::endl;
    }

    act_occurences.clear();
  }

  // Make the orders absolute (now that we know the end of each sequence)
  for (auto &s : seqs) {
    _propagate_seq(s.second, seqs);
    LOG_VERBOSE << "Sequence " << (s.first % 1000)
                << " start:" << s.second.start_dot_order
                << " depth:" << s.second.depth_dot_order << std::endl;
  }

  for (int i = 0; i < (int)action_pool.get_capacity(); i++) {
    auto &a = action_pool.at_bare(i);
    if (not _include_in_snapshot(a))
      continue;
    if (a.dbg.dot_order == DisconnectedAction)
      continue;
    if (a.dbg.dot_base == 0)
      continue;
    auto it = seqs.find(a.dbg.dot_base);
    auto original_dot_order = a.dbg.dot_order;
    if (not a.dbg.index)
      a.dbg.dot_order +=
          it->second.depth_dot_order + it->second.start_dot_order;
    else {
      auto seq = a.dbg.dot_order >> 24;
      auto level = (a.dbg.dot_order >> 4) & ((1 << 20) - 1);
      auto nudge = a.dbg.dot_order & 0x0F;
      auto &indices = it->second.indices;
      int order = level * 10 + nudge;
      for (size_t j = 0; j < indices.size(); j++) {
        if (j + 1 < (size_t)seq)
          order += (indices[j].level + 1) * 10 + 5;
      }
      a.dbg.dot_order = it->second.start_dot_order + order;
    }
    LOG_VERBOSE << "Action " << i << " with base " << (a.dbg.dot_base % 1000)
                << " and index " << a.dbg.index << " got order "
                << a.dbg.dot_order << " from " << original_dot_order
                << std::endl;
    a.dbg.dot_base = 0;
  }

  return max_unconnected;
}
#endif

#if defined(__cpp_lib_filesystem)
#include <filesystem>
void show_snapshot_path(std::string f) {
  std::filesystem::path path(f);
  LOG_INFO << "Snapshot path is <" << std::filesystem::absolute(path) << ">"
           << std::endl;
}
#else
void show_snapshot_path(std::string f) {
  LOG_INFO << "Snapshot path is <" << f << ">" << std::endl;
}
#endif

enum class SnapshotFormatEnum { UNDEFINED, DOT, PYDICT };
SnapshotFormatEnum SnapshotFormat = SnapshotFormatEnum::UNDEFINED;
static bool first_item;

static void write_snap_graph_prolog(std::ostream &stream, std::string suffix,
                                    simtime_t final_time) {
  first_item = true;
  switch (SnapshotFormat) {
  case SnapshotFormatEnum::DOT:
    stream << "digraph graphname {" << std::endl;
    break;
  case SnapshotFormatEnum::PYDICT:
    stream << "snapshot= {" << std::endl
           << "  'final_time':" << simtime_to_double(final_time) << ","
           << std::endl
           << "  'arguments': [";
    for (auto x : ctx.raw_arguments) {
      stream << "\"" << x << "\",";
    }
    stream << "\"\"]," << std::endl << "  'stats':{";
    ctx.total_stats->to_pydict(stream, final_time);
    stream << "  }," << std::endl
           << "  'suffix':'" << suffix << "'," << std::endl
           << "  'graph': {" << std::endl
           << "    'directed':True," << std::endl
           << "    'metadata': {" << std::endl
           << "      'node_label_size': 14," << std::endl
           << "      'node_label_color': 'green'," << std::endl
           << "      'edge_label_size': 10," << std::endl
           << "      'edge_label_color': 'blue'," << std::endl
           << "    }," << std::endl
           << "    'nodes': {" << std::endl;
    break;
  default:
    MUST_DIE;
  }
}

static void write_snap_node(std::ostream &stream, int action_index,
                            AtomicAction &action, int x, int y, int z) {
  switch (SnapshotFormat) {
  case SnapshotFormatEnum::DOT:
    stream << "a" << action_index << " [label=\""
           << action.get_label_string(action_index) << "\"; "
           << "color=\"" << action.get_color_string() << "\"; "
           << "pos=\"" << x << "," << y << "\"; "
           << "];" << std::endl;
    break;
  case SnapshotFormatEnum::PYDICT:
    if (not first_item)
      stream << "," << std::endl;
    stream << "      " << action_index << ":{ 'label':\""
           << action.get_label_string(action_index) << "\", 'metadata':{ "
           << "'color':'" << action.get_color_string() << "', "
           << "'x':'" << x << "','y':'" << y << "','z':'" << z << "', "
           << "'hover':\"" << action.get_label_string(action_index) << "\", "
           << "'issuer':'" << action.issuer << "', "
           << "'state':'" << action.get_state_string() << "', "
           << "'next_time':'" << simtime_to_double(action.next_time) << "' "
#ifdef SHOW_DEBUG
           << ",'desc':'" << action.dbg.desc << "' "
           << ",'index':'" << action.dbg.index << "' "
           << ",'source':'" << action.dbg.source[0];
    if (action.dbg.source[1].rank >= 0)
      stream << '|' << action.dbg.source[1];
    stream << "'"
#endif
           << " }}";
    // Other node metadata
    //  'size': 15,
    //   'shape': 'rectangle',
    //   'opacity': 0.7,
    //   'label_color': 'red',
    //   'label_size': 20,
    //   'border_color': 'black',
    //   'border_size': 3,
    break;
  default:
    MUST_DIE;
  }
  first_item = false;
}

static void write_snap_node_link_break(std::ostream &stream) {
  switch (SnapshotFormat) {
  case SnapshotFormatEnum::DOT:
    stream << std::endl;
    break;
  case SnapshotFormatEnum::PYDICT:
    stream << std::endl
           << "    }," << std::endl
           << "    'edges':[" << std::endl;
    break;
  default:
    MUST_DIE;
  }
  first_item = true;
}

static void write_snap_link(std::ostream &stream, linkind_t link_index,
                            AALink &link) {
  switch (SnapshotFormat) {
  case SnapshotFormatEnum::DOT:
    stream << "a" << link.parent << " -> a" << link.child << " [label=\""
           << link.get_label_string(link_index) << " \";];" << std::endl;
    break;
  case SnapshotFormatEnum::PYDICT:
    if (not first_item)
      stream << "," << std::endl;
    stream << "      {'source':" << link.parent << ", 'target':" << link.child
           << ", "
           << "'label':\"" << link.get_label_string(link_index)
           << "\", 'metadata':{ "
           << "'hover':\"" << link.get_label_string(link_index) << "\", "
           << "'state':'" << link.state << "', "
           << "'delay':'" << link.transition_delay << "', "
           << "'type':'" << link.type << "' "
           << "}}";
    break;
  default:
    MUST_DIE;
  }
  first_item = false;
}

static void write_snap_graph_epilog(std::ostream &stream, bool skip_graph) {
  switch (SnapshotFormat) {
  case SnapshotFormatEnum::DOT:
    stream << "}" << std::endl;
    break;
  case SnapshotFormatEnum::PYDICT:
    stream << std::endl;
    if (skip_graph)
      stream << "    }" << std::endl;
    else
      stream << "    ]" << std::endl;
    stream << "  }" << std::endl << "}" << std::endl;
    break;
  default:
    MUST_DIE;
  }
}

void AAPool::save_snapshot(std::string suffix, simtime_t now) {
  (void)now;
  if (SnapshotFormat == SnapshotFormatEnum::UNDEFINED) {
    auto format_str = ctx.vm["snapshots_format"].as<std::string>();
    if (format_str == "dot") {
      SnapshotFormat = SnapshotFormatEnum::DOT;
    } else if (format_str == "pydict") {
      SnapshotFormat = SnapshotFormatEnum::PYDICT;
    } else {
      MUST_DIE
    }
  }
  std::stringstream ss;
  ss << ctx.vm["stats_folder"].as<std::string>() << "/snap_" << suffix;
  switch (SnapshotFormat) {
  case SnapshotFormatEnum::DOT:
    ss << ".dot";
    break;
  case SnapshotFormatEnum::PYDICT:
    ss << ".py";
    break;
  default:
    MUST_DIE;
  };
  std::string file_name = ss.str();
  show_snapshot_path(file_name);

  // May be useful to touch this variable with gdb -> volatile
  volatile bool skip_graph = (SnapshotsType == -1);

  std::ofstream file(file_name.c_str());
  assert(file.good());
  write_snap_graph_prolog(file, suffix, get_final_time());

#ifdef SHOW_DEBUG
  actind_t max_unconnected;
#endif
  
  if (skip_graph)
    goto fin_snapshot;

#ifdef SHOW_DEBUG
  max_unconnected = _compute_order();
#endif

  linkind_t link_index;
  for (int i = 0; i < (int)action_pool.get_capacity(); i++) {
    auto &a = action_pool.at_bare(i);
    if (not _include_in_snapshot(a))
      continue;
    // Compute an approximate/optimistic time for forecoming actions
    link_index = a.first_child;
    while (link_index != NoLink) {
      auto &link = link_at(link_index);
      if (link.child != NoAction) {
        auto &aa = action_pool.at(link.child);
        if (aa.state == AtomicAction::WAITING_START ||
            aa.state == AtomicAction::WAITING_PRECOND ||
            aa.state == AtomicAction::INITIALIZING) {
          aa.prev_time =
              std::max(aa.prev_time, a.next_time + link.transition_delay);
        }
      }
      link_index = link.next_from_parent;
    }

    // Build the position property
    int z_pos = 0;
#ifdef SHOW_DEBUG
    // assert(a.dot_order == -2 || a.dot_order >= 0);
    int x_pos = a.dbg.dot_order * 50;
    if (a.dbg.dot_order == DisconnectedAction)
      x_pos = (i - max_unconnected - 3) * 50;
    if (a.dbg.index)
      z_pos = 50 * a.dbg.index.i[2] + 10 * a.dbg.index.i[1];
#else
    int x_pos = i * 50;
#endif
    int y_pos = (a.issuer == NoResource ? 0 : a.issuer * 300 + (i % 8) * 30);
    write_snap_node(file, i, a, x_pos, y_pos, z_pos);
  }
  write_snap_node_link_break(file);
  for (int i = 0; i < (int)action_pool.get_capacity(); i++) {
    auto &a = action_pool.at_bare(i);

    link_index = a.first_child;
    while (link_index != NoLink) {
      auto &link = link_at(link_index);
      if (link.child != NoAction) {
        auto &aa = action_pool.at_bare(link.child);
        if (aa.state != AtomicAction::INVALID) {
          write_snap_link(file, link_index, link);
        }
      }
      link_index = link.next_from_parent;
    }
  }

fin_snapshot:
  write_snap_graph_epilog(file, skip_graph);
  file.close();
}

std::ostream &operator<<(std::ostream &out, const SequenceIndex &x) {
  if (x.seq == 0) {
    out << "(--)";
    return out;
  }
  out << '(' << x.seq << '/' << x.level << ':' << x.i[0] << ',' << x.i[1] << ','
      << x.i[2] << ')';
  return out;
}

std::ostream &operator<<(std::ostream &out, const SourceInfo &x) {
  out << "(rank:" << x.rank << " line:" << x.line << '@' << x.epoch << ')';
  return out;
}
