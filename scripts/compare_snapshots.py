#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os.path
from taz import snapshot as snap

if len(sys.argv) < 4 :
    print("Syntax: <path to self snapshot> <path to other snapshot> <attribute 1> [attribute2] ... [attributeN]")
    print("Attributes have the shape snapshot/xx graph/xx node/xx or edge/xx")
    exit(-1)


self_s=snap.Snapshot(sys.argv[1]) 
other_s=snap.Snapshot(sys.argv[2]) 

if not self_s.compare(other_s,sys.argv[3:]):
    print(f"Comparison failed")
    sys.exit(1)
