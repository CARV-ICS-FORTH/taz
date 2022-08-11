import os
import subprocess
import re
import glob
from taz.utils import parsing as uparsing
import json

script_path = os.path.dirname(os.path.realpath(__file__))
workspace_path = os.path.join(script_path,"..","..")

class RunWrapper:

    def __init__(self, name, d):
        self.name = name
        self.d = d

    def run(self, binary_path):
        if "working_path" in self.d:
            os.chdir(self.d["working_path"])
        # Remove previous snapshots, to be sure
        snap_pattern = "snap*.py"
        if 'stats_folder' in self.d:
            snap_pattern = self.d['stats_folder']+"/snap*.py"
        old_snapshots = glob.glob(snap_pattern)
        for f in old_snapshots:
            os.remove(f)
        args_protected = '\" \"'.join(self.d['args'])
        cmd = f"${{TAZ_TEST_WRAP_COMMAND}} {binary_path} \"{args_protected}\""
        if not 'silent' in self.d:
            print(f"Will execute {self.name} : {cmd}")

        result = self.d
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=True)
        (result["out"], to_discard) = proc.communicate()
        result['exit_code'] = proc.returncode
        new_snapshots = glob.glob(snap_pattern)
        for f in new_snapshots:
            d = uparsing.parse_python_data(f, "snapshot")
            if d:
                result[d['suffix']] = d
        return result

    def generate_ctest(self):
        ctest_file_path = f"{workspace_path}/tests/CMakeLists.txt"
        relpath = self.d["relpath"]
        # https://stackoverflow.com/questions/3621296/python-cleaning-up-a-string
        rx = re.compile('\W+')
        name_cleaned = "taz_simulate_" + rx.sub('_', self.name).strip()
        # Use a different folder for the results of each test
        stats_pathname = f"tmp/tests/{name_cleaned}"
        stats_path = f"{workspace_path}/{stats_pathname}"
        if not os.path.exists(stats_path):
            os.makedirs(stats_path)
        # Prepare argument list
        args2 = self.d["args"]+[f"--stats_folder=$$/{stats_pathname}"]
        args_protected = [f"\"{x}\"" if " " in x else x for x in args2]
        args_list_cmake = " ".join(args_protected).replace(
            "$$", "${CMAKE_SOURCE_DIR}")
        proxy_params = "${TAZ_TEST_WRAP_COMMAND}"
        test_proxy = ""
        if "test_proxy" in self.d and len(self.d["test_proxy"]) > 0:
            test_proxy = self.d["test_proxy"]
            proxy_params = f"${{CMAKE_SOURCE_DIR}}/scripts/venv/bin/python3 ${{CMAKE_SOURCE_DIR}}/scripts/do_test_{test_proxy}.py {name_cleaned}"
        cmd_file_path=f"{stats_path}/cmd.txt" 
        
        with open(cmd_file_path, "a") as file:
            file.write(f"cd {workspace_path}/{relpath}\n")
            args_list_cmd=args_list_cmake.replace("${CMAKE_SOURCE_DIR}",workspace_path)
            file.write(f'{proxy_params} {workspace_path}/taz-simulate {args_list_cmd}\n')
            file.write(f"cd {workspace_path}\n")

        with open(ctest_file_path, "a") as ctest_file:
            ctest_file.write(f'''
add_test( NAME \"{name_cleaned}\" 
  WORKING_DIRECTORY ${{CMAKE_SOURCE_DIR}}/{relpath}
  COMMAND {proxy_params} $<TARGET_FILE:taz-simulate> {args_list_cmake}
  )\n''')
            if self.d["will_fail"]:
                ctest_file.write(
                    f"set_property (TEST {name_cleaned} PROPERTY WILL_FAIL true)\n")
            timeout = self.d['test_timeout']
            ctest_file.write(
                f"set_property (TEST {name_cleaned} PROPERTY TIMEOUT {timeout})\n")
            if "test_properties" in self.d:
                for key, value in self.d["test_properties"].items():
                    ctest_file.write(
                        f"set_property (TEST {name_cleaned} PROPERTY {key} {value})\n")

    def generate_launch_profile(self, debugger_type):
        args_list_launch = "\",\n\"".join(
            self.d["args"]).replace("$$", "${workspaceFolder}")
        launch_file_path = os.path.join(workspace_path,".vscode/launch.json")
        relpath = self.d['relpath']
        with open(launch_file_path, "a") as launch_file:
            if debugger_type == 'gnu':
                launch_file.write(f'''
            {{
                "name": "{self.name}",
                "type": "cppdbg",
                "request": "launch",
                "program": "${{workspaceFolder}}/build/taz-simulate",
                "args": [
                "{args_list_launch}"
                ],
                "stopAtEntry": false,
                "cwd": "${{workspaceFolder}}/{relpath}",
                "environment": [],
                "externalConsole": false
            }},''')
            elif debugger_type == 'msvc':
                launch_file.write(f'''
            {{
                "name": "{self.name}",
                "type": "cppvsdbg",
                "request": "launch",
                "program": "${{workspaceFolder}}/build/Debug/taz-simulate.exe",
                "args": [
                "{args_list_launch}"
                ],
                "stopAtEntry": false,
                "cwd": "${{workspaceFolder}}/{relpath}",
                "environment": [],
                "console": "internalConsole"
            }},''')
            else:
                print(f"{debugger_type} is not supported!!")
#            "MIMode": "gdb",
#            "setupCommands": [
#                {
#                    "description": "Enable pretty-printing for gdb",
#                   "text": "-enable-pretty-printing",
#                    "ignoreFailures": true
#                },
#                {
#                    "description": "Set Disassembly Flavor to Intel",
#                    "text": "-gdb-set disassembly-flavor intel",
#                    "ignoreFailures": true
#                },
#                {
#                    "description": "Enable break on all exceptions",
#                    "text": "catch throw",
#                    "ignoreFailures": true
#                }
#            ]
