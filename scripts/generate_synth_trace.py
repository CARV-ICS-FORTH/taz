#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import pathlib
import sys
import os


script_path = os.path.dirname(os.path.realpath(__file__))

node_flops = 100000000000.0


def make_trace(n_ranks, start_delay, iteration_delay, n_iterations, end_delay, coll, coll_args):
    root_path = f"{script_path}/../tests/synth/{coll}_{n_ranks}x{n_iterations}"
    if not os.path.exists(root_path):
        pathlib.Path(root_path).mkdir(parents=True, exist_ok=True)
    trace_path = f"{root_path}/traces"
    if os.path.exists(trace_path):
        # print(f"Path {trace_path} exists, skipping.")
        return
    print(
        f"Generate traces for {coll} with {n_ranks} ranks x {n_iterations} iterations ..")
    os.mkdir(trace_path)
    index_file = open(f"{root_path}/trace.index", "a")
    for rank in range(n_ranks):
        index_file.write(f"@traces/trace.{rank}\n")
        trace_file = open(f"{trace_path}/trace.{rank}", "a")
        trace_file.write(f"{rank} init\n")
        trace_file.write(f"{rank} compute {start_delay*node_flops}\n")
        for iter in range(n_iterations):
            trace_file.write(f"{rank} compute {iteration_delay*node_flops}\n")
            if coll == "chain":
                next_rank = (rank+1) % n_ranks
                prev_rank = (rank+n_ranks-1) % n_ranks
                trace_file.write(f"{rank} isend {next_rank} {coll_args}\n")
                trace_file.write(f"{rank} irecv {prev_rank} {coll_args}\n")
                trace_file.write(f"{rank} waitall 2\n")
            else:
                trace_file.write(f"{rank} {coll} {coll_args}\n")
        trace_file.write(f"{rank} compute {end_delay*node_flops}\n")
        trace_file.write(f"{rank} finalize\n")
        trace_file.close()
    index_file.close()


make_trace(4, 1, 60, 1, 1, "allreduce", "65536 1")
make_trace(4, 1, 60, 10, 1, "allreduce", "65536 1")
make_trace(4, 1, 60, 50, 1, "allreduce", "65536 1")
make_trace(4, 1, 60, 150, 1, "allreduce", "65536 1")

make_trace(8, 1, 60, 1, 1, "allreduce", "65536 1")
make_trace(8, 1, 60, 10, 1, "allreduce", "65536 1")
make_trace(8, 1, 60, 50, 1, "allreduce", "65536 1")
make_trace(8, 1, 60, 150, 1, "allreduce", "65536 1")

make_trace(16, 1, 60, 1, 1, "allreduce", "65536 1")
make_trace(16, 1, 60, 10, 1, "allreduce", "65536 1")
make_trace(16, 1, 60, 50, 1, "allreduce", "65536 1")
make_trace(16, 1, 60, 150, 1, "allreduce", "65536 1")

make_trace(4, 1, 60, 1, 1, "chain", "1 65536  1")
make_trace(4, 1, 60, 10, 1, "chain", "1 65536 1")
make_trace(4, 1, 60, 50, 1, "chain", "1 65536 1")
make_trace(4, 1, 60, 150, 1, "chain", "1 65536 1")

make_trace(8, 1, 60, 1, 1, "chain", "1 65536 1")
make_trace(8, 1, 60, 10, 1, "chain", "1 65536 1")
make_trace(8, 1, 60, 50, 1, "chain", "1 65536 1")
make_trace(8, 1, 60, 150, 1, "chain", "1 65536 1")

make_trace(16, 1, 60, 1, 1, "chain", "1 65536 1")
make_trace(16, 1, 60, 10, 1, "chain", "1 65536 1")
make_trace(16, 1, 60, 50, 1, "chain", "1 65536 1")
make_trace(16, 1, 60, 150, 1, "chain", "1 65536 1")
