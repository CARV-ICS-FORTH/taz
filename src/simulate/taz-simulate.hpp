// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _ILMM_HPP_
#define _ILMM_HPP_

#include "../common/ApproxMatrix.hpp"
#include "../common/MemoryConsumer.hpp"
#include "../common/common.hpp"

#include <boost/heap/fibonacci_heap.hpp>

// Fault generation
typedef std::weibull_distribution<> FaultDistribution;
extern std::mt19937 mt19937_gen_fault_nodes;
extern std::mt19937 mt19937_gen_fault_links;
extern std::mt19937 mt19937_gen_jobs_interarrival;

// TODO:
// Improve update_shares
//-> Defer resource_heap until minimum of defered updates is smaller or equal to
// top
//-> Create a sparse vector/matrix representation of the shares
//   - Use SIMD instructions (AVX2)
//   - Implement remaining division approximately (only for the smallest(s) ) ??
//-> Opportunistically sort the elements list by share + mark sort
//   - Need markers "ordered until" and "ordered from"
//-> Prune aggressively and recover if needed
//   - Looks like it is not needed for alltoall though

// Enable tracking of resource pairs
// (i.e. O(N2) in memory but may improve compute speed quite .. when i will have
// fixed bugs)
// #define USE_RESOURCE_PAIRS

// Be VERY careful if you change that number because
//  various configurations items rely on it being 1e9 -> 1ns
constexpr int SimtimePerSec = 1000000000;

typedef uint64_t share_t;
constexpr share_t MaximumShare = 0xFFFFFFFF;
typedef uint16_t share_code_t;

typedef uint32_t resind_t;
constexpr resind_t NoResource = 0xFFFFFFFF;

typedef uint32_t actind_t;
constexpr actind_t NoAction = 0xFFFFFFFF;

typedef uint32_t linkind_t;
constexpr linkind_t NoLink = 0xFFFFFFFF;

typedef uint32_t commind_t;
constexpr commind_t NoComm = 0xFFFFFFFF;

// Turns out many collectives and apps use sendrecv. Hence we want to model
// efficiently the case where a pair of actions is added.
typedef std::array<actind_t, 2> ActGroup;
typedef std::array<linkind_t, 2> ActGroupLinks;
static_assert(std::tuple_size<ActGroup>::value == 2,
              "ActBroup must have size of two (or many changes are needed)");
static_assert(std::tuple_size<ActGroupLinks>::value ==
                  std::tuple_size<ActGroup>::value,
              "ActGroupLink and ActGroup must have the same size");

#define FOREACH_ACT_IN_GROUP(x, group)                                         \
  for (actind_t x = (group)[0], i = 0; i < 2 && x != NoAction;                 \
       i++, x = (i == 2) ? NoAction : (group)[i])

typedef uint64_t simtime_t;
constexpr simtime_t END_OF_TIME = 0xFFFFFFFFFFFFFFFF;

inline double simtime_to_double(simtime_t t) {
  return ((double)t) / (1000 * 1000 * 1000);
}

inline simtime_t double_to_simtime(double t) {
  return (simtime_t)(t * 1000 * 1000 * 1000);
}

int ceil_log2(uint64_t x);

// extern std::vector<share_t> Weights;
//  WaitingActionSet

// WorkingSet

class AAPool;
class Engine;
class Model;
class ResourceManager;
class TraceParser;

std::ostream &operator<<(std::ostream &out, const ActGroup &v);

constexpr int FactorMultRatio = 256;
template <typename I, typename T, int MR>
struct FactorSet : protected GenericParser {
  struct Factor {
    I lower;
    T mult;
    T add;
    Factor() = default;
    Factor(I lower, T mult, T add) : lower(lower), mult(mult), add(add) {}
  };
  std::vector<Factor> factors;
  void parse(std::string arg) {
    const char *ptr = init_parsing(arg.data(), ":");
    do {
      I lower = std::atol(ptr);
      ptr = get_next_token("*+");
      double mult_d = std::atof(ptr);
      T mult = (T)(mult_d * MR);
      ptr = get_next_token(",");
      T add = std::atol(ptr);
      factors.emplace_back(lower, mult, add);
      ptr = try_next_token(":");
    } while (ptr != nullptr);
  }

  T operator()(I index, T val) {
    size_t i;
    for (i = 1; i < factors.size(); i++)
      if (factors[i].lower > index)
        break;
    // Just hold interpolation for now
    return (val * factors[i - 1].mult) / MR + factors[i - 1].add;
  }

  T operator()(I x) { return (*this)(x, x); }
};

struct Statistics;
class ParserCollection;
struct Context {
  AAPool *pool;
  Engine *engine;
  Model *model;
  ResourceManager *resource_manager;
  ParserCollection *parsers;

  Statistics *tmp_stats;
  Statistics *out_stats;
  Statistics *total_stats;

  std::ofstream stats_file;
  std::ofstream jobs_file;

  boost::program_options::variables_map vm;
  FactorSet<share_t, share_t, FactorMultRatio> bw_factor;
  FactorSet<share_t, simtime_t, FactorMultRatio> lat_factor;

  std::vector<std::string> raw_arguments;

  Context()
      : pool(nullptr), engine(nullptr), model(nullptr),
        resource_manager(nullptr), parsers(nullptr) {}
  ~Context();
};

extern Context ctx;

typedef std::unordered_map<resind_t, std::vector<simtime_t>> FaultProfileMap;

#define INCREMENT_STAT(field) ctx.tmp_stats->field++;
#define INCREASE_STAT(field, value) ctx.tmp_stats->field += value;
#define ADD_INDIV_STAT(field, value) ctx.tmp_stats->field.add(value);

bool configure(int argc, char *argv[]);

// Those are variables set up by the configuration function
// and that are called too often and many places to access through vm
extern int SnapshotsType;
extern int SnapshotsOccurence;
extern simtime_t SimtimePrecision;
extern simtime_t SoftLatency;
extern share_t ShareChange;
extern share_t EagerThreshold;
extern size_t MaximumFaninFanout;
extern resind_t NumberOfCoresPerRegion;
extern resind_t NumberOfRegionsPerHost;
extern resind_t NumberOfHosts;

class CoreId {
  resind_t id;

public:
  CoreId(resind_t core_id_) : id(core_id_) {}
  CoreId(resind_t host, resind_t region, resind_t core)
      : id(core +
           NumberOfCoresPerRegion * (region + NumberOfRegionsPerHost * host)) {}

  // Get the core index within a region
  resind_t get_core() { return id % NumberOfCoresPerRegion; }
  // Get the region index within a host
  resind_t get_host_region() {
    return (id / NumberOfCoresPerRegion) % NumberOfRegionsPerHost;
  }
  // Get the host index
  resind_t get_host() {
    return id / (NumberOfRegionsPerHost * NumberOfCoresPerRegion);
  }
  // Get the global core id
  resind_t get_core_id() { return id; }
  // Get the global region id (e.g. for routing)
  resind_t get_host_region_id() { return (id / NumberOfCoresPerRegion); }
};

std::ostream &operator<<(std::ostream &out, CoreId x);

enum class JobFinishType { COMPLETION, TIMEOUT, HOST_FAILURE, LINK_FAILURE };

#endif //_ILMM_HPP_
