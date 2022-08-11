// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "VEFParser.hpp"
#include "../ResourceManager.hpp"

int VEFParser::get_or_create_trace_group(const char *trace_filename,
                                         const char *traffic_filename,
                                         int &number_of_traces) {
  std::string source(trace_filename);
  int group_id = AbstractParser::get_or_create_trace_group(
      trace_filename, traffic_filename, number_of_traces);
  if (group_id >= 0)
    return group_id;

  // The last element has been partially initialized
  group_id = groups.size() - 1;
  auto &group = groups.back();

  // Open the list file and open each process file
  LOG_INFO << "Creating stream group for " << source << "..." << std::endl;

  try {
    group.file = std::shared_ptr<std::istream>(new std::ifstream(source));
  } catch (std::exception &e) {
    std::stringstream ss;
    ss << "Could not open VEF trace <" << source
       << "> because opening failed because " << e.what();
    throw std::invalid_argument(ss.str());
    return 0;
  }
  if (not group.file->good()) {
    std::stringstream ss;
    ss << "Could not open VEF trace <" << source
       << "> because opening failed for some reason";
    throw std::invalid_argument(ss.str());
    return 0;
  }

  // Parse the header
  std::string tmp;
  std::getline(*(group.file), tmp);
  this->init_parsing(tmp.data());
  const char *ptr = this->get_next_token();
  assert(strncmp(ptr, "VEF", 3) == 0);
  ptr = this->get_next_token();
  group.nranks = std::atoi(ptr);
  ptr = this->get_next_token();
  group.num_msgs = std::strtoull(ptr, nullptr, 10);
  ptr = this->get_next_token();
  group.num_comms = std::atoi(ptr);
  ptr = this->get_next_token();
  group.num_global_collectives = std::strtoull(ptr, nullptr, 10);
  ptr = this->get_next_token();
  group.num_local_collectives = std::strtoull(ptr, nullptr, 10);
  ptr = this->get_next_token();
  assert(strncmp(ptr, "TRUE", 4) == 0);
  this->check_last_token();

  // Parse communicators
  // and save them to comm_ranks
  for (int i = 0; i < group.num_comms; i++) {
    std::getline(*(group.file), tmp);
    this->init_parsing(tmp.data());
    ptr = this->get_next_token();
    assert(*ptr == 'C');
    ptr++;
    // Assume communicator IDs start from 0 and increment
    assert(atoi(ptr) == i);
    group.ranks.push_back(std::vector<resind_t>());
    auto &ranks = group.ranks.back();
    ptr = this->get_next_token();
    ranks.push_back(atoi(ptr));
    while ((ptr = this->try_next_token()) != nullptr) {
      ranks.push_back(atoi(ptr));
    }
    // Expect first communicator to be world
    assert(i != 0 || ranks.size() == (size_t)group.nranks);
  }

  // Remember where to re-start the trace
  group.trace_start = group.file->tellg();
  return group_id;
}

// Return if there are more events in the trace
bool VEFParser::parse() {
  int total_remaining_streams = 0;
  std::string tmp;
  for (auto &g : groups) {
    if (not g.active || not g.file->good())
      continue;

    bool abort_job = false;
    bool continue_stream = (g.epoch == ctx.engine->get_comm_epoch(g.comms[0]));
    int stream_line_count = 0;
    while (continue_stream && not abort_job) {
      std::getline(*(g.file), tmp);
      if (not g.file->good()) {
        break;
      }
      g.n_lines_read++;
      stream_line_count++;
      INCREMENT_STAT(parsed_lines);

      // TODO: Do the parsing

      if (continue_stream && stream_line_count >= (int)MaxLinesByParsingPass)
        continue_stream = false;
      if (not ctx.resource_manager->is_job_running(g.job_id))
        abort_job = true;
    }
    if (abort_job)
      break;
    LOG_DEBUG << "Parsed " << stream_line_count << " entries (total "
              << g.n_lines_read << ") for job :" << g.job_id << std::endl;
    total_remaining_streams++;
  }
  return total_remaining_streams > 0;
}
actind_t VEFParser::get_dep(VEFGroup &g, DepType type, const char *ptr) {
  size_t dep_index;
  switch (type) {
  case DepType::INDEPENDENT:
    return NoAction;
  case DepType::COLLECTIVE:
    assert(*ptr == 'G');
    ptr++;
    dep_index = atoi(ptr);
    // We assume that any message will only depend upon the last completely
    // parsed collective i.e. no non-blocking collective
    assert(
        dep_index + 1 == g.collectives.size() ||
        (dep_index + 2 == g.collectives.size() && g.collectives.back() >= 0));
    // If this is true, we just need to depend upon the last blocking action
    return NoAction;
  case DepType::SEND:
    dep_index = atoi(ptr);
    assert(dep_index < g.messages.size());
    return g.messages[dep_index][0];
  case DepType::RECEIVE:
    dep_index = atoi(ptr);
    assert(dep_index < g.messages.size());
    return g.messages[dep_index][1];
  }
  MUST_DIE
}

static simtime_t get_delay(const char *ptr, double compute_time_factor) {
  double delay = atof(ptr) * compute_time_factor * 1e-9;
  return double_to_simtime(delay);
}

bool VEFParser::parse_line(VEFGroup &g, std::string &tmp) {

  if (tmp.empty())
    return false;

  const char *ptr = init_parsing(tmp.data());
  // Get ID
  ptr = get_next_token();

  if (*ptr != 'G') {
    // This is a P2P line: ID src dst tam Dep time IDdep
    assert(atoi(ptr) == (int)g.messages.size());
    ptr = get_next_token();
    int src_rank = atoi(ptr);
    assert(src_rank < g.nranks);
    resind_t src_core_id = g.cores[src_rank];
    ptr = get_next_token();
    int dst_rank = atoi(ptr);
    assert(dst_rank < g.nranks);
    resind_t dst_core_id = g.cores[dst_rank];
    ptr = get_next_token();
    share_t send_size = (share_t)strtoull(ptr, nullptr, 10);
    ptr = get_next_token();
    DepType dep_type = (DepType)atoi(ptr);
    ptr = get_next_token();
    simtime_t delay = get_delay(ptr, g.compute_time_factor);
    ptr = get_next_token();
    actind_t dep_act = get_dep(g, dep_type, ptr);
    check_last_token();
    AtomicAction::DebugInfo dbg(0, g.n_lines_read, g.epoch);

    actind_t transfer_act = NoAction;
    actind_t receiver_act = NoAction;
    ctx.engine->enqueue_raw_comm(src_core_id, dst_core_id, send_size,
                                 {dep_act, NoAction}, {NoAction, NoAction},
                                 delay, END_OF_TIME, transfer_act, receiver_act,
                                 dbg);
    g.messages.push_back({transfer_act, receiver_act});
    return true;
  }

  // This is a Collective line: ID ID_COMM ID_OP task send_size recv_size Dep
  // time IDdep
  ptr++;
  int coll_id = atoi(ptr);
  assert(coll_id <= (int)g.collectives.size());
  ptr = get_next_token();
  int comm_index = atoi(ptr);
  assert(comm_index < (int)g.comms.size());
  commind_t comm = g.comms[comm_index];
  ptr = get_next_token();
  CollectiveType coll_type = (CollectiveType)atoi(ptr);
  ptr = get_next_token();
  int rank = atoi(ptr);
  ptr = get_next_token();
  share_t send_size = (share_t)strtoull(ptr, nullptr, 10);
  ptr = get_next_token();
  share_t recv_size = (share_t)strtoull(ptr, nullptr, 10);
  ptr = get_next_token();
  DepType dep_type = (DepType)atoi(ptr);
  ptr = get_next_token();
  simtime_t delay = get_delay(ptr, g.compute_time_factor);
  ptr = get_next_token();
  actind_t dep_act = get_dep(g, dep_type, ptr);
  check_last_token();
  AtomicAction::DebugInfo dbg(0, g.n_lines_read, g.epoch);

  if (dep_act != NoAction) {
    resind_t core_id = g.get_core_id(comm_index, rank);
    ctx.engine->enqueue_compute(core_id, delay);
    ctx.engine->move_to_next_blocking(core_id, {dep_act, NoAction}, true);
  }

  if (coll_id == (int)g.collectives.size()) {
    g.collectives.push_back(0);
  }
  int &root_index = g.collectives[coll_id];
  assert(root_index >= 0);
  bool done = false;
  switch (coll_type) {
  case CollectiveType::BROADCAST:
    done = ctx.engine->enqueue_barrier(comm, dbg);
    break;
  case CollectiveType::REDUCE:
    if (recv_size) {
      root_index = rank;
      send_size = recv_size;
    }
    done = ctx.engine->enqueue_reduce(comm, root_index, send_size, 0, dbg);
    break;
  case CollectiveType::ALLREDUCE:
    assert(send_size == recv_size);
    done = ctx.engine->enqueue_allreduce(comm, send_size, 0, dbg);
    break;
  case CollectiveType::GATHER:
    NOT_IMPLEMENTED_YET
  case CollectiveType::SCATTER:
    NOT_IMPLEMENTED_YET
  case CollectiveType::ALLGATHER:
    done = ctx.engine->enqueue_allgather(comm, send_size, dbg);
    break;
  case CollectiveType::BARRIER:
    done = ctx.engine->enqueue_barrier(comm, dbg);
    break;
  case CollectiveType::ALLTOALL:
    if (recv_size == 1)
      send_size /= g.get_comm_size(comm_index);
    done = ctx.engine->enqueue_alltoall(comm, send_size, dbg);
    break;
  default:
    MUST_DIE
  }
  if (not done)
    return false;
  root_index = -1;
  return true;
}
