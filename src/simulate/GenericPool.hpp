// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _GENERICPOOL_HPP
#define _GENERICPOOL_HPP

#include "taz-simulate.hpp"

template <typename T, typename I, I NoElement, int BSBits>
class GenericPool : public MemoryConsumer {

  static const I BlockSize = (1 << BSBits);
  typedef std::array<T, BlockSize> Block;
  std::vector<Block *> blocks;

  I count;
  I first_free;

public:
  GenericPool<T, I, NoElement, BSBits>(std::string name)
      : MemoryConsumer(name), count(0), first_free(NoElement) {}

  ~GenericPool<T, I, NoElement, BSBits>() {
    for (auto block : blocks)
      delete block;
    blocks.clear();
  }

  T &at_bare(I index) {
    I block_index = index >> BSBits;
    assert(block_index < (I)blocks.size());
    return blocks[block_index]->at(index & (BlockSize - 1));
  }

  T &at(I index) {
    auto &res = at_bare(index);
    assert(res.state != T::INVALID);
    return res;
  }

  T &new_element(I &index) {
    count++;
    if (first_free == NoElement) {
      I ii = BlockSize * (I)blocks.size();
      index = ii;
      ii++;
      first_free = ii;
      blocks.push_back(new Block);
      for (size_t i = 1; i < BlockSize - 1; i++) {
        ii++;
        blocks.back()->at(i).next = ii;
      }
      blocks.back()->back().next = NoElement;
      auto &res = blocks.back()->front();
      res.next = NoElement;
      return res;
    }
    index = first_free;
    auto &res = at_bare(first_free);
    first_free = res.next;
    return res;
  }

  void free_element(I index) {
    assert(count);
    count--;
#ifdef REUSE_POOL_ELEMENTS
    at(index).next = first_free;
    first_free = index;
#endif
    at(index).state = T::INVALID;
  }

  I get_count() const { return count; }

  I get_capacity() const { return BlockSize * (I)blocks.size(); }

  virtual size_t get_footprint() {
    size_t footprint = sizeof(GenericPool<T, I, NoElement, BSBits>);
    footprint += blocks.size() * sizeof(Block);
    for (auto b : blocks) {
      for (size_t i = 1; i < BlockSize - 1; i++) {
        footprint += GET_HEAP_FOOTPRINT(b->at(i));
      }
    }
    return footprint;
  }
};

#endif //_GENERICPOOL_HPP
