#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os
import tarfile
import glob
import math
from taz import run_wrapper as rw
from taz.matrix import Matrix as mat
import matplotlib.pyplot  as plt
import numpy as np

script_path = os.path.dirname(os.path.realpath(__file__))
test_work_path = os.path.join(script_path, "../tmp")

zero_color = [0xFF,0xFF,0xFF ]
my_color_map=[ 
    [0xB3,0xB3,0xB3], # (Bytes) 1-9  -> greys  
    [0x66,0x66,0x66], #10-99
    [0x1A,0x1A,0x1A], #100-999
    [0x80,0xB3,0xFF], #1E3 (KBytes) and upwards ->blues
    [0x2A,0x7F,0xFF], #1E4 and upwards
    [0x00,0x44,0xAA], #1E5 and upwards
    [0xAA,0xDE,0x87],#1E6 (MBytes) and upwards -> greens
    [0x71,0xC8,0x37],#1E7 and upwards
    [0x44,0x78,0x21],#1E8 and upwards
    [0xFF,0xE6,0x80],#1E9 (GBytes) and upwards -> yellows
    [0xFF,0xD4,0x28],#1E10 and upwards
    [0xD4,0xAA,0x00],#1E11 and upwards
    [0xFF,0xB3,0x80],#1E12 (TeraBytes) and upwards -> oranges
    [0xFF,0x7F,0x2A],#1E13 and upwards
    [0xD4,0x55,0x00],#1E14 and upwards
    [0xFF,0x80,0x80],#1E15 (Petabytes) and upwards -> reds
    [0xFF,0x2A,0x2A],#1E16 and upwards
    [0xD4,0x00,0x00]]#1E17 and upwards

def generate_traffic_image(m, m_size, trace_path):
    a=np.ones((m.shape[0],m.shape[1],3),dtype='int64')*0xFF
    mat_log10=np.floor(np.log10(m,where=m >0))
    for i in range(m.shape[0]):
        for j in range(m.shape[1]):
            if(m[i,j] > 1E17 ):
                a[i,j,:]=colors[-1]
            elif(m[i,j]>0):
                a[i,j,:]=my_color_map[int(mat_log10[i,j])]
    size_inches=5 + 2 * math.log10(m_size)
    plt.figure(figsize=[size_inches,size_inches],dpi=200.0)
    plt.imshow(a)
    plt.savefig(f"{trace_path}/traffic.png",metadata={'optimize':'1'})
    plt.close()
    print(f"Generated an image of size {size_inches} inches for an app of {m_size} ranks.")


def generate_app_traffic(topology_size,binary_path,trace_path, extra_args) :
    if not os.path.exists(os.path.join(trace_path,"trace.index")):
        print(f"App is not decompressed (yet), so skipping {trace_path}...")
        return
    traffic_file=os.path.join(trace_path,"traffic.tazm")
    trace_archive=os.path.join(trace_path,"trace.tar.gz")

    if os.path.exists(traffic_file):
        print(f"Traffic file already exists at {trace_path}, skipping.. (remove file to force re-generation)")
        traffic_image=os.path.join(trace_path,"traffic.png")
        if not os.path.exists(traffic_image):
            traffic = mat()
            traffic.read(traffic_file)
            m=traffic.np_array
            generate_traffic_image(m, m.shape[0], trace_path)
            print(f"(Re-)generated the traffic image at {trace_path}")            
        return

    # Run the programm
    base_run_args = [f"--topology={topology_size}",
            "--node_nominal_mtbf_log10=100", "--link_nominal_mtbf_log10=100" ,
            "--job_compute_factor=1e-11",
            "--inhibit_debug_messages=1",
            "--endtime=360000",
            "--collect_traffic=1",
            f"--stats_folder={test_work_path}"
            ] 
    
    run_args = base_run_args + extra_args + [f"--job_ti={trace_path}/trace.index"]
    r = rw.RunWrapper("collect_traffic",{'stats_folder':test_work_path,'work_path':test_work_path,'args':run_args})
    result = r.run(binary_path)
    if result['exit_code'] != 0 :
        print("Command failed with :")
        print(result['out'])
        return

    traffic = mat()
    traffic.read(os.path.join(test_work_path,"traffic.tazm"))
    m=traffic.np_array
    sum0=m.sum(0)
    sum1=m.sum(1)
    if sum(sum1) == 0 :
        print("There is no communication registered in that application, skipping.. ")
        return 

    trace_end= 1+ max(np.max(np.nonzero( sum0)),np.max(np.nonzero(sum1)))
    print(f'Actual trace size is {trace_end}')
    m2=m[:trace_end,:trace_end]

    generate_traffic_image(m2, trace_end , trace_path)

    traffic.np_array=m2
    traffic.write('APPROX',traffic_file)
    t=tarfile.open(trace_archive)
    
    return traffic.np_array
    #plt.imshow(traffic.np_array) 
    



if __name__ == '__main__':

    if len(sys.argv) <= 1:
        print("Syntax: <path to binary> [taz arg1] ... [taz arg N]")
        sys.exit(1)

    binary_path = sys.argv[1]

    # Rough checks
    if not os.path.exists(binary_path):
        print(
            f"Could not find binary path {binary_path}. abort.")
        sys.exit(2)

    #Decompress traces if needed
    for trace_path in glob.glob(f"{script_path}/../tests/apps/*"):
        print(f"Working on path {trace_path}...")
        generate_app_traffic("MEDIUM",binary_path,trace_path, sys.argv[2:])

