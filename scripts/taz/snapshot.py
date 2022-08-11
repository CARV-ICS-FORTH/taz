import os
import gravis as gv
from taz.utils import parsing as uparsing


class Snapshot:

    def __init__(self, snapshot_path):
        if not os.path.exists(snapshot_path):
            raise ValueError(
                f"Could not find snapshot file at {snapshot_path})")
        self.d = uparsing.parse_python_data(snapshot_path, "snapshot")
        self.path = snapshot_path

    def compare(self, other, attributes_list):
        success = True
        self_nodes = list(self.d['graph']['nodes'].keys())
        s_nnodes = len(self_nodes)
        other_nodes = list(other.d['graph']['nodes'].keys())
        o_nnodes = len(other_nodes)
        if s_nnodes != o_nnodes:
            print(
                f"Different number of nodes (other:{o_nnodes} self:{s_nnodes})")
            success = False

        s_nedges = len(self.d['graph']['edges'])
        o_nedges = len(other.d['graph']['edges'])
        if s_nedges != o_nedges:
            print(
                f"Different number of edges (other:{o_nedges} self:{s_nedges})")
            success = False

        # Prepare attributes
        def compare_dict_values(dict_self, dict_other, key):
            if not key in dict_other:
                print(f"key {key} is not present in other!!!!")
                success = False
                return
            if not key in dict_self:
                print(f"key {key} is not present in self.")
                success = False
                return
            if dict_self[key] != dict_other[key]:
                print(
                    f"key {key} is different (other:{dict_other[key]}  self: {dict_self[key]}).")
                success = False

        node_attributes = []
        edge_attributes = []

        for attr in attributes_list:
            a = attr.split('/')
            domain = a[0].strip()
            name = a[1].strip()
            if domain == "snapshot":
                compare_dict_values(self.d, other.d, name)
            elif domain == "snapshot":
                compare_dict_values(self.d['graph'], other.d['graph'], name)
            elif domain == "node":
                node_attributes = node_attributes+[name]
            elif domain == "edge":
                edge_attributes = edge_attributes+[name]
            else:
                print(f"I do not know domain {domain}")

        # 4. Compare nodes
        nnodes = max(s_nnodes, o_nnodes)
        for i in range(nnodes):
            s_id = None
            s_dict = None
            e_id = None
            e_dict = None
            if i < s_nnodes:
                s_id = self_nodes[i]
                s_dict = self.d['graph']['nodes'][s_id]
            if i < o_nnodes:
                e_id = other_nodes[i]
                o_dict = other.d['graph']['nodes'][e_id]
            if i >= o_nnodes:
                print(f"Added node {s_id} with content {s_dict}")
                success = False
            elif i >= s_nnodes:
                print(f"Removed node {s_id} with content {e_dict}")
                success = False
            elif s_id != e_id:
                print(
                    f"Node IDs mismatch at position {i} (expected:{e_id} this run:{s_id})")
                success = False
            else:
                for name in node_attributes:
                    compare_dict_values(e_dict, s_dict, name)

        # 5. Compare edges
        nedges = max(s_nedges, o_nedges)
        # print(f"n:{nedges}, ex:{e_nedges}, this:{s_nedges}")
        for i in range(nedges):
            s_dict = None
            e_dict = None
            if i < s_nedges:
                s_dict = self.d['graph']['edges'][i]
            if i < o_nedges:
                o_dict = other.d['graph']['edges'][i]
            if i >= o_nedges:
                print(f"Added edge {s_dict}")
                success = False
            elif i >= s_nedges:
                print(f"Removed edge {e_dict}")
                success = False
            else:
                compare_dict_values(e_dict, s_dict, "source")
                compare_dict_values(e_dict, s_dict, "target")
                for name in edge_attributes:
                    compare_dict_values(e_dict, s_dict, name)

        return success

    def show(self, generator, file_extension):
        fig = None
        base, to_discard = os.path.splitext(self.path)
        if (generator == "d3"):
            fig = gv.d3(self.d, graph_height=900,
                        node_size_factor=3, node_drag_fix=True)
        elif (generator == "vis"):
            fig = gv.vis(self.d, graph_height=900,
                         node_size_factor=3, node_drag_fix=True)
        elif (generator == "three"):
            fig = gv.three(self.d, graph_height=900,
                           node_size_factor=3, node_drag_fix=True)
        else:
            raise ValueError(
                f"Does not support this generator ({generator}), abort.")

        if (file_extension == "html"):
            fig.export_html(base+".html", overwrite=True)
        elif (file_extension == "svg"):
            fig.export_svg(base+".svg")
        elif (file_extension == "png"):
            fig.export_png(base+".png")
        else:
            raise ValueError(
                f"Does not support this type of file ({file_extension}), abort.")

        print(f"Exported snapshot to {base+'.'+file_extension}")
