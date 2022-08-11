//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../CollectiveEngine.hpp"
#include "../Engine.hpp"

constexpr size_t DepStrSize = 30;
constexpr size_t NRanksToPrint = 5;
void CollectiveEngine::print_deps() {
  if (InhibitVerboseMessages)
    return;
  size_t comm_size = indices.size();
  std::vector<std::string> lines(deps_per_rank + 1);
  for (size_t rank = 0; rank < comm_size; rank++) {
    auto &ind = indices[rank];
    std::stringstream ss0;
    ss0 << " " << rank << "=st:" << ind.start << " sz:" << ind.size;
    std::string str0 = ss0.str();
    while (str0.size() < DepStrSize)
      str0 += " ";
    lines[0] += str0;
    for (size_t i = 0; i < deps_per_rank; i++) {
      std::stringstream ss;
      // Check that we have invalid deps only where we should
      if (i == ind.next_offset)
        ss << "->:" << deps[ind.start + i];
      else {
        size_t offset =
            (i < ind.next_offset ? ind.next_offset - i
                                 : deps_per_rank + ind.next_offset - i);
        if (offset <= ind.size) {
          assert(deps[ind.start + i].state != Dependency::INVALID);
          ss << "+" << (offset % 10) << ":" << deps[ind.start + i];
        } else {
          assert(deps[ind.start + i].state == Dependency::INVALID);
          ss << " ...";
        }
      }
      std::string str = ss.str();
      assert(str.size() < DepStrSize);
      while (str.size() < DepStrSize)
        str += " ";
      lines[i + 1] += str;
    }

    if (rank % NRanksToPrint == NRanksToPrint - 1) {
      for (size_t i = 0; i <= deps_per_rank; i++) {
        LOG_VERBOSE << lines[i] << std::endl;
        lines[i].clear();
      }
      LOG_VERBOSE << std::endl;
    }
  }

  if (not lines[0].empty()) {
    for (size_t offset = 0; offset <= deps_per_rank; offset++) {
      LOG_VERBOSE << lines[offset] << std::endl;
    }
  }
}

void CollectiveEngine::_pop_dependency(std::vector<actind_t> &unused_acts,
                                       resind_t rank, size_t &offset, int jobid,
                                       bool &out_used) {
  auto &ind = indices[rank];
  Dependency::State dep_state;
#ifndef NDEBUG
  bool first = true;
#endif
  do {
    size_t i = ind.start + offset;
    auto &d2 = deps[i];
    if (d2.other_dep == std::string::npos) {
      // We can seal because the other rank is done
      if (d2.used &&
          ctx.pool->at_const(d2.in_act).state == AtomicAction::INITIALIZING) {
        ctx.pool->seal_action(d2.in_act, engine.get_last_time());
        ctx.pool->set_fin_criteria(d2.in_act,
                                   AtomicAction::FIN_ON_ALL_PROPAGATED, jobid);
      }
    } else {
      // Allow for sealing when other rank also wants to seal
      assert(d2.other_dep < deps.size());
      deps[d2.other_dep].other_dep = std::string::npos;
    }
    ind.size--;
    dep_state = d2.state;
#ifndef NDEBUG
    assert(first || out_used == d2.used);
    assert(not first || dep_state == Dependency::FIRST ||
           dep_state == Dependency::SINGLE);
    first = false;
#endif
    out_used = d2.used;
    if (not out_used)
      unused_acts.push_back(d2.in_act);

    d2.state = Dependency::INVALID;
    offset = (offset + 1) % deps_per_rank;
  } while (dep_state == Dependency::FIRST || dep_state == Dependency::MIDDLE);
}

size_t CollectiveEngine::_push_dependency(resind_t rank, actind_t in_act,
                                          size_t other_dep,
                                          Dependency::State state,
                                          simtime_t compute_delay, int jobid) {
  assert(rank < (resind_t)indices.size());
  auto &ind = indices[rank];
  size_t dep_index = ind.start + ind.next_offset;
  auto &d = deps[dep_index];
  bool used;
  std::vector<actind_t> unused_acts;
  // Do we need to evict this dependency ?
  if (d.state != Dependency::INVALID) {
    auto next_pop_offset = ind.next_offset;
    _pop_dependency(unused_acts, rank, next_pop_offset, jobid, used);
    assert(used);
  }

  d.in_act = in_act;
  d.other_dep = other_dep;
  d.state = state;
  d.compute_delay = compute_delay;
  d.used = false;
  ind.size++;
  ind.next_offset = (ind.next_offset + 1) % deps_per_rank;
  return dep_index;
}

void CollectiveEngine::_use_dependency(std::vector<ActGroup> &out_acts,
                                       resind_t rank, size_t offset,
                                       simtime_t &out_delay) {
  out_acts.clear();
  out_acts.push_back(ActGroup{NoAction, NoAction});
  const auto &ind = indices[rank];
  assert(offset <= ind.size);
  offset = (deps_per_rank + ind.next_offset - offset) % deps_per_rank;
  size_t i = ind.start + offset;
  auto &d = deps[i];
  assert(d.state != Dependency::INVALID);
  out_acts[0][0] = d.in_act;
  d.used = true;
  out_delay = d.compute_delay;
  if (d.state == Dependency::SINGLE)
    return;
  assert(d.state == Dependency::LAST);
  size_t j = 1;
  for (;;) {
    offset = (deps_per_rank + offset - 1) % deps_per_rank;
    i = ind.start + offset;
    auto &d2 = deps[i];
    // FIXME: We need to be able to pass different delays for each action
    // assert(d2.compute_delay == out_delay);
    if (j % 2 == 1)
      out_acts[j / 2][1] = d2.in_act;
    else
      out_acts.push_back(ActGroup{d2.in_act, NoAction});
    d2.used = true;
    if (d2.state == Dependency::FIRST)
      break;
    assert(d2.state == Dependency::MIDDLE);
  }
}

void CollectiveEngine::_create_collective(
    commind_t comm, const std::vector<std::shared_ptr<AbstractCS>> &sequences,
    std::string prefix) {
  const auto &c = engine.comm_pool.at(comm);
  const resind_t comm_size = (resind_t)c.cores.size();

  deps_per_rank = 0;
  std::vector<ActGroup> snd_acts, rcv_acts;
  // Init the dependencies from the node
  for (auto &round : sequences) {
    deps_per_rank = std::max(deps_per_rank, round->deps_per_rank);
  }
  deps.resize(comm_size * deps_per_rank);
  for (auto &d : deps) {
    d.state = Dependency::INVALID;
  }
  indices.resize(comm_size);
  for (resind_t rank = 0; rank < comm_size; rank++) {
    indices[rank].start = rank * deps_per_rank;
    indices[rank].size = indices[rank].next_offset = 0;
    auto &n = engine.cores[c.cores[rank]];
    Dependency::State dep_state =
        (n.last_blocking_actions[1] != NoAction ? Dependency::FIRST
                                                : Dependency::SINGLE);
    _push_dependency(rank, n.last_blocking_actions[0], std::string::npos,
                     dep_state, n.delay_from_last, c.job_id);
    if (n.last_blocking_actions[1] != NoAction)
      _push_dependency(rank, n.last_blocking_actions[1], std::string::npos,
                       Dependency::LAST, n.delay_from_last, c.job_id);
  }

  LOG_VERBOSE << " At collective start:" << std::endl;
  this->print_deps();

  simtime_t snd_delay, rcv_delay;
  int round_index = 0;
  for (auto &round : sequences) {
    AtomicAction::DebugInfo dbg(round->dbg);
    round_index++;
#ifdef SHOW_DEBUG
    std::stringstream ss;
    ss << prefix << ":" << round_index;
#endif
    SequenceIndex index = round->init_index(round_index);
    AbstractCS::CommParams params;
    do {
      params = round->f(index);
      LOG_VERBOSE << "Index " << index << " => " << params << std::endl;
      if (params) {
        // Create the communication
        assert(params.snd_rank < comm_size);
        resind_t snd_node = c.cores[params.snd_rank];
        assert(params.snd_dep_offset > 0);
        _use_dependency(snd_acts, params.snd_rank, params.snd_dep_offset,
                        snd_delay);
        assert(snd_acts.size() >= 1);
        assert(params.rcv_rank < comm_size);
        resind_t rcv_node = c.cores[params.rcv_rank];
        assert(params.rcv_dep_offset > 0);
        _use_dependency(rcv_acts, params.rcv_rank, params.rcv_dep_offset,
                        rcv_delay);
        assert(rcv_acts.size() >= 1);
        actind_t transfer_act = NoAction;
        actind_t receiver_act = NoAction;
#ifdef SHOW_DEBUG
        dbg.desc = ss.str();
        dbg.index = index;
#endif
        engine.enqueue_raw_comm(snd_node, rcv_node, params.size, snd_acts[0],
                                rcv_acts[0], snd_delay, rcv_delay, transfer_act,
                                receiver_act, dbg);
        for (size_t i = 1; i < snd_acts.size(); i++)
          ctx.pool->add_child(engine.get_last_time(), snd_acts[i], transfer_act,
                              snd_delay, 1, CounterType::TO_SEND, false, true);
        for (size_t i = 1; i < rcv_acts.size(); i++)
          ctx.pool->add_child(engine.get_last_time(), rcv_acts[i], receiver_act,
                              rcv_delay, 1, CounterType::TO_RECV, false, true);

        assert(params.snd_rank != params.rcv_rank);
        auto snd_dep =
            _push_dependency(params.snd_rank, transfer_act, std::string::npos,
                             params.snd_dep_type, params.snd_delay, c.job_id);
        auto rcv_dep =
            _push_dependency(params.rcv_rank, receiver_act, std::string::npos,
                             params.rcv_dep_type, params.rcv_delay, c.job_id);
        if (transfer_act == receiver_act) {
          deps[snd_dep].other_dep = rcv_dep;
          deps[rcv_dep].other_dep = snd_dep;
        }
        this->print_deps();
      }
    } while (not round->advance(index));
  } // End of sequences

  // And set the last action for each node
  std::vector<actind_t> unused_acts;
  for (resind_t i_rank = 0; i_rank < comm_size; i_rank++) {
    resind_t node_index = c.cores[i_rank];
    auto &ind = indices[i_rank];
    unused_acts.clear();
    bool used;
    size_t last_offset =
        diff_mod<size_t>(ind.next_offset, ind.size, deps_per_rank);
    while (ind.size) {
      _pop_dependency(unused_acts, i_rank, last_offset, c.job_id, used);
    }

    bool require_waitall = (unused_acts.size() > 2);
    if (not require_waitall) {
      assert(not unused_acts.empty());
      ActGroup act{unused_acts[0], NoAction};
      if (unused_acts.size() == 2)
        act[1] = unused_acts[1];
      engine.move_to_next_blocking(node_index, act, false);
    }
    for (auto index : unused_acts) {
      if (ctx.pool->at_const(index).state == AtomicAction::INITIALIZING)
        ctx.pool->seal_action(index, engine.get_last_time());
    }
    if (require_waitall) {
      std::string prefix2 = prefix + "_FINAL_WAITALL";
      AtomicAction::DebugInfo dbg = sequences.back()->dbg;
      engine._do_waitall((int)node_index, unused_acts, (int)unused_acts.size(),
                         prefix2.c_str(), true, dbg);
    }
  }
}
