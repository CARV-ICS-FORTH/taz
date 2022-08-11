// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "../common/common.hpp"

#include <boost/stacktrace.hpp>
#include <mpi.h>

#include "instances_generated.h"
#include "main_generated.h"

typedef boost::stacktrace::stacktrace StackTrace;

// A mock-up class until we get Flatbuffer there
class PrimitiveInstance {};

void start_measurement();
void end_measurement(PrimitiveInstance &instance);
void abort_application(std::string primitive, std::string cause,
                       StackTrace &stacktrace);
