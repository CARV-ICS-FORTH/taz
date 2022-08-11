#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os
import tarfile
import glob

script_path = os.path.dirname(os.path.realpath(__file__))

#Decompress traces if needed
for trace_path in glob.glob(f"{script_path}/../tests/apps/*"):
    if os.path.exists(os.path.join(trace_path,"trace.index")):
        print(f"Already decompressed, so skipping {trace_path}...")
    else:
        trace_archive=os.path.join(trace_path,"trace.tar.gz")
        if not os.path.exists(trace_archive):
            print(f"Could not find archive in {trace_path}. abort.")
        else:
            t=tarfile.open(trace_archive)
            os.chdir(trace_path)
            print(f"Decompressing {trace_archive}...")
            t.extractall()
