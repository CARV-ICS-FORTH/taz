// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_MAIN_TAZ_PROFILE_H_
#define FLATBUFFERS_GENERATED_MAIN_TAZ_PROFILE_H_

#include "flatbuffers/flatbuffers.h"

// Ensure the included flatbuffers.h is the same version as when this file was
// generated, otherwise it may not be compatible.
static_assert(FLATBUFFERS_VERSION_MAJOR == 23 &&
              FLATBUFFERS_VERSION_MINOR == 5 &&
              FLATBUFFERS_VERSION_REVISION == 26,
             "Non-compatible flatbuffers version included");

#include "instances_generated.h"

namespace taz {
namespace profile {

struct ProfileHeader;
struct ProfileHeaderBuilder;

struct Exec;
struct ExecBuilder;

struct TestMatch;

struct Jump;
struct JumpBuilder;

struct Return;
struct ReturnBuilder;

struct Exit;
struct ExitBuilder;

struct ProfileFile;
struct ProfileFileBuilder;

enum TraceStep : uint8_t {
  TraceStep_NONE = 0,
  TraceStep_Exec = 1,
  TraceStep_Jump = 2,
  TraceStep_Return = 3,
  TraceStep_Exit = 4,
  TraceStep_MIN = TraceStep_NONE,
  TraceStep_MAX = TraceStep_Exit
};

inline const TraceStep (&EnumValuesTraceStep())[5] {
  static const TraceStep values[] = {
    TraceStep_NONE,
    TraceStep_Exec,
    TraceStep_Jump,
    TraceStep_Return,
    TraceStep_Exit
  };
  return values;
}

inline const char * const *EnumNamesTraceStep() {
  static const char * const names[6] = {
    "NONE",
    "Exec",
    "Jump",
    "Return",
    "Exit",
    nullptr
  };
  return names;
}

inline const char *EnumNameTraceStep(TraceStep e) {
  if (::flatbuffers::IsOutRange(e, TraceStep_NONE, TraceStep_Exit)) return "";
  const size_t index = static_cast<size_t>(e);
  return EnumNamesTraceStep()[index];
}

template<typename T> struct TraceStepTraits {
  static const TraceStep enum_value = TraceStep_NONE;
};

template<> struct TraceStepTraits<taz::profile::Exec> {
  static const TraceStep enum_value = TraceStep_Exec;
};

template<> struct TraceStepTraits<taz::profile::Jump> {
  static const TraceStep enum_value = TraceStep_Jump;
};

template<> struct TraceStepTraits<taz::profile::Return> {
  static const TraceStep enum_value = TraceStep_Return;
};

template<> struct TraceStepTraits<taz::profile::Exit> {
  static const TraceStep enum_value = TraceStep_Exit;
};

bool VerifyTraceStep(::flatbuffers::Verifier &verifier, const void *obj, TraceStep type);
bool VerifyTraceStepVector(::flatbuffers::Verifier &verifier, const ::flatbuffers::Vector<::flatbuffers::Offset<void>> *values, const ::flatbuffers::Vector<uint8_t> *types);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(8) TestMatch FLATBUFFERS_FINAL_CLASS {
 private:
  uint64_t value_;
  uint64_t step_index_;

 public:
  TestMatch()
      : value_(0),
        step_index_(0) {
  }
  TestMatch(uint64_t _value, uint64_t _step_index)
      : value_(::flatbuffers::EndianScalar(_value)),
        step_index_(::flatbuffers::EndianScalar(_step_index)) {
  }
  uint64_t value() const {
    return ::flatbuffers::EndianScalar(value_);
  }
  uint64_t step_index() const {
    return ::flatbuffers::EndianScalar(step_index_);
  }
};
FLATBUFFERS_STRUCT_END(TestMatch, 16);

struct ProfileHeader FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef ProfileHeaderBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_PROGRAM = 4,
    VT_ARGS = 6,
    VT_ENV = 8,
    VT_MPI_VERSION = 10,
    VT_N_PROCESSES = 12,
    VT_HOSTS = 14,
    VT_METRICS = 16,
    VT_METRIC_FIELDS = 18
  };
  const ::flatbuffers::String *program() const {
    return GetPointer<const ::flatbuffers::String *>(VT_PROGRAM);
  }
  const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *args() const {
    return GetPointer<const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *>(VT_ARGS);
  }
  const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *env() const {
    return GetPointer<const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *>(VT_ENV);
  }
  const ::flatbuffers::String *mpi_version() const {
    return GetPointer<const ::flatbuffers::String *>(VT_MPI_VERSION);
  }
  int32_t n_processes() const {
    return GetField<int32_t>(VT_N_PROCESSES, 0);
  }
  const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *hosts() const {
    return GetPointer<const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *>(VT_HOSTS);
  }
  const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *metrics() const {
    return GetPointer<const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *>(VT_METRICS);
  }
  const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *metric_fields() const {
    return GetPointer<const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>> *>(VT_METRIC_FIELDS);
  }
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_PROGRAM) &&
           verifier.VerifyString(program()) &&
           VerifyOffset(verifier, VT_ARGS) &&
           verifier.VerifyVector(args()) &&
           verifier.VerifyVectorOfStrings(args()) &&
           VerifyOffset(verifier, VT_ENV) &&
           verifier.VerifyVector(env()) &&
           verifier.VerifyVectorOfStrings(env()) &&
           VerifyOffset(verifier, VT_MPI_VERSION) &&
           verifier.VerifyString(mpi_version()) &&
           VerifyField<int32_t>(verifier, VT_N_PROCESSES, 4) &&
           VerifyOffset(verifier, VT_HOSTS) &&
           verifier.VerifyVector(hosts()) &&
           verifier.VerifyVectorOfStrings(hosts()) &&
           VerifyOffset(verifier, VT_METRICS) &&
           verifier.VerifyVector(metrics()) &&
           verifier.VerifyVectorOfStrings(metrics()) &&
           VerifyOffset(verifier, VT_METRIC_FIELDS) &&
           verifier.VerifyVector(metric_fields()) &&
           verifier.VerifyVectorOfStrings(metric_fields()) &&
           verifier.EndTable();
  }
};

struct ProfileHeaderBuilder {
  typedef ProfileHeader Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  void add_program(::flatbuffers::Offset<::flatbuffers::String> program) {
    fbb_.AddOffset(ProfileHeader::VT_PROGRAM, program);
  }
  void add_args(::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> args) {
    fbb_.AddOffset(ProfileHeader::VT_ARGS, args);
  }
  void add_env(::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> env) {
    fbb_.AddOffset(ProfileHeader::VT_ENV, env);
  }
  void add_mpi_version(::flatbuffers::Offset<::flatbuffers::String> mpi_version) {
    fbb_.AddOffset(ProfileHeader::VT_MPI_VERSION, mpi_version);
  }
  void add_n_processes(int32_t n_processes) {
    fbb_.AddElement<int32_t>(ProfileHeader::VT_N_PROCESSES, n_processes, 0);
  }
  void add_hosts(::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> hosts) {
    fbb_.AddOffset(ProfileHeader::VT_HOSTS, hosts);
  }
  void add_metrics(::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> metrics) {
    fbb_.AddOffset(ProfileHeader::VT_METRICS, metrics);
  }
  void add_metric_fields(::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> metric_fields) {
    fbb_.AddOffset(ProfileHeader::VT_METRIC_FIELDS, metric_fields);
  }
  explicit ProfileHeaderBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<ProfileHeader> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<ProfileHeader>(end);
    return o;
  }
};

inline ::flatbuffers::Offset<ProfileHeader> CreateProfileHeader(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    ::flatbuffers::Offset<::flatbuffers::String> program = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> args = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> env = 0,
    ::flatbuffers::Offset<::flatbuffers::String> mpi_version = 0,
    int32_t n_processes = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> hosts = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> metrics = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>> metric_fields = 0) {
  ProfileHeaderBuilder builder_(_fbb);
  builder_.add_metric_fields(metric_fields);
  builder_.add_metrics(metrics);
  builder_.add_hosts(hosts);
  builder_.add_n_processes(n_processes);
  builder_.add_mpi_version(mpi_version);
  builder_.add_env(env);
  builder_.add_args(args);
  builder_.add_program(program);
  return builder_.Finish();
}

inline ::flatbuffers::Offset<ProfileHeader> CreateProfileHeaderDirect(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    const char *program = nullptr,
    const std::vector<::flatbuffers::Offset<::flatbuffers::String>> *args = nullptr,
    const std::vector<::flatbuffers::Offset<::flatbuffers::String>> *env = nullptr,
    const char *mpi_version = nullptr,
    int32_t n_processes = 0,
    const std::vector<::flatbuffers::Offset<::flatbuffers::String>> *hosts = nullptr,
    const std::vector<::flatbuffers::Offset<::flatbuffers::String>> *metrics = nullptr,
    const std::vector<::flatbuffers::Offset<::flatbuffers::String>> *metric_fields = nullptr) {
  auto program__ = program ? _fbb.CreateString(program) : 0;
  auto args__ = args ? _fbb.CreateVector<::flatbuffers::Offset<::flatbuffers::String>>(*args) : 0;
  auto env__ = env ? _fbb.CreateVector<::flatbuffers::Offset<::flatbuffers::String>>(*env) : 0;
  auto mpi_version__ = mpi_version ? _fbb.CreateString(mpi_version) : 0;
  auto hosts__ = hosts ? _fbb.CreateVector<::flatbuffers::Offset<::flatbuffers::String>>(*hosts) : 0;
  auto metrics__ = metrics ? _fbb.CreateVector<::flatbuffers::Offset<::flatbuffers::String>>(*metrics) : 0;
  auto metric_fields__ = metric_fields ? _fbb.CreateVector<::flatbuffers::Offset<::flatbuffers::String>>(*metric_fields) : 0;
  return taz::profile::CreateProfileHeader(
      _fbb,
      program__,
      args__,
      env__,
      mpi_version__,
      n_processes,
      hosts__,
      metrics__,
      metric_fields__);
}

struct Exec FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef ExecBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_INSTANCE_TYPE = 4,
    VT_INSTANCE = 6,
    VT_METRIC_FIELDS = 8
  };
  taz::profile::PrimitiveInstance instance_type() const {
    return static_cast<taz::profile::PrimitiveInstance>(GetField<uint8_t>(VT_INSTANCE_TYPE, 0));
  }
  const void *instance() const {
    return GetPointer<const void *>(VT_INSTANCE);
  }
  template<typename T> const T *instance_as() const;
  const taz::profile::SendInstance *instance_as_MPI_SEND() const {
    return instance_type() == taz::profile::PrimitiveInstance_MPI_SEND ? static_cast<const taz::profile::SendInstance *>(instance()) : nullptr;
  }
  const taz::profile::RecvInstance *instance_as_MPI_RECV() const {
    return instance_type() == taz::profile::PrimitiveInstance_MPI_RECV ? static_cast<const taz::profile::RecvInstance *>(instance()) : nullptr;
  }
  const ::flatbuffers::Vector<uint64_t> *metric_fields() const {
    return GetPointer<const ::flatbuffers::Vector<uint64_t> *>(VT_METRIC_FIELDS);
  }
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint8_t>(verifier, VT_INSTANCE_TYPE, 1) &&
           VerifyOffset(verifier, VT_INSTANCE) &&
           VerifyPrimitiveInstance(verifier, instance(), instance_type()) &&
           VerifyOffset(verifier, VT_METRIC_FIELDS) &&
           verifier.VerifyVector(metric_fields()) &&
           verifier.EndTable();
  }
};

template<> inline const taz::profile::SendInstance *Exec::instance_as<taz::profile::SendInstance>() const {
  return instance_as_MPI_SEND();
}

template<> inline const taz::profile::RecvInstance *Exec::instance_as<taz::profile::RecvInstance>() const {
  return instance_as_MPI_RECV();
}

struct ExecBuilder {
  typedef Exec Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  void add_instance_type(taz::profile::PrimitiveInstance instance_type) {
    fbb_.AddElement<uint8_t>(Exec::VT_INSTANCE_TYPE, static_cast<uint8_t>(instance_type), 0);
  }
  void add_instance(::flatbuffers::Offset<void> instance) {
    fbb_.AddOffset(Exec::VT_INSTANCE, instance);
  }
  void add_metric_fields(::flatbuffers::Offset<::flatbuffers::Vector<uint64_t>> metric_fields) {
    fbb_.AddOffset(Exec::VT_METRIC_FIELDS, metric_fields);
  }
  explicit ExecBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<Exec> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<Exec>(end);
    return o;
  }
};

inline ::flatbuffers::Offset<Exec> CreateExec(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    taz::profile::PrimitiveInstance instance_type = taz::profile::PrimitiveInstance_NONE,
    ::flatbuffers::Offset<void> instance = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<uint64_t>> metric_fields = 0) {
  ExecBuilder builder_(_fbb);
  builder_.add_metric_fields(metric_fields);
  builder_.add_instance(instance);
  builder_.add_instance_type(instance_type);
  return builder_.Finish();
}

inline ::flatbuffers::Offset<Exec> CreateExecDirect(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    taz::profile::PrimitiveInstance instance_type = taz::profile::PrimitiveInstance_NONE,
    ::flatbuffers::Offset<void> instance = 0,
    const std::vector<uint64_t> *metric_fields = nullptr) {
  auto metric_fields__ = metric_fields ? _fbb.CreateVector<uint64_t>(*metric_fields) : 0;
  return taz::profile::CreateExec(
      _fbb,
      instance_type,
      instance,
      metric_fields__);
}

struct Jump FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef JumpBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_TEST_VARIABLE = 4,
    VT_TEST_MATCHES = 6
  };
  uint32_t test_variable() const {
    return GetField<uint32_t>(VT_TEST_VARIABLE, 0);
  }
  const ::flatbuffers::Vector<const taz::profile::TestMatch *> *test_matches() const {
    return GetPointer<const ::flatbuffers::Vector<const taz::profile::TestMatch *> *>(VT_TEST_MATCHES);
  }
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint32_t>(verifier, VT_TEST_VARIABLE, 4) &&
           VerifyOffset(verifier, VT_TEST_MATCHES) &&
           verifier.VerifyVector(test_matches()) &&
           verifier.EndTable();
  }
};

struct JumpBuilder {
  typedef Jump Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  void add_test_variable(uint32_t test_variable) {
    fbb_.AddElement<uint32_t>(Jump::VT_TEST_VARIABLE, test_variable, 0);
  }
  void add_test_matches(::flatbuffers::Offset<::flatbuffers::Vector<const taz::profile::TestMatch *>> test_matches) {
    fbb_.AddOffset(Jump::VT_TEST_MATCHES, test_matches);
  }
  explicit JumpBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<Jump> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<Jump>(end);
    return o;
  }
};

inline ::flatbuffers::Offset<Jump> CreateJump(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t test_variable = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<const taz::profile::TestMatch *>> test_matches = 0) {
  JumpBuilder builder_(_fbb);
  builder_.add_test_matches(test_matches);
  builder_.add_test_variable(test_variable);
  return builder_.Finish();
}

inline ::flatbuffers::Offset<Jump> CreateJumpDirect(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t test_variable = 0,
    const std::vector<taz::profile::TestMatch> *test_matches = nullptr) {
  auto test_matches__ = test_matches ? _fbb.CreateVectorOfStructs<taz::profile::TestMatch>(*test_matches) : 0;
  return taz::profile::CreateJump(
      _fbb,
      test_variable,
      test_matches__);
}

struct Return FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef ReturnBuilder Builder;
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           verifier.EndTable();
  }
};

struct ReturnBuilder {
  typedef Return Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  explicit ReturnBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<Return> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<Return>(end);
    return o;
  }
};

inline ::flatbuffers::Offset<Return> CreateReturn(
    ::flatbuffers::FlatBufferBuilder &_fbb) {
  ReturnBuilder builder_(_fbb);
  return builder_.Finish();
}

struct Exit FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef ExitBuilder Builder;
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           verifier.EndTable();
  }
};

struct ExitBuilder {
  typedef Exit Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  explicit ExitBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<Exit> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<Exit>(end);
    return o;
  }
};

inline ::flatbuffers::Offset<Exit> CreateExit(
    ::flatbuffers::FlatBufferBuilder &_fbb) {
  ExitBuilder builder_(_fbb);
  return builder_.Finish();
}

struct ProfileFile FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef ProfileFileBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_HEADER = 4,
    VT_STEPS_TYPE = 6,
    VT_STEPS = 8,
    VT_INSTANCES_TYPE = 10,
    VT_INSTANCES = 12
  };
  const taz::profile::ProfileHeader *header() const {
    return GetPointer<const taz::profile::ProfileHeader *>(VT_HEADER);
  }
  const ::flatbuffers::Vector<uint8_t> *steps_type() const {
    return GetPointer<const ::flatbuffers::Vector<uint8_t> *>(VT_STEPS_TYPE);
  }
  const ::flatbuffers::Vector<::flatbuffers::Offset<void>> *steps() const {
    return GetPointer<const ::flatbuffers::Vector<::flatbuffers::Offset<void>> *>(VT_STEPS);
  }
  const ::flatbuffers::Vector<uint8_t> *instances_type() const {
    return GetPointer<const ::flatbuffers::Vector<uint8_t> *>(VT_INSTANCES_TYPE);
  }
  const ::flatbuffers::Vector<::flatbuffers::Offset<void>> *instances() const {
    return GetPointer<const ::flatbuffers::Vector<::flatbuffers::Offset<void>> *>(VT_INSTANCES);
  }
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_HEADER) &&
           verifier.VerifyTable(header()) &&
           VerifyOffset(verifier, VT_STEPS_TYPE) &&
           verifier.VerifyVector(steps_type()) &&
           VerifyOffset(verifier, VT_STEPS) &&
           verifier.VerifyVector(steps()) &&
           VerifyTraceStepVector(verifier, steps(), steps_type()) &&
           VerifyOffset(verifier, VT_INSTANCES_TYPE) &&
           verifier.VerifyVector(instances_type()) &&
           VerifyOffset(verifier, VT_INSTANCES) &&
           verifier.VerifyVector(instances()) &&
           VerifyPrimitiveInstanceVector(verifier, instances(), instances_type()) &&
           verifier.EndTable();
  }
};

struct ProfileFileBuilder {
  typedef ProfileFile Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  void add_header(::flatbuffers::Offset<taz::profile::ProfileHeader> header) {
    fbb_.AddOffset(ProfileFile::VT_HEADER, header);
  }
  void add_steps_type(::flatbuffers::Offset<::flatbuffers::Vector<uint8_t>> steps_type) {
    fbb_.AddOffset(ProfileFile::VT_STEPS_TYPE, steps_type);
  }
  void add_steps(::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<void>>> steps) {
    fbb_.AddOffset(ProfileFile::VT_STEPS, steps);
  }
  void add_instances_type(::flatbuffers::Offset<::flatbuffers::Vector<uint8_t>> instances_type) {
    fbb_.AddOffset(ProfileFile::VT_INSTANCES_TYPE, instances_type);
  }
  void add_instances(::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<void>>> instances) {
    fbb_.AddOffset(ProfileFile::VT_INSTANCES, instances);
  }
  explicit ProfileFileBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<ProfileFile> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<ProfileFile>(end);
    return o;
  }
};

inline ::flatbuffers::Offset<ProfileFile> CreateProfileFile(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    ::flatbuffers::Offset<taz::profile::ProfileHeader> header = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<uint8_t>> steps_type = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<void>>> steps = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<uint8_t>> instances_type = 0,
    ::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<void>>> instances = 0) {
  ProfileFileBuilder builder_(_fbb);
  builder_.add_instances(instances);
  builder_.add_instances_type(instances_type);
  builder_.add_steps(steps);
  builder_.add_steps_type(steps_type);
  builder_.add_header(header);
  return builder_.Finish();
}

inline ::flatbuffers::Offset<ProfileFile> CreateProfileFileDirect(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    ::flatbuffers::Offset<taz::profile::ProfileHeader> header = 0,
    const std::vector<uint8_t> *steps_type = nullptr,
    const std::vector<::flatbuffers::Offset<void>> *steps = nullptr,
    const std::vector<uint8_t> *instances_type = nullptr,
    const std::vector<::flatbuffers::Offset<void>> *instances = nullptr) {
  auto steps_type__ = steps_type ? _fbb.CreateVector<uint8_t>(*steps_type) : 0;
  auto steps__ = steps ? _fbb.CreateVector<::flatbuffers::Offset<void>>(*steps) : 0;
  auto instances_type__ = instances_type ? _fbb.CreateVector<uint8_t>(*instances_type) : 0;
  auto instances__ = instances ? _fbb.CreateVector<::flatbuffers::Offset<void>>(*instances) : 0;
  return taz::profile::CreateProfileFile(
      _fbb,
      header,
      steps_type__,
      steps__,
      instances_type__,
      instances__);
}

inline bool VerifyTraceStep(::flatbuffers::Verifier &verifier, const void *obj, TraceStep type) {
  switch (type) {
    case TraceStep_NONE: {
      return true;
    }
    case TraceStep_Exec: {
      auto ptr = reinterpret_cast<const taz::profile::Exec *>(obj);
      return verifier.VerifyTable(ptr);
    }
    case TraceStep_Jump: {
      auto ptr = reinterpret_cast<const taz::profile::Jump *>(obj);
      return verifier.VerifyTable(ptr);
    }
    case TraceStep_Return: {
      auto ptr = reinterpret_cast<const taz::profile::Return *>(obj);
      return verifier.VerifyTable(ptr);
    }
    case TraceStep_Exit: {
      auto ptr = reinterpret_cast<const taz::profile::Exit *>(obj);
      return verifier.VerifyTable(ptr);
    }
    default: return true;
  }
}

inline bool VerifyTraceStepVector(::flatbuffers::Verifier &verifier, const ::flatbuffers::Vector<::flatbuffers::Offset<void>> *values, const ::flatbuffers::Vector<uint8_t> *types) {
  if (!values || !types) return !values && !types;
  if (values->size() != types->size()) return false;
  for (::flatbuffers::uoffset_t i = 0; i < values->size(); ++i) {
    if (!VerifyTraceStep(
        verifier,  values->Get(i), types->GetEnum<TraceStep>(i))) {
      return false;
    }
  }
  return true;
}

inline const taz::profile::ProfileFile *GetProfileFile(const void *buf) {
  return ::flatbuffers::GetRoot<taz::profile::ProfileFile>(buf);
}

inline const taz::profile::ProfileFile *GetSizePrefixedProfileFile(const void *buf) {
  return ::flatbuffers::GetSizePrefixedRoot<taz::profile::ProfileFile>(buf);
}

inline const char *ProfileFileIdentifier() {
  return "TAZP";
}

inline bool ProfileFileBufferHasIdentifier(const void *buf) {
  return ::flatbuffers::BufferHasIdentifier(
      buf, ProfileFileIdentifier());
}

inline bool SizePrefixedProfileFileBufferHasIdentifier(const void *buf) {
  return ::flatbuffers::BufferHasIdentifier(
      buf, ProfileFileIdentifier(), true);
}

inline bool VerifyProfileFileBuffer(
    ::flatbuffers::Verifier &verifier) {
  return verifier.VerifyBuffer<taz::profile::ProfileFile>(ProfileFileIdentifier());
}

inline bool VerifySizePrefixedProfileFileBuffer(
    ::flatbuffers::Verifier &verifier) {
  return verifier.VerifySizePrefixedBuffer<taz::profile::ProfileFile>(ProfileFileIdentifier());
}

inline void FinishProfileFileBuffer(
    ::flatbuffers::FlatBufferBuilder &fbb,
    ::flatbuffers::Offset<taz::profile::ProfileFile> root) {
  fbb.Finish(root, ProfileFileIdentifier());
}

inline void FinishSizePrefixedProfileFileBuffer(
    ::flatbuffers::FlatBufferBuilder &fbb,
    ::flatbuffers::Offset<taz::profile::ProfileFile> root) {
  fbb.FinishSizePrefixed(root, ProfileFileIdentifier());
}

}  // namespace profile
}  // namespace taz

#endif  // FLATBUFFERS_GENERATED_MAIN_TAZ_PROFILE_H_
