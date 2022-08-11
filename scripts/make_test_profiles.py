#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os
from taz import run_wrapper as rw
from taz.utils import dictionary as udict
from taz.utils import ranges as uranges
import json
import shutil
import random

if len(sys.argv) < 3 or (sys.argv[1] != "msvc" and sys.argv[1] != "gnu"):
    print("Syntax: <msvc/gnu> <Do heavy tests (0|1)>")
    exit(-1)

debugger_type = sys.argv[1]
script_path = os.path.dirname(os.path.realpath(__file__))

# d is a dictionary
# Used keys are
# * relpath
# * will_fail
# * skip_launch
# * skip_test
# * test_timeout
# * test_proxy (if set, call tests/do_test_{test_proxy} {testname} {program }{arguments})

AddHeavyTests = (sys.argv[2] == "1") or (
    sys.argv[2] == "TRUE") or (sys.argv[2] == "ON")
DefaultTestTimeout = 300  # seconds
LongTestTimeout = 3600  # seconds
RandomTestsPerAlgorithm = 600
RandomPicksInRange = 200
RandomCommSizeRange = [2, 66]
RandomMsgSizeRange = [4, 1 << 16]
RandomSegSizeRange = [512, 1 << 14]
RandomOutstandingReqsRange = [4, 2048]

if AddHeavyTests:
    print(f"Include heavy tests... ({AddHeavyTests})")
else:
    print(f"DO NOT Include heavy tests...({AddHeavyTests})")

# Args is a string where S is replaced by message size and R is replaced by the root rank
# algs is an array of dionaries.
# Each dictionary MUST define field "i" for the implementation index.
# It may additionally defined the following keys:
# * comm_sizes
# * msg_sizes
# * roots


def test_collective(d, collname, args, algs):
    #First, just generate a simple test to make sure that parsing is OK
    args_final = args.replace("R", str(1)).replace("S", str(1333))
    name = f"{collname}_parse"
    d["args"] = [
        "--topology=TINY",
        "--endtime=100",
        "--inhibit_debug_messages=1",
        "--snapshots_type=2",
        "--snapshots_occurence=8",
        "--snapshots_format=pydict",
        "--eager_threshold=256",
        f"--{collname}_alg=2",
        "-j",
        f";6;* init;* compute 1;* {collname} {args_final};* compute 1;* finalize;",
    ]
    rw.RunWrapper(name, d).generate_launch_profile(debugger_type)
    #Same basic case, actually running the simulation
    name = f"{collname}_full"
    d["args"] = [
        "--topology=TINY",
        "--endtime=100",
        "--inhibit_debug_messages=1",
        "--eager_threshold=256",
        f"--{collname}_alg=2",
        "-j",
        f";6;* init;* compute 1;* {collname} {args_final};* compute 1;* finalize;",
    ]
    rw.RunWrapper(name, d).generate_launch_profile(debugger_type)
    base_cases = []
    random_cases = []
    # Add default implementation cases
    algs = algs+[{"i": 0}]
    for alg in algs:
        comm_sizes = [2, 4, 7, 16]
        rand_comm_sizes = uranges.get_rand_logspace(
            RandomCommSizeRange[0], RandomCommSizeRange[1], RandomPicksInRange)
        if "comm_sizes" in alg:
            rand_comm_sizes = comm_sizes = alg["comm_sizes"]
        msg_sizes = [4, 65536]
        rand_msg_sizes = uranges.get_rand_logspace(
            RandomMsgSizeRange[0], RandomMsgSizeRange[1], RandomPicksInRange)
        if "msg_sizes" in alg:
            rand_msg_sizes = msg_sizes = alg["msg_sizes"]
        roots = [0]
        if "roots" in alg:
            roots = alg["roots"]
        rand_segsizes = uranges.get_rand_logspace(
            RandomSegSizeRange[0], RandomSegSizeRange[1], RandomPicksInRange)
        rand_outstanding_reqs = uranges.get_rand_logspace(
            RandomOutstandingReqsRange[0], RandomOutstandingReqsRange[1], RandomPicksInRange)
        base_dd = udict.product_dict(i=[alg["i"]], comm_size=comm_sizes,
                                     msg_size=msg_sizes, root=roots)
        base_cases = base_cases+[x for x in base_dd]
        for j in range(RandomTestsPerAlgorithm):
            comm_size = rand_comm_sizes[random.randrange(len(rand_comm_sizes))]
            root = random.randrange(comm_size)
            msg_size = rand_msg_sizes[random.randrange(len(rand_msg_sizes))]
            segsize = rand_segsizes[random.randrange(len(rand_segsizes))]
            outstanding_reqs = rand_outstanding_reqs[random.randrange(
                len(rand_outstanding_reqs))]
            random_cases = random_cases + [{"i": alg["i"], "comm_size":comm_size, "msg_size":msg_size,
                                            "root":root, "segsize":segsize, "outstanding_reqs":outstanding_reqs}]

    # Make base tests
    for c in base_cases:
        alg_index = c["i"]
        comm_size = c["comm_size"]
        root = c["root"]
        msg_size = c["msg_size"]
        if (root >= comm_size):
            continue
        args_final = args.replace("R", str(root)).replace("S", str(msg_size))
        # d["test_proxy"] = "snapshot"
        name = f"{collname}_I{alg_index}_C{comm_size}_M{msg_size}_R{root}_snapshot"
        coll_param = f"--{collname}_alg={alg_index}"
        if not alg_index:
            coll_param = ""
        d['args'] = [
            "--topology=TINY",
            "--endtime=100",
            "--parse_only=1",
            "--inhibit_debug_messages=1",
            "--snapshots_type=2",
            "--snapshots_occurence=8",
            "--snapshots_format=pydict",
            "--eager_threshold=256",
            coll_param,
            "-j",
            f";{comm_size};* init;* compute 1;* {collname} {args_final};* compute 1;* finalize;",
        ]
        rw.RunWrapper(name, d).generate_ctest()
        # del d["test_proxy"]
        name = f"{collname}_I{alg_index}_C{comm_size}_M{msg_size}_R{root}_full"
        d['args'] = [
            "--topology=TINY",
            "--endtime=100",
            "--inhibit_debug_messages=1",
            "--snapshots_type=2",
            "--snapshots_occurence=4",
            "--eager_threshold=256",
            coll_param,
            "-j",
            f";{comm_size};* init;* compute 1;* {collname} {args_final};* compute 1;* finalize;",
        ]
        rw.RunWrapper(name, d).generate_ctest()

    # Make more random tests

    name = f"{collname}_random"
    stats_pathname = f"tmp/tests/taz_simulate_{name}"
    stats_path = f"{script_path}/../{stats_pathname}"
    if not os.path.exists(stats_path):
        os.makedirs(stats_path)
    with open(f"{stats_path}/runs.py", "wt") as file:
        file.write("runs=[\n")
        index = 0
        for c in random_cases:
            alg_index = c["i"]
            comm_size = c["comm_size"]
            msg_size = c["msg_size"]
            root = c["root"]
            segsize = c["segsize"]
            outstanding_reqs = c["outstanding_reqs"]
            topology = "TINY"
            if comm_size > 16:
                topology = "MEDIUM"
            args_final = args.replace(
                "R", str(root)).replace("S", str(msg_size))
            coll_param = f"--{collname}_alg={alg_index}"
            if not alg_index:
                coll_param = ""
            d["inputs"] = c
            d["args"] = [
                f"--topology={topology}",
                "--endtime=10000",
                "--inhibit_debug_messages=1",
                "--snapshots_type=0",
                "--snapshots_occurence=8",
                "--snapshots_format=pydict",
                "--eager_threshold=256",
                "--maximum_fanin_fanout=20",
                coll_param,
                f"--forced_segsize={segsize}",
                f"--forced_max_outstanding_reqs={outstanding_reqs}",
                "-j",
                f";{comm_size};* init;* compute 1;* {collname} {args_final};* compute 1;* finalize;",
            ]
            r = rw.RunWrapper(name, d)
            dump = json.dumps(r.d)
            if index > 0:
                file.write(",\n")
            file.write(f"{{'name':'{name}_{index}','d':'{dump}'}}")
            index = index+1
        file.write("\n]")

    if collname != "alltoall" or AddHeavyTests:
        d["test_proxy"] = "random"
        d["args"] = []
        d["test_timeout"] = LongTestTimeout
        rw.RunWrapper(name, d).generate_ctest()
        del d["test_proxy"]
        d["test_timeout"] = DefaultTestTimeout


def make_profile(d, name, args):
    d['args'] = args
    r = rw.RunWrapper(name, d)
    if not d['skip_test']:
        r.generate_ctest()
    if not d['skip_launch']:
        r.generate_launch_profile(debugger_type)


# Create tmp if needed
test_path = script_path+"/../tmp/tests/"
if not os.path.exists(test_path):
    os.makedirs(test_path)

# Remove existing test tmp folders
test_dirs = os.listdir(test_path)
for d in test_dirs:
    shutil.rmtree(test_path+d)

# Write prologs
with open(script_path+"/../.vscode/launch.json", "wt") as launch_file:
    launch_file.write('''{
        //Generated by tests/make_test_profiles.py
        "version": "0.2.0",
        "configurations": [''')

with open(script_path+"/../tests/CMakeLists.txt", "wt") as ctest_file:
    ctest_file.write(" ## Generated by scripts/make_test_profiles.py")


# Write tests
d = {
    "relpath": "tmp",
    "will_fail": False,
    "skip_test": False,
    "skip_launch": False,
    "test_timeout": DefaultTestTimeout,
    "debugger_type": debugger_type
}

# --- Basic parsing ---
d["will_fail"] = True
make_profile(d, "help", ['--help'])
d["will_fail"] = False
make_profile(d, "inline trace", [
    "--topology=TINY",
                "--endtime=360000",
                "-j",
                ";16;* init;* compute 10;* compute 10;* finalize;",
])

# --- Point to point examples ---
make_profile(d, "pt2pt simple",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index"
              ])
make_profile(d, "pt2pt simple rm traffic",
             ["--topology=TINY",
              "--resource_mapper=traffic",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index"
              ])
make_profile(d, "pt2pt concurrent",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/concurrent/trace.index"
              ])
make_profile(d, "pt2pt non-blocking",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/nonblocking/trace.index"
              ])
make_profile(d, "pt2pt simple x3",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--job_avail=1",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--job_avail=2",
                "--job_ti=$$/tests/point2point/simple/trace.index"
              ])

# --- Link faults ---

make_profile(d, "link fault no effect",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--link_fault_profile=0:1e-8~2e-8"
              ])

make_profile(d, "link fault abort noconsume",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--link_fault_profile=0:1e-6~2e-6"
              ])

make_profile(d, "link fault abort consume",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--link_fault_profile=0:1.3e-6~2e-6"
              ])

make_profile(d, "link fault abort noconsume next OK",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--link_fault_profile=0:1e-6~2e-6",
                "--job_avail=1",
                "--job_ti=$$/tests/point2point/simple/trace.index"
              ])

make_profile(d, "link fault abort consume next OK",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--link_fault_profile=0:1.3e-6~2e-6",
                "--job_avail=1",
                "--job_ti=$$/tests/point2point/simple/trace.index"
              ])

make_profile(d, "node fault no effect",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--node_fault_profile=4:1e-8~2e-8"
              ])

make_profile(d, "node fault app crash",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--node_fault_profile=0:1e-6~2e-6"
              ])

make_profile(d, "node fault app crash next OK",
             ["--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/point2point/simple/trace.index",
                "--node_fault_profile=0:1e-6~2e-6",
                "--job_avail=1",
                "--job_ti=$$/tests/point2point/simple/trace.index"
              ])


# --- Collectives ---
test_collective(d, "barrier", "", udict.merge_dicts([
                {"i": 1, "comm_sizes": [2]}, {"i": 2}, {"i": 3}], {"msg_sizes": [0]}))
roots = {"roots": [0, 3]}
test_collective(d, "bcast", "S R 2", udict.merge_dicts(
    [{"i": 1}, {"i": 2}, {"i": 3}], roots))
test_collective(d, "allgather", "S S 2 2", [{"i": 1}, {
                "i": 2}, {"i": 3}, {"i": 4}, {"i": 5}])
test_collective(d, "alltoall", "S S 2 2", [{"i": 1}, {"i": 2}, {"i": 3}])
test_collective(d, "reduce", "S 0.6 R 2", udict.merge_dicts(
    [{"i": 1}, {"i": 2}, {"i": 3}, {"i": 4}], roots))
test_collective(d, "allreduce", "S 2", [
                {"i": 1}, {"i": 3}, {"i": 4}, {"i": 5}])

# --- Synthetic traces ---
make_profile(d, "synth tiny",
             [
                "--topology=TINY",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/synth/chain_4x10/trace.index"
             ]
             )

# --- Application traces ---
d["test_proxy"] = "app"
make_profile(d, "miniAero 4",
             [
                 "--topology=TINY",
                 "--inhibit_debug_messages=1",
                 "--job_compute_factor=1e-11",
                 "--job_ti=$$/tests/apps/miniAero_4/trace.index"
             ]
             )
make_profile(d, "miniAero 32",
             [
                 "--topology=MEDIUM",
                 "--inhibit_debug_messages=1",
                 "--job_compute_factor=1e-11",
                 "--job_ti=$$/tests/apps/miniAero_32/trace.index"
             ]
             )
make_profile(d, "miniAero 128",
             [
                 "--topology=MEDIUM",
                 "--inhibit_debug_messages=1",
                 "--job_compute_factor=1e-11",
                 "--job_ti=$$/tests/apps/miniAero_128/trace.index"
             ]
             )

nas_benchmarks = ["cg", "dt", "ep", "lu", "mg", "sp"]
for index,bench in enumerate(nas_benchmarks):
    make_profile(d, f"{bench} S 16",
                 [
                     "--topology=TINY",
                     "--job_compute_factor=1e-11",
                     f"--job_ti=$$/tests/apps/{bench}_S_16/trace.index"
                 ]
                 )

    make_profile(d, f"{bench} S 16 trf",
                    [
                        "--topology=TINY",
                        "--job_compute_factor=1e-11",
                        f"--job_traffic=$$/tests/apps/{bench}_S_16/traffic.tazm",
                        f"--job_iterations={index+1}",
                        "--job_ti=#TRF#"
                    ]
                    )

if AddHeavyTests:
    d["skip_launch"] = True
    d["test_timeout"] = LongTestTimeout
    make_profile(d, "miniAero 512",
                 [
                    "--topology=MEDIUM",
                    "--inhibit_debug_messages=1",
                    "--job_compute_factor=1e-11",
                    "--job_ti=$$/tests/apps/miniAero_512/trace.index"
                 ]
                 )
    for bench in nas_benchmarks:
        make_profile(d, f"{bench} D 256",
                     [
                         "--topology=MEDIUM",
                         "--job_compute_factor=1e-11",
                         f"--job_ti=$$/tests/apps/{bench}_D_256/trace.index"
                     ]
                     )
    d["test_timeout"] = DefaultTestTimeout  # seconds
    d["skip_launch"] = False
del d["test_proxy"]

# --- Complex use case ---
d['test_properties'] = {'DEPENDS': "miniAero_4;cg_S_16"}
make_profile(d, "program options", [
    "--conf=$$/tests/conf/basic.conf",
                "--inhibit_debug_messages=1",
                "--job_compute_factor=1e-11",
                "-j",
                "$$/tests/apps/miniAero_4/trace.index",
                "--topology=MEDIUM",
                "--endtime=3600",
                "--job_ti=$$/tests/apps/cg_S_16/trace.index",
                "--job_avail=10",
                "--job_repeat=50",
                "--job_ti=$$/tests/synth/allreduce_16x1/trace.index"
])
make_profile(d, "program options rm traffic", [
    "--conf=$$/tests/conf/basic.conf",
                "--inhibit_debug_messages=1",
                "--job_compute_factor=1e-11",
                "--resource_mapper=traffic",
                "--rm_only=1",
                "-j",
                "$$/tests/apps/miniAero_4/trace.index",
                "--topology=MEDIUM",
                "--endtime=3600",
                "--job_ti=$$/tests/apps/cg_S_16/trace.index",
                "--job_avail=10",
                "--job_repeat=50",
                "--job_ti=$$/tests/synth/allreduce_16x1/trace.index"
])
d

del d['test_properties']

# --- Resource manager ---
make_profile(d, "rm traffic simple",
             [
                "--inhibit_debug_messages=1",
                "--job_compute_factor=1e-11",
                "--resource_mapper=traffic",
                "--rm_only=1",
                "--job_repeat=1",
                "-j",
                ";128;* init;* compute 1;* reduce 4 0.6 0 2;* compute 1;* finalize;",
                "--topology=MEDIUM",
                "--endtime=36000",
                "--node_nominal_mtbf_log10=20"
             ]
             )

if not AddHeavyTests:
    d["skip_test"]=True
make_profile(d, "rm traffic overwhelm",
             [
                "--inhibit_debug_messages=1",
                "--job_compute_factor=1e-11",
                "--resource_mapper=traffic",
                "--rm_traffic_scotch_nthreads=8",
                "--rm_only=1",
                "--job_repeat=1",
                "-j",
                ";128;* init;* compute 1;* reduce 4 0.6 0 2;* compute 1;* finalize;",
                "-j",
                ";37;* init;* compute 1;* reduce 4 0.6 0 2;* compute 1;* finalize;",
                "-j",
                ";353;* init;* compute 1;* reduce 4 0.6 0 2;* compute 1;* finalize;",
                "-j",
                ";512;* init;* compute 1;* reduce 4 0.6 0 2;* compute 1;* finalize;",
                "-j",
                ";34;* init;* compute 1;* reduce 4 0.6 0 2;* compute 1;* finalize;",
                "-j",
                ";11;* init;* compute 1;* reduce 4 0.6 0 2;* compute 1;* finalize;",
                "-j",
                ";254;* init;* compute 1;* reduce 4 0.6 0 2;* compute 1;* finalize;",
                "--topology=MEDIUM",
                "--endtime=36000",
                "--node_nominal_mtbf_log10=20"
             ]
             )
d["skip_test"]=False

make_profile(d, "resource manager",
             [
                "--topology=TINY",
                "--inhibit_debug_messages=1",
                "--job_avail=0",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/synth/chain_4x1/trace.index",
                "--job_avail=10",
                "--job_ti=$$/tests/synth/chain_4x10/trace.index",
                "--job_avail=300",
                "--job_ti=$$/tests/synth/chain_4x50/trace.index",
                "--job_avail=400",
                "--job_ti=$$/tests/synth/chain_8x1/trace.index",
                "--job_avail=500",
                "--job_ti=$$/tests/synth/chain_16x10/trace.index",
                "--job_avail=1000",
                "--job_ti=$$/tests/synth/chain_4x1/trace.index"
             ])


make_profile(d, "traffic resource manager",
             [
                 "--topology=TINY",
                                 "--endtime=360000",
                "--inhibit_debug_messages=1",
                "--resource_mapper=traffic",
                "--node_nominal_mtbf_log10=30",
                "--rm_dump_job_mappings=1",
                "--job_avail=0",
                "--job_compute_factor=1e-11",
                "--job_ti=$$/tests/synth/chain_4x1/trace.index",
                "--job_avail=1",
                "--job_ti=$$/tests/synth/chain_4x10/trace.index",
                "--job_avail=2",
                "--job_ti=$$/tests/synth/chain_4x50/trace.index",
                "--job_avail=3",
                "--job_ti=$$/tests/synth/chain_8x1/trace.index",
                "--job_avail=4",
                "--job_ti=$$/tests/synth/chain_16x10/trace.index",
                "--job_avail=5",
                "--job_ti=$$/tests/synth/chain_4x1/trace.index",
                "--job_avail=6",
                "--job_ti=$$/tests/synth/chain_4x1/trace.index"
             ])


#Complete experiments (use as test only if we enabled heavy tests)
if AddHeavyTests:
    d['test_properties'] = {'DEPENDS': "cg_S_16;cg_D_256;ep_D_256;ep_S_16;lu_S_16;lu_D_256;mg_S_16;mg_D_256;miniAero_4;miniAero_32;miniAero_128;miniAero_512;sp_S_16;sp_D_256"}
else:
    d["skip_test"]=True


#Traffic generation configuration
make_profile(d, "traffic-based mapping ",
             [
                "-t","MEDIUM",
                "-c","@tests/conf/apps_stretched.conf",
                "--inhibit_verbose_messages=1",
                "--inhibit_debug_messages=1",
                "-e","100000",
                "--warmup_period=10000",
                "--drain_period=10000",
                "--node_nominal_mtbf_log10=100",
                "--node_fault_random_seed=0",
                "--link_nominal_mtbf_log10=100",
                "--link_fault_random_seed=0",
                "--collect_state_changes=1",
                "--snapshots_type=-1",
                "--snapshots_format=pydict",
                "--snapshots_occurence=8",
                "--rm_dump_job_mappings=1",
                "--resource_mapper=traffic",
                "--rm_traffic_scotch_nthreads=14"
             ]
            )

#Fault free mapping experiment
make_profile(d, "mapping fault-free",
             [
                "-t","MEDIUM",
                "-c","@tests/conf/apps_stretched.conf",
                "--inhibit_verbose_messages=1",
                "--inhibit_debug_messages=1",
                "-e","100000",
                "--warmup_period=10000",
                "--drain_period=10000",
                "--node_nominal_mtbf_log10=100",
                "--link_nominal_mtbf_log10=100",
                "--snapshots_type=-1",
                "--snapshots_format=pydict",
                "--snapshots_occurence=8",
                "--rm_dump_job_mappings=1",
                "--resource_mapper=traffic",
                "--rm_traffic_scotch_nthreads=14",
                "--rm_only=1"
             ]
            )

if AddHeavyTests:
    d["skip_test"]=False
    del d['test_properties'] 


# Write epilog
with open(script_path+"/../.vscode/launch.json", "a") as launch_file:
    launch_file.write('''   ]
}''')

with open(script_path+"/../tests/CMakeLists.txt", "a") as ctest_file:
    ctest_file.write('''
''')

#
