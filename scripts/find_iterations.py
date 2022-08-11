#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os
import math
import functools
import itertools

nflops=1e11

def format_trace_filename(line,index_path):
    l=line.strip()
    if l[0] == '@':
        return index_path+"/"+l[1:]
    return l

def get_list_of_traces(index_filename):
    index_file = open(index_filename, "r")
    lines=index_file.readlines()
    index_path=os.path.dirname(os.path.abspath(index_filename))
    trace_files=[ format_trace_filename(f,index_path) for f in lines if len(f.strip())!=0 ]
    return trace_files


commands_map={}

def get_hash(line):
    tokens=line.split(' ')
    if(len(tokens)<2):
        print(f"Line <{','.join(tokens)}> is not complete, aborting")
        return [0,0,0]
    cmd=tokens[1].strip()
    cmd_hash=hash(cmd)
    if not cmd_hash in commands_map:
        commands_map[cmd_hash]=[cmd,1]
    else:
        commands_map[cmd_hash][1]=commands_map[cmd_hash][1]+1
    #print(f"<{cmd}>")
    if(cmd=="enter_iteration"):
        print("Found iteration markers in that trace file, aborting")
        return [0,0,0]
    h=0
    if cmd=="compute":
        time=float(tokens[2])
        if time >= 1e6:
            time = time / nflops
            #print(f"Line <{line}> has time > 1s")
        return [cmd_hash, math.log2(time),time]
    #h=hash(tokens[1])
    h=hash('_'.join(tokens))
    if h==0:
        print(f"Line <{line}> leads to zero hash, aborting")
        return [0,0,0]
    return [h,0,0]

def do_find(hashes, prefix):
    #A dictionary that contains info about potential cycles
    #0: the element~=line index of the first occurence
    #1: the period of the cycles (in elements)
    cycle_starts={}
    live_cycles={}
    hash_prints=[h[0]%1024 for h in hashes]
    #print(hash_prints)
    print(f"Do find at {prefix}")
    #Start, period, total number of lines
    best_cycle = [0,0,0]
    for index,this_hash in enumerate(hashes):
        #Detect new cycles to test
        #if this_hash !=0:
        if not this_hash[0] in cycle_starts:
            cycle_starts[this_hash[0]]=[index,-1]
        elif cycle_starts[this_hash[0]][1] < 1:
            #If this is the second occurence, note the period
            cycle_starts[this_hash[0]][1]=index-cycle_starts[this_hash[0]][0]
            live_cycles[this_hash[0]]=cycle_starts[this_hash[0]]

        rm_cycles=[]
        for k,v in live_cycles.items():
            #print(f"{k}:{v}")
            length =index-v[0]
            expected_hash=hashes[v[0]+ (length%v[1])]
            if this_hash != expected_hash:
                #period=','.join(str(hashes[v[0]:v[0]+v[1]]))
                if  v[1] >= 2 and length >= 2*v[1] :
                    print(f"Found cycle [{hash_prints[v[0]:v[0]+v[1]]}] start at {v[0]+prefix} with period {v[1]} going on for {length}")
                    alt_best=do_find(hashes[index:],prefix+index)
                    if alt_best[2] > best_cycle[2]:
                        best_cycle=alt_best
                if length > best_cycle[2] and length >= 2*v[1] :
                    best_cycle=[v[0],v[1],length]
                rm_cycles=rm_cycles+[k]

        for rm_hash in rm_cycles:
            cycle_starts[rm_hash][1]=-1
            del live_cycles[rm_hash]
    

    return best_cycle+[len(hashes)]

def map_hash(x,y):
    #print(x)
    return [x[0],x[1][0] == y[0] and math.fabs(x[1][1] -y[1])<1 , 1, y[2] ] 

def red_hash(x,y):
    if not x[1] and not y[1]:
        return [0,False,0,0,False,False,[]]
    if x[1] and not y[1]:
        #print(f"Red({x},{y})->x")
        x[4]=False
        return x
    if not x[1] and y[1]:
        #print(f"Red({x},{y})->y")
        x[3]=False
        return y
    #Need to check if we can merge those
    merge=[x[0],True,x[2]+y[2]]
    #print(f"Red({x},{y})->merge {merge}")
    return merge
    #print("Cannot merge")


def do_find2(hashes):
    #Start index, period, num. lines, compute
    best_cycle=[0,0,0,0]
    hash_prints=[h[0]%1024 for h in hashes]
    #print(hashes[0:100])
    compute_total=functools.reduce(lambda x,y: [0,0,x[2]+y[2]],  hashes)
    print(f"Total computation {compute_total}")
    length=len(hashes)
    for period in range(2,min(500,int(length/2))):
        m=list(map( map_hash, enumerate(hashes[0:length-period]),hashes[period:]))
        
        this_cycle=[0,0,0,0]
        for k,gr in itertools.groupby(m,lambda x:x[1]):
            if not k:
                continue
            g=list(gr)
            numlines=sum([x[2] for x in g])
            comp_time=sum([x[3] for x in g])
            if numlines>this_cycle[2]:
                if comp_time<this_cycle[3]:
                    pass
                    #print("Numlines and compute disagree 1")
                this_cycle=[g[0][0],period,numlines,comp_time]
            elif comp_time>this_cycle[3]:
                pass
                #print("Numlines and compute disagree 2")

        #print(m)
        #red=functools.reduce(red_hash, m )
        #print(red)
        if this_cycle[2] < period:
            continue
        cover=period+this_cycle[2]
        if cover > best_cycle[2]:
            print(f"For period {period}, best sequence starts at {this_cycle[0]} and lasts {cover} lines / {this_cycle[3]} seconds")
            best_cycle=[this_cycle[0],period,cover]

    return best_cycle+[len(hashes)]


def find_best_cycle(trace_filename):
    with open(trace_filename, "r") as file:
        hashes=[get_hash(line) for line in file if len(line.strip()) != 0]

    if 0 in hashes:
        return None

    return do_find2(hashes)

def insert_iteration_marks(list_of_traces,best_cycle):
    compute_change=0
    for rank,inf in enumerate(list_of_traces):
        outf=inf+".tmp"
        with open(inf, "r") as infile, open(outf, "w") as outfile:  
            line_index=0
            for line in infile:
                if(len(line.strip())==0):
                    continue
                if line_index>=best_cycle[0] and line_index<best_cycle[0]+best_cycle[2] and (line_index-best_cycle[0]) % best_cycle[1] ==0 :
                    outfile.write(f"{rank} enter_iteration\n") 
                if line_index==best_cycle[0]+best_cycle[2]: 
                    outfile.write(f"{rank} exit_iterations\n") 
                tokens=line.split(" ")
                if(tokens[1].strip()=="compute" and float(tokens[2])>1e6 ):
                    outfile.write(f"{tokens[0]} {tokens[1]} {float(tokens[2])/nflops}")
                    compute_change=compute_change+1
                else:
                    outfile.write(line)
                line_index=line_index+1    
    print(f"Patched compute time on {compute_change} occurences")

def commit_tmp_files(list_of_traces):
    for inf in list_of_traces:
        outf=inf+".tmp"
        if not os.path.exists(inf) or not os.path.exists(outf):
            print(f"Could not find files for {inf}")
            continue
        os.remove(inf)
        os.rename(outf,inf)

if len(sys.argv) < 2:
    print("Syntax: <path to trace index file>")
    exit()

list_of_traces=get_list_of_traces(sys.argv[1])
print(f"Found {len(list_of_traces)} traces")
best_cycle=find_best_cycle(list_of_traces[0])
for k,v in commands_map.items():
    print(v)

print(best_cycle)
if best_cycle:
    cover_percent=int(100*best_cycle[2]/best_cycle[3])
    print(f"Best cycle starts at line {best_cycle[0]}, with period {best_cycle[1]}, and represent {cover_percent}% of the trace)")
    answer = ""
    while answer not in ["y", "n"]:
        answer = input("OK to continue? [Y/N]? ").lower()
    if answer == "y":
        insert_iteration_marks(list_of_traces,best_cycle)
        commit_tmp_files(list_of_traces)
        print("Done modifying the traces")
    else:
        print("Aborted traces modification")
