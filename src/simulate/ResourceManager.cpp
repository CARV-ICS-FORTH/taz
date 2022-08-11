// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "ResourceManager.hpp"
#include "resource_management/BackfillingScheduler.hpp"
#include "resource_management/FCFSScheduler.hpp"
#include "resource_management/FixedMapper.hpp"
#include "resource_management/LinearMapper.hpp"
#include "resource_management/LinearVetoMapper.hpp"
#include "resource_management/TrafficMapper.hpp"

std::mt19937 mt19937_gen_jobs_interarrival;

extern std::ofstream jobs_file;

void ResourceManager::_update_event(simtime_t now, simtime_t update_time,
                                    bool force_progress) {
  simtime_t schedule_period =
      double_to_simtime(ctx.vm["resource_scheduler_period"].as<double>());
  if (force_progress && update_time <= now)
    update_time = now + 1;
  simtime_t new_time =
      ((update_time + schedule_period - 1) / schedule_period) * schedule_period;
  if (not event_handle.check_liveness()) {
    event_handle =
        ctx.pool->push_event(new_time, 0, EventHeapElement::JOB_SCHEDULE);
    next_schedule_time = new_time;
    return;
  }
  // If we already have a schedule round planned before, no need to change the
  // event
  if (not force_progress && next_schedule_time < new_time)
    return;
  ctx.pool->update_event(event_handle, new_time, 0,
                         EventHeapElement::JOB_SCHEDULE);
  next_schedule_time = new_time;
}

void ResourceManager::_instantiate_handlers() {
  assert(schedulers.empty());
  assert(mapper == nullptr);

  std::string scheduler_name_list =
      ctx.vm["resource_scheduler"].as<std::string>();
  size_t start_pos = 0;
  bool last_token = false;
  while (not last_token) {
    auto end_pos = scheduler_name_list.find('|', start_pos);
    last_token = (end_pos == std::string::npos);
    std::size_t ssize = last_token ? std::string::npos : end_pos - start_pos;
    std::string scheduler_name = scheduler_name_list.substr(start_pos, ssize);
    start_pos = end_pos + 1;
    if (scheduler_name == "fcfs")
      schedulers.push_back(new FCFSScheduler);
    else if (scheduler_name == "backfilling")
      schedulers.push_back(new BackfillingScheduler);
    else {
      LOG_ERROR << "I do not know scheduler <" << scheduler_name << ">. Abort!"
                << std::endl;
      MUST_DIE
    }
  }

  std::string mapper_name = ctx.vm["resource_mapper"].as<std::string>();
  if (mapper_name == "linear")
    mapper = new LinearMapper;
  else if (mapper_name == "linear_veto")
    mapper = new LinearVetoMapper;
  else if (mapper_name == "traffic" || mapper_name == "traffic_uniform")
    mapper = new TrafficMapper;
  else if (mapper_name == "fixed")
    mapper = new FixedMapper;
  else {
    LOG_ERROR << "I do not know mapper <" << mapper_name << ">. Abort!"
              << std::endl;
    MUST_DIE
  }
}

ResourceManager::~ResourceManager() {
  for (auto s : schedulers)
    delete s;
  schedulers.clear();
  if (mapper)
    delete mapper;
  mapper = nullptr;
}

int ResourceManager::push_job(ParserCollection::TraceType trace_type,
                              double availability_time,
                              double interarrival_time, double rel_nominal_time,
                              double walltime, int n_iterations,
                              double compute_time_factor, int ranks_per_host,
                              std::string job_source,
                              std::string traffic_source,
                              std::string mapping_source, simtime_t now) {
  simtime_t avail = double_to_simtime(availability_time);
  assert(avail >= last_job_availability);
  last_job_id++;
  last_job_availability = avail;
  double interarrival_rate = interarrival_time ? 1.0 / interarrival_time : 0;
  JobDescriptor desc;
  desc.trace_type = trace_type;
  desc.availability = avail;
  desc.compute_time_factor = compute_time_factor;
  desc.ranks_per_host = ranks_per_host;
  desc.job_source = job_source;
  desc.traffic_source = traffic_source;
  desc.mapping_source = mapping_source;
  desc.interarrival_rate = interarrival_rate;
  desc.job_id = last_job_id;
  desc.n_iterations = n_iterations;
  desc.rel_nominal_time = double_to_simtime(rel_nominal_time);
  desc.rel_wall_time = double_to_simtime(walltime);
  for (auto s : schedulers)
    s->on_new_job(desc, now);
  mapper->on_new_job(desc, now);
  job_queue.push(desc);
  _update_event(now, avail);
  return last_job_id;
}

bool ResourceManager::do_schedule(simtime_t now) {
  event_handle.free();
  bool parse_only = ctx.vm["parse_only"].as<bool>();
  assert(not parse_only or job_queue.size() == 1);
  std::vector<resind_t> cores_mapping;
  size_t scheduler_index = 0;

  if (not job_queue_is_empty())
    LOG_VERBOSE << "@" << simtime_to_double(now)
                << ", scheduling available jobs: "
                << this->get_available_jobs(now) << std::endl;

  while (scheduler_index < schedulers.size() && not job_queue.empty() &&
         (job_queue.top().availability <= now || parse_only)) {
    // Do the scheduling bit
    bool peek_top = false;
    auto job_handle =
        schedulers[scheduler_index]->select_next_job(job_queue, peek_top, now);
    if (not peek_top && job_handle == JobHeap::handle_type()) {
      scheduler_index++;
      continue;
    }
    auto &job = peek_top ? job_queue.top() : *job_handle;

    // Create trace group and perform a few checks
    int number_of_traces = -1;
    size_t group_id = ctx.parsers->get_or_create_trace_group(
        job.trace_type, job.job_source.c_str(), job.traffic_source.c_str(),
        number_of_traces);
    if (number_of_traces > (int)n_cores) {
      std::stringstream ss;
      ss << "Trace <" << job.job_source.c_str()
         << "> requires more node than the platform can offer ("
         << number_of_traces << " > " << n_cores << ")" << std::endl;
      throw std::invalid_argument(ss.str());
      return 0;
    }
    // Now can we find a mapping for this job?
    double expected_duration =
        job.rel_nominal_time ? simtime_to_double(job.rel_nominal_time) : 3600.0;
    resind_t required_cores = number_of_traces;
    resind_t required_hosts = ceil_div((int)required_cores, job.ranks_per_host);
    auto hosts_alloc = mapper->map_job(job.job_id, job, now, required_hosts,
                                       expected_duration);
    if (hosts_alloc.empty()) {
      if (schedulers[scheduler_index]->on_mapping_failed(job_handle))
        scheduler_index++;
      auto job_copy = job;
      job_copy.nhosts = required_hosts;
      if (peek_top) {
        // Dirty trick because I cannot get to modify top ?!
        job_queue.pop();
        job_queue.push(job_copy);
      } else
        job_queue.update(job_handle, job_copy);
      continue;
    }

    // We did find an adequate interval set, let's get the party started.
    auto desc = job;
    desc.start_time = now;
    desc.nhosts = required_hosts;
    desc.stream_group_index = group_id;
    desc.hosts = hosts_alloc;
    cores_mapping.clear();
    cores_mapping.reserve(required_cores);
    resind_t remaining_ranks = required_cores;
    // Compute core allocation (from host allocation)
    resind_t max_ranks_per_region =
        ceil_div(job.ranks_per_host, (int)NumberOfRegionsPerHost);
    for (auto &inter : hosts_alloc) {
      for (resind_t h = inter.lower(); boost::icl::contains(inter, h); h++) {
        for (resind_t r = 0; r < NumberOfRegionsPerHost; r++) {
          CoreId id_start(h, r, 0);
          resind_t nranks_in_region =
              std::min(max_ranks_per_region, remaining_ranks);
          remaining_ranks -= nranks_in_region;
          for (resind_t i = 0; i < nranks_in_region; i++)
            cores_mapping.push_back(id_start.get_core_id() + i);
        }
      }
    }
    if (ctx.vm["rm_dump_job_mappings"].as<bool>()) {
      std::stringstream ss;
      ss << ctx.vm["stats_folder"].as<std::string>() << "/mapping_"
         << job.job_id << ".tazm";
      ApproxMatrix mat;
      mat.init(1, required_cores);
      for (size_t i = 0; i < required_cores; i++) {
        mat.add_weight(0, i, cores_mapping[i]);
      }
      mat.insert_property("job_source", job.job_source);
      mat.insert_property("traffic_source", job.traffic_source);
      mat.insert_property("mapping_source", job.mapping_source);
      mat.insert_property("n_iterations", (int64_t)job.n_iterations);
      mat.insert_property(
          "nominal_time",
          simtime_to_double(job.start_time + job.rel_nominal_time));
      mat.insert_property("compute_time_factor", job.compute_time_factor);
      mat.insert_property("availability", simtime_to_double(job.availability));
      mat.insert_property("start_time", simtime_to_double(job.start_time));
      mat.insert_property("job_id", (int64_t)job.job_id);
      mat.save(ss.str(), "mapping", ApproxMatrix::EXACT);
    }

    LOG_INFO << "At time " << simtime_to_double(now) << ", allocate hosts "
             << hosts_alloc << " and group "
             << ctx.parsers->get_group_pretty_string(group_id) << " for job "
             << desc.job_id << std::endl;
    if (ctx.vm["rm_only"].as<bool>()) {
      // Just queue an event for the job to complete at nominal time
      simtime_t finish_time = now + desc.rel_nominal_time;
      job_event_handles[desc.job_id] =
          ctx.pool->push_event(now + desc.rel_nominal_time, desc.job_id,
                               EventHeapElement::JOB_COMPLETE);
      LOG_INFO << "Resource management mode only: job " << desc.job_id
               << " will finish at time " << simtime_to_double(finish_time)
               << std::endl;
    } else {
      ctx.parsers->init_job(now, now + desc.rel_wall_time,
                            desc.compute_time_factor, desc.job_id, group_id,
                            cores_mapping, desc.n_iterations);
    }
    ongoing_jobs.insert(std::make_pair(desc.job_id, desc));
    for (auto s : schedulers)
      s->on_mapping_succeeded(job_handle, desc.job_id, now + desc.rel_wall_time,
                              desc.nhosts);
    if (peek_top)
      job_queue.pop();
    else
      job_queue.erase(job_handle);
  }

  if (job_queue.empty()) {
    event_handle.free();
    next_schedule_time = END_OF_TIME;
    return true;
  }

  _update_event(now, job_queue.top().availability, true);
  return false;
}

void ResourceManager::on_job_finished(int job_id, simtime_t time,
                                      JobFinishType type) {
  auto it = ongoing_jobs.find(job_id);
  if (it == ongoing_jobs.end())
    ctx.pool->show_events();
  assert(it != ongoing_jobs.end());
  for (auto s : schedulers)
    s->on_job_finished(job_id, it->second, time, type);
  mapper->on_job_finished(job_id, it->second, time, type);
  LOG_INFO << "At time " << simtime_to_double(time) << ", de-allocate hosts "
           << it->second.hosts << " and group "
           << ctx.parsers->get_group_pretty_string(
                  it->second.stream_group_index)
           << " since job " << job_id << " has finished because " << type
           << std::endl;

  ctx.jobs_file << job_id << "," << type << ","
                << simtime_to_double(it->second.availability) << ","
                << it->second.hosts.size() << ","
                << simtime_to_double(it->second.start_time) << ","
                << simtime_to_double(time) << ","
                << simtime_to_double(it->second.start_time +
                                     it->second.rel_nominal_time)
                << ",";

  // Add a row to the jobs file
  if (type == JobFinishType::COMPLETION) {
    // id,success,start,end,slowdown,accounted
    double slowdown = ((time - it->second.start_time) * 100.0) /
                          (double)it->second.rel_nominal_time -
                      100.0;
    simtime_t warmup_period =
        double_to_simtime(ctx.vm["warmup_period"].as<double>());
    simtime_t drain_period =
        double_to_simtime(ctx.vm["drain_period"].as<double>());
    simtime_t endtime = double_to_simtime(ctx.vm["endtime"].as<double>());
    simtime_t account_start_time = std::min(warmup_period, endtime);
    simtime_t account_end_time =
        std::max(account_start_time, endtime - drain_period);

    simtime_t s = std::max(account_start_time, it->second.start_time);
    simtime_t e = std::min(account_end_time, time);
    double accounted = simtime_to_double(e - s);

    ctx.jobs_file << slowdown << "," << accounted << std::endl;
  } else {
    ctx.jobs_file << "," << std::endl;
  }

  if (ctx.vm["rm_only"].as<bool>()) {
    auto it_handle = job_event_handles.find(job_id);
    assert(it_handle != job_event_handles.end());
    job_event_handles.erase(it_handle);
  } else
    ctx.parsers->finalize_job(time, job_id, it->second.stream_group_index,
                              type);

  if (it->second.interarrival_rate > 0) {
    // Re push job after an exponenatially-distributed interarrival delay
    std::exponential_distribution<double> distr(it->second.interarrival_rate);
    double interarrival = distr(mt19937_gen_jobs_interarrival);
    double schedule_period = ctx.vm["resource_scheduler_period"].as<double>();
    if (interarrival < schedule_period)
      interarrival = schedule_period;
    auto desc = it->second;
    desc.availability = time + double_to_simtime(interarrival);
    desc.stream_group_index = std::numeric_limits<size_t>::max();
    last_job_id++;
    desc.job_id = last_job_id;
    _update_event(time, desc.availability);
    job_queue.push(desc);
  }

  ongoing_jobs.erase(it);
}

void ResourceManager::on_host_state_change(resind_t host_index, simtime_t now,
                                           bool is_faulty) {
  for (auto s : schedulers)
    s->on_host_state_change(host_index, now, is_faulty);
  mapper->on_host_state_change(host_index, now, is_faulty);
}
