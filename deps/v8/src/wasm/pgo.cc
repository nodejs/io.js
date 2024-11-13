// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/pgo.h"

#include "src/wasm/decoder.h"
#include "src/wasm/wasm-module-builder.h"  // For {ZoneBuffer}.

namespace v8::internal::wasm {

constexpr uint8_t kFunctionExecutedBit = 1 << 0;
constexpr uint8_t kFunctionTieredUpBit = 1 << 1;

class ProfileGenerator {
 public:
  ProfileGenerator(const WasmModule* module,
                   const std::atomic<uint32_t>* tiering_budget_array)
      : module_(module),
        type_feedback_mutex_guard_(&module->type_feedback.mutex),
        tiering_budget_array_(tiering_budget_array) {}

  base::OwnedVector<uint8_t> GetProfileData() {
    ZoneBuffer buffer{&zone_};

    SerializeTypeFeedback(buffer);
    SerializeTieringInfo(buffer);

    return base::OwnedVector<uint8_t>::Of(buffer);
  }

 private:
  void SerializeTypeFeedback(ZoneBuffer& buffer) {
    const std::unordered_map<uint32_t, FunctionTypeFeedback>&
        feedback_for_function = module_->type_feedback.feedback_for_function;

    // Get an ordered list of function indexes, so we generate deterministic
    // data.
    std::vector<uint32_t> ordered_function_indexes;
    ordered_function_indexes.reserve(feedback_for_function.size());
    for (const auto& entry : feedback_for_function) {
      // Skip functions for which we have no feedback.
      if (entry.second.feedback_vector.empty()) continue;
      ordered_function_indexes.push_back(entry.first);
    }
    std::sort(ordered_function_indexes.begin(), ordered_function_indexes.end());

    buffer.write_u32v(static_cast<uint32_t>(ordered_function_indexes.size()));
    for (const uint32_t func_index : ordered_function_indexes) {
      buffer.write_u32v(func_index);
      // Serialize {feedback_vector}.
      const FunctionTypeFeedback& feedback =
          feedback_for_function.at(func_index);
      buffer.write_u32v(static_cast<uint32_t>(feedback.feedback_vector.size()));
      for (const CallSiteFeedback& call_site_feedback :
           feedback.feedback_vector) {
        int cases = call_site_feedback.num_cases();
        buffer.write_i32v(cases);
        for (int i = 0; i < cases; ++i) {
          buffer.write_i32v(call_site_feedback.function_index(i));
          buffer.write_i32v(call_site_feedback.call_count(i));
        }
      }
      // Serialize {call_targets}.
      buffer.write_u32v(static_cast<uint32_t>(feedback.call_targets.size()));
      for (uint32_t call_target : feedback.call_targets) {
        buffer.write_u32v(call_target);
      }
    }
  }

  void SerializeTieringInfo(ZoneBuffer& buffer) {
    const std::unordered_map<uint32_t, FunctionTypeFeedback>&
        feedback_for_function = module_->type_feedback.feedback_for_function;
    const uint32_t initial_budget = v8_flags.wasm_tiering_budget;
    for (uint32_t declared_index = 0;
         declared_index < module_->num_declared_functions; ++declared_index) {
      uint32_t func_index = declared_index + module_->num_imported_functions;
      auto feedback_it = feedback_for_function.find(func_index);
      int prio = feedback_it == feedback_for_function.end()
                     ? 0
                     : feedback_it->second.tierup_priority;
      DCHECK_LE(0, prio);
      uint32_t remaining_budget =
          tiering_budget_array_[declared_index].load(std::memory_order_relaxed);
      DCHECK_GE(initial_budget, remaining_budget);

      bool was_tiered_up = prio > 0;
      bool was_executed = was_tiered_up || remaining_budget != initial_budget;

      // TODO(13209): Make this less V8-specific for productionization.
      buffer.write_u8((was_executed ? kFunctionExecutedBit : 0) |
                      (was_tiered_up ? kFunctionTieredUpBit : 0));
    }
  }

 private:
  const WasmModule* module_;
  AccountingAllocator allocator_;
  Zone zone_{&allocator_, "wasm::ProfileGenerator"};
  base::SharedMutexGuard<base::kShared> type_feedback_mutex_guard_;
  const std::atomic<uint32_t>* const tiering_budget_array_;
};

void DeserializeTypeFeedback(Decoder& decoder, const WasmModule* module) {
  base::SharedMutexGuard<base::kShared> type_feedback_guard{
      &module->type_feedback.mutex};
  std::unordered_map<uint32_t, FunctionTypeFeedback>& feedback_for_function =
      module->type_feedback.feedback_for_function;
  uint32_t num_entries = decoder.consume_u32v("num function entries");
  CHECK_LE(num_entries, module->num_declared_functions);
  for (uint32_t missing_entries = num_entries; missing_entries > 0;
       --missing_entries) {
    FunctionTypeFeedback feedback;
    uint32_t function_index = decoder.consume_u32v("function index");
    // Deserialize {feedback_vector}.
    uint32_t feedback_vector_size =
        decoder.consume_u32v("feedback vector size");
    feedback.feedback_vector.resize(feedback_vector_size);
    for (CallSiteFeedback& feedback : feedback.feedback_vector) {
      int num_cases = decoder.consume_i32v("num cases");
      if (num_cases == 0) continue;  // no feedback
      if (num_cases == 1) {          // monomorphic
        int called_function_index = decoder.consume_i32v("function index");
        int call_count = decoder.consume_i32v("call count");
        feedback = CallSiteFeedback{called_function_index, call_count};
      } else {  // polymorphic
        auto* polymorphic = new CallSiteFeedback::PolymorphicCase[num_cases];
        for (int i = 0; i < num_cases; ++i) {
          polymorphic[i].function_index =
              decoder.consume_i32v("function index");
          polymorphic[i].absolute_call_frequency =
              decoder.consume_i32v("call count");
        }
        feedback = CallSiteFeedback{polymorphic, num_cases};
      }
    }
    // Deserialize {call_targets}.
    uint32_t num_call_targets = decoder.consume_u32v("num call targets");
    feedback.call_targets =
        base::OwnedVector<uint32_t>::NewForOverwrite(num_call_targets);
    for (uint32_t& call_target : feedback.call_targets) {
      call_target = decoder.consume_u32v("call target");
    }

    // Finally, insert the new feedback into the map. Overwrite existing
    // feedback, but check for consistency.
    auto [feedback_it, is_new] =
        feedback_for_function.emplace(function_index, std::move(feedback));
    if (!is_new) {
      FunctionTypeFeedback& old_feedback = feedback_it->second;
      CHECK(old_feedback.feedback_vector.empty() ||
            old_feedback.feedback_vector.size() == feedback_vector_size);
      CHECK_EQ(old_feedback.call_targets.as_vector(),
               feedback.call_targets.as_vector());
      std::swap(old_feedback.feedback_vector, feedback.feedback_vector);
    }
  }
}

std::unique_ptr<ProfileInformation> DeserializeTieringInformation(
    Decoder& decoder, const WasmModule* module) {
  std::vector<uint32_t> executed_functions;
  std::vector<uint32_t> tiered_up_functions;
  uint32_t start = module->num_imported_functions;
  uint32_t end = start + module->num_declared_functions;
  for (uint32_t func_index = start; func_index < end; ++func_index) {
    uint8_t tiering_info = decoder.consume_u8("tiering info");
    CHECK_EQ(0, tiering_info & ~3);
    bool was_executed = tiering_info & kFunctionExecutedBit;
    bool was_tiered_up = tiering_info & kFunctionTieredUpBit;
    if (was_tiered_up) tiered_up_functions.push_back(func_index);
    if (was_executed) executed_functions.push_back(func_index);
  }

  return std::make_unique<ProfileInformation>(std::move(executed_functions),
                                              std::move(tiered_up_functions));
}

std::unique_ptr<ProfileInformation> RestoreProfileData(
    const WasmModule* module, base::Vector<uint8_t> profile_data) {
  Decoder decoder{profile_data.begin(), profile_data.end()};

  DeserializeTypeFeedback(decoder, module);
  std::unique_ptr<ProfileInformation> pgo_info =
      DeserializeTieringInformation(decoder, module);

  CHECK(decoder.ok());
  CHECK_EQ(decoder.pc(), decoder.end());

  return pgo_info;
}

void DumpProfileToFile(const WasmModule* module,
                       base::Vector<const uint8_t> wire_bytes,
                       std::atomic<uint32_t>* tiering_budget_array) {
  CHECK(!wire_bytes.empty());
  // File are named `profile-wasm-<hash>`.
  // We use the same hash as for reported scripts, to make it easier to
  // correlate files to wasm modules (see {CreateWasmScript}).
  uint32_t hash = static_cast<uint32_t>(GetWireBytesHash(wire_bytes));
  base::EmbeddedVector<char, 32> filename;
  SNPrintF(filename, "profile-wasm-%08x", hash);

  ProfileGenerator profile_generator{module, tiering_budget_array};
  base::OwnedVector<uint8_t> profile_data = profile_generator.GetProfileData();

  PrintF(
      "Dumping Wasm PGO data to file '%s' (module size %zu, %u declared "
      "functions, %zu bytes PGO data)\n",
      filename.begin(), wire_bytes.size(), module->num_declared_functions,
      profile_data.size());
  if (FILE* file = base::OS::FOpen(filename.begin(), "wb")) {
    size_t written = fwrite(profile_data.begin(), 1, profile_data.size(), file);
    CHECK_EQ(profile_data.size(), written);
    base::Fclose(file);
  }
}

std::unique_ptr<ProfileInformation> LoadProfileFromFile(
    const WasmModule* module, base::Vector<const uint8_t> wire_bytes) {
  CHECK(!wire_bytes.empty());
  // File are named `profile-wasm-<hash>`.
  // We use the same hash as for reported scripts, to make it easier to
  // correlate files to wasm modules (see {CreateWasmScript}).
  uint32_t hash = static_cast<uint32_t>(GetWireBytesHash(wire_bytes));
  base::EmbeddedVector<char, 32> filename;
  SNPrintF(filename, "profile-wasm-%08x", hash);

  FILE* file = base::OS::FOpen(filename.begin(), "rb");
  if (!file) {
    PrintF("No Wasm PGO data found: Cannot open file '%s'\n", filename.begin());
    return {};
  }

  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  rewind(file);

  PrintF("Loading Wasm PGO data from file '%s' (%zu bytes)\n", filename.begin(),
         size);
  base::OwnedVector<uint8_t> profile_data =
      base::OwnedVector<uint8_t>::NewForOverwrite(size);
  for (size_t read = 0; read < size;) {
    read += fread(profile_data.begin() + read, 1, size - read, file);
    CHECK(!ferror(file));
  }

  base::Fclose(file);

  return RestoreProfileData(module, profile_data.as_vector());
}

}  // namespace v8::internal::wasm
