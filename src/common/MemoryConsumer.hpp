// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "common.hpp"
#include <boost/heap/fibonacci_heap.hpp>

class MemoryConsumer {

  static std::vector<MemoryConsumer *> instances;
  std::string name;

public:
  MemoryConsumer(std::string name) : name(name) { instances.push_back(this); }

  virtual ~MemoryConsumer() {
    // Just to get valgrind pleased...
    auto it = std::find(instances.begin(), instances.end(), this);
    instances.erase(it);
  }

  // This member needs to be overloaded for each child class
  virtual size_t get_footprint() = 0;

  typedef std::pair<std::string, size_t> MemoryFootprintType;
  static std::vector<MemoryFootprintType> get_consumers_footprint() {
    std::vector<MemoryFootprintType> res;
    for (auto c : instances) {
      res.emplace_back(c->name, c->get_footprint());
    }
    return res;
  }
};

template <typename T> struct HeapConsumer {
  static size_t get_heap_footprint(const T & /*x*/) { return 0; }
};

#define GET_HEAP_FOOTPRINT(x) HeapConsumer<decltype((x))>::get_heap_footprint(x)

template <typename T2> struct HeapConsumer<std::vector<T2>> {
  static size_t get_heap_footprint(const std::vector<T2> &x) {
    return x.capacity() * sizeof(T2);
  }
};

template <typename T3> struct HeapConsumer<std::vector<std::vector<T3>>> {
  static size_t get_heap_footprint(const std::vector<std::vector<T3>> &x) {
    size_t footprint = x.capacity() * sizeof(std::vector<T3>);
    for (const auto &e : x) {
      footprint += GET_HEAP_FOOTPRINT(e);
    }
    return footprint;
  }
};

template <typename K, typename V>
struct HeapConsumer<std::vector<std::unordered_map<K, V>>> {
  static size_t get_heap_footprint(const std::unordered_map<K, V> &x) {
    size_t footprint =
        x.bucket_count() * x.bucket_size() * (sizeof(K) + sizeof(V));
    for (const auto &e : x) {
      footprint += GET_HEAP_FOOTPRINT(e.second);
    }
    return footprint;
  }
};

template <typename T2> struct HeapConsumer<boost::heap::fibonacci_heap<T2>> {
  static size_t get_heap_footprint(const boost::heap::fibonacci_heap<T2> &x) {
    return x.size() * sizeof(T2);
  }
};
