
#include "node_snapshotable.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include "aliased_buffer-inl.h"
#include "base_object-inl.h"
#include "blob_serializer_deserializer-inl.h"
#include "debug_utils-inl.h"
#include "embedded_data.h"
#include "encoding_binding.h"
#include "env-inl.h"
#include "json_parser.h"
#include "node_blob.h"
#include "node_builtins.h"
#include "node_contextify.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_file.h"
#include "node_internals.h"
#include "node_main_instance.h"
#include "node_metadata.h"
#include "node_modules.h"
#include "node_process.h"
#include "node_snapshot_builder.h"
#include "node_url.h"
#include "node_v8.h"
#include "node_v8_platform-inl.h"
#include "timers.h"

#if HAVE_INSPECTOR
#include "inspector/worker_inspector.h"  // ParentInspectorHandle
#endif

namespace node {

using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::ObjectTemplate;
using v8::SnapshotCreator;
using v8::StartupData;
using v8::String;
using v8::TryCatch;
using v8::Value;

const uint32_t SnapshotData::kMagic;

std::ostream& operator<<(std::ostream& output,
                         const builtins::CodeCacheInfo& info) {
  output << "<builtins::CodeCacheInfo id=" << info.id
         << ", length=" << info.data.length << ">\n";
  return output;
}

std::ostream& operator<<(std::ostream& output,
                         const std::vector<builtins::CodeCacheInfo>& vec) {
  output << "{\n";
  for (const auto& info : vec) {
    output << info;
  }
  output << "}\n";
  return output;
}

std::ostream& operator<<(std::ostream& output,
                         const std::vector<uint8_t>& vec) {
  output << "{\n";
  for (const auto& i : vec) {
    output << i << ",";
  }
  output << "}";
  return output;
}

std::ostream& operator<<(std::ostream& output,
                         const std::vector<PropInfo>& vec) {
  output << "{\n";
  for (const auto& info : vec) {
    output << "  " << info << ",\n";
  }
  output << "}";
  return output;
}

std::ostream& operator<<(std::ostream& output, const PropInfo& info) {
  output << "{ \"" << info.name << "\", " << std::to_string(info.id) << ", "
         << std::to_string(info.index) << " }";
  return output;
}

std::ostream& operator<<(std::ostream& output,
                         const std::vector<std::string>& vec) {
  output << "{\n";
  for (const auto& info : vec) {
    output << "  \"" << info << "\",\n";
  }
  output << "}";
  return output;
}

std::ostream& operator<<(std::ostream& output, const RealmSerializeInfo& i) {
  output << "{\n"
         << "// -- builtins begins --\n"
         << i.builtins << ",\n"
         << "// -- builtins ends --\n"
         << "// -- persistent_values begins --\n"
         << i.persistent_values << ",\n"
         << "// -- persistent_values ends --\n"
         << "// -- native_objects begins --\n"
         << i.native_objects << ",\n"
         << "// -- native_objects ends --\n"
         << i.context << ",  // context\n"
         << "}";
  return output;
}

std::ostream& operator<<(std::ostream& output, const EnvSerializeInfo& i) {
  output << "{\n"
         << "// -- async_hooks begins --\n"
         << i.async_hooks << ",\n"
         << "// -- async_hooks ends --\n"
         << i.tick_info << ",  // tick_info\n"
         << i.immediate_info << ",  // immediate_info\n"
         << i.timeout_info << ",  // timeout_info\n"
         << "// -- performance_state begins --\n"
         << i.performance_state << ",\n"
         << "// -- performance_state ends --\n"
         << i.exit_info << ",  // exit_info\n"
         << i.stream_base_state << ",  // stream_base_state\n"
         << i.should_abort_on_uncaught_toggle
         << ",  // should_abort_on_uncaught_toggle\n"
         << "// -- principal_realm begins --\n"
         << i.principal_realm << ",\n"
         << "// -- principal_realm ends --\n"
         << "}";
  return output;
}

class SnapshotDeserializer : public BlobDeserializer<SnapshotDeserializer> {
 public:
  explicit SnapshotDeserializer(std::string_view v)
      : BlobDeserializer<SnapshotDeserializer>(
            per_process::enabled_debug_list.enabled(
                DebugCategory::SNAPSHOT_SERDES),
            v) {}

  template <typename T,
            std::enable_if_t<!std::is_same<T, std::string>::value>* = nullptr,
            std::enable_if_t<!std::is_arithmetic<T>::value>* = nullptr>
  T Read();
};

class SnapshotSerializer : public BlobSerializer<SnapshotSerializer> {
 public:
  SnapshotSerializer()
      : BlobSerializer<SnapshotSerializer>(
            per_process::enabled_debug_list.enabled(
                DebugCategory::SNAPSHOT_SERDES)) {
    // Currently the snapshot blob built with an empty script is around 4MB.
    // So use that as the default sink size.
    sink.reserve(4 * 1024 * 1024);
  }

  template <typename T,
            std::enable_if_t<!std::is_same<T, std::string>::value>* = nullptr,
            std::enable_if_t<!std::is_arithmetic<T>::value>* = nullptr>
  size_t Write(const T& data);
};

// Layout of v8::StartupData
// [  4/8 bytes       ] raw_size
// [ |raw_size| bytes ] contents
template <>
v8::StartupData SnapshotDeserializer::Read() {
  Debug("Read<v8::StartupData>()\n");

  int raw_size = ReadArithmetic<int>();
  Debug("size=%d\n", raw_size);

  CHECK_GT(raw_size, 0);  // There should be no startup data of size 0.
  // The data pointer of v8::StartupData would be deleted so it must be new'ed.
  std::unique_ptr<char> buf = std::unique_ptr<char>(new char[raw_size]);
  ReadArithmetic<char>(buf.get(), raw_size);

  return v8::StartupData{buf.release(), raw_size};
}

template <>
size_t SnapshotSerializer::Write(const v8::StartupData& data) {
  Debug("\nWrite<v8::StartupData>() size=%d\n", data.raw_size);

  CHECK_GT(data.raw_size, 0);  // There should be no startup data of size 0.
  size_t written_total = WriteArithmetic<int>(data.raw_size);
  written_total +=
      WriteArithmetic<char>(data.data, static_cast<size_t>(data.raw_size));

  Debug("Write<v8::StartupData>() wrote %d bytes\n\n", written_total);
  return written_total;
}

// Layout of builtins::CodeCacheInfo
// [  4/8 bytes ]  length of the module id string
// [    ...     ]  |length| bytes of module id
// [  4/8 bytes ]  length of module code cache
// [    ...     ]  |length| bytes of module code cache
template <>
builtins::CodeCacheInfo SnapshotDeserializer::Read() {
  Debug("Read<builtins::CodeCacheInfo>()\n");

  std::string id = ReadString();
  auto owning_ptr =
      std::make_shared<std::vector<uint8_t>>(ReadVector<uint8_t>());
  builtins::BuiltinCodeCacheData code_cache_data{std::move(owning_ptr)};
  builtins::CodeCacheInfo result{id, code_cache_data};

  if (is_debug) {
    std::string str = ToStr(result);
    Debug("Read<builtins::CodeCacheInfo>() %s\n", str.c_str());
  }
  return result;
}

template <>
size_t SnapshotSerializer::Write(const builtins::CodeCacheInfo& info) {
  Debug("\nWrite<builtins::CodeCacheInfo>() id = %s"
        ", length=%d\n",
        info.id.c_str(),
        info.data.length);

  size_t written_total = WriteString(info.id);

  written_total += WriteArithmetic<size_t>(info.data.length);
  written_total += WriteArithmetic(info.data.data, info.data.length);

  Debug("Write<builtins::CodeCacheInfo>() wrote %d bytes\n", written_total);
  return written_total;
}

// Layout of PropInfo
// [ 4/8 bytes ]  length of the data name string
// [    ...    ]  |length| bytes of data name
// [  4 bytes  ]  index in the PropInfo vector
// [ 4/8 bytes ]  index in the snapshot blob, can be used with
//                GetDataFromSnapshotOnce().
template <>
PropInfo SnapshotDeserializer::Read() {
  Debug("Read<PropInfo>()\n");

  PropInfo result;
  result.name = ReadString();
  result.id = ReadArithmetic<uint32_t>();
  result.index = ReadArithmetic<SnapshotIndex>();

  if (is_debug) {
    std::string str = ToStr(result);
    Debug("Read<PropInfo>() %s\n", str.c_str());
  }

  return result;
}

template <>
size_t SnapshotSerializer::Write(const PropInfo& data) {
  if (is_debug) {
    std::string str = ToStr(data);
    Debug("Write<PropInfo>() %s\n", str.c_str());
  }

  size_t written_total = WriteString(data.name);
  written_total += WriteArithmetic<uint32_t>(data.id);
  written_total += WriteArithmetic<SnapshotIndex>(data.index);

  Debug("Write<PropInfo>() wrote %d bytes\n", written_total);
  return written_total;
}

// Layout of AsyncHooks::SerializeInfo
// [ 4/8 bytes ]  snapshot index of async_ids_stack
// [ 4/8 bytes ]  snapshot index of fields
// [ 4/8 bytes ]  snapshot index of async_id_fields
// [ 4/8 bytes ]  snapshot index of js_execution_async_resources
// [ 4/8 bytes ]  length of native_execution_async_resources
// [   ...     ]  snapshot indices of each element in
//                native_execution_async_resources
template <>
AsyncHooks::SerializeInfo SnapshotDeserializer::Read() {
  Debug("Read<AsyncHooks::SerializeInfo>()\n");

  AsyncHooks::SerializeInfo result;
  result.async_ids_stack = ReadArithmetic<AliasedBufferIndex>();
  result.fields = ReadArithmetic<AliasedBufferIndex>();
  result.async_id_fields = ReadArithmetic<AliasedBufferIndex>();
  result.js_execution_async_resources = ReadArithmetic<SnapshotIndex>();
  result.native_execution_async_resources = ReadVector<SnapshotIndex>();

  if (is_debug) {
    std::string str = ToStr(result);
    Debug("Read<AsyncHooks::SerializeInfo>() %s\n", str.c_str());
  }

  return result;
}
template <>
size_t SnapshotSerializer::Write(const AsyncHooks::SerializeInfo& data) {
  if (is_debug) {
    std::string str = ToStr(data);
    Debug("Write<AsyncHooks::SerializeInfo>() %s\n", str.c_str());
  }

  size_t written_total =
      WriteArithmetic<AliasedBufferIndex>(data.async_ids_stack);
  written_total += WriteArithmetic<AliasedBufferIndex>(data.fields);
  written_total += WriteArithmetic<AliasedBufferIndex>(data.async_id_fields);
  written_total +=
      WriteArithmetic<SnapshotIndex>(data.js_execution_async_resources);
  written_total +=
      WriteVector<SnapshotIndex>(data.native_execution_async_resources);

  Debug("Write<AsyncHooks::SerializeInfo>() wrote %d bytes\n", written_total);
  return written_total;
}

// Layout of TickInfo::SerializeInfo
// [ 4/8 bytes ]  snapshot index of fields
template <>
TickInfo::SerializeInfo SnapshotDeserializer::Read() {
  Debug("Read<TickInfo::SerializeInfo>()\n");

  TickInfo::SerializeInfo result;
  result.fields = ReadArithmetic<AliasedBufferIndex>();

  if (is_debug) {
    std::string str = ToStr(result);
    Debug("Read<TickInfo::SerializeInfo>() %s\n", str.c_str());
  }

  return result;
}

template <>
size_t SnapshotSerializer::Write(const TickInfo::SerializeInfo& data) {
  if (is_debug) {
    std::string str = ToStr(data);
    Debug("Write<TickInfo::SerializeInfo>() %s\n", str.c_str());
  }

  size_t written_total = WriteArithmetic<AliasedBufferIndex>(data.fields);

  Debug("Write<TickInfo::SerializeInfo>() wrote %d bytes\n", written_total);
  return written_total;
}

// Layout of TickInfo::SerializeInfo
// [ 4/8 bytes ]  snapshot index of fields
template <>
ImmediateInfo::SerializeInfo SnapshotDeserializer::Read() {
  Debug("Read<ImmediateInfo::SerializeInfo>()\n");

  ImmediateInfo::SerializeInfo result;
  result.fields = ReadArithmetic<AliasedBufferIndex>();
  if (is_debug) {
    std::string str = ToStr(result);
    Debug("Read<ImmediateInfo::SerializeInfo>() %s\n", str.c_str());
  }
  return result;
}

template <>
size_t SnapshotSerializer::Write(const ImmediateInfo::SerializeInfo& data) {
  if (is_debug) {
    std::string str = ToStr(data);
    Debug("Write<ImmediateInfo::SerializeInfo>() %s\n", str.c_str());
  }

  size_t written_total = WriteArithmetic<AliasedBufferIndex>(data.fields);

  Debug("Write<ImmediateInfo::SerializeInfo>() wrote %d bytes\n",
        written_total);
  return written_total;
}

// Layout of PerformanceState::SerializeInfo
// [ 4/8 bytes ]  snapshot index of root
// [ 4/8 bytes ]  snapshot index of milestones
// [ 4/8 bytes ]  snapshot index of observers
template <>
performance::PerformanceState::SerializeInfo SnapshotDeserializer::Read() {
  Debug("Read<PerformanceState::SerializeInfo>()\n");

  performance::PerformanceState::SerializeInfo result;
  result.root = ReadArithmetic<AliasedBufferIndex>();
  result.milestones = ReadArithmetic<AliasedBufferIndex>();
  result.observers = ReadArithmetic<AliasedBufferIndex>();
  if (is_debug) {
    std::string str = ToStr(result);
    Debug("Read<PerformanceState::SerializeInfo>() %s\n", str.c_str());
  }
  return result;
}

template <>
size_t SnapshotSerializer::Write(
    const performance::PerformanceState::SerializeInfo& data) {
  if (is_debug) {
    std::string str = ToStr(data);
    Debug("Write<PerformanceState::SerializeInfo>() %s\n", str.c_str());
  }

  size_t written_total = WriteArithmetic<AliasedBufferIndex>(data.root);
  written_total += WriteArithmetic<AliasedBufferIndex>(data.milestones);
  written_total += WriteArithmetic<AliasedBufferIndex>(data.observers);

  Debug("Write<PerformanceState::SerializeInfo>() wrote %d bytes\n",
        written_total);
  return written_total;
}

// Layout of IsolateDataSerializeInfo
// [ 4/8 bytes ]  length of primitive_values vector
// [    ...    ]  |length| of primitive_values indices
// [ 4/8 bytes ]  length of template_values vector
// [    ...    ]  |length| of PropInfo data
template <>
IsolateDataSerializeInfo SnapshotDeserializer::Read() {
  Debug("Read<IsolateDataSerializeInfo>()\n");

  IsolateDataSerializeInfo result;
  result.primitive_values = ReadVector<SnapshotIndex>();
  result.template_values = ReadVector<PropInfo>();
  if (is_debug) {
    std::string str = ToStr(result);
    Debug("Read<IsolateDataSerializeInfo>() %s\n", str.c_str());
  }
  return result;
}

template <>
size_t SnapshotSerializer::Write(const IsolateDataSerializeInfo& data) {
  if (is_debug) {
    std::string str = ToStr(data);
    Debug("Write<IsolateDataSerializeInfo>() %s\n", str.c_str());
  }

  size_t written_total = WriteVector<SnapshotIndex>(data.primitive_values);
  written_total += WriteVector<PropInfo>(data.template_values);

  Debug("Write<IsolateDataSerializeInfo>() wrote %d bytes\n", written_total);
  return written_total;
}

template <>
RealmSerializeInfo SnapshotDeserializer::Read() {
  Debug("Read<RealmSerializeInfo>()\n");
  RealmSerializeInfo result;
  result.builtins = ReadVector<std::string>();
  result.persistent_values = ReadVector<PropInfo>();
  result.native_objects = ReadVector<PropInfo>();
  result.context = ReadArithmetic<SnapshotIndex>();
  return result;
}

template <>
size_t SnapshotSerializer::Write(const RealmSerializeInfo& data) {
  if (is_debug) {
    std::string str = ToStr(data);
    Debug("\nWrite<RealmSerializeInfo>() %s\n", str.c_str());
  }

  // Use += here to ensure order of evaluation.
  size_t written_total = WriteVector<std::string>(data.builtins);
  written_total += WriteVector<PropInfo>(data.persistent_values);
  written_total += WriteVector<PropInfo>(data.native_objects);
  written_total += WriteArithmetic<SnapshotIndex>(data.context);

  Debug("Write<RealmSerializeInfo>() wrote %d bytes\n", written_total);
  return written_total;
}

template <>
EnvSerializeInfo SnapshotDeserializer::Read() {
  Debug("Read<EnvSerializeInfo>()\n");
  EnvSerializeInfo result;
  result.async_hooks = Read<AsyncHooks::SerializeInfo>();
  result.tick_info = Read<TickInfo::SerializeInfo>();
  result.immediate_info = Read<ImmediateInfo::SerializeInfo>();
  result.timeout_info = ReadArithmetic<AliasedBufferIndex>();
  result.performance_state =
      Read<performance::PerformanceState::SerializeInfo>();
  result.exit_info = ReadArithmetic<AliasedBufferIndex>();
  result.stream_base_state = ReadArithmetic<AliasedBufferIndex>();
  result.should_abort_on_uncaught_toggle = ReadArithmetic<AliasedBufferIndex>();
  result.principal_realm = Read<RealmSerializeInfo>();
  return result;
}

template <>
size_t SnapshotSerializer::Write(const EnvSerializeInfo& data) {
  if (is_debug) {
    std::string str = ToStr(data);
    Debug("\nWrite<EnvSerializeInfo>() %s\n", str.c_str());
  }

  // Use += here to ensure order of evaluation.
  size_t written_total = Write<AsyncHooks::SerializeInfo>(data.async_hooks);
  written_total += Write<TickInfo::SerializeInfo>(data.tick_info);
  written_total += Write<ImmediateInfo::SerializeInfo>(data.immediate_info);
  written_total += WriteArithmetic<AliasedBufferIndex>(data.timeout_info);
  written_total += Write<performance::PerformanceState::SerializeInfo>(
      data.performance_state);
  written_total += WriteArithmetic<AliasedBufferIndex>(data.exit_info);
  written_total += WriteArithmetic<AliasedBufferIndex>(data.stream_base_state);
  written_total +=
      WriteArithmetic<AliasedBufferIndex>(data.should_abort_on_uncaught_toggle);
  written_total += Write<RealmSerializeInfo>(data.principal_realm);

  Debug("Write<EnvSerializeInfo>() wrote %d bytes\n", written_total);
  return written_total;
}

// Layout of SnapshotMetadata
// [  1 byte   ]  type of the snapshot
// [ 4/8 bytes ]  length of the node version string
// [    ...    ]  |length| bytes of node version
// [ 4/8 bytes ]  length of the node arch string
// [    ...    ]  |length| bytes of node arch
// [ 4/8 bytes ]  length of the node platform string
// [    ...    ]  |length| bytes of node platform
// [  4 bytes  ]  v8 cache version tag
template <>
SnapshotMetadata SnapshotDeserializer::Read() {
  Debug("Read<SnapshotMetadata>()\n");

  SnapshotMetadata result;
  result.type = static_cast<SnapshotMetadata::Type>(ReadArithmetic<uint8_t>());
  result.node_version = ReadString();
  result.node_arch = ReadString();
  result.node_platform = ReadString();
  result.flags = static_cast<SnapshotFlags>(ReadArithmetic<uint32_t>());

  if (is_debug) {
    std::string str = ToStr(result);
    Debug("Read<SnapshotMetadata>() %s\n", str.c_str());
  }
  return result;
}

template <>
size_t SnapshotSerializer::Write(const SnapshotMetadata& data) {
  if (is_debug) {
    std::string str = ToStr(data);
    Debug("\nWrite<SnapshotMetadata>() %s\n", str.c_str());
  }
  size_t written_total = 0;
  // We need the Node.js version, platform and arch to match because
  // Node.js may perform synchronizations that are platform-specific and they
  // can be changed in semver-patches.
  Debug("Write snapshot type %d\n", static_cast<uint8_t>(data.type));
  written_total += WriteArithmetic<uint8_t>(static_cast<uint8_t>(data.type));
  Debug("Write Node.js version %s\n", data.node_version.c_str());
  written_total += WriteString(data.node_version);
  Debug("Write Node.js arch %s\n", data.node_arch);
  written_total += WriteString(data.node_arch);
  Debug("Write Node.js platform %s\n", data.node_platform);
  written_total += WriteString(data.node_platform);
  Debug("Write snapshot flags %" PRIx32 "\n",
        static_cast<uint32_t>(data.flags));
  written_total += WriteArithmetic<uint32_t>(static_cast<uint32_t>(data.flags));
  return written_total;
}

// Layout of the snapshot blob
// [   4 bytes    ]  kMagic
// [   4/8 bytes  ]  length of Node.js version string
// [    ...       ]  contents of Node.js version string
// [   4/8 bytes  ]  length of Node.js arch string
// [    ...       ]  contents of Node.js arch string
// [    ...       ]  v8_snapshot_blob_data from SnapshotCreator::CreateBlob()
// [    ...       ]  isolate_data_info
// [    ...       ]  env_info
// [    ...       ]  code_cache

std::vector<char> SnapshotData::ToBlob() const {
  std::vector<char> result;
  SnapshotSerializer w;

  w.Debug("SnapshotData::ToBlob()\n");

  size_t written_total = 0;

  // Metadata
  w.Debug("0x%x: Write magic %" PRIx32 "\n", w.sink.size(), kMagic);
  written_total += w.WriteArithmetic<uint32_t>(kMagic);
  w.Debug("0x%x: Write metadata\n", w.sink.size());
  written_total += w.Write<SnapshotMetadata>(metadata);
  w.Debug("0x%x: Write snapshot blob\n", w.sink.size());
  written_total += w.Write<v8::StartupData>(v8_snapshot_blob_data);
  w.Debug("0x%x: Write IsolateDataSerializeInfo\n", w.sink.size());
  written_total += w.Write<IsolateDataSerializeInfo>(isolate_data_info);
  w.Debug("0x%x: Write EnvSerializeInfo\n", w.sink.size());
  written_total += w.Write<EnvSerializeInfo>(env_info);
  w.Debug("0x%x: Write CodeCacheInfo\n", w.sink.size());
  written_total += w.WriteVector<builtins::CodeCacheInfo>(code_cache);
  w.Debug("SnapshotData::ToBlob() Wrote %d bytes\n", written_total);

  // Return using the temporary value to enable copy elision.
  std::swap(result, w.sink);
  return result;
}

void SnapshotData::ToFile(FILE* out) const {
  const std::vector<char> sink = ToBlob();
  size_t num_written = fwrite(sink.data(), sink.size(), 1, out);
  CHECK_EQ(num_written, 1);
  CHECK_EQ(fflush(out), 0);
}

const SnapshotData* SnapshotData::FromEmbedderWrapper(
    const EmbedderSnapshotData* data) {
  return data != nullptr ? data->impl_ : nullptr;
}

EmbedderSnapshotData::Pointer SnapshotData::AsEmbedderWrapper() const {
  return EmbedderSnapshotData::Pointer{new EmbedderSnapshotData(this, false)};
}

bool SnapshotData::FromFile(SnapshotData* out, FILE* in) {
  return FromBlob(out, ReadFileSync(in));
}

bool SnapshotData::FromBlob(SnapshotData* out, const std::vector<char>& in) {
  return FromBlob(out, std::string_view(in.data(), in.size()));
}

bool SnapshotData::FromBlob(SnapshotData* out, std::string_view in) {
  SnapshotDeserializer r(in);
  r.Debug("SnapshotData::FromBlob()\n");

  DCHECK_EQ(out->data_ownership, SnapshotData::DataOwnership::kOwned);

  // Metadata
  uint32_t magic = r.ReadArithmetic<uint32_t>();
  r.Debug("Read magic %" PRIx32 "\n", magic);
  CHECK_EQ(magic, kMagic);
  out->metadata = r.Read<SnapshotMetadata>();
  r.Debug("Read metadata\n");
  if (!out->Check()) {
    return false;
  }

  out->v8_snapshot_blob_data = r.Read<v8::StartupData>();
  r.Debug("Read isolate_data_info\n");
  out->isolate_data_info = r.Read<IsolateDataSerializeInfo>();
  out->env_info = r.Read<EnvSerializeInfo>();
  r.Debug("Read code_cache\n");
  out->code_cache = r.ReadVector<builtins::CodeCacheInfo>();

  r.Debug("SnapshotData::FromBlob() read %d bytes\n", r.read_total);
  return true;
}

bool SnapshotData::Check() const {
  if (metadata.node_version != per_process::metadata.versions.node) {
    fprintf(stderr,
            "Failed to load the startup snapshot because it was built with"
            "Node.js version %s and the current Node.js version is %s.\n",
            metadata.node_version.c_str(),
            NODE_VERSION);
    return false;
  }

  if (metadata.node_arch != per_process::metadata.arch) {
    fprintf(stderr,
            "Failed to load the startup snapshot because it was built with"
            "architecture %s and the architecture is %s.\n",
            metadata.node_arch.c_str(),
            NODE_ARCH);
    return false;
  }

  if (metadata.node_platform != per_process::metadata.platform) {
    fprintf(stderr,
            "Failed to load the startup snapshot because it was built with"
            "platform %s and the current platform is %s.\n",
            metadata.node_platform.c_str(),
            NODE_PLATFORM);
    return false;
  }

  // TODO(joyeecheung): check incompatible Node.js flags.
  return true;
}

SnapshotData::~SnapshotData() {
  if (data_ownership == DataOwnership::kOwned &&
      v8_snapshot_blob_data.data != nullptr) {
    delete[] v8_snapshot_blob_data.data;
  }
}

static std::string GetCodeCacheDefName(const std::string& id) {
  char buf[64] = {0};
  size_t size = id.size();
  CHECK_LT(size, sizeof(buf));
  for (size_t i = 0; i < size; ++i) {
    char ch = id[i];
    buf[i] = (ch == '-' || ch == '/') ? '_' : ch;
  }
  return std::string(buf) + std::string("_cache_data");
}

static std::string FormatSize(size_t size) {
  char buf[64] = {0};
  if (size < 1024) {
    snprintf(buf, sizeof(buf), "%.2fB", static_cast<double>(size));
  } else if (size < 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.2fKB", static_cast<double>(size / 1024));
  } else {
    snprintf(
        buf, sizeof(buf), "%.2fMB", static_cast<double>(size / 1024 / 1024));
  }
  return buf;
}

template <typename T>
void WriteByteVectorLiteral(std::ostream* ss,
                            const T* vec,
                            size_t size,
                            const char* var_name,
                            bool use_array_literals) {
  constexpr bool is_uint8_t = std::is_same_v<T, uint8_t>;
  static_assert(is_uint8_t || std::is_same_v<T, char>);
  constexpr const char* type_name = is_uint8_t ? "uint8_t" : "char";
  if (!use_array_literals) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(vec);
    *ss << "static const " << type_name << " *" << var_name << " = ";
    *ss << (is_uint8_t ? R"(reinterpret_cast<const uint8_t *>(")" : "\"");
    for (size_t i = 0; i < size; i++) {
      const uint8_t ch = data[i];
      *ss << GetOctalCode(ch);
      if (i % 64 == 63) {
        // Go to a newline every 64 bytes since many text editors have
        // problems with very long lines.
        *ss << "\"\n\"";
      }
    }
    *ss << (is_uint8_t ? "\");\n" : "\";\n");
  } else {
    *ss << "static const " << type_name << " " << var_name << "[] = {";
    for (size_t i = 0; i < size; i++) {
      *ss << std::to_string(vec[i]) << (i == size - 1 ? '\n' : ',');
      if (i % 64 == 63) {
        // Print a newline every 64 units and a offset to improve
        // readability.
        *ss << "  // " << (i / 64) << "\n";
      }
    }
    *ss << "};\n";
  }
}

static void WriteCodeCacheInitializer(std::ostream* ss,
                                      const std::string& id,
                                      size_t size) {
  std::string def_name = GetCodeCacheDefName(id);
  *ss << "    { \"" << id << "\",\n";
  *ss << "      {" << def_name << ",\n";
  *ss << "       " << size << ",\n";
  *ss << "      }\n";
  *ss << "    },\n";
}

void FormatBlob(std::ostream& ss,
                const SnapshotData* data,
                bool use_array_literals) {
  ss << R"(#include <cstddef>
#include "env.h"
#include "node_snapshot_builder.h"
#include "v8.h"

// This file is generated by tools/snapshot. Do not edit.

namespace node {
)";

  WriteByteVectorLiteral(&ss,
                         data->v8_snapshot_blob_data.data,
                         data->v8_snapshot_blob_data.raw_size,
                         "v8_snapshot_blob_data",
                         use_array_literals);

  ss << R"(static const int v8_snapshot_blob_size = )"
     << data->v8_snapshot_blob_data.raw_size << ";\n";

  // Windows can't deal with too many large vector initializers.
  // Store the data into static arrays first.
  for (const auto& item : data->code_cache) {
    std::string var_name = GetCodeCacheDefName(item.id);
    WriteByteVectorLiteral(&ss,
                           item.data.data,
                           item.data.length,
                           var_name.c_str(),
                           use_array_literals);
  }

  ss << R"(const SnapshotData snapshot_data {
  // -- data_ownership begins --
  SnapshotData::DataOwnership::kNotOwned,
  // -- data_ownership ends --
  // -- metadata begins --
)" << data->metadata
     << R"(,
  // -- metadata ends --
  // -- v8_snapshot_blob_data begins --
  { v8_snapshot_blob_data, v8_snapshot_blob_size },
  // -- v8_snapshot_blob_data ends --
  // -- isolate_data_info begins --
)" << data->isolate_data_info
     << R"(
  // -- isolate_data_info ends --
  ,
  // -- env_info begins --
)" << data->env_info
     << R"(
  // -- env_info ends --
  ,
  // -- code_cache begins --
  {)";
  for (const auto& item : data->code_cache) {
    WriteCodeCacheInitializer(&ss, item.id, item.data.length);
  }
  ss << R"(
  }
  // -- code_cache ends --
};

const SnapshotData* SnapshotBuilder::GetEmbeddedSnapshotData() {
  return &snapshot_data;
}
}  // namespace node
)";
}

// Reset context settings that need to be initialized again after
// deserialization.
static void ResetContextSettingsBeforeSnapshot(Local<Context> context) {
  // Reset the AllowCodeGenerationFromStrings flag to true (default value) so
  // that it can be re-initialized with v8 flag
  // --disallow-code-generation-from-strings and recognized in
  // node::InitializeContextRuntime.
  context->AllowCodeGenerationFromStrings(true);
}

const std::vector<intptr_t>& SnapshotBuilder::CollectExternalReferences() {
  static auto registry = std::make_unique<ExternalReferenceRegistry>();
  return registry->external_references();
}

void SnapshotBuilder::InitializeIsolateParams(const SnapshotData* data,
                                              Isolate::CreateParams* params) {
  CHECK_NULL(params->external_references);
  CHECK_NULL(params->snapshot_blob);
  params->external_references = CollectExternalReferences().data();
  params->snapshot_blob =
      const_cast<v8::StartupData*>(&(data->v8_snapshot_blob_data));
}

SnapshotFlags operator|(SnapshotFlags x, SnapshotFlags y) {
  return static_cast<SnapshotFlags>(static_cast<uint32_t>(x) |
                                    static_cast<uint32_t>(y));
}

SnapshotFlags operator&(SnapshotFlags x, SnapshotFlags y) {
  return static_cast<SnapshotFlags>(static_cast<uint32_t>(x) &
                                    static_cast<uint32_t>(y));
}

SnapshotFlags operator|=(/* NOLINT (runtime/references) */ SnapshotFlags& x,
                         SnapshotFlags y) {
  return x = x | y;
}

bool WithoutCodeCache(const SnapshotFlags& flags) {
  return static_cast<bool>(flags & SnapshotFlags::kWithoutCodeCache);
}

bool WithoutCodeCache(const SnapshotConfig& config) {
  return WithoutCodeCache(config.flags);
}

std::optional<SnapshotConfig> ReadSnapshotConfig(const char* config_path) {
  std::string config_content;
  int r = ReadFileSync(&config_content, config_path);
  if (r != 0) {
    FPrintF(stderr,
            "Cannot read snapshot configuration from %s: %s\n",
            config_path,
            uv_strerror(r));
    return std::nullopt;
  }

  JSONParser parser;
  if (!parser.Parse(config_content)) {
    FPrintF(stderr, "Cannot parse JSON from %s\n", config_path);
    return std::nullopt;
  }

  SnapshotConfig result;
  result.builder_script_path = parser.GetTopLevelStringField("builder");
  if (!result.builder_script_path.has_value()) {
    FPrintF(stderr,
            "\"builder\" field of %s is not a non-empty string\n",
            config_path);
    return std::nullopt;
  }

  std::optional<bool> WithoutCodeCache =
      parser.GetTopLevelBoolField("withoutCodeCache");
  if (!WithoutCodeCache.has_value()) {
    FPrintF(stderr,
            "\"withoutCodeCache\" field of %s is not a boolean\n",
            config_path);
    return std::nullopt;
  }
  if (WithoutCodeCache.value()) {
    result.flags |= SnapshotFlags::kWithoutCodeCache;
  }

  return result;
}

ExitCode BuildSnapshotWithoutCodeCache(
    SnapshotData* out,
    const std::vector<std::string>& args,
    const std::vector<std::string>& exec_args,
    std::optional<std::string_view> builder_script_content,
    const SnapshotConfig& config) {
  DCHECK(builder_script_content.has_value() ==
         config.builder_script_path.has_value());
  // The default snapshot is meant to be runtime-independent and has more
  // restrictions. We do not enable the inspector and do not run the event
  // loop when building the default snapshot to avoid inconsistencies, but
  // we do for the fully customized one, and they are expected to fixup the
  // inconsistencies using v8.startupSnapshot callbacks.
  SnapshotMetadata::Type snapshot_type =
      builder_script_content.has_value()
          ? SnapshotMetadata::Type::kFullyCustomized
          : SnapshotMetadata::Type::kDefault;

  std::vector<std::string> errors;
  auto setup = CommonEnvironmentSetup::CreateForSnapshotting(
      per_process::v8_platform.Platform(), &errors, args, exec_args, config);
  if (!setup) {
    for (const std::string& err : errors)
      fprintf(stderr, "%s: %s\n", args[0].c_str(), err.c_str());
    return ExitCode::kBootstrapFailure;
  }

  Isolate* isolate = setup->isolate();
  {
    HandleScope scope(isolate);
    TryCatch bootstrapCatch(isolate);

    auto print_Exception = OnScopeLeave([&]() {
      if (bootstrapCatch.HasCaught()) {
        PrintCaughtException(
            isolate, isolate->GetCurrentContext(), bootstrapCatch);
      }
    });

    // Run the custom main script for fully customized snapshots.
    if (snapshot_type == SnapshotMetadata::Type::kFullyCustomized) {
      Context::Scope context_scope(setup->context());
      Environment* env = setup->env();
#if HAVE_INSPECTOR
        env->InitializeInspector({});
#endif
        if (LoadEnvironment(env, builder_script_content.value()).IsEmpty()) {
          return ExitCode::kGenericUserError;
        }

        // FIXME(joyeecheung): right now running the loop in the snapshot
        // builder might introduce inconsistencies in JS land that need to
        // be synchronized again after snapshot restoration.
        ExitCode exit_code =
            SpinEventLoopInternal(env).FromMaybe(ExitCode::kGenericUserError);
        if (exit_code != ExitCode::kNoFailure) {
          return exit_code;
        }
    }
  }

  return SnapshotBuilder::CreateSnapshot(out, setup.get());
}

ExitCode BuildCodeCacheFromSnapshot(SnapshotData* out,
                                    const std::vector<std::string>& args,
                                    const std::vector<std::string>& exec_args) {
  RAIIIsolate raii_isolate(out);
  Isolate* isolate = raii_isolate.get();
  v8::Locker locker(isolate);
  Isolate::Scope isolate_scope(isolate);
  HandleScope handle_scope(isolate);
  TryCatch bootstrapCatch(isolate);

  auto print_Exception = OnScopeLeave([&]() {
    if (bootstrapCatch.HasCaught()) {
      PrintCaughtException(
          isolate, isolate->GetCurrentContext(), bootstrapCatch);
    }
  });

  Local<Context> context = Context::New(isolate);
  Context::Scope context_scope(context);
  builtins::BuiltinLoader builtin_loader;
  // Regenerate all the code cache.
  if (!builtin_loader.CompileAllBuiltinsAndCopyCodeCache(
          context,
          out->env_info.principal_realm.builtins,
          &(out->code_cache))) {
    return ExitCode::kGenericUserError;
  }
  if (per_process::enabled_debug_list.enabled(DebugCategory::MKSNAPSHOT)) {
    for (const auto& item : out->code_cache) {
      std::string size_str = FormatSize(item.data.length);
      per_process::Debug(DebugCategory::MKSNAPSHOT,
                         "Generated code cache for %d: %s\n",
                         item.id.c_str(),
                         size_str.c_str());
    }
  }
  return ExitCode::kNoFailure;
}

ExitCode SnapshotBuilder::Generate(
    SnapshotData* out,
    const std::vector<std::string>& args,
    const std::vector<std::string>& exec_args,
    std::optional<std::string_view> builder_script_content,
    const SnapshotConfig& snapshot_config) {
  ExitCode code = BuildSnapshotWithoutCodeCache(
      out, args, exec_args, builder_script_content, snapshot_config);
  if (code != ExitCode::kNoFailure) {
    return code;
  }

  if (!WithoutCodeCache(snapshot_config)) {
    per_process::Debug(
        DebugCategory::CODE_CACHE,
        "---\nGenerate code cache to complement snapshot\n---\n");
    // Deserialize the snapshot to recompile code cache. We need to do this in
    // the second pass because V8 requires the code cache to be compiled with a
    // finalized read-only space.
    return BuildCodeCacheFromSnapshot(out, args, exec_args);
  }

  return ExitCode::kNoFailure;
}

ExitCode SnapshotBuilder::CreateSnapshot(SnapshotData* out,
                                         CommonEnvironmentSetup* setup) {
  const SnapshotConfig* config = setup->isolate_data()->snapshot_config();
  DCHECK_NOT_NULL(config);
  SnapshotMetadata::Type snapshot_type =
      config->builder_script_path.has_value()
          ? SnapshotMetadata::Type::kFullyCustomized
          : SnapshotMetadata::Type::kDefault;
  Isolate* isolate = setup->isolate();
  Environment* env = setup->env();
  SnapshotCreator* creator = setup->snapshot_creator();

  {
    HandleScope scope(isolate);
    Local<Context> main_context = setup->context();

    // The default context with only things created by V8.
    Local<Context> default_context = Context::New(isolate);

    // The context used by the vm module.
    Local<Context> vm_context;
    {
      Local<ObjectTemplate> global_template =
          setup->isolate_data()->contextify_global_template();
      CHECK(!global_template.IsEmpty());
      if (!contextify::ContextifyContext::CreateV8Context(
               isolate, global_template, nullptr, nullptr)
               .ToLocal(&vm_context)) {
        return ExitCode::kStartupSnapshotFailure;
      }
    }

    // The Node.js-specific context with primodials, can be used by workers
    // TODO(joyeecheung): investigate if this can be used by vm contexts
    // without breaking compatibility.
    Local<Context> base_context = NewContext(isolate);
    if (base_context.IsEmpty()) {
      return ExitCode::kBootstrapFailure;
    }
    ResetContextSettingsBeforeSnapshot(base_context);

    {
      Context::Scope context_scope(main_context);

      if (per_process::enabled_debug_list.enabled(DebugCategory::MKSNAPSHOT)) {
        env->ForEachRealm([](Realm* realm) { realm->PrintInfoForSnapshot(); });
        fprintf(stderr, "Environment = %p\n", env);
      }

      // Clean up the states left by the inspector because V8 cannot serialize
      // them. They don't need to be persisted and can be created from scratch
      // after snapshot deserialization.
      RunAtExit(env);
#if HAVE_INSPECTOR
      env->StopInspector();
#endif

      // Serialize the native states
      out->isolate_data_info = setup->isolate_data()->Serialize(creator);
      out->env_info = env->Serialize(creator);

      ResetContextSettingsBeforeSnapshot(main_context);
    }

    // Global handles to the contexts can't be disposed before the
    // blob is created. So initialize all the contexts before adding them.
    // TODO(joyeecheung): figure out how to remove this restriction.
    creator->SetDefaultContext(default_context);
    size_t index = creator->AddContext(vm_context);
    CHECK_EQ(index, SnapshotData::kNodeVMContextIndex);
    index = creator->AddContext(base_context);
    CHECK_EQ(index, SnapshotData::kNodeBaseContextIndex);
    index = creator->AddContext(
        main_context,
        v8::SerializeInternalFieldsCallback(SerializeNodeContextInternalFields,
                                            env),
        v8::SerializeContextDataCallback(SerializeNodeContextData, env));
    CHECK_EQ(index, SnapshotData::kNodeMainContextIndex);
  }

  // Must be out of HandleScope
  SnapshotCreator::FunctionCodeHandling handling =
      WithoutCodeCache(*config) ? SnapshotCreator::FunctionCodeHandling::kClear
                                : SnapshotCreator::FunctionCodeHandling::kKeep;
  out->v8_snapshot_blob_data = creator->CreateBlob(handling);

  // We must be able to rehash the blob when we restore it or otherwise
  // the hash seed would be fixed by V8, introducing a vulnerability.
  if (!out->v8_snapshot_blob_data.CanBeRehashed()) {
    return ExitCode::kStartupSnapshotFailure;
  }

  out->metadata = SnapshotMetadata{snapshot_type,
                                   per_process::metadata.versions.node,
                                   per_process::metadata.arch,
                                   per_process::metadata.platform,
                                   config->flags};

  // We cannot resurrect the handles from the snapshot, so make sure that
  // no handles are left open in the environment after the blob is created
  // (which should trigger a GC and close all handles that can be closed).
  bool queues_are_empty =
      env->req_wrap_queue()->IsEmpty() && env->handle_wrap_queue()->IsEmpty();
  if (!queues_are_empty ||
      per_process::enabled_debug_list.enabled(DebugCategory::MKSNAPSHOT)) {
    PrintLibuvHandleInformation(env->event_loop(), stderr);
  }
  if (!queues_are_empty) {
    return ExitCode::kStartupSnapshotFailure;
  }
  return ExitCode::kNoFailure;
}

ExitCode SnapshotBuilder::GenerateAsSource(
    const char* out_path,
    const std::vector<std::string>& args,
    const std::vector<std::string>& exec_args,
    const SnapshotConfig& config,
    bool use_array_literals) {
  std::string builder_script_content;
  std::optional<std::string_view> builder_script_optional;
  if (config.builder_script_path.has_value()) {
    std::string_view builder_script_path = config.builder_script_path.value();
    int r = ReadFileSync(&builder_script_content, builder_script_path.data());
    if (r != 0) {
      FPrintF(stderr,
              "Cannot read main script %s for building snapshot. %s: %s",
              builder_script_path,
              uv_err_name(r),
              uv_strerror(r));
      return ExitCode::kGenericUserError;
    }
    builder_script_optional = builder_script_content;
  }

  std::ofstream out(out_path, std::ios::out | std::ios::binary);
  if (!out) {
    FPrintF(stderr, "Cannot open %s for output.\n", out_path);
    return ExitCode::kGenericUserError;
  }

  SnapshotData data;
  ExitCode exit_code =
      Generate(&data, args, exec_args, builder_script_optional, config);
  if (exit_code != ExitCode::kNoFailure) {
    return exit_code;
  }
  FormatBlob(out, &data, use_array_literals);

  if (!out) {
    std::cerr << "Failed to write to " << out_path << "\n";
    exit_code = node::ExitCode::kGenericUserError;
  }

  return exit_code;
}

SnapshotableObject::SnapshotableObject(Realm* realm,
                                       Local<Object> wrap,
                                       EmbedderObjectType type)
    : BaseObject(realm, wrap), type_(type) {}

std::string SnapshotableObject::GetTypeName() const {
  switch (type_) {
#define V(PropertyName, NativeTypeName)                                        \
  case EmbedderObjectType::k_##PropertyName: {                                 \
    return #NativeTypeName;                                                    \
  }
    SERIALIZABLE_OBJECT_TYPES(V)
#undef V
    default: { UNREACHABLE(); }
  }
}

void DeserializeNodeContextData(Local<Context> holder,
                                int index,
                                StartupData payload,
                                void* callback_data) {
  // We will reset all the pointers in Environment::AssignToContext()
  // via the realm constructor.
  switch (index) {
    case ContextEmbedderIndex::kEnvironment:
    case ContextEmbedderIndex::kContextifyContext:
    case ContextEmbedderIndex::kRealm:
    case ContextEmbedderIndex::kContextTag: {
      uint64_t index_64;
      int size = sizeof(index_64);
      CHECK_EQ(payload.raw_size, size);
      memcpy(&index_64, payload.data, payload.raw_size);
      CHECK_EQ(index_64, static_cast<uint64_t>(index));
      break;
    }
    default:
      UNREACHABLE();
  }
}

StartupData SerializeNodeContextData(Local<Context> holder,
                                     int index,
                                     void* callback_data) {
  // For pointer values, we need to return some non-empty data so that V8
  // does not serialize them verbatim, making the snapshot unreproducible.
  switch (index) {
    case ContextEmbedderIndex::kEnvironment:
    case ContextEmbedderIndex::kContextifyContext:
    case ContextEmbedderIndex::kRealm:
    case ContextEmbedderIndex::kContextTag: {
      void* data = holder->GetAlignedPointerFromEmbedderData(index);
      per_process::Debug(
          DebugCategory::MKSNAPSHOT,
          "Serialize context data, index=%d, holder=%p, ptr=%p\n",
          static_cast<int>(index),
          *holder,
          data);
      // We use uint64_t to avoid padding.
      uint64_t index_64 = static_cast<uint64_t>(index);
      // It must be allocated with new[] because V8 will call delete[] on it.
      size_t size = sizeof(index_64);
      char* startup_data = new char[size];
      memcpy(startup_data, &index_64, size);
      return {startup_data, static_cast<int>(size)};
    }
    default:
      UNREACHABLE();
  }
}

void DeserializeNodeInternalFields(Local<Object> holder,
                                   int index,
                                   StartupData payload,
                                   void* callback_data) {
  if (payload.raw_size == 0) {
    return;
  }

  per_process::Debug(DebugCategory::MKSNAPSHOT,
                     "Deserialize internal field %d of %p, size=%d\n",
                     static_cast<int>(index),
                     (*holder),
                     static_cast<int>(payload.raw_size));

  Environment* env = static_cast<Environment*>(callback_data);

  // To deserialize the first field, check the type and re-tag the object.
  if (index == BaseObject::kEmbedderType) {
    int size = sizeof(EmbedderTypeInfo);
    DCHECK_EQ(payload.raw_size, size);
    EmbedderTypeInfo read_data;
    memcpy(&read_data, payload.data, size);
    // For now we only support non-cppgc objects.
    CHECK_EQ(read_data.mode, EmbedderTypeInfo::MemoryMode::kBaseObject);
    BaseObject::TagBaseObject(env->isolate_data(), holder);
    return;
  }

  // To deserialize the second field, enqueue a deserialize request.
  DCHECK_IS_SNAPSHOT_SLOT(index);
  const InternalFieldInfoBase* info =
      reinterpret_cast<const InternalFieldInfoBase*>(payload.data);
  // TODO(joyeecheung): we can add a constant kNodeEmbedderId to the
  // beginning of every InternalFieldInfoBase to ensure that we don't
  // step on payloads that were not serialized by Node.js.
  switch (info->type) {
#define V(PropertyName, NativeTypeName)                                        \
  case EmbedderObjectType::k_##PropertyName: {                                 \
    per_process::Debug(DebugCategory::MKSNAPSHOT,                              \
                       "Object %p is %s\n",                                    \
                       (*holder),                                              \
                       #NativeTypeName);                                       \
    env->EnqueueDeserializeRequest(                                            \
        NativeTypeName::Deserialize,                                           \
        holder,                                                                \
        index,                                                                 \
        info->Copy<NativeTypeName::InternalFieldInfo>());                      \
    break;                                                                     \
  }
    SERIALIZABLE_OBJECT_TYPES(V)
#undef V
    default: {
      // This should only be reachable during development when trying to
      // deserialize a snapshot blob built by a version of Node.js that
      // has more recognizable EmbedderObjectTypes than the deserializing
      // Node.js binary.
      fprintf(stderr,
              "Unknown embedder object type %" PRIu8 ", possibly caused by "
              "mismatched Node.js versions\n",
              static_cast<uint8_t>(info->type));
      ABORT();
    }
  }
}

StartupData SerializeNodeContextInternalFields(Local<Object> holder,
                                               int index,
                                               void* callback_data) {
  // For the moment we do not set any internal fields in ArrayBuffer
  // or ArrayBufferViews, so just return nullptr.
  if (holder->IsArrayBuffer() || holder->IsArrayBufferView()) {
    CHECK_NULL(holder->GetAlignedPointerFromInternalField(index));
    return StartupData{nullptr, 0};
  }

  // Use the V8 convention and serialize unknown objects verbatim.
  Environment* env = static_cast<Environment*>(callback_data);
  if (!BaseObject::IsBaseObject(env->isolate_data(), holder)) {
    per_process::Debug(DebugCategory::MKSNAPSHOT,
                       "Serialize unknown object, index=%d, holder=%p\n",
                       static_cast<int>(index),
                       *holder);
    return StartupData{nullptr, 0};
  }

  per_process::Debug(DebugCategory::MKSNAPSHOT,
                     "Serialize BaseObject, index=%d, holder=%p\n",
                     static_cast<int>(index),
                     *holder);

  BaseObject* object_ptr = static_cast<BaseObject*>(
      holder->GetAlignedPointerFromInternalField(BaseObject::kSlot));
  // If the native object is already set to null, ignore it.
  if (object_ptr == nullptr) {
    return StartupData{nullptr, 0};
  }

  DCHECK(object_ptr->is_snapshotable());
  SnapshotableObject* obj = static_cast<SnapshotableObject*>(object_ptr);

  // To serialize the type field, save data in a EmbedderTypeInfo.
  if (index == BaseObject::kEmbedderType) {
    int size = sizeof(EmbedderTypeInfo);
    // We need to use placement new because V8 calls delete[] on the returned
    // data.
    // The () syntax at the end would zero-initialize the block and make
    // the padding reproducible.
    char* data = new char[size]();
    // TODO(joyeecheung): support cppgc objects.
    new (data) EmbedderTypeInfo(obj->type(),
                                EmbedderTypeInfo::MemoryMode::kBaseObject);
    return StartupData{data, size};
  }

  // To serialize the slot field, invoke Serialize() method on the object.
  DCHECK_IS_SNAPSHOT_SLOT(index);

  per_process::Debug(DebugCategory::MKSNAPSHOT,
                     "Object %p is %s, ",
                     *holder,
                     obj->GetTypeName());
  InternalFieldInfoBase* info = obj->Serialize(index);

  per_process::Debug(DebugCategory::MKSNAPSHOT,
                     "payload size=%d\n",
                     static_cast<int>(info->length));
  return StartupData{reinterpret_cast<const char*>(info),
                     static_cast<int>(info->length)};
}

void SerializeSnapshotableObjects(Realm* realm,
                                  SnapshotCreator* creator,
                                  RealmSerializeInfo* info) {
  HandleScope scope(realm->isolate());
  Local<Context> context = realm->context();
  uint32_t i = 0;
  realm->ForEachBaseObject([&](BaseObject* obj) {
    // If there are any BaseObjects that are not snapshotable left
    // during context serialization, V8 would crash due to unregistered
    // global handles and print detailed information about them.
    if (!obj->is_snapshotable()) {
      return;
    }
    SnapshotableObject* ptr = static_cast<SnapshotableObject*>(obj);

    std::string type_name = ptr->GetTypeName();
    per_process::Debug(DebugCategory::MKSNAPSHOT,
                       "Serialize snapshotable object %i (%p), "
                       "object=%p, type=%s\n",
                       static_cast<int>(i),
                       ptr,
                       *(ptr->object()),
                       type_name);

    if (ptr->PrepareForSerialization(context, creator)) {
      SnapshotIndex index = creator->AddData(context, obj->object());
      per_process::Debug(DebugCategory::MKSNAPSHOT,
                         "Serialized with index=%d\n",
                         static_cast<int>(index));
      info->native_objects.push_back({type_name, i, index});
    }
    i++;
  });
}

void RunEmbedderPreload(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->embedder_preload());
  CHECK_EQ(args.Length(), 2);
  Local<Value> process_obj = args[0];
  Local<Value> require_fn = args[1];
  CHECK(process_obj->IsObject());
  CHECK(require_fn->IsFunction());
  env->embedder_preload()(env, process_obj, require_fn);
}

void CompileSerializeMain(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsString());
  Local<String> filename = args[0].As<String>();
  Local<String> source = args[1].As<String>();
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  // TODO(joyeecheung): do we need all of these? Maybe we would want a less
  // internal version of them.
  std::vector<Local<String>> parameters = {
      FIXED_ONE_BYTE_STRING(isolate, "require"),
      FIXED_ONE_BYTE_STRING(isolate, "__filename"),
      FIXED_ONE_BYTE_STRING(isolate, "__dirname"),
  };
  Local<Function> fn;
  if (contextify::CompileFunction(context, filename, source, &parameters)
          .ToLocal(&fn)) {
    args.GetReturnValue().Set(fn);
  }
}

void SetSerializeCallback(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->snapshot_serialize_callback().IsEmpty());
  CHECK(args[0]->IsFunction());
  env->set_snapshot_serialize_callback(args[0].As<Function>());
}

void SetDeserializeCallback(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->snapshot_deserialize_callback().IsEmpty());
  CHECK(args[0]->IsFunction());
  env->set_snapshot_deserialize_callback(args[0].As<Function>());
}

void SetDeserializeMainFunction(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->snapshot_deserialize_main().IsEmpty());
  CHECK(args[0]->IsFunction());
  env->set_snapshot_deserialize_main(args[0].As<Function>());
}

constexpr const char* kAnonymousMainPath = "__node_anonymous_main";

std::string GetAnonymousMainPath() {
  return kAnonymousMainPath;
}

namespace mksnapshot {

BindingData::BindingData(Realm* realm,
                         v8::Local<v8::Object> object,
                         InternalFieldInfo* info)
    : SnapshotableObject(realm, object, type_int),
      is_building_snapshot_buffer_(
          realm->isolate(),
          1,
          MAYBE_FIELD_PTR(info, is_building_snapshot_buffer)) {
  if (info == nullptr) {
    object
        ->Set(
            realm->context(),
            FIXED_ONE_BYTE_STRING(realm->isolate(), "isBuildingSnapshotBuffer"),
            is_building_snapshot_buffer_.GetJSArray())
        .Check();
  } else {
    is_building_snapshot_buffer_.Deserialize(realm->context());
  }
  // Reset the status according to the current state of the realm.
  bool is_building_snapshot = realm->isolate_data()->is_building_snapshot();
  DCHECK_IMPLIES(is_building_snapshot,
                 realm->isolate_data()->snapshot_data() == nullptr);
  is_building_snapshot_buffer_[0] = is_building_snapshot ? 1 : 0;
  is_building_snapshot_buffer_.MakeWeak();
}

bool BindingData::PrepareForSerialization(Local<Context> context,
                                          v8::SnapshotCreator* creator) {
  DCHECK_NULL(internal_field_info_);
  internal_field_info_ = InternalFieldInfoBase::New<InternalFieldInfo>(type());
  internal_field_info_->is_building_snapshot_buffer =
      is_building_snapshot_buffer_.Serialize(context, creator);
  // Return true because we need to maintain the reference to the binding from
  // JS land.
  return true;
}

InternalFieldInfoBase* BindingData::Serialize(int index) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  InternalFieldInfo* info = internal_field_info_;
  internal_field_info_ = nullptr;
  return info;
}

void BindingData::Deserialize(Local<Context> context,
                              Local<Object> holder,
                              int index,
                              InternalFieldInfoBase* info) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  v8::HandleScope scope(context->GetIsolate());
  Realm* realm = Realm::GetCurrent(context);
  // Recreate the buffer in the constructor.
  InternalFieldInfo* casted_info = static_cast<InternalFieldInfo*>(info);
  BindingData* binding =
      realm->AddBindingData<BindingData>(holder, casted_info);
  CHECK_NOT_NULL(binding);
}

void BindingData::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("is_building_snapshot_buffer",
                      is_building_snapshot_buffer_);
}

void CreatePerContextProperties(Local<Object> target,
                                Local<Value> unused,
                                Local<Context> context,
                                void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  realm->AddBindingData<BindingData>(target);
}

void CreatePerIsolateProperties(IsolateData* isolate_data,
                                Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();
  SetMethod(isolate, target, "runEmbedderPreload", RunEmbedderPreload);
  SetMethod(isolate, target, "compileSerializeMain", CompileSerializeMain);
  SetMethod(isolate, target, "setSerializeCallback", SetSerializeCallback);
  SetMethod(isolate, target, "setDeserializeCallback", SetDeserializeCallback);
  SetMethod(isolate,
            target,
            "setDeserializeMainFunction",
            SetDeserializeMainFunction);
  target->Set(FIXED_ONE_BYTE_STRING(isolate, "anonymousMainPath"),
              OneByteString(isolate, kAnonymousMainPath));
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(RunEmbedderPreload);
  registry->Register(CompileSerializeMain);
  registry->Register(SetSerializeCallback);
  registry->Register(SetDeserializeCallback);
  registry->Register(SetDeserializeMainFunction);
}
}  // namespace mksnapshot

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(
    mksnapshot, node::mksnapshot::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(mksnapshot,
                              node::mksnapshot::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(mksnapshot,
                                node::mksnapshot::RegisterExternalReferences)
