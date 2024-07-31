// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_STREAMING_DECODER_H_
#define V8_WASM_STREAMING_DECODER_H_

#include <memory>

#include "src/base/macros.h"
#include "src/base/vector.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-result.h"

namespace v8 {
namespace internal {
namespace wasm {
class NativeModule;

// This class is an interface for the StreamingDecoder to start the processing
// of the incoming module bytes.
class V8_EXPORT_PRIVATE StreamingProcessor {
 public:
  virtual ~StreamingProcessor() = default;
  // Process the first 8 bytes of a WebAssembly module. Returns true if the
  // processing finished successfully and the decoding should continue.
  virtual bool ProcessModuleHeader(base::Vector<const uint8_t> bytes) = 0;

  // Process all sections but the code section. Returns true if the processing
  // finished successfully and the decoding should continue.
  virtual bool ProcessSection(SectionCode section_code,
                              base::Vector<const uint8_t> bytes,
                              uint32_t offset) = 0;

  // Process the start of the code section. Returns true if the processing
  // finished successfully and the decoding should continue.
  virtual bool ProcessCodeSectionHeader(int num_functions, uint32_t offset,
                                        std::shared_ptr<WireBytesStorage>,
                                        int code_section_start,
                                        int code_section_length) = 0;

  // Process a function body. Returns true if the processing finished
  // successfully and the decoding should continue.
  virtual bool ProcessFunctionBody(base::Vector<const uint8_t> bytes,
                                   uint32_t offset) = 0;

  // Report the end of a chunk.
  virtual void OnFinishedChunk() = 0;
  // Report the end of the stream. This will be called even after an error has
  // been detected. In any case, the parameter is the total received bytes.
  virtual void OnFinishedStream(base::OwnedVector<const uint8_t> bytes,
                                bool after_error) = 0;
  // Report the abortion of the stream.
  virtual void OnAbort() = 0;

  // Attempt to deserialize the module. Supports embedder caching.
  virtual bool Deserialize(base::Vector<const uint8_t> module_bytes,
                           base::Vector<const uint8_t> wire_bytes) = 0;
};

// The StreamingDecoder takes a sequence of byte arrays, each received by a call
// of {OnBytesReceived}, and extracts the bytes which belong to section payloads
// and function bodies.
class V8_EXPORT_PRIVATE StreamingDecoder {
 public:
  virtual ~StreamingDecoder() = default;

  // The buffer passed into OnBytesReceived is owned by the caller.
  virtual void OnBytesReceived(base::Vector<const uint8_t> bytes) = 0;

  virtual void Finish(bool can_use_compiled_module = true) = 0;

  virtual void Abort() = 0;

  // Notify the StreamingDecoder that the job was discarded and the
  // StreamingProcessor should not be called anymore.
  virtual void NotifyCompilationDiscarded() = 0;

  // Caching support.
  // Sets the callback that is called after a new chunk of the module is tiered
  // up.
  using MoreFunctionsCanBeSerializedCallback =
      std::function<void(const std::shared_ptr<NativeModule>&)>;

  void SetMoreFunctionsCanBeSerializedCallback(
      MoreFunctionsCanBeSerializedCallback callback) {
    more_functions_can_be_serialized_callback_ = std::move(callback);
  }

  // Passes previously compiled module bytes from the embedder's cache.
  // The content shouldn't be used until Finish(true) is called.
  void SetCompiledModuleBytes(base::Vector<const uint8_t> bytes) {
    compiled_module_bytes_ = bytes;
  }

  virtual void NotifyNativeModuleCreated(
      const std::shared_ptr<NativeModule>& native_module) = 0;

  const std::string& url() const { return *url_; }
  std::shared_ptr<const std::string> shared_url() const { return url_; }

  void SetUrl(base::Vector<const char> url) {
    url_->assign(url.begin(), url.size());
  }

  static std::unique_ptr<StreamingDecoder> CreateAsyncStreamingDecoder(
      std::unique_ptr<StreamingProcessor> processor);

  static std::unique_ptr<StreamingDecoder> CreateSyncStreamingDecoder(
      Isolate* isolate, WasmFeatures enabled,
      CompileTimeImports compile_imports, Handle<Context> context,
      const char* api_method_name_for_errors,
      std::shared_ptr<CompilationResultResolver> resolver);

 protected:
  bool deserializing() const { return !compiled_module_bytes_.empty(); }

  const std::shared_ptr<std::string> url_ = std::make_shared<std::string>();
  MoreFunctionsCanBeSerializedCallback
      more_functions_can_be_serialized_callback_;
  // The content of `compiled_module_bytes_` shouldn't be used until
  // Finish(true) is called.
  base::Vector<const uint8_t> compiled_module_bytes_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_STREAMING_DECODER_H_
