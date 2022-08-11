#Copyright 2022 Fabien Chaix
#SPDX-License-Identifier: LGPL-2.1-only
#!/usr/bin/env python3

import re
import sys
from dataclasses import dataclass
import os

if len(sys.argv) < 3:
    print("Syntax: <folder containing the MPI primitives list> <path to MPI include directory>")
    exit(-1)

script_path = os.path.dirname(os.path.realpath(__file__))
primitives_list_file = f"{sys.argv[1]}/primitives.list"
pmpi_binding_file = f"{script_path}/../src/profile/pmpi_binding.cpp"
instances_fbs_file = f"{sys.argv[1]}/instances.fbs"


@dataclass(frozen=True)
class Argument:
    base_type:  str
    name:       str
    is_const:   bool
    pointer:    str
    is_array:   bool

    def get_string(self, skip_name=False) -> str:
        ret = self.base_type + " "
        if (self.is_const):
            ret = "const " + ret
        if (self.pointer):
            ret = ret + self.pointer
        if (not skip_name):
            ret = ret+self.name
        if (self.is_array):
            ret = ret+"[]"
        ret = ret.strip(" ")
        return ret

    def __str__(self) -> str:
        return self.get_string()

    def __lt__(self, b) -> bool:
        return str(self) < str(b)


@dataclass(frozen=True)
class Primitive:
    action:     str
    def_str:    str
    func:       str
    outtype:    Argument
    args_str:   str
    args_raw:   list
    args:       list

    def __str__(self) -> str:
        args_list = [str(x) for x in self.args]
        return str(self.outtype) + " " + self.func + "("+",".join(args_list)+")"

# Based on https://stackoverflow.com/questions/60978672/python-string-to-camelcase


def to_camel_case(text):
    s = text.replace("-", " ").replace("_", " ")
    s = s.split()
    return ''.join(i.capitalize() for i in s)


def get_fbs_argument(mpi_argument: Argument):
    mpi_type = mpi_argument.get_string(skip_name=True)
    # Those are usually addresses and offsets, we do not care for replay
    if (mpi_type in ["void", "void *", "const void *", "MPI_Offset",
                     "MPI_Offset *", "MPI_Aint", "MPI_Aint *",
                     "MPI_Aint []", "const MPI_Aint []"]):
        return None

    fbs_type = None
    # Keep integers and strings as is
    if (mpi_type in ["int", "MPI_Count", "MPI_Fint"]):
        fbs_type = "int"
    elif (mpi_type in ["int *", "int []", "const int []", "MPI_Count *", "MPI_Fint *", "const MPI_Fint *"]):
        fbs_type = "[int]"
    elif (mpi_type in ["char *", "const char *", "const char []"]):
        fbs_type = "string"
    elif (mpi_type in ["char ***", "char **[]", "char *[]"]):
        fbs_type = "[string]"
    # Save a string representation of the function pointers
    elif (mpi_type in ["MPI_Comm_copy_attr_function *", "MPI_Comm_delete_attr_function *",
                       "MPI_Comm_errhandler_function *", "MPI_Datarep_conversion_function *",
                       "MPI_Datarep_extent_function *", "MPI_File_errhandler_function *",
                       "MPI_Grequest_cancel_function *", "MPI_Grequest_free_function *",
                       "MPI_Grequest_query_function *", "MPI_Type_copy_attr_function *",
                       "MPI_Type_delete_attr_function *", "MPI_User_function *",
                       "MPI_Win_copy_attr_function *", "MPI_Win_delete_attr_function *",
                       "MPI_Win_errhandler_function *"]):
        fbs_type = "string"
    # Use Data typeto compute sizes in bytes
    elif (mpi_type in ["MPI_Datatype", "MPI_Datatype *",
                       "MPI_Datatype []", "const MPI_Datatype []"]):
        return None
    # Create variable and save participating ranks
    elif (mpi_type in ["MPI_Comm *", "MPI_Group *"]):
        fbs_type = "[int]"
    # Save variable
    elif (mpi_type in ["MPI_Comm", "MPI_Group",
                       "MPI_Errhandler", "MPI_Errhandler *",
                       "MPI_File", "MPI_File *", "MPI_Info", "MPI_Info *",
                       "MPI_Op", "MPI_Op *", "MPI_Win", "MPI_Win *"]):
        fbs_type = "int"
    # Save variables
    elif (mpi_type in ["const MPI_Info []"]):
        fbs_type = "[int]"
    # Create/Save variable and save content (e.g. node)
    elif (mpi_type in ["MPI_Message", "MPI_Message *",
                       "MPI_Request", "MPI_Request *", "MPI_Request []",
                       "MPI_Status *", "MPI_Status []", "const MPI_Status *"]):
        fbs_type = "[int]"
    else:
        print(
            f"Cannot handle argument <{mpi_argument}> with full type <{mpi_type}>, abort")
        sys.exit(10)

    fbs_name = mpi_argument.name.replace('_', '')
    # print((fbs_type, fbs_name))
    return (fbs_type, fbs_name)


mpi_header_file = f"{sys.argv[2]}/mpi.h"
f = open(mpi_header_file, mode="r")
if (not f):
    print(f"Could not open MPI header file <{mpi_header_file}>, abort!")
    sys.exit(-2)

pmpi_primitive_re = re.compile(r".*\s+(PMPI_\w*).*\(.*")
pmpi_primitives = []
for line in f:
    # print(line)
    m = re.match(pmpi_primitive_re, line)
    if (not m):
        continue
    pmpi_primitives.append(m.group(1))

pmpi_primitives.sort()
print(f"In mpi.h, found {len(pmpi_primitives)} PMPI function definitions.")
# print(f"Found those PMPI calls: {pmpi_primitives}")

primitive_re = re.compile(r"\s*(\w+)\s*:\s+(\w+)\s+(MPI_\w*)\s*\((.*)\)")
outtype_re = re.compile(r"\s*(const|)\s*(\w+)\s*(\**)\s*")
arg_re = re.compile(r"\s*(const|)\s*([\w.]+)\s*(\**)\s*(\w*)\s*(\[\s*\]|)\s*")

f = open(primitives_list_file, mode="r")
stats_dict = {'empty': 0, 'comment': 0, 'misformed': 0,
              'abort_no_pmpi_def': 0, 'abort': 0, 'adhoc': 0, 'profile': 0}

if (not f):
    print(f"Could not open MPI primitives list file <{sys.argv[1]}>, abort!")
    sys.exit(-2)

raw_content = f.read()
f.close()
primitives_str = [x.replace('\n', ' ') for x in re.split(r';', raw_content)]
all_argnames = []
all_base_types = []
all_args = []
fbs_instance_members = []

primitives = []

for p_str in primitives_str:
    p_str = p_str.strip(" \t\r\n")
    if len(p_str) == 0:
        stats_dict['empty'] += 1
        continue
    if p_str[0] == '#':
        stats_dict['comment'] += 1
        continue
    m = re.match(primitive_re, p_str)
    if not m:
        stats_dict['misformed'] += 1
        print(f"Skipping <{p_str}>..")
        continue
    # print(f"G[0]={m.group(0)}")
    # print(f"G[1]={m.group(1)}")
    action = m.group(1)

    outtype_str = m.group(2)
    # print(f"Out type=<{m.group(2)}>")
    outtype_m = re.match(outtype_re, outtype_str)
    outtype_is_const = bool(outtype_m.group(1))
    outtype_base_type = outtype_m.group(2)
    # print(f"Out type=<{outtype_m.group(0)}><{outtype_m.group(1)}><{outtype_m.group(2)}><{outtype_m.group(3)}><{outtype_m.group(4)}>")
    outtype_pointer = outtype_m.group(3)
    outtype = Argument(outtype_base_type, "",
                       outtype_is_const, outtype_pointer, False)

    func = m.group(3)
    args_str = m.group(4)
    args_raw = [x.strip(' \t\n\t\r') for x in re.split(r',', args_str)]

    if (f"P{func}" not in pmpi_primitives):
        stats_dict['abort_no_pmpi_def'] += 1
        action = "ABORT"
        # print(f"Skipping function {func} because there is no corresponding PMPI function.")
    elif action == "ABORT":
        stats_dict['abort'] += 1
    elif action == "PROFILE":
        stats_dict['profile'] += 1
    elif action == "ADHOC":
        stats_dict['adhoc'] += 1
    else:
        print(f"For primitive {func}, action {action} is WRONG!")
        sys.exit(5)

    # print(f"Found function {p.func} -> action:{p.action} outtype:{p.outtype.toString()} args:{p.args_str}")

    p = Primitive(action, p_str, func, outtype, args_str, args_raw, list())

    # Parse arguments if we actually need it
    if (action == "PROFILE"):
        for a_str in p.args_raw:
            # print(f"a=<{a_str}>")
            m = re.match(arg_re, a_str)
            # print(f"G[0]={m.group(0)}")
            is_const = bool(m.group(1))
            base_type = m.group(2)
            pointer = m.group(3)
            name = m.group(4)
            is_array = bool(m.group(5))
            all_argnames.append(name)
            all_base_types.append(base_type)
            a = Argument(base_type, name, is_const, pointer, is_array)
            p.args.append(a)
            all_args.append(a)
            fbs_arg = get_fbs_argument(a)
            if (fbs_arg):
                fbs_instance_members.append(fbs_arg)

    # print(f"Gives primitive object {p}")
    primitives.append(p)

stats_str = ", ".join([f"{key}={value}" for key, value in stats_dict.items()])
print(
    f"In the primitive list, found {len(primitives)} functions, of which {stats_str}")

all_base_types = list(set(all_base_types))
all_base_types.sort()
all_argnames = list(set(all_argnames))
all_argnames.sort()
full_args = [str(x) for x in list(set(all_args))]
full_args.sort()
full_types = list(set([x.get_string(True) for x in list(set(all_args))]))
full_types.sort()
fbs_instance_members = list(set(fbs_instance_members))
fbs_instance_members.sort()
# If we get the name multiple times, disambiguate
done_stuff = True
while (done_stuff):
    done_stuff = False
    for idx1, item1 in enumerate(fbs_instance_members):
        match_index = -1
        for idx2, item2 in enumerate(fbs_instance_members):
            if (idx1 == idx2):
                continue
            if (item1[1] == item2[1]):
                done_stuff = True
                chg_index = idx1
                if (len(item1[0]) < len(item2[0])):
                    chg_index = idx2
                fbs_instance_members[chg_index] = (
                    fbs_instance_members[chg_index][0],
                    fbs_instance_members[chg_index][1] + "s")
                print(
                    f"Disambiguated argument name to {item1} and {item2}.")
                break

# print(f"Found {len(all_argnames)} argnames of {(len(all_base_types))} types => Full args ({len(full_args)})")
# print(f"Base types={all_base_types}")
# print(f"Full types={full_types}")
# print(f"Arg names={all_argnames}")
# print(f"Full args={full_args}")
# print(
#    f"Found {len(fbs_instance_members)} framebuffer members={fbs_instance_members}")

primitive_funcs = []
for p in primitives:
    primitive_funcs.append(p.func.upper())

primitive_funcs.sort()
primitive_enum_list = ",".join(primitive_funcs)
# print(f"Primitives={primitive_enum_list}")

# Generate the flatbuffer instances file

f = open(instances_fbs_file, mode="w")

primitive_types = ','.join([to_camel_case(x.func) for x in primitives])

fbs_args = ";\n\t".join([f"{x[1]}: {x[0]}"for x in fbs_instance_members])

f.write(f"""
//IDL file generated by generate_mpi_primitives.py 
//Create an aggregate table for all Primitive types 
//This is the preferred way with flatbuffer,
// since only members that are set are transmitted.

namespace fbs.taz.profile;

enum PrimitiveType : uint16 {{{primitive_types}}}

table PrimitiveInstance {{
    type :PrimitiveType ;
    {fbs_args};
}}
""")
f.close()

# Generate the MPI->PMPI mapping

f = open(pmpi_binding_file, mode="w")
if (not f):
    print(f"Could not open PMPI binding file <pmpi_binding_file>, abort!")
    sys.exit(-3)

# Preamble
f.write("""
//Generated by generate_mpi_primitives.py

#include "taz-profile.hpp"

""")

for p in primitives:
    if (p.action == "ADHOC"):
        # Just skip that one (it should be defined in adhoc.cpp
        continue

    if (p.action == "ABORT"):
        f.write(f"""
{p}{{
  StackTrace stacktrace;
  abort_application(\"{p.func}\", "Primitive is marked Abort in taz-profile primitives-list",
                       stacktrace ); 
  MUST_DIE;      
}}""")
        continue

    if (p.action == "PROFILE"):
        pmpi_args = ",".join([x.name for x in p.args])
        pmpi_call = f"P{p.func}({pmpi_args})"
        f.write(f"""
{p}{{
    {p.outtype} res={pmpi_call};
    return res;      
}}""")
        continue

    print(
        f"I do not know what to do with primitive {p} with type {p.action}. Abort")
    sys.exit(-4)

f.close()
