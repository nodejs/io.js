// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/constant-expression.h"

#include "src/handles/handles.h"
#include "src/heap/factory-inl.h"
#include "src/heap/factory.h"
#include "src/objects/oddball.h"
#include "src/roots/roots.h"
#include "src/wasm/constant-expression-interface.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

WireBytesRef ConstantExpression::wire_bytes_ref() const {
  DCHECK_EQ(kind(), kWireBytesRef);
  return WireBytesRef(OffsetField::decode(bit_field_),
                      LengthField::decode(bit_field_));
}

ValueOrError EvaluateConstantExpression(
    Zone* zone, ConstantExpression expr, ValueType expected, Isolate* isolate,
    Handle<WasmTrustedInstanceData> trusted_instance_data,
    Handle<WasmTrustedInstanceData> shared_trusted_instance_data) {
  switch (expr.kind()) {
    case ConstantExpression::kEmpty:
      UNREACHABLE();
    case ConstantExpression::kI32Const:
      return WasmValue(expr.i32_value());
    case ConstantExpression::kRefNull:
      return WasmValue(
          expected == kWasmExternRef || expected == kWasmNullExternRef ||
                  expected == kWasmNullExnRef || expected == kWasmExnRef
              ? Cast<Object>(isolate->factory()->null_value())
              : Cast<Object>(isolate->factory()->wasm_null()),
          ValueType::RefNull(expr.repr()));
    case ConstantExpression::kRefFunc: {
      uint32_t index = expr.index();
      const WasmModule* module = trusted_instance_data->module();
      bool function_is_shared =
          module->types[module->functions[index].sig_index].is_shared;
      Handle<WasmFuncRef> value = WasmTrustedInstanceData::GetOrCreateFuncRef(
          isolate,
          function_is_shared ? shared_trusted_instance_data
                             : trusted_instance_data,
          index);
      return WasmValue(value, expected);
    }
    case ConstantExpression::kWireBytesRef: {
      WireBytesRef ref = expr.wire_bytes_ref();

      base::Vector<const uint8_t> module_bytes =
          trusted_instance_data->native_module()->wire_bytes();

      const uint8_t* start = module_bytes.begin() + ref.offset();
      const uint8_t* end = module_bytes.begin() + ref.end_offset();

      auto sig = FixedSizeSignature<ValueType>::Returns(expected);
      // We have already validated the expression, so we might as well
      // revalidate it as non-shared, which is strictly more permissive.
      // TODO(14616): Rethink this.
      constexpr bool kIsShared = false;
      FunctionBody body(&sig, ref.offset(), start, end, kIsShared);
      WasmDetectedFeatures detected;
      const WasmModule* module = trusted_instance_data->module();
      ValueOrError result;
      {
        // We need a scope for the decoder because its destructor resets some
        // Zone elements, which has to be done before we reset the Zone
        // afterwards.
        // We use FullValidationTag so we do not have to create another template
        // instance of WasmFullDecoder, which would cost us >50Kb binary code
        // size.
        WasmFullDecoder<Decoder::FullValidationTag, ConstantExpressionInterface,
                        kConstantExpression>
            decoder(zone, module, WasmEnabledFeatures::All(), &detected, body,
                    module, isolate, trusted_instance_data,
                    shared_trusted_instance_data);

        decoder.DecodeFunctionBody();

        result = decoder.interface().has_error()
                     ? ValueOrError(decoder.interface().error())
                     : ValueOrError(decoder.interface().computed_value());
      }

      zone->Reset();

      return result;
    }
  }
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
