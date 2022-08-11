// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#pragma once

#ifdef _WIN32
#include <iso646.h>
#else
#include <sys/resource.h>
#endif

#include <cmath>
#include <cstring>

#include "config.hpp"
#include <algorithm>
#include <array>
#include <boost/program_options.hpp>
#include <ctime>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// #include <cassert>
void ___abort(std::string file, int line, std::string expression,
              int exit_code);
#undef assert
#ifndef NDEBUG
#define assert(x)                                                              \
  if (not(x)) {                                                                \
    std::stringstream ss;                                                      \
    ss << "Assertion " << #x << " failed ";                                    \
    ___abort(__FILE__, __LINE__, ss.str(), -1);                                \
    __builtin_unreachable();                                                   \
  }
#else
#define assert(x)
#endif

#define assert_always(x, message)                                              \
  if (not(x)) {                                                                \
    std::stringstream ss;                                                      \
    ss << "Always assertion " << #x << " failed : " << message;                \
    ___abort(__FILE__, __LINE__, ss.str(), -2);                                \
    __builtin_unreachable();                                                   \
  }

#define MUST_DIE                                                               \
  {                                                                            \
    ___abort(__FILE__, __LINE__, "Should not have reached this line!", -3);    \
    __builtin_unreachable();                                                   \
  }

#define NOT_IMPLEMENTED_YET                                                    \
  {                                                                            \
    ___abort(__FILE__, __LINE__, "This is not implemented yet!", -4);          \
    __builtin_unreachable();                                                   \
  }

#define check_result(x, expected_result)                                       \
  {                                                                            \
    int result = (x);                                                          \
    if (result != expected_result) {                                           \
      std::stringstream ss;                                                    \
      ss << "Expression (" << #x << ") returned " << result << " while "       \
         << expected_result << " was expected";                                \
      ___abort(__FILE__, __LINE__, ss.str(), -5);                              \
      __builtin_unreachable();                                                 \
    }                                                                          \
  }

// Show the many debug messages
#ifndef NDEBUG
#define SHOW_DEBUG
#endif
//  Use PAPI interface to profile regions of code
//  #define USE_PAPI

// Reusing pool elements is much better for performance,
// but it makes debugging more tedious
#ifdef NDEBUG
#define REUSE_POOL_ELEMENTS
#endif

#ifdef SHOW_DEBUG
extern bool InhibitVerboseMessages;
#define LOG_VERBOSE                                                            \
  if (not InhibitVerboseMessages)                                              \
  std::cout
extern bool InhibitDebugMessages;
#define LOG_DEBUG                                                              \
  if (not InhibitDebugMessages)                                                \
  std::cout
#else
constexpr bool InhibitVerboseMessages = true;
#define LOG_VERBOSE                                                            \
  if (0)                                                                       \
  std::cout
constexpr bool InhibitDebugMessages = true;
#define LOG_DEBUG                                                              \
  if (0)                                                                       \
  std::cout
#endif

#define LOG_INFO std::cout
#define LOG_ERROR std::cerr

template <typename T>
std::ostream &operator<<(std::ostream &out, const std::vector<T> &v) {
  for (auto &x : v)
    out << x << " ";
  return out;
}

#ifdef _WIN32
#include <memoryapi.h>

inline void *map_file_to_memory(std::string filename) {
  FILE *file = open(filename.c_str(), 'r');
  auto mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0);
  void *ptr = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  assert_always(ptr);
  return ptr;
}

#else

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

inline void *map_file_to_memory(std::string filename) {
  struct stat file_stats;
  auto fd = open(filename.c_str(), O_RDONLY);
  assert_always(fd != 0, "Cannot open file "
                             << filename << " that we want to map to memory");
  auto res = fstat(fd, &file_stats);
  assert_always(res == 0, "Cannot stat file "
                              << filename << " that we want to map to memory");
  void *ptr = mmap(nullptr, file_stats.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert_always(ptr, "Cannot mmap file " << filename);
  return ptr;
}

#endif

// Miscellaneous math functions

template <typename T> T ceil_log2(T x) {
  assert(x > 0);
  x--;
  T i = 0;
  while (x != 0) {
    i++;
    x >>= 1;
  }
  return i;
}

// TODO: make that efficient with 64 - __builtin_clzl(x);
template <typename T> T floor_log2(T x) {
  assert(x > 0);
  T i = 0;
  while (x != 0) {
    i++;
    x >>= 1;
  }
  i--;
  return i;
}

template <typename T> bool is_pof2(T x) {
  assert(x > 0);
  return x & (x - 1);
}
template <typename T> T trunc_pof2(T x, T p) { return ((x >> p) << p); }
template <typename T> T diff_mod(T x, T y, T m) {
  assert(m > 0);
  return (x + m - y) % m;
}
template <typename T> T ceil_div(T x, T y) {
  assert(y > 0);
  return (x + y - 1) / y;
}

void start_profile_region(const char *name);
void end_profile_region(const char *name);

class GenericParser {
  // Storage for strtok_r
  char *strtok_ptr;
  void _trim(char *ptr);

public:
  const char *init_parsing(char *str, const char *delimiters = " ");
  const char *try_next_token(const char *delimiters = " ");
  const char *get_next_token(const char *delimiters = " ");
  const char *check_last_token(const char *delimiters = " ");
};

void process_mem_usage(double &vm_usage, double &resident_set);
