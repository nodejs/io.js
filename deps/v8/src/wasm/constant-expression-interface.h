// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_CONSTANT_EXPRESSION_INTERFACE_H_
#define V8_WASM_CONSTANT_EXPRESSION_INTERFACE_H_

#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/wasm-value.h"

namespace v8 {
namespace internal {

class WasmTrustedInstanceData;
class JSArrayBuffer;

namespace wasm {

// An interface for WasmFullDecoder used to decode constant expressions.
// This interface has two modes: only validation (when {isolate_ == nullptr}),
// and code-generation (when {isolate_ != nullptr}). We merge two distinct
// functionalities in one class to reduce the number of WasmFullDecoder
// instantiations, and thus V8 binary code size.
// In code-generation mode, the result can be retrieved with {computed_value()}
// if {!has_error()}, or with {error()} otherwise.
class V8_EXPORT_PRIVATE ConstantExpressionInterface {
 public:
  using ValidationTag = Decoder::FullValidationTag;
  static constexpr DecodingMode decoding_mode = kConstantExpression;
  static constexpr bool kUsesPoppedArgs = true;

  struct Value : public ValueBase<ValidationTag> {
    WasmValue runtime_value;

    template <typename... Args>
    explicit Value(Args&&... args) V8_NOEXCEPT
        : ValueBase(std::forward<Args>(args)...) {}
  };

  using Control = ControlBase<Value, ValidationTag>;
  using FullDecoder =
      WasmFullDecoder<ValidationTag, ConstantExpressionInterface,
                      decoding_mode>;

  ConstantExpressionInterface(
      const WasmModule* module, Isolate* isolate,
      Handle<WasmTrustedInstanceData> trusted_instance_data,
      Handle<WasmTrustedInstanceData> shared_trusted_instance_data)
      : module_(module),
        outer_module_(nullptr),
        isolate_(isolate),
        trusted_instance_data_(trusted_instance_data),
        shared_trusted_instance_data_(shared_trusted_instance_data) {
    DCHECK_NOT_NULL(isolate);
  }

  explicit ConstantExpressionInterface(WasmModule* outer_module)
      : module_(nullptr), outer_module_(outer_module), isolate_(nullptr) {}

#define EMPTY_INTERFACE_FUNCTION(name, ...) \
  V8_INLINE void name(FullDecoder* decoder, ##__VA_ARGS__) {}
  INTERFACE_META_FUNCTIONS(EMPTY_INTERFACE_FUNCTION)
#undef EMPTY_INTERFACE_FUNCTION
#define UNREACHABLE_INTERFACE_FUNCTION(name, ...) \
  V8_INLINE void name(FullDecoder* decoder, ##__VA_ARGS__) { UNREACHABLE(); }
  INTERFACE_NON_CONSTANT_FUNCTIONS(UNREACHABLE_INTERFACE_FUNCTION)
#undef UNREACHABLE_INTERFACE_FUNCTION

#define DECLARE_INTERFACE_FUNCTION(name, ...) \
  void name(FullDecoder* decoder, ##__VA_ARGS__);
  INTERFACE_CONSTANT_FUNCTIONS(DECLARE_INTERFACE_FUNCTION)
#undef DECLARE_INTERFACE_FUNCTION

  WasmValue computed_value() const {
    DCHECK(generate_value());
    // The value has to be initialized.
    DCHECK_NE(computed_value_.type(), kWasmVoid);
    return computed_value_;
  }
  bool end_found() const { return end_found_; }
  bool has_error() const { return error_ != MessageTemplate::kNone; }
  MessageTemplate error() const {
    DCHECK(has_error());
    DCHECK_EQ(computed_value_.type(), kWasmVoid);
    return error_;
  }

 private:
  bool generate_value() const { return isolate_ != nullptr && !has_error(); }
  Handle<WasmTrustedInstanceData> GetTrustedInstanceDataForTypeIndex(
      uint32_t index);

  bool end_found_ = false;
  WasmValue computed_value_;
  MessageTemplate error_ = MessageTemplate::kNone;
  const WasmModule* module_;
  WasmModule* outer_module_;
  Isolate* isolate_;
  Handle<WasmTrustedInstanceData> trusted_instance_data_;
  Handle<WasmTrustedInstanceData> shared_trusted_instance_data_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_CONSTANT_EXPRESSION_INTERFACE_H_
