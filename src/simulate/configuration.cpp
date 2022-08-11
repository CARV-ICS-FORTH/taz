// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "ParserCollection.hpp"
#include "ResourceManager.hpp"
#include "Statistics.hpp"
#include "taz-simulate.hpp"

namespace po = boost::program_options;
using namespace std;

void show_parsed_options(const po::parsed_options &parsed_options) {
  for (auto o : parsed_options.options) {
    LOG_VERBOSE << o.string_key << ": ";
    int i = 0;
    for (auto v : o.value) {
      LOG_VERBOSE << i << "=" << v << " ";
      i++;
    }
    LOG_VERBOSE << endl;
  }
}

std::string process_filename(std::string filename) {
  if (filename[0] == '@') {
    std::string tmp = ROOT_FOLDER;
    tmp += "/";
    tmp += filename.substr(1);
    return tmp;
  }
  return filename;
}

void fill_job_options(po::parsed_options &parsed_options,
                      unordered_map<string, string> &job_options) {
  std::vector<po::option> new_options;
  LOG_VERBOSE << "Before:" << std::endl;
  show_parsed_options(parsed_options);

  for (size_t i = 0; i < parsed_options.options.size(); i++) {
    auto &opt = parsed_options.options[i];
    std::stringstream ss;
    if (opt.value.empty()) {
      new_options.push_back(opt);
      ss << "--" << opt.string_key;
      ctx.raw_arguments.push_back(ss.str());
      continue;
    }
    // Generate raw arguments to replay
    std::string v = opt.value[0];
    for (auto &c : v)
      if (c == '\\')
        c = '/';
    ss << "--" << opt.string_key << '=' << v;
    ctx.raw_arguments.push_back(ss.str());
    if (opt.string_key.substr(0, 4) != "job_") {
      new_options.push_back(opt);
      continue;
    }
    assert(opt.value.size() == 1);

    if (opt.string_key == "job_ti" || opt.string_key == "job_traffic" ||
        opt.string_key == "job_mapping")
      opt.value[0] = process_filename(opt.value[0]);

    // Defer job options until just before the job index file
    if (opt.string_key != "job_ti") {
      job_options[opt.string_key] = opt.value[0];
      continue;
    }
    for (auto &kv : job_options)
      new_options.emplace_back(po::option(kv.first, {kv.second}));
    new_options.push_back(opt);
  }
  parsed_options.options.swap(new_options);
  LOG_VERBOSE << "After:" << std::endl;
  show_parsed_options(parsed_options);
}

po::options_description get_options_description() {

  po::options_description global_desc("Global options");
  // clang-format off
  global_desc.add_options()
    ("help", "produce help message")
    ("conf,c",po::value<vector<string>>()->composing(),
      "Additional configuration files to be parsed AFTER "
      "the command line arguments" )
    ("cores_per_host_region", po::value<resind_t>()->default_value(1),
      "Number of cores in each Uniform Memory Access region")
    ("host_regions", po::value<resind_t>()->default_value(1),
      "Number of Uniform Memory Access regions in each host. Assuming 1, 2 or 4.")
    ("topology,t", po::value<string>()->required(),
      "The topology to simulate among "
      "TINY=16n|MEDIUM=1024n|LARGE=64Kn"
      "This does not include connections between host regions"
      " (which are generated automatically)."
      "Alternatively, one may describe a torus using this syntax:\n"
      "TORUS:i,j,k,l,..,n where i,j,k,l,..,n are sizes in the different torus dimensions.\n"
      "Instead a fat tree could be described with:\n"
      "FATTREE:n:c0,..,cn:p0,..pn:l0,..ln where:\n"
      " n is the number of switch levels \n"
      " c0..cn is the number of children of each switch, for each level.\n"
      " p0..pn is the number of parents for each switch/host, for each level.\n"
      " l0..ln is the number of links for each child-parent pair, for each level\n"
    )
    ("warmup_period", po::value<double>()->default_value(1000),
      "Job statistics are ignored until the end of the warmup time, in seconds.")
    ("drain_period", po::value<double>()->default_value(1000),
      "Job statistics are ignored after the start of the drain period, before endtime, in seconds.")
    ("endtime,e", po::value<double>()->default_value(3600),
      "The maximum time at which the simulation will end.")
    ("collect_traffic",po::value<bool>()->default_value(false),"If set to true,"
     " the size of each communication will be logged and a total aggregate matrix "
     "of the communications will be saved in the statistics folder, in the traffic.tazm file.")
    ("export_topology",po::value<bool>()->default_value(false),"If set to true,"
    " the adjacency matrix of the topology  will be saved in the statistics folder,"
    " in the topology.tazm file.");
  // clang-format on

  po::options_description job_desc("Job options");
  // clang-format off
  job_desc.add_options()
    ("job_ti,j",po::value<vector<string>>()->composing(),
      "The description of Time Independent (ti) applicative trace for a job."
      " The last value defined for job options before "
      " this argument are applied."
      "If the value starts with ';', this is a semicomma-separated list of trace elements."
      " Syntax is ;<number of processes>;<line 1>;<line2>;...;<lineN>.\n"
      "If the value starts with '#VEF#', the rest  is the path to a VEF trace file.\n"
      "If the value starts with '#TRF#', the rest is is ignored, "
      "and the traffic matrix is used to roughly model the app.\n"
      "Otherwise, the value is the path to the index file using the Simgrid TI format.")
    ("job_vef",po::value<vector<string>>()->composing(),
      "The description of VEF applicative trace for a job."
      " The last value defined for job options before "
      " this argument are applied."
      "The value is the path to the index file")
    ("job_trf",po::value<vector<string>>()->composing(),
      "The description of VEF applicative trace for a job."
      " The last value defined for job options before "
      " this argument are applied."
      "The value is the path to the index file")
    ("job_avail,a",po::value<vector<double>>()->composing(),
     "The time when the following job(s) will be available for scheduling")
    ("job_repeat",po::value<vector<double>>()->composing(),
     "If 0, job is NOT repeated. If positive, defines the delay "
     "between the end of a job instance to the availability of the next.")
    ("job_nominal",po::value<vector<double>>()->composing(),
      "The nominal spantime for this job, to compute system productivity")
    ("job_timeout",po::value<vector<double>>()->composing(),
      "The time after which the job is assumed crashed, "
      " and drained from the system")
    ("job_traffic",po::value<vector<string>>()->composing(),
      "The .tazm file containing the approximate traffic generated by this application."
      " This is required for each task to use traffic-aware mapping.")
    ("job_iterations",po::value<vector<int>>()->composing(),
      "For traces that support iteration, define how many iterations"
      " will be executed before proceeding to the epilog of the job. "
      "Negative values mean to just run the original trace.")
    ("job_ranks_per_host",po::value<vector<int>>()->composing(),
      "The maximum number of ranks per host for this job")
    ("job_compute_factor",po::value<vector<double>>()->composing(),
      "Apply factor to the compute times given in traces."
      " Can be used to compensate for tracing issues, or to "
      " artifically expand traces in time.")
      ("job_mapping",po::value<vector<string>>()->composing(),
      "If 'fixed' mapping is used, this option is mandatory. "
      "It contains the name of a tazm file containing a vector of 1 column and "
      "as many rows as processes in this job. Values are the hosts where to map each process."
      " If any host is already in use or faulty, the simulation behaviour is undefined.");
  // clang-format on

  po::options_description platform_desc("Platform options");
  // clang-format off
  platform_desc.add_options()
    ("link_capacity",po::value<uint64_t>()->default_value(10000000000 / 8),
      "The capacity of the interconnect links in Bytes per second")
    ("loopback_link_capacity",po::value<uint64_t>()->default_value(1000000000000 / 8),
      "The capacity of the processor-internal links in Bytes per second")
    ("link_concurrency",po::value<uint16_t>()->default_value(4096),
      "The number of flows that can go though at link at a given time."
      " For now, only the first link of the route is checked.")
    ("link_latency",po::value<uint64_t>()->default_value(1000),
      "The latency of the interconnect links in nanoseconds")
    ("loopback_link_latency",po::value<uint64_t>()->default_value(10),
      "The latency of the processor-internal links in nanoseconds")
    ("node_flops",po::value<uint64_t>()->default_value(100000000000),
      "The power of processors in FLOPS")
    ("soft_latency",po::value<uint64_t>()->default_value(50),
      "The minimum software latency inserted between actions, in nanoseconds")
    ("allreduce_latency",po::value<simtime_t>()->default_value(200),
      "The latency to perform a reduction, in nanoseconds")
  ;
  // clang-format on

  po::options_description resource_manager_desc("Resource Manager options");

  // clang-format off
  resource_manager_desc.add_options()
    ("resource_scheduler",po::value<std::string>()->default_value("fcfs"),
    "By default ('fcfs' policy), the processes are scheduled in the order of their availability.\n")
    ("resource_mapper",po::value<std::string>()->default_value("linear"),
    "By default ('linear' policy), the processes are mapped linearly to hosts of the system.\n"
    "If 'linear_veto' is selected, linear allocation is used but less "
    " reliable hosts are not used (c.f. mapping_reliability_target).\n"
    "If 'traffic_uniform' is selected, the allocation of jobs "
    "is is optimized for uniform traffic.\n"
    "If 'traffic' is selected, traffic information for the jobs "
    "is used to optimize mapping.\n"
    "If 'fixed' is selected, a predefined mapping is used for each job, c.f. --job_mapping.")
    ("resource_scheduler_period",po::value<double>()->default_value(30),
      "The period between two scheduling calls to the resource manager, in seconds.")
    ("rm_linear_veto_reliability_target",po::value<double>()->default_value(0.0),
      "When using 'linear_veto' mapping, this is the criteria to discard unreliable nodes."
      " Roughly, the Resource Manager tries to get the probability that the job "
      " completes to at least this target."
      "Hence 0 means we do not care about jobs failing, and 1 means we want "
      "to be sure the job will succceed, but this could prove difficult/impossible.")
    ("rm_traffic_scotch_map_strategy",po::value<std::string>()->default_value(""),
    "When using 'traffic' mapping, the strategy to use to map jobs onto the system topology")
    ("rm_traffic_scotch_bipart_strategy",po::value<std::string>()->default_value(""),
    "When using 'traffic' mapping, the strategy to use partition the system topology")
    ("rm_traffic_scotch_random_seed",po::value<int>(),
    "When using 'traffic' mapping, defines the random  seed to be used. "
    "If more than one SCOTCH thread is used, results may vary from run to run for a given seed. "
    "If this option is not defined, deterministic behaviour is used (even with multiple threads).")
    ("rm_traffic_scotch_nthreads",po::value<int>()->default_value(1),
    "When using 'traffic' mapping, the number of threads to be used to perform mapping. "
    "For reproductibility concerns, check rm_traffic_scotch_random_seed.")
    ("rm_dump_job_mappings",po::value<bool>()->default_value(false),
    "Dump each job core mapping to file mapping_XXX.tazm, where XXX is the job ID.")
    ;
  // clang-format on

  po::options_description sim_desc("Simulation internal options");
  // clang-format off
  sim_desc.add_options()
    ("share_unit",po::value<uint64_t>()->default_value(1),
      "The granularity to share resources")
    ("share_change_threshold",po::value<share_t>()->default_value(1),
      "Changes in share within +-share_change_threshold/128 "
      "will be ignored by the LMM model.")
    ("simtime_precision",po::value<uint64_t>()->default_value(50),
      "The time granularity to recompute sharings in the LMM model, in nanoseconds")
    ("model_gamma",po::value<double>()->default_value(1),
      "The gamma parameter")
    ("model_weight_s",po::value<double>()->default_value(1),
      "The Weight-S parameter")
    ("model_latency_factors",po::value<string>()->default_value("0:*1+0"),
      "The latency factors parameter." 
      " Syntax is <lower>:*<mult>+<add>,<lower>:*<mult>+<add>."
      " For instance, 0:*3.5+24,64:*1.01+52. Unit for add is nanoseconds.")
    ("model_bandwidth_factors",po::value<string>()->default_value("0:*1+16"),
      "The bandwidth factors parameter." 
      " Syntax is <lower>:*<mult>+<add>,<lower>:*<mult>+<add>." 
      " For instance, 0:*3.5+24,64:*1.01+52. Unit for add is bytes.")
    ("maximum_fanin_fanout",po::value<size_t>()->default_value(120),
      "Split large fan-ins and fan-out of action dependencies to this size.")
    ;
  // clang-format on

  po::options_description softcomm_desc("Software communication options");
  // clang-format off
  softcomm_desc.add_options()
    ("eager_threshold",po::value<share_t>()->default_value(256),
      "Messages smaller than the threshold use Eager protocol. Larger ones use Rendezvous.")
    ("forced_segsize",po::value<share_t>(),
      "When set, this is the segment size to use when the algorithm requires a segment size")
    ("forced_max_outstanding_reqs",po::value<int>(),
      "When set, maximum outstanding requests to use when the algorithm requires a maximum outstanding request number")  
    ("allreduce_alg",po::value<int>(),
      "Force the algorithm used for the AllReduce primitive")
    ("allgather_alg",po::value<int>(),
      "Force the algorithm used for the AllGather primitive")
    ("alltoall_alg",po::value<int>(),
      "Force the algorithm used for the Alltoall primitive")
    ("barrier_alg",po::value<int>(),
      "Force the algorithm used for the Barrier primitive")
    ("bcast_alg",po::value<int>(),
      "Force the algorithm used for the Broadcast primitive")
    ("reduce_alg",po::value<int>(),
      "Force the algorithm used for the Reduce primitive")
    ;
  // clang-format on

  po::options_description fault_desc("Fault injection options");
  // clang-format off
  fault_desc.add_options()
    ("node_fault_profile",po::value<std::vector<std::string>>(),
      "Optionally define a set a deterministic set of failure and recovery "
      "events for a given node. Use one argument per node."
      "Syntax is: <nodeindex>:<failtime1>-<recovertime1>:...:<failtimeN>~<recovertimeN>"
      " where <nodeindex> is the ID of the node, and <failtimeX> (resp. <recovertimeX>)"
      " are absolute times that must increase."
      " One must specify both failure and recovery time. For the later, "
      "'END' can be used to specify that resource will not recover during the simulation")    
    ("node_nominal_mtbf_log10",po::value<double>()->default_value(7.5),
      "The log10 of the nominal Mean Time Between Failures for hosts, in seconds."
      " This applies after the fault profile has finished")    
    ("node_nominal_mttr_log10",po::value<double>()->default_value(4),
      "The log10 of the nominal Mean Time To Repair for hosts, in seconds."
      " This applies after the fault profile has finished")    
    ("link_fault_profile",po::value<std::vector<std::string>>(),
      "Optionally define a set a deterministic set of failure and recovery "
      "events for a given link. Use one argument per link."
      "Syntax is: <linkindex>:<failtime1>-<recovertime1>:...:<failtimeN>~<recovertimeN>"
      " where <linkindex> is the ID of the link, and <failtimeX> (resp. <recovertimeX>)"
      " are absolute times that must increase."
      " One must specify both failure and recovery time. For the later, "
      "'END' can be used to specify that resource will not recover during the simulation")    
    ("link_nominal_mtbf_log10",po::value<double>()->default_value(8.5),
      "The log10 of the nominal Mean Time Between Failures for link, in seconds.")
    ("link_nominal_mttr_log10",po::value<double>()->default_value(4),
      "The log10 of the nominal Mean Time To Repair for links, in seconds."
      " This applies after the fault profile has finished")    
    ("mtbf_stdev_scale_log10",po::value<double>()->default_value(1.5),
      "The stdev used to pick the scale of link and nodes Weibul distribution.")
    ("mtbf_stdev_shape_log10",po::value<double>()->default_value(0.3),
      "The stdev used to pick the shape of link and nodes Weibul distribution.")
    ("mtbf_clamping_factor",po::value<double>()->default_value(3),
      "Clamping factor to compute actual Mean Time Between Failures,"
      " w.r.t. stddev.")
    ("mttr_stdev_log10",po::value<double>()->default_value(3),
      "The stdev used to pick the recovery times.")
    ("node_fault_random_seed",po::value<int>()->default_value(0),
      "The seed used to initiate the pseudo-random number generator "
      "used in node faults generation.")
    ("link_fault_random_seed",po::value<int>()->default_value(0),
      "The seed used to initiate the pseudo-random number generator "
      "used in link faults generation.")
      ("collect_state_changes",po::value<bool>()->default_value(false),
      "If true, a matrix state_changes.tazm is created."
      " The matrix has 2 columns, and as many rows as there are state changes."
      " Value in the first column is the time in seconds when the event took place."
      " Value in the second column is a bitfield:"
      " b62=0 -> fault, =1 -> recovery; b61=0 -> node, =1 -> link; b60-0 -> index of resource.");
  // clang-format on

  po::options_description debug_desc("Debug and reporting options");
  // clang-format off
  debug_desc.add_options()
      ("stats_folder",po::value<string>()->default_value("."),
      "The folder where resulting statistics and snapshots will be written.")
    ("stats_suffix",po::value<string>()->default_value("0"),
      "The suffix for resulting files.")
    ("stats_period",po::value<int>()->default_value(30),
      "Sampling period of statistics (and progress display), in seconds.")
    ("snapshots_type",po::value<int>()->default_value(0),
      "What type of snapshots of the action graph should we take?"
      " 0:No, 1:Skip invalids, 2:Print all, 3:Short, -1:Only stats")
    ("snapshots_format",po::value<std::string>()->default_value("dot"),
      "What format do we write snapshots to?"
      " dot or pydict ")
    ("snapshots_occurence",po::value<int>()->default_value(0),
      "When should we take snapshots of the action graph?"
      " Bitmask: 1:After parsing, 2: After each engine internal iteration"
      ", 4:After engine run, 8:On simulation end")
    ("inhibit_debug_messages",po::value<bool>()->default_value(false),
      "Should we inhibit debug messages?")
    ("inhibit_verbose_messages",po::value<bool>()->default_value(true),
      "Should we inhibit verbose messages?")
    ("rm_only",po::value<bool>()->default_value(false),
      "Only perform resource management (i.e. schedule and map)."
      "Each job completes at its nominal time")
    ("parse_only",po::value<bool>()->default_value(false),
      "Only parse traces (e.g. to capture snapshots)."
      "This only applies if rm_only is not set.");
  // clang-format on

  po::options_description desc("ILMM options");
  desc.add(global_desc)
      .add(job_desc)
      .add(platform_desc)
      .add(resource_manager_desc)
      .add(sim_desc)
      .add(softcomm_desc)
      .add(fault_desc)
      .add(debug_desc);
  return desc;
}

bool parse_configuration(const po::options_description &desc, int argc,
                         char *argv[]) {
  unordered_map<string, string> job_options_map;
  job_options_map.emplace("job_traffic", "");
  job_options_map.emplace("job_mapping", "");
  job_options_map.emplace("job_avail", "0");
  job_options_map.emplace("job_repeat", "0");
  job_options_map.emplace("job_nominal", "3600");
  job_options_map.emplace("job_timeout", "7200");
  job_options_map.emplace("job_iterations", "-1");
  job_options_map.emplace("job_ranks_per_host", "0");
  job_options_map.emplace("job_compute_factor", "1.0");
  job_options_map.emplace("job_mapping", "");

  LOG_DEBUG << "Parsing environment.." << std::endl;
  auto parsed_env_options = po::parse_environment(desc, "ILMM");
  fill_job_options(parsed_env_options, job_options_map);
  po::store(parsed_env_options, ctx.vm);

  LOG_DEBUG << "Parsing command line..." << std::endl;
  auto parsed_cmd_options = po::parse_command_line(argc, argv, desc);
  fill_job_options(parsed_cmd_options, job_options_map);
  po::store(parsed_cmd_options, ctx.vm);

  if (ctx.vm.count("conf")) {
    for (auto conf_file : ctx.vm["conf"].as<vector<string>>()) {
      conf_file = process_filename(conf_file);
      LOG_INFO << "Parsing configuration file <" << conf_file << ">..." << endl;
      auto parsed_file_options = po::parse_config_file(conf_file.c_str(), desc);
      fill_job_options(parsed_file_options, job_options_map);
      po::store(parsed_file_options, ctx.vm);
    }
  }

  if (ctx.vm.count("help")) {
    LOG_INFO << desc << "\n";
    return false;
  }

  po::notify(ctx.vm);

  /*
  if (ctx.vm.count("compression")) {
    LOG_INFO << "Compression level was set to "
             << ctx.vm["compression"].as<int>() << ".\n";
  } else {
    LOG_INFO << "Compression level was not set.\n";
  }
    */
  return true;
}

FaultProfileMap parse_fault_profiles(const char *argument_name) {
  FaultProfileMap res;
  if (ctx.vm.count(argument_name) == 0)
    return res;
  auto v = ctx.vm[argument_name].as<std::vector<std::string>>();
  GenericParser parser;
  for (auto s : v) {
    // Parse each entry
    const char *ptr = parser.init_parsing(s.data(), ":");
    resind_t id = std::atoi(ptr);
    assert(res.find(id) == res.end());
    std::vector<simtime_t> time_vector;
    for (;;) {
      // Parse each pair of times and append them to the vector
      ptr = parser.try_next_token("~");
      if (ptr == nullptr)
        break;
      double failure_time_f = std::atof(ptr);
      simtime_t failure_time = double_to_simtime(failure_time_f);
      ptr = parser.get_next_token(":");
      simtime_t recovery_time = END_OF_TIME;
      if (std::strcmp(ptr, "END") != 0) {
        double recovery_time_f = std::atof(ptr);
        recovery_time = double_to_simtime(recovery_time_f);
      }
      assert(time_vector.empty() || time_vector.back() <= failure_time);
      assert(failure_time <= recovery_time);
      time_vector.push_back(failure_time);
      time_vector.push_back(recovery_time);
    }
    // Mirror the vector
    const size_t N = time_vector.size();
    std::vector<simtime_t> time_vector_reverse(N, 0);
    for (size_t i = 0; i < N; i++)
      time_vector_reverse[i] = time_vector[N - i - 1];
    res.insert({id, time_vector_reverse});
  }
  return res;
}

bool configure(int argc, char *argv[]) {
  po::options_description desc = get_options_description();
  bool parsing_success = false;
  try {
    parsing_success = parse_configuration(desc, argc, argv);
  } catch (std::exception &e) {
    LOG_ERROR << "Arguments parsing failed because " << e.what() << " !"
              << std::endl;
  }
  if (not parsing_success)
    return false;

  SnapshotsType = ctx.vm["snapshots_type"].as<int>();
  SnapshotsOccurence = ctx.vm["snapshots_occurence"].as<int>();
#ifdef SHOW_DEBUG
  InhibitDebugMessages = ctx.vm["inhibit_debug_messages"].as<bool>();
  InhibitVerboseMessages = ctx.vm["inhibit_verbose_messages"].as<bool>();
#endif
  SimtimePrecision = ctx.vm["simtime_precision"].as<simtime_t>();
  SoftLatency = ctx.vm["soft_latency"].as<simtime_t>();
  ShareChange = ctx.vm["share_change_threshold"].as<share_t>();
  EagerThreshold = ctx.vm["eager_threshold"].as<share_t>();
  MaximumFaninFanout = ctx.vm["maximum_fanin_fanout"].as<share_t>();
  NumberOfRegionsPerHost = ctx.vm["host_regions"].as<resind_t>();
  NumberOfCoresPerRegion = ctx.vm["cores_per_host_region"].as<resind_t>();
  ctx.bw_factor.parse(ctx.vm["model_bandwidth_factors"].as<string>());
  ctx.lat_factor.parse(ctx.vm["model_latency_factors"].as<string>());

  // Make sure that one action will not propagate to other actions within the
  // same sim. cycle
  assert(SoftLatency >= SimtimePrecision);
  // Minimum link delay (i.e. SOFT_latency) must be larger than simulation
  // precision

  std::stringstream stats_filename;
  std::stringstream jobs_filename;
  stats_filename << ctx.vm["stats_folder"].as<string>() << "/stats_"
                 << ctx.vm["stats_suffix"].as<string>() << ".csv";
  jobs_filename << ctx.vm["stats_folder"].as<string>() << "/jobs_"
                << ctx.vm["stats_suffix"].as<string>() << ".csv";

  ctx.tmp_stats = new Statistics();
  ctx.out_stats = new Statistics();
  ctx.total_stats = new Statistics();
  ctx.stats_file.open(stats_filename.str().c_str());
  ctx.jobs_file.open(jobs_filename.str().c_str());
  Statistics::print_csv_header();
  std::string jobs_type;
  std::vector<resind_t> torus_dimensions;
  simtime_t max_simulation_time =
      double_to_simtime(ctx.vm["endtime"].as<double>());

  mt19937_gen_fault_nodes.seed(ctx.vm["node_fault_random_seed"].as<int>());
  mt19937_gen_fault_links.seed(ctx.vm["link_fault_random_seed"].as<int>());

  ctx.pool = AAPool::get_instance();
  ctx.pool->push_event(max_simulation_time, 0,
                       EventHeapElement::SIMULATION_END);
  ctx.parsers = new ParserCollection;
  ctx.model = new Model;
  auto node_fault_profiles = parse_fault_profiles("node_fault_profile");
  auto link_fault_profiles = parse_fault_profiles("link_fault_profile");
  ctx.engine = new Engine(node_fault_profiles, link_fault_profiles);
  NumberOfHosts = ctx.engine->get_number_of_hosts();
  ctx.resource_manager = new ResourceManager();

  /*  for (auto &x : ctx.vm) {
      LOG_DEBUG << "key:" << x.first << " empty" << x.second.empty()
                << " defaulted:" << x.second.defaulted() << std::endl;
    }*/
  if (ctx.vm.count("job_ti") == 0) {
    LOG_ERROR << "You did not define any job, aborting" << std::endl;
    LOG_INFO << desc << "\n";
    return false;
  }

  const vector<string> &ti_jobs = ctx.vm["job_ti"].as<vector<string>>();
  const vector<string> &ti_traffic_files =
      ctx.vm["job_traffic"].as<vector<string>>();
  const vector<string> &ti_mapping_files =
      ctx.vm["job_mapping"].as<vector<string>>();
  const vector<double> &avails = ctx.vm["job_avail"].as<vector<double>>();
  const vector<double> &repeats = ctx.vm["job_repeat"].as<vector<double>>();
  const vector<double> &nominals = ctx.vm["job_nominal"].as<vector<double>>();
  const vector<double> &timeouts = ctx.vm["job_timeout"].as<vector<double>>();
  const vector<int> &ranks_per_host =
      ctx.vm["job_ranks_per_host"].as<vector<int>>();
  const vector<int> &iterations = ctx.vm["job_iterations"].as<vector<int>>();
  const vector<double> &compute_factors =
      ctx.vm["job_compute_factor"].as<vector<double>>();

  assert(avails.size() == ti_jobs.size());
  assert(repeats.size() == ti_jobs.size());
  assert(nominals.size() == ti_jobs.size());
  assert(timeouts.size() == ti_jobs.size());
  assert(iterations.size() == ti_jobs.size());
  assert(ranks_per_host.size() == ti_jobs.size());
  assert(compute_factors.size() == ti_jobs.size());
  assert(ti_traffic_files.size() == ti_jobs.size());
  assert(ti_mapping_files.size() == ti_jobs.size());

  const int CoresPerHost = NumberOfRegionsPerHost * NumberOfCoresPerRegion;

  for (size_t i = 0; i < ti_jobs.size(); i++) {
    auto ranks = ranks_per_host[i];
    assert(ranks <= CoresPerHost);
    if (ranks == 0)
      ranks = CoresPerHost;
    auto v = ti_jobs[i];
    if (v.substr(0, 5) == "|VEF|") {
      v = v.substr(5);
      ctx.resource_manager->push_job(
          ParserCollection::VEF, avails[i], repeats[i], nominals[i],
          timeouts[i], iterations[i], compute_factors[i], ranks, ti_jobs[i],
          ti_traffic_files[i], ti_mapping_files[i], 0);
      continue;
    }
    if (v.substr(0, 5) == "|TRF|") {
      v = v.substr(5);
      ctx.resource_manager->push_job(
          ParserCollection::TRAFFIC, avails[i], repeats[i], nominals[i],
          timeouts[i], iterations[i], compute_factors[i], ranks,
          ti_traffic_files[i], "", ti_mapping_files[i], 0);
      continue;
    }

    ctx.resource_manager->push_job(
        ParserCollection::SIMGRID_TI, avails[i], repeats[i], nominals[i],
        timeouts[i], iterations[i], compute_factors[i], ranks, ti_jobs[i],
        ti_traffic_files[i], ti_mapping_files[i], 0);
  }

// Most likely need to increase number of files that can be opened
#ifndef _WIN32
  struct rlimit rlim;
  int ret = getrlimit(RLIMIT_NOFILE, &rlim);
  rlim.rlim_cur = rlim.rlim_max;
  ret = setrlimit(RLIMIT_NOFILE, &rlim);
  if (ret) {
    LOG_ERROR << "Could not increase number of opened files!" << std::endl;
    return false;
  }
#else
  int requested_number_of_opened_files = 8192;
  int newmax = _setmaxstdio(requested_number_of_opened_files);
  if (newmax != requested_number_of_opened_files) {
    LOG_ERROR << "Could not increase number of opened files!" << std::endl;
    return false;
  }
#endif

  return true;
}
