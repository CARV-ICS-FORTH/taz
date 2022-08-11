import pathlib
import os


class TiTrace:

    def __init__(self, n_ranks, start_delay, iteration_delay, end_delay, coll, coll_args):
        self.n_ranks = n_ranks
        self.start_delay = start_delay
        self.iteration_delay = iteration_delay
        self.end_delay = end_delay
        self.coll = coll
        self.coll_args = coll_args

    def write(self, root_path="."):
        root_path = f"{root_path}/tests/collectives/{self.coll}_{self.n_ranks}"
        if not os.path.exists(root_path):
            pathlib.Path(root_path).mkdir(parents=True, exist_ok=True)
        trace_path = f"{root_path}/traces"
        if os.path.exists(trace_path):
            # print(f"Path {trace_path} exists, skipping.")
            return
        print(f"Generate traces for {self.coll} with {self.n_ranks}..")
        os.mkdir(trace_path)
        index_file = open(f"{root_path}/trace.index", "a")
        # for rank in range(n_ranks):
        #    index_file.write(f"@traces/trace.{rank}\n")
        #    trace_file = open(f"{trace_path}/trace.{rank}", "a")
        #    trace_file.write(f"{rank} init\n")
        #    trace_file.write(f"{rank} compute {start_delay}\n")
        #    trace_file.write(f"{rank} enter_iteration\n")
        #    trace_file.write(f"{rank} {coll} {coll_args}\n")
        #    trace_file.write(f"{rank} compute {iteration_delay}\n")
        #    trace_file.write(f"{rank} exit_iterations\n")
        #    trace_file.write(f"{rank} compute {end_delay}\n")
        #    trace_file.write(f"{rank} finalize\n")
        #    trace_file.close()

        index_file.write(f"[{self.n_ranks}]@traces/trace.all\n")
        trace_file = open(f"{trace_path}/trace.all", "a")
        trace_file.write(f"* init\n")
        trace_file.write(f"* compute {self.start_delay}\n")
        trace_file.write(f"* enter_iteration\n")
        trace_file.write(f"* {self.coll} {self.coll_args}\n")
        trace_file.write(f"* compute {self.iteration_delay}\n")
        trace_file.write(f"* exit_iterations\n")
        trace_file.write(f"* compute {self.end_delay}\n")
        trace_file.write(f"* finalize\n")
        trace_file.close()

        index_file.close()
