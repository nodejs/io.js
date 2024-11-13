// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-deopt-data.h"

#include "src/objects/deoptimization-data.h"

namespace v8::internal::wasm {

std::vector<DeoptimizationLiteral>
WasmDeoptView::BuildDeoptimizationLiteralArray() {
  DCHECK(HasDeoptData());
  static_assert(std::is_trivially_copy_assignable_v<DeoptimizationLiteral>);
  std::vector<DeoptimizationLiteral> deopt_literals(
      base_data_.deopt_literals_size);
  const uint8_t* data = deopt_data_.begin() + sizeof(base_data_) +
                        base_data_.translation_array_size +
                        sizeof(WasmDeoptEntry) * base_data_.entry_count;
  // Copy the data (as the data in the WasmCode object is potentially
  // misaligned).
  std::memcpy(deopt_literals.data(), data,
              base_data_.deopt_literals_size * sizeof(deopt_literals[0]));
  return deopt_literals;
}

base::OwnedVector<uint8_t> WasmDeoptDataProcessor::Serialize(
    int deopt_exit_start_offset, int eager_deopt_count,
    base::Vector<const uint8_t> translation_array,
    base::Vector<wasm::WasmDeoptEntry> deopt_entries,
    const ZoneDeque<DeoptimizationLiteral>& deopt_literals) {
  wasm::WasmDeoptData data;
  data.entry_count = eager_deopt_count;
  data.deopt_exit_start_offset = deopt_exit_start_offset;
  data.eager_deopt_count = eager_deopt_count;
  data.deopt_literals_size = static_cast<uint32_t>(deopt_literals.size());

  data.translation_array_size = static_cast<uint32_t>(translation_array.size());

  size_t translation_array_byte_size =
      translation_array.size() * sizeof(translation_array[0]);
  size_t deopt_entries_byte_size =
      deopt_entries.size() * sizeof(deopt_entries[0]);
  size_t deopt_literals_byte_size =
      deopt_literals.size() * sizeof(deopt_literals[0]);
  size_t byte_size = sizeof(data) + translation_array_byte_size +
                     deopt_entries_byte_size + deopt_literals_byte_size;
  auto result = base::OwnedVector<uint8_t>::New(byte_size);
  uint8_t* result_iter = result.begin();
  std::memcpy(result_iter, &data, sizeof(data));
  result_iter += sizeof(data);
  std::memcpy(result_iter, translation_array.data(),
              translation_array_byte_size);
  result_iter += translation_array_byte_size;
  std::memcpy(result_iter, deopt_entries.data(), deopt_entries_byte_size);
  result_iter += deopt_entries_byte_size;
  static_assert(std::is_trivially_copyable_v<
                std::remove_reference<decltype(deopt_literals[0])>>);
  for (const auto& literal : deopt_literals) {
    // We can't serialize objects. Wasm should never contain object literals as
    // it is isolate-independent.
    CHECK_NE(literal.kind(), DeoptimizationLiteralKind::kObject);
    std::memcpy(result_iter, &literal, sizeof(literal));
    result_iter += sizeof(literal);
  }
  DCHECK_EQ(result_iter, result.end());
  return result;
}

}  // namespace v8::internal::wasm
