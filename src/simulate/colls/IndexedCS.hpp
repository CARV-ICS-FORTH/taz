//Copyright 2022 Fabien Chaix 
//SPDX-License-Identifier: LGPL-2.1-only

#include "../AbstractCS.hpp"

struct IndexedCS : public AbstractCS {

private:
  SequenceIndex round_dimensions;

public:
  IndexedCS(size_t dependencies_per_rank, CommFunction &&in_f,
            AtomicAction::DebugInfo d, SequenceIndex dimension)
      : AbstractCS(dependencies_per_rank, in_f, d),
        round_dimensions(dimension) {}

  virtual bool advance(SequenceIndex &index) {
    // assert(index.seq == round_dimensions.seq);
    if (++index.i[0] < round_dimensions.i[0])
      return false;
    index.i[0] = 0;
    if (++index.i[1] < round_dimensions.i[1])
      return false;
    index.i[1] = 0;
    if (++index.i[2] < round_dimensions.i[2])
      return false;
    index.i[2] = 0;
    return (++index.level >= round_dimensions.level);
  }
};