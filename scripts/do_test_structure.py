#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os
from taz import snapshot as snap
from taz import run_wrapper as rw

if len(sys.argv) < 3:
    print("Syntax: <test name> <path to binary> [taz arg1] ... [taz arg N]")
    exit(-1)

script_path = os.path.dirname(os.path.realpath(__file__))
test_name = sys.argv[1]
binary_path = sys.argv[2]

attributes_to_compare=["snpashot/final_time","node/label","edge/label"]

# Rough checks
expected_path = f"{script_path}/../tests/expected/{test_name}.py"
if not os.path.exists(expected_path):
    print(
        f"Could not find expected values for test {test_name}, abort. (path is {expected_path})")
    sys.exit(2)

if not os.path.exists(binary_path):
    print(
        f"Could not find binary path {binary_path}. abort.")
    sys.exit(3)

test_work_path = f"{script_path}/../tmp/tests/{test_name}"
if not os.path.exists(test_work_path):
    print(
        f"Could not test work path {test_work_path}. abort.")
    sys.exit(3)

# Run the programm
r = rw.RunWrapper(test_name,{'work_path':test_work_path,'args':sys.argv[3:]})
result = r.run(binary_path)

expected=snap.Snapshot(expected_path) 
this_run_path=test_work_path+"snap_final.py"
this_run=snap.Snapshot(this_run_path) 

if not this_run.compare(expected,attributes_to_compare):
    print(f"Comparison failed (this_run (self): {this_run_path} expected (other):{expected_path})")
    sys.exit(4)
print("Structure test passes.")