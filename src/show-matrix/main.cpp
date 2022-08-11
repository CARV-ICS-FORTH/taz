// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#include "../common/ApproxMatrix.hpp"
#include "../common/common.hpp"
#include <iostream>

void test_matrix_log_functions();

int main(int argc, char *argv[]) {
  ApproxMatrix mat;

  // test_matrix_log_functions();

  if (argc < 3) {
    std::cout
        << "Syntax: <DENSE> <EXACT|APPROX> <matrix file1> ... [matrix file N]"
        << std::endl;
    return -1;
  }

  assert(strcmp(argv[1], "DENSE") == 0);
  ApproxMatrix::Representation repr = ApproxMatrix::APPROX;
  if (strcmp(argv[2], "EXACT") == 0) {
    repr = ApproxMatrix::EXACT;
  } else {
    assert(strcmp(argv[2], "APPROX") == 0);
  }

  for (int i = 3; i < argc; i++) {
    std::cout << "Opening matrix from <" << argv[i] << ">..." << std::endl;
    mat.load(argv[i]);
    if (mat.is_symmetric())
      std::cout << "Matrix is symmetric" << std::endl;
    else
      std::cout << "Matrix is NOT symmetric" << std::endl;
    mat.show(repr);
  }

  return 0;
}
