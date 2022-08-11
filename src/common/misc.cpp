// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only

#include "ApproxMatrix.hpp"
#include "Property.hpp"
#include "common.hpp"

#ifdef SHOW_DEBUG
bool InhibitVerboseMessages = true;
bool InhibitDebugMessages = false;
#endif

void ___abort(std::string file, int line, std::string expression,
              int exit_code) {
  std::cerr << file << ":" << line << ": " << expression << std::endl;
  // Leave a chance to GDB to trap on exception before destroying the process
  throw std::exception();
  exit(exit_code);
}

int ceil_log2(uint64_t x) {
  static const uint64_t t[6] = {0xFFFFFFFF00000000ull, 0x00000000FFFF0000ull,
                                0x000000000000FF00ull, 0x00000000000000F0ull,
                                0x000000000000000Cull, 0x0000000000000002ull};

  int y = (((x & (x - 1)) == 0) ? 0 : 1);
  int j = 32;
  int i;

  for (i = 0; i < 6; i++) {
    int k = (((x & t[i]) == 0) ? 0 : j);
    y += k;
    x >>= k;
    j >>= 1;
  }
  return y;
}

#ifdef USE_PAPI
void start_profile_region(const char *name) {
  int retval = PAPI_hl_region_begin(name);
  if (retval != PAPI_OK)
    LOG_ERROR << "PAPI got an error when starting region " << name
              << " with error " << retval << " " << PAPI_strerror(retval)
              << std::endl;
}

/* Do some computation here */

void end_profile_region(const char *name) {
  int retval = PAPI_hl_region_end(name);
  if (retval != PAPI_OK)
    LOG_ERROR << "PAPI got an error when ending region " << name
              << " with error " << retval << " " << PAPI_strerror(retval)
              << std::endl;
}

#else
void start_profile_region(const char *name) { (void)name; }
void end_profile_region(const char *name) { (void)name; }
#endif

#ifdef _WIN32
#define STRTOK_RS strtok_s
#else
#define STRTOK_RS strtok_r
#endif

void GenericParser::_trim(char *ptr) {
  auto len = strlen(ptr);
  if (ptr[len - 1] == '\r')
    ptr[len - 1] = '\0';
}

const char *GenericParser::init_parsing(char *str, const char *delimiters) {
  char *ptr = STRTOK_RS(str, delimiters, &strtok_ptr);
  _trim(ptr);
  return ptr;
}

const char *GenericParser::try_next_token(const char *delimiters) {
  char *ptr = STRTOK_RS(nullptr, delimiters, &strtok_ptr);
  if (ptr)
    _trim(ptr);
  return ptr;
}

const char *GenericParser::get_next_token(const char *delimiters) {
  char *ptr = STRTOK_RS(nullptr, delimiters, &strtok_ptr);
  assert(ptr);
  _trim(ptr);
  return ptr;
}

const char *GenericParser::check_last_token(const char *delimiters) {
  const char *ptr = STRTOK_RS(nullptr, delimiters, &strtok_ptr);
  assert(not ptr);
  return ptr;
}

#include <fstream>
#include <ios>
#include <iostream>
#include <string>
#include <unistd.h>

//////////////////////////////////////////////////////////////////////////////
//
// Based on
// https://stackoverflow.com/questions/669438/how-to-get-memory-usage-at-runtime-using-c
//
// process_mem_usage(double &, double &) - takes two doubles by reference,
// attempts to read the system-dependent data for a process' virtual memory
// size and resident set size, and return the results in KB.
//
// On failure, returns 0.0, 0.0

void process_mem_usage(double &vm_usage, double &resident_set) {
  using std::ifstream;
  using std::ios_base;
  using std::string;

  vm_usage = 0.0;
  resident_set = 0.0;

  // 'file' stat seems to give the most reliable results
  //
  ifstream stat_stream("/proc/self/stat", ios_base::in);

  // dummy vars for leading entries in stat that we don't care about
  //
  string pid, comm, state, ppid, pgrp, session, tty_nr;
  string tpgid, flags, minflt, cminflt, majflt, cmajflt;
  string utime, stime, cutime, cstime, priority, nice;
  string O, itrealvalue, starttime;

  // the two fields we want
  //
  unsigned long vsize;
  long rss;

  stat_stream >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >>
      tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >>
      stime >> cutime >> cstime >> priority >> nice >> O >> itrealvalue >>
      starttime >> vsize >> rss; // don't care about the rest

  stat_stream.close();

  long page_size_kb = sysconf(_SC_PAGE_SIZE) /
                      1024; // in case x86-64 is configured to use 2MB pages
  vm_usage = vsize / 1024.0;
  resident_set = rss * page_size_kb;
}

void test_matrix_log_functions() {

  // clang-format off
  uint64_t to_log_python_output[300]={0,1,2,3,4,5,6,7,8,8,9,9,10,10,11,11,12,12,12,12,13,13,13,13,14,14,14,14,15,15,15,15,16,16,16,16,16,16,16,16,17,17,17,17,17,17,17,17,18,18,18,18,18,18,18,18,19,19,19,19,19,19,19,19,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28};
  uint64_t from_log_python_output[200]={0,1,2,3,4,5,6,7,8,10,12,14,16,20,24,28,32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,640,768,896,1024,1280,1536,1792,2048,2560,3072,3584,4096,5120,6144,7168,8192,10240,12288,14336,16384,20480,24576,28672,32768,40960,49152,57344,65536,81920,98304,114688,131072,163840,196608,229376,262144,327680,393216,458752,524288,655360,786432,917504,1048576,1310720,1572864,1835008,2097152,2621440,3145728,3670016,4194304,5242880,6291456,7340032,8388608,10485760,12582912,14680064,16777216,20971520,25165824,29360128,33554432,41943040,50331648,58720256,67108864,83886080,100663296,117440512,134217728,167772160,201326592,234881024,268435456,335544320,402653184,469762048,536870912,671088640,805306368,939524096,1073741824,1342177280,1610612736,1879048192,2147483648,2684354560,3221225472,3758096384,4294967296,5368709120,6442450944,7516192768,8589934592,10737418240,12884901888,15032385536,17179869184,21474836480,25769803776,30064771072,34359738368,42949672960,51539607552,60129542144,68719476736,85899345920,103079215104,120259084288,137438953472,171798691840,206158430208,240518168576,274877906944,343597383680,412316860416,481036337152,549755813888,687194767360,824633720832,962072674304,1099511627776,1374389534720,1649267441664,1924145348608,2199023255552,2748779069440,3298534883328,3848290697216,4398046511104,5497558138880,6597069766656,7696581394432,8796093022208,10995116277760,13194139533312,15393162788864,17592186044416,21990232555520,26388279066624,30786325577728,35184372088832,43980465111040,52776558133248,61572651155456,70368744177664,87960930222080,105553116266496,123145302310912,140737488355328,175921860444160,211106232532992,246290604621824,281474976710656,351843720888320,422212465065984,492581209243648,562949953421312,703687441776640,844424930131968,985162418487296,1125899906842624,1407374883553280,1688849860263936,1970324836974592};
  // clang-format on

  for (uint64_t i = 0; i < 300; i++) {
    uint64_t j = ApproxMatrix::convert_to_log(i);
    uint64_t expected_j = to_log_python_output[i];
    if (j != expected_j) {
      std::cout << "to_log inconsistent for x=" << i << ": C++ gives " << j
                << " and Python gives " << expected_j << std::endl;
      MUST_DIE
    }
  }

  for (uint64_t i = 0; i < 200; i++) {
    uint64_t j = ApproxMatrix::convert_from_log(i);
    uint64_t expected_j = from_log_python_output[i];
    if (j != expected_j) {
      std::cout << "from_log inconsistent for x=" << i << ": C++ gives " << j
                << " and Python gives " << expected_j << std::endl;
      MUST_DIE
    }
  }

  std::cout << "C++ matrix log function conform to Python implementation."
            << std::endl;
}

std::ostream &operator<<(std::ostream &os, const Property &x) {
  if (x.is_string())
    os << x.to_string();
  else if (x.is_string_vector())
    os << x.to_string_vector();
  else if (x.is_double())
    os << x.to_double();
  else if (x.is_double_vector())
    os << x.to_double_vector();
  else if (x.is_int())
    os << x.to_int();
  else if (x.is_int_vector())
    os << x.to_int_vector();
  else
    MUST_DIE
  return os;
}

std::vector<unsigned long> Property::to_mask() {
  std::vector<unsigned long> res;
  unsigned long x = 0;
  if (type == STRING) {
    std::string str = vec_str[0];
    bool is_binary = (str[1] == 'b' || str[1] == 'b');
    bool is_hexadecimal = (str[1] == 'x' || str[1] == 'X');
    assert_always(str[0] == '0' && (is_binary || is_hexadecimal),
                  "Property is not a valid format string <" << str << ">");
    if (is_binary) {
      assert_always(str.length() <= 64 + 2,
                    "Does not support masks more than 64 bits long");
      for (size_t i = 2; i < str.length(); i++) {
        assert_always(str[i] == '1' || str[i] == '0',
                      "Invalid binary mask string <"
                          << str << ">") if (str[i] == '1') x += 1;
        x <<= 1;
      }
    } else {
      // Hexadecimal
      assert_always(str.length() <= 8 + 2,
                    "Does not support masks more than 64 bits long");
      for (size_t i = 2; i < str.length(); i++) {
        char c = str[i];
        bool is_numeric = (c >= '0' && c <= '9');
        bool is_lower = (c >= 'a' && c <= 'f');
        bool is_upper = (c >= 'A' && c <= 'F');
        assert_always(is_numeric || is_lower || is_upper,
                      "Invalid hexadecimal mask string <" << str << ">");
        if (is_numeric)
          x += (c - '0');
        else if (is_lower)
          x += (c - 'a') + 10;
        else if (is_upper)
          x += (c - 'A') + 10;
        x <<= 4;
      }
    }
    res.push_back(x);
    return res;
  }
  assert_always(type == INT,
                "Does not support masks that are not int nor string.");
  res.push_back((unsigned long)vec_i[0]);
  return res;
}

f::Offset<f::Vector<f::Offset<ts::Property>>>
CreatePropertyMap(const PropertyMap &map, f::FlatBufferBuilder &builder) {

  std::vector<f::Offset<ts::Property>> prop_vec;
  for (auto &kv : map) {
    if (kv.second.is_int_vector())
      prop_vec.push_back(ts::CreatePropertyDirect(builder, kv.first.c_str(),
                                                  &(kv.second.to_int_vector()),
                                                  nullptr, nullptr));
    else if (kv.second.is_double_vector())
      prop_vec.push_back(
          ts::CreatePropertyDirect(builder, kv.first.c_str(), nullptr,
                                   &(kv.second.to_double_vector()), nullptr));
    else if (kv.second.is_string_vector()) {
      std::vector<f::Offset<f::String>> string_vec;
      for (auto &s : kv.second.to_string_vector()) {
        string_vec.push_back(builder.CreateString(s));
      }
      prop_vec.push_back(ts::CreatePropertyDirect(
          builder, kv.first.c_str(), nullptr, nullptr, &string_vec));
    } else {
      MUST_DIE
    }
  }

  return builder.CreateVector(prop_vec);
}

void init_property_map_from_fbs(
    PropertyMap &map, const f::Vector<f::Offset<ts::Property>> *fbs_vector) {
  map.clear();
  for (const auto &prop : *fbs_vector) {
    std::string name = prop->name()->str();
    if (prop->v_ints()) {
      std::vector<int64_t> v;
      for (const auto &x : *(prop->v_ints()))
        v.push_back(x);
      map.emplace(name, v);
    } else if (prop->v_floats()) {
      std::vector<double> v;
      for (const auto &x : *(prop->v_floats()))
        v.push_back(x);
      map.emplace(name, v);
    } else if (prop->v_str()) {
      std::vector<std::string> v;
      for (const auto &x : *(prop->v_str()))
        v.push_back(x->str());
      map.emplace(name, v);
    } else {
      MUST_DIE
    }
  }
}

std::vector<MemoryConsumer *> MemoryConsumer::instances;
