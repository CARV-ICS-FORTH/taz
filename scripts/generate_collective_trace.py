#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os.path
from taz import ti_trace as tr

script_path = os.path.dirname(os.path.realpath(__file__))
root_path = f"{script_path}/.."

tr.TiTrace(4, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(8, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(16, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(32, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(64, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(128, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(256, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(512, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(1024, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(2048, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(4096, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)
tr.TiTrace(65536, 1, 1, 1, "alltoall", "65536 65536 1 1").write(root_path)

tr.TiTrace(16, 1, 1, 1, "allgather", "65536 65536 1 1").write(root_path)
tr.TiTrace(512, 1, 1, 1, "allgather", "65536 65536 1 1").write(root_path)

tr.TiTrace(16, 1, 1, 1, "reduce", "1 0 0 0").write(root_path)

tr.TiTrace(4, 1, 1, 1, "allreduce", "1 0 0 0").write(root_path)
tr.TiTrace(7, 1, 1, 1, "allreduce", "1 0 0 0").write(root_path)

tr.TiTrace(16, 1, 1, 1, "bcast", "1 0 0").write(root_path)
