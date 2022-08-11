// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#pragma once

#include "MemoryConsumer.hpp"
#include "Property.hpp"
#include "common.hpp"
#include "matrix_generated.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace f = flatbuffers;
namespace ts = fbs::taz::profile;

class ApproxMatrix {

  template <typename T> friend class HeapConsumer;

  PropertyMap properties;

  uint32_t nrows;
  uint32_t ncols;
  std::vector<uint64_t> counts;
  uint64_t &_at(uint32_t row, uint32_t col) {
    assert(row < nrows);
    assert(col < ncols);
    uint64_t idx = row * ncols + (uint64_t)col;
    return counts[idx];
  }

  const uint64_t &_at_const(uint32_t row, uint32_t col) const {
    assert(row < nrows);
    assert(col < ncols);
    uint64_t idx = row * ncols + (uint64_t)col;
    return counts[idx];
  }

  void _show_properties() const {
    if (properties.empty())
      return;
    std::cout << "Properties:" << std::endl;
    for (const auto &p : properties)
      std::cout << p.first << " :\t" << p.second << std::endl;
  }

  void _show_approx() const {
    std::cout << "base-2 logarithm of matrix of dimension " << nrows << "x"
              << ncols << std::endl;
    _show_properties();
    std::cout.fill('0');

    // Labels for columns
    std::cout << "  :|";
    for (size_t i = 0; i < ncols; i += 10) {
      std::cout << "--";
      std::cout.width(5);
      std::cout << i;
      std::cout << std::resetiosflags(std::ios_base::adjustfield);
      std::cout << "--|";
    }
    std::cout << std::endl;

    std::cout << "  :";
    std::cout.width(1);
    for (size_t i = 0; i < ncols; i++) {
      std::cout << (i % 10);
    }
    std::cout << std::endl;

    // Now all the rows
    for (size_t i = 0; i < nrows; i++) {
      std::cout.width(2);
      std::cout << i << ":";
      std::cout << std::resetiosflags(std::ios_base::adjustfield);

      for (size_t j = 0; j < ncols; j++) {
        auto x = _at_const(i, j);
        if (x == 0) {
          std::cout << ' ';
          continue;
        }
        uint8_t log2_x = floor_log2(x);
        if (log2_x < 10)
          log2_x += '0';
        else
          log2_x += 'A' - 10;
        std::cout.width(1);
        std::cout << log2_x;
        std::cout << std::resetiosflags(std::ios_base::adjustfield);
      }
      std::cout << ':' << std::endl;
    }

    std::resetiosflags(std::cout.flags());
  }

  template <typename T> static constexpr size_t get_n_digits(T x) {
    if (x == 0)
      return 1;
    float logx = std::ceil(std::log10((float)x));
    return (size_t)logx;
  }

  void _show_exact() const {
    constexpr int LineWidth = 160;

    size_t data_digits = get_n_digits<uint64_t>(
        *(std::max_element(counts.begin(), counts.end())));
    size_t row_digits = get_n_digits<uint32_t>(nrows);
    size_t col_digits = get_n_digits<uint32_t>(ncols);
    size_t ncols_per_line =
        (LineWidth - 4 - row_digits - 2 * col_digits) / (data_digits + 1);
    assert(ncols_per_line > 1);

    std::cout << "Matrix of dimension " << nrows << "x" << ncols << std::endl;
    _show_properties();

    for (size_t i = 0; i < nrows; i++) {
      bool new_row = true;
      size_t col_index = 0;
      for (size_t j = 0; j < ncols; j++) {
        if (new_row) {
          std::cout << std::endl;
          std::cout.fill('0');
          std::cout.width(row_digits);
          std::cout << i << "[";
          std::cout << std::resetiosflags(std::ios_base::adjustfield);
          std::cout.width(col_digits);
          std::cout << j << "," << std::min(j + ncols_per_line, (size_t)ncols)
                    << "[:";
          new_row = false;
        }

        auto x = _at_const(i, j);
        std::cout.fill(' ');
        std::cout.width(data_digits);
        std::cout << x << ' ';
        std::cout << std::resetiosflags(std::ios_base::adjustfield);
        col_index++;
        if (col_index == ncols_per_line) {
          col_index = 0;
          new_row = true;
        }
      }
    }
    std::cout << std::endl;
    std::resetiosflags(std::cout.flags());
  }

public:
  enum Representation { EXACT, APPROX };

  std::vector<std::string> get_property_list() {
    std::vector<std::string> key_list;
    for (auto &kv : properties) {
      key_list.push_back(kv.first);
    }
    return key_list;
  }

  const Property &get_property(const std::string &name) {
    assert_always(properties.find(name) != properties.end(),
                  "Could not find property " << name << " in matrix!");
    return properties[name];
  }

  void insert_property(const std::string &name, Property &&value) {
    properties.insert({name, value});
  }

  uint32_t get_nrows() const { return nrows; }
  uint32_t get_ncols() const { return ncols; }
  static uint8_t convert_to_log(uint64_t x) {
    if (x < 4)
      return x;
    int log2_x = floor_log2(x);
    uint64_t mantissa = x >> (log2_x - 2);
    return (uint8_t)(((log2_x - 1) << 2) | (mantissa & 3));
  }

  static constexpr uint64_t convert_from_log(uint8_t x) {
    if (x < 4)
      return x;
    int log2_x = x >> 2;
    uint64_t res = 0x4 | (x & 0x3);
    return res << (log2_x - 1);
  }

  // If ncols is zero, make a square matrix
  void init(uint32_t _nrows, uint32_t _ncols = 0) {
    nrows = _nrows;
    ncols = _ncols == 0 ? _nrows : _ncols;
    uint64_t size = (uint64_t)nrows * ncols;
    counts.resize(size);
    for (auto &x : counts)
      x = 0;
  }

  void add_weight(uint32_t row, uint32_t col, uint64_t value) {
    _at(row, col) += value;
  }

  void fill(uint64_t value) {
    for (auto &x : counts)
      x = value;
  }

  void fill_sequence(std::vector<uint64_t> values) {
    size_t idx = 0;
    size_t n = values.size();
    for (auto &x : counts) {
      x = values[idx];
      idx++;
      if (idx == n)
        idx = 0;
    }
  }

  void fill_symmetric_sequence(std::vector<uint64_t> values) {
    size_t idx = 0;
    size_t n = values.size();
    for (uint32_t r = 0; r < nrows; r++) {
      _at(r, r) = values[idx++];
      if (idx == n)
        idx = 0;
      for (uint32_t c = r + 1; c < ncols; c++) {
        _at(r, c) = values[idx];
        _at(c, r) = values[idx++];
        if (idx == n)
          idx = 0;
      }
    }
  }

  bool is_symmetric() {
    if (nrows != ncols)
      return false;
    for (uint32_t r = 0; r < nrows; r++)
      for (uint32_t c = r + 1; c < ncols; c++)
        if (_at(r, c) != _at(c, r))
          return false;
    return true;
  }

  // File extension should be tazc
  void save(std::string filename, std::string name, Representation repr) const {
    f::FlatBufferBuilder builder;

    f::Offset<f::Vector<f::Offset<ts::Property>>> property_vector_offset;
    f::Offset<f::Vector<uint8_t>> approx_vector_offset;
    f::Offset<f::Vector<uint64_t>> exact_vector_offset;

    if (not properties.empty())
      property_vector_offset = CreatePropertyMap(properties, builder);

    if (repr == APPROX) {
      std::vector<uint8_t> log_data(nrows * ncols);
      for (size_t i = 0; i < nrows * ncols; i++) {
        auto x = convert_to_log(counts[i]);
        log_data[i] = x;
      }
      approx_vector_offset = builder.CreateVector(log_data);
    } else {
      assert(repr == EXACT);
      exact_vector_offset = builder.CreateVector(counts);
    }

    f::Offset<f::String> name_offset;
    if (not name.empty())
      name_offset = builder.CreateString(name);
    ts::MatrixBuilder matrix(builder);
    matrix.add_nrows(nrows);
    matrix.add_ncols(ncols);
    if (repr == APPROX) {
      matrix.add_approx_data(approx_vector_offset);
    } else {
      matrix.add_exact_data(exact_vector_offset);
    }
    if (not name_offset.IsNull())
      matrix.add_name(name_offset);
    matrix.add_properties(property_vector_offset);
    auto root = matrix.Finish();
    builder.Finish(root, "TAZM");

    std::ofstream matrix_stream(filename.c_str(), std::ios::binary);
    matrix_stream.write((const char *)builder.GetBufferPointer(),
                        builder.GetSize());
    std::cout << "Successfully saved ";
    switch (repr) {
    case EXACT:
      std::cout << "Exact";
      break;
    case APPROX:
      std::cout << "Approximate";
      break;
    }
    std::cout << " representation of matrix of dimension " << nrows << "x"
              << ncols << " to file " << filename << std::endl;
    matrix_stream.close();
  }

  void load(std::string filename) {

    // Check if file can be opened first
    try {
      std::ifstream tmp_file(filename.c_str());
    } catch (std::exception &e) {
      std::stringstream ss;
      ss << "Could not open matrix file <" << filename << "> because "
         << e.what();
      throw std::invalid_argument(ss.str());
    }

    void *ptr = map_file_to_memory(filename);
    auto matrix = ts::GetMatrix(ptr);

    // Load the Matrix flatbuffer instance, and get the number of
    // ranks
    this->init(matrix->nrows(), matrix->ncols());

    if (matrix->properties() != nullptr)
      init_property_map_from_fbs(properties, matrix->properties());

    if (matrix->approx_data() != nullptr) {
      for (size_t i = 0; i < nrows * ncols; i++) {
        auto x = matrix->approx_data()->Get(i);
        auto y = convert_from_log(x);
        // std::cout << "(" << i << "):x=" << (int)x << "->y=" << y <<
        // std::endl;
        counts[i] = y;
      }
    } else {
      assert_always(matrix->exact_data() != nullptr,
                    "Matrix " << filename
                              << " has neither exact nor aproximate data!");
      counts.assign(matrix->exact_data()->begin(), matrix->exact_data()->end());
    }
  }

  uint64_t operator()(uint32_t row, uint32_t col) const {
    return _at_const(row, col);
  }

  /*
    uint64_t& operator()(uint32_t row, uint32_t col) {
      return _at(row, col);
    }
  */

  void show(Representation repr = APPROX) const {
    if (repr == APPROX) {
      _show_approx();
    } else {
      _show_exact();
    }
  }

  bool empty() const { return nrows == 0; }
  bool is_square() const { return nrows == ncols; }
};

template <> struct HeapConsumer<ApproxMatrix> {
  static size_t get_heap_footprint(const ApproxMatrix &x) {
    size_t footprint = GET_HEAP_FOOTPRINT(x.properties);
    footprint += GET_HEAP_FOOTPRINT(x.counts);
    return footprint;
  }
};