// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "AtomicAction.hpp"
#include "Engine.hpp"
#include "Model.hpp"
#include "ParserCollection.hpp"
#include "ResourceManager.hpp"
#include "Statistics.hpp"

int SnapshotsType = 0;
int SnapshotsOccurence = 0;

simtime_t SimtimePrecision = 1;
simtime_t SoftLatency = 1;
share_t ShareChange = 0;
share_t EagerThreshold = 256;
resind_t NumberOfCoresPerRegion = 0;
resind_t NumberOfRegionsPerHost = 0;
resind_t NumberOfHosts = 0;
Context ctx;

std::ostream &operator<<(std::ostream &out, CoreId x) {
  out << x.get_host() << ".";
  if (NumberOfRegionsPerHost > 1)
    out << x.get_host_region() << '.';
  out << x.get_core();
  return out;
}

std::ostream &operator<<(std::ostream &out, const ActGroup &v) {
  FOREACH_ACT_IN_GROUP(index, v) {
    if (i)
      out << ",";
    out << index;
  }
  return out;
}

Context::~Context() {
  if (engine)
    delete engine;
  if (model)
    delete model;
  if (resource_manager)
    delete resource_manager;
  if (parsers)
    delete parsers;
  if (tmp_stats)
    delete tmp_stats;
  if (out_stats)
    delete out_stats;
  if (total_stats)
    delete total_stats;
  if (pool)
    delete pool;
}