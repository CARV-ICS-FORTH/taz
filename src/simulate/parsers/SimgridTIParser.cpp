// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "SimgridTIParser.hpp"
#include "../ResourceManager.hpp"

constexpr int NSupportedDataTypes = 25;
constexpr int DataTypeSize[NSupportedDataTypes] = {
    sizeof(double),         sizeof(int),
    sizeof(char),           sizeof(short),
    sizeof(long),           sizeof(float),
    sizeof(int8_t),         sizeof(long long),
    sizeof(signed char),    sizeof(unsigned char),
    sizeof(unsigned short), sizeof(unsigned int),
    sizeof(unsigned long),  sizeof(unsigned long long),
    sizeof(long double),    sizeof(wchar_t),
    sizeof(bool),           sizeof(int8_t),
    sizeof(int16_t),        sizeof(int32_t),
    sizeof(int64_t),        sizeof(uint8_t),
    sizeof(uint16_t),       sizeof(uint32_t),
    sizeof(uint64_t)};

constexpr share_t get_size(int count, int type) {
  if (type == -1)
    return 1;
  if (type >= 0 && type < NSupportedDataTypes)
    return count * DataTypeSize[type];
  MUST_DIE
}

// Return if there are more events in the trace
bool SimgridTIParser::parse() {
  int total_remaining_streams = 0;
  for (auto &g : groups) {
    if (not g.active || g.remaining_streams == 0)
      continue;

    int rank = 0;
    bool abort_job = false;
    for (auto &stream : g.streams) {
      if (not stream.file->good()) {
        rank++;
        continue;
      }
      resind_t node = stream.node;
      int stream_line_count = 0;
      bool continue_stream =
          (stream.epoch == ctx.engine->get_comm_epoch(g.comms[0]));
      while (continue_stream && not abort_job) {
        stream.file->getline(tmp, 1024);
        if (not stream.file->good()) {
          g.remaining_streams--;
          break;
        }
        stream.n_lines_read++;
        stream_line_count++;
        INCREMENT_STAT(parsed_lines);
        ParseResult res = parse_line(rank, g);
        switch (res) {
        case ParseResult::CONTINUE:
          break;
        case ParseResult::PAUSE:
          continue_stream = false;
          break;
        case ParseResult::ENTER_ITERATION:
          if (stream.first_iteration) {
            stream.iterations_start = stream.file->tellg();
            stream.first_iteration = true;
          }
          if (stream.remaining_iterations >= 0) {
            if (stream.remaining_iterations == 0)
              skip_to_exit_iterations(stream, rank);
            else
              stream.remaining_iterations--;
          }
          break;
        case ParseResult::EXIT_ITERATIONS:
          if (stream.remaining_iterations > 0) {
            assert(not stream.first_iteration);
            // Rewind to iterations start if needed
            stream.remaining_iterations--;
            stream.file->seekg(stream.iterations_start);
          }
          break;
        }

        if (continue_stream &&
            not ctx.engine->is_core_blocking_simulation(node) &&
            stream_line_count >= (int)MaxLinesByParsingPass)
          continue_stream = false;
        if (not ctx.resource_manager->is_job_running(g.job_id))
          abort_job = true;
      }
      if (abort_job)
        break;
      LOG_DEBUG << "Parsed " << stream_line_count << " entries (total "
                << stream.n_lines_read << ") for rank:" << rank
                << "=node:" << node << std::endl;
      rank++;
    }
    total_remaining_streams += g.remaining_streams;
  }
  return total_remaining_streams > 0;
}

SimgridTIParser::ParseResult
SimgridTIParser::parse_line(int rank, SimgridTIGroup &group) {

  if (not strlen(tmp))
    return ParseResult::PAUSE;

  const char *ptr = init_parsing(tmp);
  assert(*ptr == '*' || rank == atoi(ptr));
  StreamEntry &stream = group.streams.at(rank);
  AtomicAction::DebugInfo dbg(rank, stream.n_lines_read, stream.epoch);
  // Get instruction
  ptr = get_next_token();

  if (strcmp(ptr, "compute") == 0) {
    start_profile_region("main_parsecompute");
    ptr = get_next_token();
    double time = atof(ptr);
    check_last_token();
    assert(group.compute_time_factor > 0);
    time *= group.compute_time_factor;
    // We expect that reallistically there should not be a period of more than
    // 100000 seconds of continuous computation without communication
    assert(time < 1e5);
    assert(time > 1e-15);
    // Add some variance to it
    double stdev = time / 10.0;
    std::normal_distribution<double> distribution(time, stdev);
    time = distribution(generator);
    group.total_compute_time += time;
    // time =
    //     std::max(time, (NODE_FLOPS * SoftLatency) /
    //     (double)SIMTIME_PER_SEC);
    ctx.engine->enqueue_compute(group.comms[0], rank, time);
    end_profile_region("main_parsecompute");
    return ParseResult::CONTINUE;
  }

  bool is_send = (strcmp(ptr, "send") == 0);
  bool is_isend = (strcmp(ptr, "isend") == 0);
  bool is_recv = (strcmp(ptr, "recv") == 0);
  bool is_irecv = (strcmp(ptr, "irecv") == 0);
  if (is_send || is_isend || is_recv || is_irecv) {
    start_profile_region("main_parsesendrecv");
    ptr = get_next_token();
    int dst_rank = atoi(ptr);
    assert(dst_rank < group.nranks);
    ptr = get_next_token();
    int tag = atoi(ptr);
    ptr = get_next_token();
    share_t count = atol(ptr);
    ptr = get_next_token();
    int type = atoi(ptr);
    share_t size = get_size(count, type);
    check_last_token();
    if (is_send || is_isend)
      ctx.engine->enqueue_send(group.comms[0], rank, dst_rank, size, tag,
                               is_send, dbg);
    else
      ctx.engine->enqueue_receive(group.comms[0], dst_rank, rank, size, tag,
                                  is_recv, dbg);
    end_profile_region("main_parsesendrecv");
    return ParseResult::CONTINUE;
  }

  if (strcmp(ptr, "sendRecv") == 0) {
    start_profile_region("main_parsesendAndRecv");
    ptr = get_next_token();
    share_t send_count = atol(ptr);
    ptr = get_next_token();
    int dst_rank = atoi(ptr);
    assert(dst_rank < group.nranks);
    ptr = get_next_token();
    share_t recv_count = atol(ptr);
    ptr = get_next_token();
    int src_rank = atoi(ptr);
    assert(src_rank < group.nranks);
    ptr = get_next_token();
    int send_type = atoi(ptr);
    share_t send_size = get_size(send_count, send_type);
    ptr = get_next_token();
    int recv_type = atoi(ptr);
    share_t recv_size = get_size(recv_count, recv_type);
    check_last_token();
    ctx.engine->enqueue_send_receive(group.comms[0], src_rank, rank, dst_rank,
                                     recv_size, send_size, 555, dbg);
    end_profile_region("main_parsesendAndRecv");
    return ParseResult::CONTINUE;
  }

  if (strcmp(ptr, "allreduce") == 0) {
    start_profile_region("main_parseallreduce");
    ptr = get_next_token();
    share_t send_count = atol(ptr);
    ptr = get_next_token();
    // This part is tricky, and depends on Simgrid version
    // In older version, it was not set (and this field was in fact the send
    // type) In newer version, this is the computation time and send type is
    // afterwards
    simtime_t reduce_time_i = 0;
    double reduce_time_f = atof(ptr);
    ptr = try_next_token();
    int send_type = 0;
    if (not ptr) {
      send_type = (int)reduce_time_f;
      reduce_time_i = ctx.vm["allreduce_latency"].as<simtime_t>();
    } else {
      send_type = atoi(ptr);
      reduce_time_i = double_to_simtime(reduce_time_f);
      check_last_token();
    }
    share_t send_size = get_size(send_count, send_type);
    bool ret = ctx.engine->enqueue_allreduce(group.comms[0], send_size,
                                             reduce_time_i, dbg);
    stream.epoch++;
    end_profile_region("main_parseallreduce");
    return ret ? ParseResult::CONTINUE : ParseResult::PAUSE;
  }

  if (strcmp(ptr, "allgather") == 0) {
    start_profile_region("main_parseallgather");
    ptr = get_next_token();
    share_t send_count = atol(ptr);
    ptr = get_next_token();
#ifndef NDEBUG
    share_t recv_count = atol(ptr);
#endif
    ptr = get_next_token();
    int send_type = atoi(ptr);
    share_t send_size = get_size(send_count, send_type);
    ptr = get_next_token();
#ifndef NDEBUG
    int recv_type = atoi(ptr);
    assert(send_size == get_size(recv_count, recv_type));
#endif
    check_last_token();
    bool ret = ctx.engine->enqueue_allgather(group.comms[0], send_size, dbg);
    stream.epoch++;
    end_profile_region("main_parseallgather");
    return ret ? ParseResult::CONTINUE : ParseResult::PAUSE;
  }
  if (strcmp(ptr, "alltoall") == 0) {
    start_profile_region("main_parsealltoall");
    ptr = get_next_token();
    share_t send_count = atol(ptr);
    ptr = get_next_token();
#ifndef NDEBUG
    share_t recv_count = atol(ptr);
#endif
    ptr = get_next_token();
    int send_type = atoi(ptr);
    share_t send_size = get_size(send_count, send_type);
    ptr = get_next_token();
#ifndef NDEBUG
    int recv_type = atoi(ptr);
    assert(send_size == get_size(recv_count, recv_type));
#endif
    check_last_token();
    bool ret = ctx.engine->enqueue_alltoall(group.comms[0], send_size, dbg);
    stream.epoch++;
    end_profile_region("main_parsealltoall");
    return ret ? ParseResult::CONTINUE : ParseResult::PAUSE;
  }
  if (strcmp(ptr, "wait") == 0) {
    start_profile_region("main_parsewait");
    ptr = get_next_token();
    ptr = get_next_token();
    ptr = get_next_token();
    check_last_token();
    ctx.engine->enqueue_waitall(group.comms[0], rank, 1, dbg);
    end_profile_region("main_parsewait");
    return ParseResult::CONTINUE;
  }
  if (strcmp(ptr, "waitall") == 0) {
    start_profile_region("main_parsewaitall");
    ptr = get_next_token();
    int n_requests = atoi(ptr);
    check_last_token();
    ctx.engine->enqueue_waitall(group.comms[0], rank, n_requests, dbg);
    end_profile_region("main_parsewaitall");
    return ParseResult::CONTINUE;
  }
  if (strcmp(ptr, "barrier") == 0) {
    start_profile_region("main_barrier");
    check_last_token();
    bool ret = ctx.engine->enqueue_barrier(group.comms[0], dbg);
    stream.epoch++;
    end_profile_region("main_barrier");
    return ret ? ParseResult::CONTINUE : ParseResult::PAUSE;
  }
  if (strcmp(ptr, "bcast") == 0) {
    start_profile_region("main_parsebcast");
    ptr = get_next_token();
    share_t send_count = atol(ptr);
    ptr = try_next_token();
    int root_rank = 0;
    int send_type = 0; // Default is either byte or double
    if (ptr) {
      root_rank = atoi(ptr);
      ptr = try_next_token();
      if (ptr) {
        send_type = atoi(ptr);
        check_last_token();
      }
    }
    share_t send_size = get_size(send_count, send_type);
    bool ret =
        ctx.engine->enqueue_bcast(group.comms[0], root_rank, send_size, dbg);
    stream.epoch++;
    end_profile_region("main_parsebcast");
    return ret ? ParseResult::CONTINUE : ParseResult::PAUSE;
  }
  if (strcmp(ptr, "reduce") == 0) {
    start_profile_region("main_parsereduce");
    ptr = get_next_token();
    share_t send_count = atol(ptr);
    ptr = get_next_token();
    double reduce_time = atof(ptr);
    ptr = try_next_token();
    int root_rank = 0;
    int send_type = 0;
    if (ptr) {
      root_rank = atoi(ptr);
      ptr = try_next_token();
      if (ptr) {
        send_type = atoi(ptr);
        check_last_token();
      }
    }
    share_t send_size = get_size(send_count, send_type);
    simtime_t reduce_time_i = double_to_simtime(reduce_time);
    bool ret = ctx.engine->enqueue_reduce(group.comms[0], root_rank, send_size,
                                          reduce_time_i, dbg);
    stream.epoch++;
    end_profile_region("main_parsereduce");
    return ret ? ParseResult::CONTINUE : ParseResult::PAUSE;
  }

  if (strcmp(ptr, "init") == 0) {
    // Just skip that for now
    check_last_token();
    bool ret = ctx.engine->enqueue_init(group.comms[0], dbg);
    stream.epoch++;
    return ret ? ParseResult::CONTINUE : ParseResult::PAUSE;
  }

  if (strcmp(ptr, "finalize") == 0) {
    // Just skip that for now
    check_last_token();
    bool ret = ctx.engine->enqueue_finalize(group.comms[0], group.job_id, dbg);
    stream.epoch++;
    return ret ? ParseResult::CONTINUE : ParseResult::PAUSE;
  }

  if (strcmp(ptr, "enter_iteration") == 0) {
    // Just skip that for now
    check_last_token();
    return ParseResult::ENTER_ITERATION;
  }

  if (strcmp(ptr, "exit_iterations") == 0) {
    // Just skip that for now
    check_last_token();
    return ParseResult::EXIT_ITERATIONS;
  }

  LOG_ERROR << "<" << ptr << "> is not a known trace primitive!" << std::endl;
  MUST_DIE;
  return ParseResult::PAUSE;
}

bool SimgridTIParser::_open_trace_file(SimgridTIGroup &group,
                                       std::string index_path,
                                       std::string filename, int rank) {
  assert(not filename.empty());
  if (filename[0] == '@') {
    filename = index_path + filename.substr(1, filename.size());
  }
  if (filename.back() == '\r')
    filename = filename.substr(0, filename.length() - 1);
  std::shared_ptr<std::istream> stream_ptr(new std::ifstream(filename));
  group.streams.push_back({0, 0, rank, NoResource, -1, true, -1, stream_ptr});
  // Make sure that the trace file can be opened properly
  if (not *(stream_ptr)) {
    std::stringstream ss;
    ss << "Could not open trace because opening " << rank << "-th trace file <"
       << filename << "> failed";
    throw std::invalid_argument(ss.str());
    return false;
  }
  group.total_lines_to_parse +=
      (size_t)std::count(std::istreambuf_iterator<char>(*(stream_ptr)),
                         std::istreambuf_iterator<char>(), '\n');
  return true;
}

void SimgridTIParser::_open_inline(SimgridTIGroup &group, const char *source,
                                   int &number_of_traces) {

  std::string source_str(source);
  auto end = source_str.find(';', 1);
  source_str.resize(end);
  number_of_traces = std::atoi(source_str.c_str() + 1);
  assert(number_of_traces > 0);
  source_str = source + end + 1;
  size_t lines = 0;
  std::replace_if(
      source_str.begin(), source_str.end(),
      [&lines](const char c) {
        if (c == ';') {
          lines++;
          return true;
        }
        return false;
      },
      '\n');
  group.total_lines_to_parse = lines * number_of_traces;
  for (int i = 0; i < number_of_traces; i++) {
    std::shared_ptr<std::istream> stream_ptr(new std::stringstream(source_str));
    group.streams.push_back({0, 0, i, NoResource, -1, true, -1, stream_ptr});
  }
}

int SimgridTIParser::get_or_create_trace_group(const char *trace_filename,
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
  group.job_source = source;
  group.expected_duration = -1.0;

  bool is_inline = (source[0] == ';');
  if (is_inline) {
    _open_inline(group, trace_filename, number_of_traces);
    return group_id;
  }
  std::ifstream index_file;
  is_good = false;
  try {
    index_file.open(source);
  } catch (std::exception &e) {
    std::stringstream ss;
    ss << "Could not open trace <" << source
       << "> because opening index file failed because " << e.what();
    throw std::invalid_argument(ss.str());
    return 0;
  }
  if (not index_file) {
    std::stringstream ss;
    ss << "Could not open trace <" << source
       << "> because opening index file failed for some reason";
    throw std::invalid_argument(ss.str());
    return 0;
  }

  std::string filename_str = source;
  size_t path_end = filename_str.find_last_of("/\\");
  std::string index_path = filename_str.substr(0, path_end + 1);

  int i = 0;
  do {
    index_file.getline(tmp, 1024);
    std::string tmp_str(tmp);
    if (not tmp_str.empty()) {
      int n_repeats = 1;
      if (tmp_str[0] == '[') {
        auto end = tmp_str.find(']', 0);
        assert(end != std::string::npos);
        std::string n_repeats_str = tmp_str.substr(1, end - 1);
        n_repeats = std::atoi(n_repeats_str.c_str());
        assert(n_repeats >= 1);
        tmp_str = tmp_str.substr(end + 1, std::string::npos);
      }
      for (int repeat = 0; repeat < n_repeats; repeat++) {
        if (not _open_trace_file(group, index_path, tmp_str, i))
          return -1;
        i++;
      }
    }
  } while (index_file.good());
  assert(i > 1);
  group.nranks = number_of_traces = i;
  is_good = true;
  LOG_INFO << "Opened " << i << " trace files" << std::endl;
  return group_id;
}

void SimgridTIParser::skip_to_exit_iterations(StreamEntry &stream, int rank) {
  (void)rank;
  const char *ptr = nullptr;
  do {
    stream.file->getline(tmp, 1024);
    assert(stream.file->good());
    if (not strlen(tmp))
      continue;

    ptr = init_parsing(tmp);
    assert(*ptr == '*' || rank == atoi(ptr));
    ptr = get_next_token();
  } while (strcmp(ptr, "exit_iterations") != 0);
}
