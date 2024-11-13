#include "node_sea.h"

#include "blob_serializer_deserializer-inl.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "json_parser.h"
#include "node_contextify.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "node_snapshot_builder.h"
#include "node_union_bytes.h"
#include "node_v8_platform-inl.h"
#include "util-inl.h"

// The POSTJECT_SENTINEL_FUSE macro is a string of random characters selected by
// the Node.js project that is present only once in the entire binary. It is
// used by the postject_has_resource() function to efficiently detect if a
// resource has been injected. See
// https://github.com/nodejs/postject/blob/35343439cac8c488f2596d7c4c1dddfec1fddcae/postject-api.h#L42-L45.
#define POSTJECT_SENTINEL_FUSE "NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2"
#include "postject-api.h"
#undef POSTJECT_SENTINEL_FUSE

#include <memory>
#include <string_view>
#include <tuple>
#include <vector>

using node::ExitCode;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Object;
using v8::ScriptCompiler;
using v8::String;
using v8::Value;

namespace node {
namespace sea {

namespace {

SeaFlags operator|(SeaFlags x, SeaFlags y) {
  return static_cast<SeaFlags>(static_cast<uint32_t>(x) |
                               static_cast<uint32_t>(y));
}

SeaFlags operator&(SeaFlags x, SeaFlags y) {
  return static_cast<SeaFlags>(static_cast<uint32_t>(x) &
                               static_cast<uint32_t>(y));
}

SeaFlags operator|=(/* NOLINT (runtime/references) */ SeaFlags& x, SeaFlags y) {
  return x = x | y;
}

class SeaSerializer : public BlobSerializer<SeaSerializer> {
 public:
  SeaSerializer()
      : BlobSerializer<SeaSerializer>(
            per_process::enabled_debug_list.enabled(DebugCategory::SEA)) {}

  template <typename T,
            std::enable_if_t<!std::is_same<T, std::string>::value>* = nullptr,
            std::enable_if_t<!std::is_arithmetic<T>::value>* = nullptr>
  size_t Write(const T& data);
};

template <>
size_t SeaSerializer::Write(const SeaResource& sea) {
  sink.reserve(SeaResource::kHeaderSize + sea.main_code_or_snapshot.size());

  Debug("Write SEA magic %x\n", kMagic);
  size_t written_total = WriteArithmetic<uint32_t>(kMagic);

  uint32_t flags = static_cast<uint32_t>(sea.flags);
  Debug("Write SEA flags %x\n", flags);
  written_total += WriteArithmetic<uint32_t>(flags);
  DCHECK_EQ(written_total, SeaResource::kHeaderSize);

  Debug("Write SEA code path %p, size=%zu\n",
        sea.code_path.data(),
        sea.code_path.size());
  written_total +=
      WriteStringView(sea.code_path, StringLogMode::kAddressAndContent);

  Debug("Write SEA resource %s %p, size=%zu\n",
        sea.use_snapshot() ? "snapshot" : "code",
        sea.main_code_or_snapshot.data(),
        sea.main_code_or_snapshot.size());
  written_total +=
      WriteStringView(sea.main_code_or_snapshot,
                      sea.use_snapshot() ? StringLogMode::kAddressOnly
                                         : StringLogMode::kAddressAndContent);

  if (sea.code_cache.has_value()) {
    Debug("Write SEA resource code cache %p, size=%zu\n",
          sea.code_cache->data(),
          sea.code_cache->size());
    written_total +=
        WriteStringView(sea.code_cache.value(), StringLogMode::kAddressOnly);
  }

  if (!sea.assets.empty()) {
    Debug("Write SEA resource assets size %zu\n", sea.assets.size());
    written_total += WriteArithmetic<size_t>(sea.assets.size());
    for (auto const& [key, content] : sea.assets) {
      Debug("Write SEA resource asset %s at %p, size=%zu\n",
            key,
            content.data(),
            content.size());
      written_total += WriteStringView(key, StringLogMode::kAddressAndContent);
      written_total += WriteStringView(content, StringLogMode::kAddressOnly);
    }
  }
  return written_total;
}

class SeaDeserializer : public BlobDeserializer<SeaDeserializer> {
 public:
  explicit SeaDeserializer(std::string_view v)
      : BlobDeserializer<SeaDeserializer>(
            per_process::enabled_debug_list.enabled(DebugCategory::SEA), v) {}

  template <typename T,
            std::enable_if_t<!std::is_same<T, std::string>::value>* = nullptr,
            std::enable_if_t<!std::is_arithmetic<T>::value>* = nullptr>
  T Read();
};

template <>
SeaResource SeaDeserializer::Read() {
  uint32_t magic = ReadArithmetic<uint32_t>();
  Debug("Read SEA magic %x\n", magic);

  CHECK_EQ(magic, kMagic);
  SeaFlags flags(static_cast<SeaFlags>(ReadArithmetic<uint32_t>()));
  Debug("Read SEA flags %x\n", static_cast<uint32_t>(flags));
  CHECK_EQ(read_total, SeaResource::kHeaderSize);

  std::string_view code_path =
      ReadStringView(StringLogMode::kAddressAndContent);
  Debug(
      "Read SEA code path %p, size=%zu\n", code_path.data(), code_path.size());

  bool use_snapshot = static_cast<bool>(flags & SeaFlags::kUseSnapshot);
  std::string_view code =
      ReadStringView(use_snapshot ? StringLogMode::kAddressOnly
                                  : StringLogMode::kAddressAndContent);

  Debug("Read SEA resource %s %p, size=%zu\n",
        use_snapshot ? "snapshot" : "code",
        code.data(),
        code.size());

  std::string_view code_cache;
  if (static_cast<bool>(flags & SeaFlags::kUseCodeCache)) {
    code_cache = ReadStringView(StringLogMode::kAddressOnly);
    Debug("Read SEA resource code cache %p, size=%zu\n",
          code_cache.data(),
          code_cache.size());
  }

  std::unordered_map<std::string_view, std::string_view> assets;
  if (static_cast<bool>(flags & SeaFlags::kIncludeAssets)) {
    size_t assets_size = ReadArithmetic<size_t>();
    Debug("Read SEA resource assets size %zu\n", assets_size);
    for (size_t i = 0; i < assets_size; ++i) {
      std::string_view key = ReadStringView(StringLogMode::kAddressAndContent);
      std::string_view content = ReadStringView(StringLogMode::kAddressOnly);
      Debug("Read SEA resource asset %s at %p, size=%zu\n",
            key,
            content.data(),
            content.size());
      assets.emplace(key, content);
    }
  }
  return {flags, code_path, code, code_cache, assets};
}

std::string_view FindSingleExecutableBlob() {
#if !defined(DISABLE_SINGLE_EXECUTABLE_APPLICATION)
  CHECK(IsSingleExecutable());
  static const std::string_view result = []() -> std::string_view {
    size_t size;
#ifdef __APPLE__
    postject_options options;
    postject_options_init(&options);
    options.macho_segment_name = "NODE_SEA";
    const char* blob = static_cast<const char*>(
        postject_find_resource("NODE_SEA_BLOB", &size, &options));
#else
    const char* blob = static_cast<const char*>(
        postject_find_resource("NODE_SEA_BLOB", &size, nullptr));
#endif
    return {blob, size};
  }();
  per_process::Debug(DebugCategory::SEA,
                     "Found SEA blob %p, size=%zu\n",
                     result.data(),
                     result.size());
  return result;
#else
  UNREACHABLE();
#endif  // !defined(DISABLE_SINGLE_EXECUTABLE_APPLICATION)
}

}  // anonymous namespace

bool SeaResource::use_snapshot() const {
  return static_cast<bool>(flags & SeaFlags::kUseSnapshot);
}

bool SeaResource::use_code_cache() const {
  return static_cast<bool>(flags & SeaFlags::kUseCodeCache);
}

SeaResource FindSingleExecutableResource() {
  static const SeaResource sea_resource = []() -> SeaResource {
    std::string_view blob = FindSingleExecutableBlob();
    per_process::Debug(DebugCategory::SEA,
                       "Found SEA resource %p, size=%zu\n",
                       blob.data(),
                       blob.size());
    SeaDeserializer deserializer(blob);
    return deserializer.Read<SeaResource>();
  }();
  return sea_resource;
}

bool IsSingleExecutable() {
  return postject_has_resource();
}

void IsSea(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(IsSingleExecutable());
}

void IsExperimentalSeaWarningNeeded(const FunctionCallbackInfo<Value>& args) {
  bool is_building_sea =
      !per_process::cli_options->experimental_sea_config.empty();
  if (is_building_sea) {
    args.GetReturnValue().Set(true);
    return;
  }

  if (!IsSingleExecutable()) {
    args.GetReturnValue().Set(false);
    return;
  }

  SeaResource sea_resource = FindSingleExecutableResource();
  args.GetReturnValue().Set(!static_cast<bool>(
      sea_resource.flags & SeaFlags::kDisableExperimentalSeaWarning));
}

std::tuple<int, char**> FixupArgsForSEA(int argc, char** argv) {
  // Repeats argv[0] at position 1 on argv as a replacement for the missing
  // entry point file path.
  if (IsSingleExecutable()) {
    static std::vector<char*> new_argv;
    new_argv.reserve(argc + 2);
    new_argv.emplace_back(argv[0]);
    new_argv.insert(new_argv.end(), argv, argv + argc);
    new_argv.emplace_back(nullptr);
    argc = new_argv.size() - 1;
    argv = new_argv.data();
  }

  return {argc, argv};
}

namespace {

struct SeaConfig {
  std::string main_path;
  std::string output_path;
  SeaFlags flags = SeaFlags::kDefault;
  std::unordered_map<std::string, std::string> assets;
};

std::optional<SeaConfig> ParseSingleExecutableConfig(
    const std::string& config_path) {
  std::string config;
  int r = ReadFileSync(&config, config_path.c_str());
  if (r != 0) {
    const char* err = uv_strerror(r);
    FPrintF(stderr,
            "Cannot read single executable configuration from %s: %s\n",
            config_path,
            err);
    return std::nullopt;
  }

  SeaConfig result;
  JSONParser parser;
  if (!parser.Parse(config)) {
    FPrintF(stderr, "Cannot parse JSON from %s\n", config_path);
    return std::nullopt;
  }

  result.main_path =
      parser.GetTopLevelStringField("main").value_or(std::string());
  if (result.main_path.empty()) {
    FPrintF(stderr,
            "\"main\" field of %s is not a non-empty string\n",
            config_path);
    return std::nullopt;
  }

  result.output_path =
      parser.GetTopLevelStringField("output").value_or(std::string());
  if (result.output_path.empty()) {
    FPrintF(stderr,
            "\"output\" field of %s is not a non-empty string\n",
            config_path);
    return std::nullopt;
  }

  std::optional<bool> disable_experimental_sea_warning =
      parser.GetTopLevelBoolField("disableExperimentalSEAWarning");
  if (!disable_experimental_sea_warning.has_value()) {
    FPrintF(stderr,
            "\"disableExperimentalSEAWarning\" field of %s is not a Boolean\n",
            config_path);
    return std::nullopt;
  }
  if (disable_experimental_sea_warning.value()) {
    result.flags |= SeaFlags::kDisableExperimentalSeaWarning;
  }

  std::optional<bool> use_snapshot = parser.GetTopLevelBoolField("useSnapshot");
  if (!use_snapshot.has_value()) {
    FPrintF(
        stderr, "\"useSnapshot\" field of %s is not a Boolean\n", config_path);
    return std::nullopt;
  }
  if (use_snapshot.value()) {
    result.flags |= SeaFlags::kUseSnapshot;
  }

  std::optional<bool> use_code_cache =
      parser.GetTopLevelBoolField("useCodeCache");
  if (!use_code_cache.has_value()) {
    FPrintF(
        stderr, "\"useCodeCache\" field of %s is not a Boolean\n", config_path);
    return std::nullopt;
  }
  if (use_code_cache.value()) {
    if (use_snapshot.value()) {
      // TODO(joyeecheung): code cache in snapshot should be configured by
      // separate snapshot configurations.
      FPrintF(stderr,
              "\"useCodeCache\" is redundant when \"useSnapshot\" is true\n");
    } else {
      result.flags |= SeaFlags::kUseCodeCache;
    }
  }

  auto assets_opt = parser.GetTopLevelStringDict("assets");
  if (!assets_opt.has_value()) {
    FPrintF(stderr,
            "\"assets\" field of %s is not a map of strings\n",
            config_path);
    return std::nullopt;
  } else if (!assets_opt.value().empty()) {
    result.flags |= SeaFlags::kIncludeAssets;
    result.assets = std::move(assets_opt.value());
  }

  return result;
}

ExitCode GenerateSnapshotForSEA(const SeaConfig& config,
                                const std::vector<std::string>& args,
                                const std::vector<std::string>& exec_args,
                                const std::string& builder_script_content,
                                const SnapshotConfig& snapshot_config,
                                std::vector<char>* snapshot_blob) {
  SnapshotData snapshot;
  // TODO(joyeecheung): make the arguments configurable through the JSON
  // config or a programmatic API.
  std::vector<std::string> patched_args = {args[0], config.main_path};
  ExitCode exit_code = SnapshotBuilder::Generate(&snapshot,
                                                 patched_args,
                                                 exec_args,
                                                 builder_script_content,
                                                 snapshot_config);
  if (exit_code != ExitCode::kNoFailure) {
    return exit_code;
  }
  auto& persistents = snapshot.env_info.principal_realm.persistent_values;
  auto it = std::find_if(
      persistents.begin(), persistents.end(), [](const PropInfo& prop) {
        return prop.name == "snapshot_deserialize_main";
      });
  if (it == persistents.end()) {
    FPrintF(
        stderr,
        "%s does not invoke "
        "v8.startupSnapshot.setDeserializeMainFunction(), which is required "
        "for snapshot scripts used to build single executable applications."
        "\n",
        config.main_path);
    return ExitCode::kGenericUserError;
  }
  // We need the temporary variable for copy elision.
  std::vector<char> temp = snapshot.ToBlob();
  *snapshot_blob = std::move(temp);
  return ExitCode::kNoFailure;
}

std::optional<std::string> GenerateCodeCache(std::string_view main_path,
                                             std::string_view main_script) {
  RAIIIsolate raii_isolate(SnapshotBuilder::GetEmbeddedSnapshotData());
  Isolate* isolate = raii_isolate.get();

  v8::Isolate::Scope isolate_scope(isolate);
  HandleScope handle_scope(isolate);

  Local<Context> context = Context::New(isolate);
  Context::Scope context_scope(context);

  errors::PrinterTryCatch bootstrapCatch(
      isolate, errors::PrinterTryCatch::kPrintSourceLine);

  Local<String> filename;
  if (!String::NewFromUtf8(isolate,
                           main_path.data(),
                           NewStringType::kNormal,
                           main_path.length())
           .ToLocal(&filename)) {
    return std::nullopt;
  }

  Local<String> content;
  if (!String::NewFromUtf8(isolate,
                           main_script.data(),
                           NewStringType::kNormal,
                           main_script.length())
           .ToLocal(&content)) {
    return std::nullopt;
  }

  std::vector<Local<String>> parameters = {
      FIXED_ONE_BYTE_STRING(isolate, "exports"),
      FIXED_ONE_BYTE_STRING(isolate, "require"),
      FIXED_ONE_BYTE_STRING(isolate, "module"),
      FIXED_ONE_BYTE_STRING(isolate, "__filename"),
      FIXED_ONE_BYTE_STRING(isolate, "__dirname"),
  };

  // TODO(RaisinTen): Using the V8 code cache prevents us from using `import()`
  // in the SEA code. Support it.
  // Refs: https://github.com/nodejs/node/pull/48191#discussion_r1213271430
  Local<Function> fn;
  if (!contextify::CompileFunction(context, filename, content, &parameters)
           .ToLocal(&fn)) {
    return std::nullopt;
  }

  std::unique_ptr<ScriptCompiler::CachedData> cache{
      ScriptCompiler::CreateCodeCacheForFunction(fn)};
  std::string code_cache(cache->data, cache->data + cache->length);
  return code_cache;
}

int BuildAssets(const std::unordered_map<std::string, std::string>& config,
                std::unordered_map<std::string, std::string>* assets) {
  for (auto const& [key, path] : config) {
    std::string blob;
    int r = ReadFileSync(&blob, path.c_str());
    if (r != 0) {
      const char* err = uv_strerror(r);
      FPrintF(stderr, "Cannot read asset %s: %s\n", path.c_str(), err);
      return r;
    }
    assets->emplace(key, std::move(blob));
  }
  return 0;
}

ExitCode GenerateSingleExecutableBlob(
    const SeaConfig& config,
    const std::vector<std::string>& args,
    const std::vector<std::string>& exec_args) {
  std::string main_script;
  // TODO(joyeecheung): unify the file utils.
  int r = ReadFileSync(&main_script, config.main_path.c_str());
  if (r != 0) {
    const char* err = uv_strerror(r);
    FPrintF(stderr, "Cannot read main script %s:%s\n", config.main_path, err);
    return ExitCode::kGenericUserError;
  }

  std::vector<char> snapshot_blob;
  bool builds_snapshot_from_main =
      static_cast<bool>(config.flags & SeaFlags::kUseSnapshot);
  if (builds_snapshot_from_main) {
    // TODO(joyeecheung): allow passing snapshot configuration in SEA configs.
    SnapshotConfig snapshot_config;
    snapshot_config.builder_script_path = main_script;
    ExitCode exit_code = GenerateSnapshotForSEA(
        config, args, exec_args, main_script, snapshot_config, &snapshot_blob);
    if (exit_code != ExitCode::kNoFailure) {
      return exit_code;
    }
  }

  std::optional<std::string_view> optional_sv_code_cache;
  std::string code_cache;
  if (static_cast<bool>(config.flags & SeaFlags::kUseCodeCache)) {
    std::optional<std::string> optional_code_cache =
        GenerateCodeCache(config.main_path, main_script);
    if (!optional_code_cache.has_value()) {
      FPrintF(stderr, "Cannot generate V8 code cache\n");
      return ExitCode::kGenericUserError;
    }
    code_cache = optional_code_cache.value();
    optional_sv_code_cache = code_cache;
  }

  std::unordered_map<std::string, std::string> assets;
  if (!config.assets.empty() && BuildAssets(config.assets, &assets) != 0) {
    return ExitCode::kGenericUserError;
  }
  std::unordered_map<std::string_view, std::string_view> assets_view;
  for (auto const& [key, content] : assets) {
    assets_view.emplace(key, content);
  }
  SeaResource sea{
      config.flags,
      config.main_path,
      builds_snapshot_from_main
          ? std::string_view{snapshot_blob.data(), snapshot_blob.size()}
          : std::string_view{main_script.data(), main_script.size()},
      optional_sv_code_cache,
      assets_view};

  SeaSerializer serializer;
  serializer.Write(sea);

  uv_buf_t buf = uv_buf_init(serializer.sink.data(), serializer.sink.size());
  r = WriteFileSync(config.output_path.c_str(), buf);
  if (r != 0) {
    const char* err = uv_strerror(r);
    FPrintF(stderr, "Cannot write output to %s:%s\n", config.output_path, err);
    return ExitCode::kGenericUserError;
  }

  FPrintF(stderr,
          "Wrote single executable preparation blob to %s\n",
          config.output_path);
  return ExitCode::kNoFailure;
}

}  // anonymous namespace

ExitCode BuildSingleExecutableBlob(const std::string& config_path,
                                   const std::vector<std::string>& args,
                                   const std::vector<std::string>& exec_args) {
  std::optional<SeaConfig> config_opt =
      ParseSingleExecutableConfig(config_path);
  if (config_opt.has_value()) {
    ExitCode code =
        GenerateSingleExecutableBlob(config_opt.value(), args, exec_args);
    return code;
  }

  return ExitCode::kGenericUserError;
}

void GetAsset(const FunctionCallbackInfo<Value>& args) {
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value key(args.GetIsolate(), args[0]);
  SeaResource sea_resource = FindSingleExecutableResource();
  if (sea_resource.assets.empty()) {
    return;
  }
  auto it = sea_resource.assets.find(*key);
  if (it == sea_resource.assets.end()) {
    return;
  }
  // We cast away the constness here, the JS land should ensure that
  // the data is not mutated.
  std::unique_ptr<v8::BackingStore> store = ArrayBuffer::NewBackingStore(
      const_cast<char*>(it->second.data()),
      it->second.size(),
      [](void*, size_t, void*) {},
      nullptr);
  Local<ArrayBuffer> ab = ArrayBuffer::New(args.GetIsolate(), std::move(store));
  args.GetReturnValue().Set(ab);
}

MaybeLocal<Value> LoadSingleExecutableApplication(
    const StartExecutionCallbackInfo& info) {
  // Here we are currently relying on the fact that in NodeMainInstance::Run(),
  // env->context() is entered.
  Local<Context> context = Isolate::GetCurrent()->GetCurrentContext();
  Environment* env = Environment::GetCurrent(context);
  SeaResource sea = FindSingleExecutableResource();

  CHECK(!sea.use_snapshot());
  // TODO(joyeecheung): this should be an external string. Refactor UnionBytes
  // and make it easy to create one based on static content on the fly.
  Local<Value> main_script =
      ToV8Value(env->context(), sea.main_code_or_snapshot).ToLocalChecked();
  return info.run_cjs->Call(
      env->context(), Null(env->isolate()), 1, &main_script);
}

bool MaybeLoadSingleExecutableApplication(Environment* env) {
#ifndef DISABLE_SINGLE_EXECUTABLE_APPLICATION
  if (!IsSingleExecutable()) {
    return false;
  }

  SeaResource sea = FindSingleExecutableResource();

  if (sea.use_snapshot()) {
    // The SEA preparation blob building process should already enforce this,
    // this check is just here to guard against the unlikely case where
    // the SEA preparation blob has been manually modified by someone.
    CHECK(!env->snapshot_deserialize_main().IsEmpty());
    LoadEnvironment(env, StartExecutionCallback{});
    return true;
  }

  LoadEnvironment(env, LoadSingleExecutableApplication);
  return true;
#endif
  return false;
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  SetMethod(context, target, "isSea", IsSea);
  SetMethod(context,
            target,
            "isExperimentalSeaWarningNeeded",
            IsExperimentalSeaWarningNeeded);
  SetMethod(context, target, "getAsset", GetAsset);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(IsSea);
  registry->Register(IsExperimentalSeaWarningNeeded);
  registry->Register(GetAsset);
}

}  // namespace sea
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(sea, node::sea::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(sea, node::sea::RegisterExternalReferences)
