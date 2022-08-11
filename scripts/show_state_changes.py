#Copyright 2022 Fabien Chaix 
#SPDX-License-Identifier: LGPL-2.1-only

#!/usr/bin/env python3

import sys
import os
from taz.matrix import Matrix 

def show_state_changes(filename) :
    m=Matrix()
    m.read(filename)
    print(m.np_array.shape)
    assert(m.np_array.shape[1]==2)
    link_changes=[0,0]
    node_changes=[0,0]
    arr=[]
    n_events=m.np_array.shape[0]
    for i in range(n_events):
        row=m.np_array[i,:]
        d={'time':row[0]}
        s="@" + str(row[0])+"[s]: "
        is_link= (row[1] & 0x2000000000000000)
        if is_link:
            s+="LINK "
            d['resource_type']="LINK"
        else:
            s+="NODE "
            d['resource_type']="NODE"
        resource_index=row[1] & 0x1FFFFFFFFFFFFFFF
        d['resource_index']=resource_index
        s+=str(resource_index)
        if row[1] & 0x4000000000000000: 
            s+=" RECOVERY"
            d['event_type']="RECOVERY"
            if is_link:
                link_changes[1]+=1
            else:
                node_changes[1]+=1
        else: 
            s+=" FAILURE"
            d['event_type']="FAILURE"
            if is_link:
                link_changes[0]+=1
            else:
                node_changes[0]+=1
        arr.append(d)
        print(s)
        #print(f"{s} 0x{row[1]:x}")
    print(f"Total {n_events} state changes, of which "
        f"node failures: {node_changes[0]} and recoveries: {node_changes[1]};"
        f" and link failures: {link_changes[0]} and recoveries: {link_changes[1]}") 
    return arr

if __name__ == '__main__':

    if len(sys.argv) < 2:
        print("Syntax: <path to state changes matrix>")
        exit(1)

    state_changes_path = sys.argv[1]
    if os.path.isdir(state_changes_path):
        state_changes_path=os.path.join(state_changes_path,"state_changes.tazm")
    base, file_extension = os.path.splitext(state_changes_path)
    if file_extension != ".tazm" or not  os.path.exists(state_changes_path):
        print(f"You need to provide either a .tazm file or a directory that contains a state_changes.tazm file.")
        sys.exit(2)

    arr=show_state_changes(state_changes_path)
    #print(arr)