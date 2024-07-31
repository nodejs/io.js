// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/isolate.h"
#include "src/wasm/streaming-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-serialization.h"

namespace v8 {
namespace internal {
namespace wasm {

class V8_EXPORT_PRIVATE SyncStreamingDecoder : public StreamingDecoder {
 public:
  SyncStreamingDecoder(Isolate* isolate, WasmFeatures enabled,
                       CompileTimeImports compile_imports,
                       Handle<Context> context,
                       const char* api_method_name_for_errors,
                       std::shared_ptr<CompilationResultResolver> resolver)
      : isolate_(isolate),
        enabled_(enabled),
        compile_imports_(compile_imports),
        context_(context),
        api_method_name_for_errors_(api_method_name_for_errors),
        resolver_(resolver) {}

  // The buffer passed into OnBytesReceived is owned by the caller.
  void OnBytesReceived(base::Vector<const uint8_t> bytes) override {
    buffer_.emplace_back(bytes.size());
    CHECK_EQ(buffer_.back().size(), bytes.size());
    std::memcpy(buffer_.back().data(), bytes.data(), bytes.size());
    buffer_size_ += bytes.size();
  }

  void Finish(bool can_use_compiled_module) override {
    // We copy all received chunks into one byte buffer.
    auto bytes = std::make_unique<uint8_t[]>(buffer_size_);
    uint8_t* destination = bytes.get();
    for (auto& chunk : buffer_) {
      std::memcpy(destination, chunk.data(), chunk.size());
      destination += chunk.size();
    }
    CHECK_EQ(destination - bytes.get(), buffer_size_);

    // Check if we can deserialize the module from cache.
    if (can_use_compiled_module && deserializing()) {
      HandleScope scope(isolate_);
      SaveAndSwitchContext saved_context(isolate_, *context_);

      MaybeHandle<WasmModuleObject> module_object = DeserializeNativeModule(
          isolate_, compiled_module_bytes_,
          base::Vector<const uint8_t>(bytes.get(), buffer_size_),
          compile_imports_, base::VectorOf(url()));

      if (!module_object.is_null()) {
        Handle<WasmModuleObject> module = module_object.ToHandleChecked();
        resolver_->OnCompilationSucceeded(module);
        return;
      }
    }

    // Compile the received bytes synchronously.
    ModuleWireBytes wire_bytes(bytes.get(), bytes.get() + buffer_size_);
    ErrorThrower thrower(isolate_, api_method_name_for_errors_);
    MaybeHandle<WasmModuleObject> module_object = GetWasmEngine()->SyncCompile(
        isolate_, enabled_, compile_imports_, &thrower, wire_bytes);
    if (thrower.error()) {
      resolver_->OnCompilationFailed(thrower.Reify());
      return;
    }
    Handle<WasmModuleObject> module = module_object.ToHandleChecked();
    resolver_->OnCompilationSucceeded(module);
  }

  void Abort() override {
    // Abort is fully handled by the API, we only clear the buffer.
    buffer_.clear();
  }

  void NotifyCompilationDiscarded() override { buffer_.clear(); }

  void NotifyNativeModuleCreated(
      const std::shared_ptr<NativeModule>&) override {
    // This function is only called from the {AsyncCompileJob}.
    UNREACHABLE();
  }

 private:
  Isolate* isolate_;
  const WasmFeatures enabled_;
  const CompileTimeImports compile_imports_;
  Handle<Context> context_;
  const char* api_method_name_for_errors_;
  std::shared_ptr<CompilationResultResolver> resolver_;

  std::vector<std::vector<uint8_t>> buffer_;
  size_t buffer_size_ = 0;
};

std::unique_ptr<StreamingDecoder> StreamingDecoder::CreateSyncStreamingDecoder(
    Isolate* isolate, WasmFeatures enabled, CompileTimeImports compile_imports,
    Handle<Context> context, const char* api_method_name_for_errors,
    std::shared_ptr<CompilationResultResolver> resolver) {
  return std::make_unique<SyncStreamingDecoder>(
      isolate, enabled, compile_imports, context, api_method_name_for_errors,
      std::move(resolver));
}
}  // namespace wasm
}  // namespace internal
}  // namespace v8
