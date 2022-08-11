//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../taz-simulate.hpp"

// Directly inspired from
// https://github.com/open-mpi/ompi/blob/main/ompi/mca/coll/base/coll_base_topo.c
template <typename T, T None> class Tree {

  T comm_size;
  T max_fanout;

  std::vector<T> edges;
  // For each node, the layout is the following:
  //+0            : parent rank
  //+1~max_fanout : chidren ranks
  //+max_fanout+1 : flags

  T &_parent_of(T rank) {
    assert(rank < comm_size);
    return edges[rank * (max_fanout + 2)];
  }

  T &_child_of(T rank, T i) {
    assert(rank < comm_size);
    assert(i < max_fanout);
    return edges[rank * (max_fanout + 2) + 1 + i];
  }

  T &_flags_of(T rank) {
    assert(rank < comm_size);
    return edges[rank * (max_fanout + 2) + 1 + max_fanout];
  }

  void _init(T size, T fanout) {
    assert(size >= 1);
    assert(fanout >= 1);
    comm_size = size;
    max_fanout = fanout;
    edges.resize(comm_size * (2 + fanout));
    for (T i = 0; i < comm_size; i++) {
      edges[i * (2 + fanout)] = None;
      edges[i * (2 + fanout) + 1] = None;
    }
  }

  void _set_tree_element(T rank, T root, T parent, T level, T delta) {
    assert(_parent_of(rank) == None);
    _parent_of(rank) = parent;
    T next_delta = delta * max_fanout;
    T shiftedrank = diff_mod(rank, root, comm_size);
    T i;
    for (i = 0; i < max_fanout; i++) {
      T child = (shiftedrank + (i + 1) * delta);
      if (child >= comm_size)
        break;
      child = (child + root) % comm_size;
      _child_of(rank, i) = child;
      _set_tree_element(child, root, rank, level + 1, next_delta);
    }
    if (i != max_fanout)
      _child_of(rank, i) = None;
  }

  void _set_inorder_bintree_element(T rank, T parent, T size, T shift) {
    T realrank = rank + shift;
    assert(_parent_of(realrank) == None);
    _parent_of(realrank) = parent + shift;

    /* Compute the size of the right subtree */
    T rightsize = size >> 1;

    /* Determine the left and right child of this parent  */
    T lchild = None;
    T rchild = None;
    if (size > 1) {
      assert(rank > 0);
      lchild = rank - 1;
      if (lchild > 0) {
        rchild = rightsize - 1;
      }
    }

    // Deal with left subtree
    if (lchild != None) {
      _child_of(realrank, 0) = lchild + shift;
      _set_inorder_bintree_element(lchild - rightsize, rank - rightsize,
                                   size - rightsize - 1, shift + rightsize);
    } else {
      _child_of(realrank, 0) = None;
      return;
    }

    // Deal with right subtree
    if (rchild != None) {
      _child_of(realrank, 1) = rchild + shift;
      _set_inorder_bintree_element(rchild, rank, rightsize, shift);
    } else
      _child_of(realrank, 1) = None;
  }

  void _set_bmtree_element(T rank, T root, T parent, T mask) {
    T realrank = (rank + root) % comm_size;
    assert(_parent_of(realrank) == None);
    _parent_of(realrank) = parent;

    T child_index = 0;
    while (mask < comm_size) {
      T child = (rank ^ mask);
      if (child >= comm_size)
        break;
      T realchild = (child + root) % comm_size;
      _child_of(realrank, child_index) = realchild;
      mask <<= 1;
      _set_bmtree_element(child, root, realrank, mask);
      child_index++;
    }
    if (child_index < max_fanout)
      _child_of(realrank, child_index) = None;
  }

  void _set_inorder_bmtree_element(T rank, T root, T parent, T mask) {
    T realrank = (rank + root) % comm_size;
    assert(_parent_of(realrank) == None);
    _parent_of(realrank) = parent;

    T child_index = 0;
    while (mask < comm_size) {
      T child = (rank ^ mask);
      T realchild = (child + root) % comm_size;
      if (child < rank) {
        assert(realchild == parent);
        break;
      } else if (child > comm_size)
        break;
      _child_of(realrank, child_index) = realchild;
      mask <<= 1;
      _set_inorder_bmtree_element(child, root, realrank, mask);
      child_index++;
    }
    if (child_index < max_fanout)
      _child_of(realrank, child_index) = None;
  }

  void _set_kmtree_element(T rank, T root, T parent, T radix, T start_mask) {
    T realrank = (rank + root) % comm_size;
    assert(_parent_of(realrank) == None);
    _parent_of(realrank) = parent;
    T child_index = 0;
    T mask = start_mask;
    while (mask > 0) {
      for (T i = 1; i < radix; i++) {
        T child = rank + i * mask;
        if (child >= comm_size)
          break;
        T realchild = (child + root) % comm_size;
        _child_of(realrank, child_index) = realchild;
        _set_kmtree_element(child, root, realrank, radix, mask / radix);
        child_index++;
      }
      mask /= radix;
    }
    if (child_index < max_fanout)
      _child_of(realrank, child_index) = None;
  }

  void _set_chain_element(T rank, T parent, T children) {
    assert(_parent_of(rank) == None);
    _parent_of(rank) = parent;
    if (children == 0) {
      _child_of(rank, 0) = None;
      return;
    }
    T child = (rank + 1) % comm_size;
    _child_of(rank, 0) = child;
    if (max_fanout > 1)
      _child_of(rank, 1) = None;
    _set_chain_element(child, rank, children - 1);
  }

  void _show_element(T rank, T parent, std::stringstream &stream, T level) {
    (void)parent;
    assert(_parent_of(rank) == parent);
    T child0 = _child_of(rank, 0);
    if (child0 == None) {
      LOG_INFO << stream.str() << std::endl;
      return;
    } else {
      stream << "\t->" << child0;
      _show_element(child0, rank, stream, level + 1);
    }

    for (T i = 1; i < max_fanout && _child_of(rank, i) != None; i++) {
      std::stringstream s;
      for (T l = 0; l < level; l++)
        s << " \t  ";
      T child = _child_of(rank, i);
      s << "\t->" << child;
      _show_element(child, rank, s, level + 1);
    }
  }

  void _set_element_level(T rank, T level) {
    _flags_of(rank) = level << 16;
    for (resind_t i = 0; i < get_children_count(rank); i++) {
      _set_element_level(_child_of(rank, i), level + 1);
    }
  }

public:
  void build_tree(T root, T fanout, T size) {
    _init(size, fanout);
    _set_tree_element(root, root, None, 0, 1);
    if (not InhibitVerboseMessages)
      show();
  }

  void build_in_order_bintree(T size) {
    _init(size, 2);
    _set_inorder_bintree_element(comm_size - 1, None, size, 0);
    if (not InhibitVerboseMessages)
      show();
  }

  void build_bmtree(T root, T size, bool in_order) {
    // Estimate maximum fanout
    T fanout = 2;
    T x = size >> 2;
    for (; x > 0; x >>= 1)
      fanout++;
    _init(size, fanout);
    if (in_order) {
      // FIXME: This is buggy, needs fixing...
      MUST_DIE
      //_set_inorder_bmtree_element(0, root, None, 1);
    } else
      _set_bmtree_element(0, root, None, 1);
    if (not InhibitVerboseMessages)
      show();
  }

  void build_kmtree(T root, T radix, T size) {

    /* nchilds <= (radix - 1) * \ceil(\log_{radix}(comm_size)) */
    T log_radix = 0;
    T mask;
    for (mask = 1; mask < size; mask *= radix)
      log_radix++;
    T fanout = (radix - 1) * log_radix;
    _init(size, fanout);
    mask /= radix;
    _set_kmtree_element(0, root, None, radix, mask);
    if (not InhibitVerboseMessages)
      show();
  }

  void build_chain(T root, T fanout, T size) {
    if ((size - 1) < fanout)
      fanout = size - 1;
    _init(size, fanout);
    _parent_of(root) = None;
    if (size == 1) {
      _child_of(root, 0) = None;
      return;
    }
    // Calculate maximum chain length
    T maxchainlen = (size - 1) / fanout;
    T mark = (size - 1) % fanout;
    if (mark != 0) {
      maxchainlen++;
    } else {
      mark = fanout + 1;
    }
    T head = 1;
    // Create the chains
    for (T i = 0; i < fanout; i++) {
      T len = (i >= mark) ? maxchainlen - 1 : maxchainlen;
      T realhead = (root + head) % size;
      _child_of(root, i) = realhead;
      _set_chain_element(realhead, root, len - 1);
      head += len;
    }
    assert(head == size);
    if (not InhibitVerboseMessages)
      show();
  }

  // This needs to be applied after the tree has been built (obviously)
  void compute_levels(T root) {
    assert(is_root(root));
    _set_element_level(root, 0);
  }

  bool is_leaf(T rank) { return _child_of(rank, 0) == None; }
  bool is_root(T rank) { return _parent_of(rank) == None; }
  T get_size() { return comm_size; }
  T get_max_fanout() { return max_fanout; }
  T get_parent(T rank) { return _parent_of(rank); }
  T get_child(T rank, T i) { return _child_of(rank, i); }
  T get_children_count(T rank) {
    T count = 0;
    for (; count < max_fanout && _child_of(rank, count) != None; count++)
      ;
    return count;
  }
  T &get_flags(T rank) { return _flags_of(rank); }

  void show() {
    // Find root
    T root;
    for (root = 0; root < comm_size && not is_root(root); root++)
      ;
    assert(root != comm_size);
    std::stringstream stream;
    stream << root;
    _show_element(root, None, stream, 0);
  }
};