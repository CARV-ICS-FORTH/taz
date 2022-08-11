// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#include "AbstractJobHandling.hpp"

std::ostream &operator<<(std::ostream &out, const JobDescriptor &x) {
  out << "Job" << x.job_id << "@" << simtime_to_double(x.availability) << "[s]";
  if (x.nhosts > 0)
    out << ",w/" << x.nhosts << '*' << x.ranks_per_host;
  if (x.start_time != END_OF_TIME)
    out << ",s:" << simtime_to_double(x.start_time)
        << ",n:" << simtime_to_double(x.start_time + x.rel_nominal_time)
        << ",w:" << simtime_to_double(x.start_time + x.rel_wall_time);
  return out;
}

std::ostream &operator<<(std::ostream &out, JobFinishType x) {
  switch (x) {
  case JobFinishType::COMPLETION:
    out << "COMPLETION";
    break;
  case JobFinishType::TIMEOUT:
    out << "TIMEOUT";
    break;
  case JobFinishType::HOST_FAILURE:
    out << "HOST_FAILURE";
    break;
  case JobFinishType::LINK_FAILURE:
    out << "LINK_FAILURE";
    break;
  }
  return out;
}
