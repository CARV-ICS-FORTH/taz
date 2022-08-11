#pragma once

#include "common.hpp"
#include "property_generated.h"

namespace f = flatbuffers;
namespace ts = fbs::taz::profile;

class Property {
  enum Type {
    NULL_,
    STRING,
    VECTOR_OF_STRINGS,
    INT,
    VECTOR_OF_INTS,
    DOUBLE,
    VECTOR_OF_DOUBLES
  } type;
  std::vector<std::string> vec_str;
  std::vector<int64_t> vec_i;
  std::vector<double> vec_d;

public:
  Property() : type(NULL_) {}
  Property(std::string str) : type(STRING), vec_str({str}) {}
  Property(const std::vector<std::string> &vec_str)
      : type(VECTOR_OF_STRINGS), vec_str(vec_str) {
    if (vec_str.size() == 1)
      type = STRING;
  }
  Property(int64_t i) : type(INT), vec_i({i}) {}
  Property(const std::vector<int64_t> &vec_i)
      : type(VECTOR_OF_INTS), vec_i(vec_i) {
    if (vec_i.size() == 1)
      type = INT;
  }
  Property(double d) : type(DOUBLE), vec_d({d}) {}
  Property(const std::vector<double> &vec_d)
      : type(VECTOR_OF_DOUBLES), vec_d(vec_d) {
    if (vec_d.size() == 1)
      type = DOUBLE;
  }
  bool is_string() const { return type == STRING; }
  std::string to_string() const {
    assert_always(is_string(),
                  "Property is not a string!") return vec_str.at(0);
  }
  bool is_string_vector() const {
    return type == STRING || type == VECTOR_OF_STRINGS;
  }
  const std::vector<std::string> &to_string_vector() const {
    assert_always(is_string_vector(),
                  "Property is not a string vector!") return vec_str;
  }

  bool is_int() const { return type == INT; }
  int64_t to_int() const {
    assert_always(is_int_vector(),
                  "Property is not an integer!") return vec_i.at(0);
  }
  bool is_int_vector() const { return type == INT || type == VECTOR_OF_INTS; }
  const std::vector<int64_t> &to_int_vector() const {
    assert_always(is_int_vector(),
                  "Property is not a int vector!") return vec_i;
  }

  bool is_double() const { return type == DOUBLE; }
  double to_double() const {
    assert_always(is_double(), "Property is not a double precision floating "
                               "point number!");
    return vec_d.at(0);
  }
  bool is_double_vector() const {
    return type == DOUBLE || type == VECTOR_OF_DOUBLES;
  }
  const std::vector<double> &to_double_vector() const {
    assert_always(is_double_vector(), "Property is not a double vector!");
    return vec_d;
  }
  std::vector<unsigned long> to_mask();
};

std::ostream &operator<<(std::ostream &os, const Property &x);

typedef std::unordered_map<std::string, Property> PropertyMap;

// Fonctions to serialize/deserialize a PropertyMap to/from a flatbuffer
f::Offset<f::Vector<f::Offset<ts::Property>>>
CreatePropertyMap(const PropertyMap &map, f::FlatBufferBuilder &builder);
void init_property_map_from_fbs(
    PropertyMap &map, const f::Vector<f::Offset<ts::Property>> *fbs_vector);

/*
class NamedProperty {
  std::string name;
  Property p;

public:
  NamedProperty() : p() {}
  explicit NamedProperty(const std::string &name, const Property &value)
      : name(name), p(value) {}
  explicit NamedProperty(const std::string &name, std::string str)
      : name(name), p(str) {}
  explicit NamedProperty(const std::string &name,
                         const std::vector<std::string> &vec_str)
      : name(name), p(vec_str) {}
  explicit NamedProperty(const std::string &name, int64_t i)
      : name(name), p(i) {}
  explicit NamedProperty(const std::string &name,
                         const std::vector<int64_t> &vec_i)
      : name(name), p(vec_i) {}
  explicit NamedProperty(const std::string &name, double d)
      : name(name), p(d) {}
  explicit NamedProperty(const std::string &name,
                         const std::vector<double> &vec_d)
      : name(name), p(vec_d) {}

  const std::string get_name() { return name; }
  const Property &get_property() { return p; }

  bool is_string() const { return p.is_string(); }
  std::string to_string() const { return p.to_string(); }
  bool is_string_vector() const { return p.is_string_vector(); }
  const std::vector<std::string> &to_string_vector() const {
    return p.to_string_vector();
  }
  bool is_int() const { return p.is_int(); }
  int64_t to_int() const { return p.to_int(); }
  bool is_int_vector() const { return p.is_int_vector(); }
  const std::vector<int64_t> &to_int_vector() const {
    return p.to_int_vector();
  }
  bool is_double() const { return p.is_double(); }
  double to_double() const { return p.to_double(); }
  bool is_double_vector() const { return p.is_double_vector(); }
  const std::vector<double> &to_double_vector() const {
    return p.to_double_vector();
  }
  std::vector<unsigned long> to_mask() { return p.to_mask(); }
};

*/
