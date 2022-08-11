// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef _COMMUNICATOR_HPP
#define _COMMUNICATOR_HPP

#include "GenericPool.hpp"
#include "taz-simulate.hpp"

struct Communicator {
  int job_id;
  std::vector<resind_t> cores;
  uint32_t countdown;
  int epoch;
  commind_t next;
  commind_t index;
  enum State { INVALID, VALID } state;
  bool try_lock() {
    if (countdown == 0) {
      // Start a new countdown
      countdown = (uint32_t)(cores.size() - 1);
      return false;
    }
    countdown--;
    if (countdown == 0) {
      epoch++;
      return true;
    }
    return false;
  }
  Communicator() = default;
  Communicator(const Communicator &x) = delete;
  Communicator(commind_t ind, int job_id_, std::vector<resind_t> &&nodev)
      : job_id(job_id_), index(ind) {
    cores.swap(nodev);
  }
};

template <> struct HeapConsumer<Communicator> {
  static size_t get_heap_footprint(const Communicator &x) {
    return GET_HEAP_FOOTPRINT(x.cores);
  }
};

class CommPool {

  GenericPool<Communicator, commind_t, NoComm, 10> pool;

public:
  CommPool(std::string name) : pool(name) {}
  commind_t create_communicator(const std::vector<resind_t> &nodev,
                                int job_id) {
    commind_t index = NoComm;
    auto &comm = pool.new_element(index);
    comm.job_id = job_id;
    comm.countdown = 0;
    comm.epoch = 0;
    comm.cores = nodev;
    comm.state = Communicator::VALID;
    comm.index = index;
    return index;
  }
  void destroy_communicator(commind_t index) { pool.free_element(index); }

  Communicator &at(commind_t index) { return pool.at(index); }
};

#endif /*_COMMUNICATOR_HPP*/