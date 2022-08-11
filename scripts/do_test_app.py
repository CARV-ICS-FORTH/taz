#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

import sys
import os
import tarfile
from taz import snapshot as snap
from taz import run_wrapper as rw

if len(sys.argv) < 3:
    print("Syntax: <test name> <path to binary> [taz arg1] ... [taz arg N]")
    exit(-1)

script_path = os.path.dirname(os.path.realpath(__file__))
test_name = sys.argv[1]
binary_path = sys.argv[2]

# Rough checks
if not os.path.exists(binary_path):
    print(
        f"Could not find binary path {binary_path}. abort.")
    sys.exit(2)

test_work_path = f"{script_path}/../tmp/tests/{test_name}"
if not os.path.exists(test_work_path):
    print(
        f"Could not test work path {test_work_path}. abort.")
    sys.exit(3)

#Decompress traces if needed
subname=test_name.removeprefix("taz_simulate_")
trace_path = f"{script_path}/../tests/apps/{subname}"
if not os.path.exists(os.path.join(trace_path,"trace.index")):
    trace_archive=os.path.join(trace_path,"trace.tar.gz")
    if not os.path.exists(trace_archive):
        print(
            f"Could not find archive in {trace_archive}. abort.")
        sys.exit(4)
    t=tarfile.open(trace_archive)
    os.chdir(trace_path)
    print("Decompressing the trace archive...")
    t.extractall()

# Run the programm
r = rw.RunWrapper(test_name,{'work_path':test_work_path,'args':sys.argv[3:]})
result = r.run(binary_path)
