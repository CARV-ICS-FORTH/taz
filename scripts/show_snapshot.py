#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os
from taz import snapshot as snap

if len(sys.argv) < 4:
    print(
        "Syntax: <path to snapshot> <generator:[d3|vis|three]> <type:[html|svg|png]>")
    exit(1)


snapshot_path = sys.argv[1]
generator = sys.argv[2]
file = sys.argv[3]


base, file_extension = os.path.splitext(snapshot_path)

isdir = os.path.isdir(snapshot_path)

if file_extension != ".py" and not isdir:
    print(f"You need to provide either a python file or a directory.")
    sys.exit(2)

list_of_bases = []
if isdir:
    for dirpath, dirs, files in os.walk(snapshot_path):
        for f in files:
            file_path = os.path.join(dirpath, f)
            base, file_extension = os.path.splitext(file_path)
            if file_extension == ".py":
                list_of_bases = list_of_bases+[base]

else:
    if (not os.path.exists(snapshot_path)):
        print(f"Could not open the snapshot path <{snapshot_path}>, abort.")
        sys.exit(3)
    list_of_bases = [base]

for base in list_of_bases:
    snapshot = None
    snapshot_path = base+".py"
    snap.Snapshot(snapshot_path).show(generator,file)
