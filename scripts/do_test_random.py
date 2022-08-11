#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os
import json
from taz import run_wrapper as rw
from taz.utils import parsing as uparsing
from threading import Thread
from threading import Semaphore
from threading import Event
from threading import Lock
import multiprocessing
import queue

if len(sys.argv) < 3:
    print("Syntax: <test name> <path to binary>")
    exit(-1)

script_path = os.path.dirname(os.path.realpath(__file__))
test_name = sys.argv[1]
binary_path = sys.argv[2]

# Rough checks
test_work_path = f"{script_path}/../tmp/tests/{test_name}"
if not os.path.exists(test_work_path):
    print(
        f"Could not test work path {test_work_path}. abort.")
    sys.exit(1)

# First alternative with a single thread


def run_sweep_seq(test_work_path):
    os.chdir(test_work_path)
    run_file = f"{test_work_path}/runs.py"
    runs = uparsing.parse_python_data(run_file, "runs")
    for line in runs:
        r = rw.RunWrapper(line['name'], json.loads(line['d']))
        result = r.run(binary_path)
        exit_code = result['exit_code']
        if result['exit_code'] != 0:
            print("Run returned exit code {exit_code}! Abort.")
            sys.exit(2)
        final_time = result['final']['final_time']
        if final_time < 2:
            print(
                f"Run simulation completed in less that 2 seconds ({final_time} [s]), which is wrong! Abort.")
            sys.exit(3)


# Second alternative with throttled multithread
def do_run_threads(line, sem, ev, disp_lock, q, test_work_path):
    name = line['name']
    # disp_lock.acquire()
    # print(f"Starting run {name}...")
    # disp_lock.release()
    r = rw.RunWrapper(line['name'], json.loads(line['d']))
    r.d['silent'] = True
    stats_index = q.get()
    # disp_lock.acquire()
    # print(f" {name} got index {stats_index}")
    # disp_lock.release()
    stats_folder = f"{test_work_path}/{stats_index}/"
    r.d['stats_folder'] = stats_folder
    r.d['args'] = r.d['args'] + [f"--stats_folder={stats_folder}"]
    result = r.run(binary_path)
    exit_code = result['exit_code']
    failed = False
    if result['exit_code'] != 0:
        #disp_lock.acquire()
        print(f"Run {name} returned exit code {exit_code}! Abort.")
        failed = True
    if not failed and not 'final' in result:
        #disp_lock.acquire()
        print(f"Could not parse the final snapshot! Abort.")
        failed = True
    if not failed:
        final_time = result['final']['final_time']
        if final_time < 2:
            #disp_lock.acquire()
            print(
                f"Run {name} simulation completed in less that 2 seconds ({final_time} [s]), which is wrong! Abort.")
            failed = True
    if not failed:
        # disp_lock.acquire()
        # print(f" {name} put back index {stats_index}")
        # disp_lock.release()
        q.put(stats_index)
        sem.release()
        return

    print(f"Used {stats_index}")
    print("Command was:")
    print(binary_path+" "+' '.join(result['args']))
    print("Output was:")
    print(result['out'].decode('utf-8'))
    #disp_lock.release()
    ev.set()


def show_progress(preamble,threads,prevlen,disp_lock):
    active_runs=[ str(index) for index,th2 in enumerate(threads) if th2.is_alive()]
    msg=f"{preamble}. Alive: "+" " .join(active_runs)
    newlen=len(msg)
    msg=msg.ljust(prevlen)
    disp_lock.acquire()
    print(f"{msg}\r", end="")
    disp_lock.release()
    return newlen

def run_sweep_threads(test_work_path, report_period=5):
    os.chdir(test_work_path)
    run_file = f"{test_work_path}/runs.py"
    runs = uparsing.parse_python_data(run_file, "runs")
    ncores = multiprocessing.cpu_count()
    sem = Semaphore(ncores)
    ev = Event()
    disp_lock = Lock()
    q = queue.Queue(maxsize=ncores)
    for i in range(ncores):
        if not os.path.exists(str(i)):
            os.mkdir(str(i))
        q.put(i)
    threads = [Thread(target=do_run_threads, name=line['name'],args=(line, sem, ev, disp_lock, q, test_work_path),daemon=True)
               for line in runs]
    prevlen=0
    for index,th in enumerate(threads):
        preamble=f"Waiting to start {index}/{len(threads)}" 
        while not ev.is_set() and not sem.acquire(timeout=5):
            prevlen=show_progress(preamble,threads,prevlen,disp_lock)
        if ev.is_set():
            break
        th.start()
        if (index % 10 == 0):
            prevlen=show_progress(preamble,threads,prevlen,disp_lock)

    print("Waiting for the last runs to complete...")

    for index,th in enumerate(threads):
        preamble=f"Waiting end of {index}" 
        while not ev.is_set() :
            th.join(5)
            if not th.is_alive():
                break
            prevlen=show_progress(preamble,threads,prevlen,disp_lock)
        if ev.is_set():
            break

    if ev.is_set():
        #disp_lock.acquire()
        print("A test failed. Kill whatever run is still ongoing...")
        #disp_lock.release()
        sys.exit(2)

    result = print(f"Successfully ran {len(runs)} simulations. Youhou!")


run_sweep_threads(test_work_path,report_period=5*60)
