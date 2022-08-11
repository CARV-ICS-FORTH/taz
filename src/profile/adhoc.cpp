// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#include "taz-profile.hpp"

#define timespec_to_ns(ts) ((ts)->tv_nsec + (uint64_t)(ts)->tv_sec * 1000000000)

namespace ts = fbs::taz::profile;
namespace f = flatbuffers;

// Be ready for threaded MPI ...
thread_local timespec last_time;
thread_local f::FlatBufferBuilder builder;
thread_local ts::ProfileFileBuilder *profile_file = nullptr;
thread_local std::vector<uint64_t> metric_fields;
thread_local std::vector<f::Offset<ts::PrimitiveInstance>> instances;
thread_local std::vector<f::Offset<void>> steps;

void initialize_profile(std::string program, std::vector<std::string> args) {

  int world_rank, n_ranks;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &n_ranks);

  // Collect information for the profile header

  std::vector<std::string> env;
  extern char **environ;
  for (char **current = environ; *current; current++) {
    env.push_back(*current);
  }

  char hostname[MPI_MAX_PROCESSOR_NAME];
  int processor_name_len = MPI_MAX_PROCESSOR_NAME;
  MPI_Get_processor_name(hostname, &processor_name_len);
  std::vector<std::string> hostnames;
  hostnames.push_back(hostname);
  std::vector<std::string> metrics;
  metrics.push_back("absolute_time");

  profile_file = new ts::ProfileFileBuilder(builder);

  auto header = ts::CreateProfileHeader(
      builder, builder.CreateString(program),
      builder.CreateVectorOfStrings(args), builder.CreateVectorOfStrings(env),
      world_rank, n_ranks, builder.CreateVectorOfStrings(hostnames),
      builder.CreateVectorOfStrings(metrics),
      builder.CreateVectorOfStrings(metrics));

  profile_file->add_header(header);
}

void finalize_profile() {

  // Assemble the file name
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  std::stringstream ss;
  std::string base = getenv("TAZ_PROFILE_BASE_FILE");
  if (not base.empty())
    ss << base << '_';
  else
    ss << "rank";
  ss << world_rank << ".tazp";

  // Finish up the trace, and close the file
  profile_file->Finish();

  std::ofstream profile_stream(ss.str(), std::ios::binary);
  profile_stream.write((const char *)builder.GetBufferPointer(),
                       builder.GetSize());
  profile_stream.close();

  delete profile_file;
  profile_file = nullptr;
}

void abort_application(std::string primitive, std::string cause,
                       StackTrace &stacktrace) {

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  std::cerr << "Abort at rank " << rank << " on primitive " << primitive
            << " because " << cause << std::endl;
  std::cerr << "Stack trace is:" << std::endl << stacktrace << std::endl;

  PMPI_Barrier(MPI_COMM_WORLD);
  // TODO: let others finalize their trace, but sync for each call is not
  // acceptable performance-wise
  finalize_profile();

  PMPI_Abort(MPI_COMM_WORLD, 0xFE);
}

// Get absolute time (and read perf counters when we get there )
void do_measure() {
  clock_gettime(CLOCK_MONOTONIC_RAW, &last_time);
  metric_fields.push_back(timespec_to_ns(&last_time));
}

void start_measurement() {
  metric_fields.clear();
  do_measure();
}

void end_measurement(f::Offset<ts::PrimitiveInstance> instance_offset) {
  do_measure();
  instances.push_back(instance_offset);
  auto metric_fields_offset = builder.CreateVector(metric_fields);
  auto exec_offset = ts::CreateTraceStep(builder, ts::TraceStepType_EXEC,
                                         instance_offset, metric_fields_offset);
  steps.push_back(exec_offset.Union());
}

int MPI_Init(int *argc, char ***argv) {

  // Build list of arguments before MPI removes some
  std::vector<std::string> args;
  for (int i = 1; i < *argc; i++)
    args.push_back((*argv)[i]);

  int res = PMPI_Init(argc, argv);
  initialize_profile((*argv)[0], args);
  return res;
}

int MPI_Finalize(void) {

  finalize_profile();
  int res = PMPI_Finalize();
  return res;
}
