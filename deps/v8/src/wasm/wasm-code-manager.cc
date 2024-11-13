// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-code-manager.h"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <optional>

#include "src/base/atomicops.h"
#include "src/base/build_config.h"
#include "src/base/iterator.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/wrappers.h"
#include "src/base/small-vector.h"
#include "src/base/string-format.h"
#include "src/base/vector.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/common/code-memory-access.h"
#include "src/common/globals.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/diagnostics/disassembler.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/objects/objects-inl.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/utils/ostreams.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/jump-table-assembler.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/names-provider.h"
#include "src/wasm/pgo.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/wasm-builtin-list.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-deopt-data.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-import-wrapper-cache.h"
#include "src/wasm/wasm-module-sourcemap.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/well-known-imports.h"

#if V8_ENABLE_DRUMBRAKE
#include "src/wasm/interpreter/wasm-interpreter-runtime.h"
#endif  // V8_ENABLE_DRUMBRAKE

#if defined(V8_OS_WIN64)
#include "src/diagnostics/unwinding-info-win64.h"
#endif  // V8_OS_WIN64

#define TRACE_HEAP(...)                                       \
  do {                                                        \
    if (v8_flags.trace_wasm_native_heap) PrintF(__VA_ARGS__); \
  } while (false)

namespace v8 {
namespace internal {
namespace wasm {

using trap_handler::ProtectedInstructionData;

// Check that {WasmCode} objects are sufficiently small. We create many of them,
// often for rather small functions.
// Increase the limit if needed, but first check if the size increase is
// justified.
#ifndef V8_GC_MOLE
static_assert(sizeof(WasmCode) <= 96);
#endif

base::AddressRegion DisjointAllocationPool::Merge(
    base::AddressRegion new_region) {
  // Find the possible insertion position by identifying the first region whose
  // start address is not less than that of {new_region}. Since there cannot be
  // any overlap between regions, this also means that the start of {above} is
  // bigger or equal than the *end* of {new_region}.
  auto above = regions_.lower_bound(new_region);
  DCHECK(above == regions_.end() || above->begin() >= new_region.end());

  // Check whether to merge with {above}.
  if (above != regions_.end() && new_region.end() == above->begin()) {
    base::AddressRegion merged_region{new_region.begin(),
                                      new_region.size() + above->size()};
    DCHECK_EQ(merged_region.end(), above->end());
    // Check whether to also merge with the region below.
    if (above != regions_.begin()) {
      auto below = above;
      --below;
      if (below->end() == new_region.begin()) {
        merged_region = {below->begin(), below->size() + merged_region.size()};
        regions_.erase(below);
      }
    }
    auto insert_pos = regions_.erase(above);
    regions_.insert(insert_pos, merged_region);
    return merged_region;
  }

  // No element below, and not adjavent to {above}: insert and done.
  if (above == regions_.begin()) {
    regions_.insert(above, new_region);
    return new_region;
  }

  auto below = above;
  --below;
  // Consistency check:
  DCHECK(above == regions_.end() || below->end() < above->begin());

  // Adjacent to {below}: merge and done.
  if (below->end() == new_region.begin()) {
    base::AddressRegion merged_region{below->begin(),
                                      below->size() + new_region.size()};
    DCHECK_EQ(merged_region.end(), new_region.end());
    regions_.erase(below);
    regions_.insert(above, merged_region);
    return merged_region;
  }

  // Not adjacent to any existing region: insert between {below} and {above}.
  DCHECK_LT(below->end(), new_region.begin());
  regions_.insert(above, new_region);
  return new_region;
}

base::AddressRegion DisjointAllocationPool::Allocate(size_t size) {
  return AllocateInRegion(size,
                          {kNullAddress, std::numeric_limits<size_t>::max()});
}

base::AddressRegion DisjointAllocationPool::AllocateInRegion(
    size_t size, base::AddressRegion region) {
  // Get an iterator to the first contained region whose start address is not
  // smaller than the start address of {region}. Start the search from the
  // region one before that (the last one whose start address is smaller).
  auto it = regions_.lower_bound(region);
  if (it != regions_.begin()) --it;

  for (auto end = regions_.end(); it != end; ++it) {
    base::AddressRegion overlap = it->GetOverlap(region);
    if (size > overlap.size()) continue;
    base::AddressRegion ret{overlap.begin(), size};
    base::AddressRegion old = *it;
    auto insert_pos = regions_.erase(it);
    if (size == old.size()) {
      // We use the full region --> nothing to add back.
    } else if (ret.begin() == old.begin()) {
      // We return a region at the start --> shrink old region from front.
      regions_.insert(insert_pos, {old.begin() + size, old.size() - size});
    } else if (ret.end() == old.end()) {
      // We return a region at the end --> shrink remaining region.
      regions_.insert(insert_pos, {old.begin(), old.size() - size});
    } else {
      // We return something in the middle --> split the remaining region
      // (insert the region with smaller address first).
      regions_.insert(insert_pos, {old.begin(), ret.begin() - old.begin()});
      regions_.insert(insert_pos, {ret.end(), old.end() - ret.end()});
    }
    return ret;
  }
  return {};
}

Address WasmCode::constant_pool() const {
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    if (constant_pool_offset_ < code_comments_offset_) {
      return instruction_start() + constant_pool_offset_;
    }
  }
  return kNullAddress;
}

Address WasmCode::handler_table() const {
  return instruction_start() + handler_table_offset_;
}

int WasmCode::handler_table_size() const {
  DCHECK_GE(constant_pool_offset_, handler_table_offset_);
  return static_cast<int>(constant_pool_offset_ - handler_table_offset_);
}

Address WasmCode::code_comments() const {
  return instruction_start() + code_comments_offset_;
}

int WasmCode::code_comments_size() const {
  DCHECK_GE(unpadded_binary_size_, code_comments_offset_);
  return static_cast<int>(unpadded_binary_size_ - code_comments_offset_);
}

std::unique_ptr<const uint8_t[]> WasmCode::ConcatenateBytes(
    std::initializer_list<base::Vector<const uint8_t>> vectors) {
  size_t total_size = 0;
  for (auto& vec : vectors) total_size += vec.size();
  // Use default-initialization (== no initialization).
  std::unique_ptr<uint8_t[]> result{new uint8_t[total_size]};
  uint8_t* ptr = result.get();
  for (auto& vec : vectors) {
    if (vec.empty()) continue;  // Avoid nullptr in {memcpy}.
    memcpy(ptr, vec.begin(), vec.size());
    ptr += vec.size();
  }
  return result;
}

void WasmCode::RegisterTrapHandlerData() {
  DCHECK(!has_trap_handler_index());
  if (kind() != WasmCode::kWasmFunction) return;
  if (protected_instructions_size_ == 0) return;

  Address base = instruction_start();

  size_t size = instructions().size();
  auto protected_instruction_data = this->protected_instructions();
  const int index =
      RegisterHandlerData(base, size, protected_instruction_data.size(),
                          protected_instruction_data.begin());

  // TODO(eholk): if index is negative, fail.
  CHECK_LE(0, index);
  set_trap_handler_index(index);
  DCHECK(has_trap_handler_index());
}

bool WasmCode::ShouldBeLogged(Isolate* isolate) {
  // The return value is cached in {WasmEngine::IsolateData::log_codes}. Ensure
  // to call {WasmEngine::EnableCodeLogging} if this return value would change
  // for any isolate. Otherwise we might lose code events.
  return isolate->IsLoggingCodeCreation();
}

std::string WasmCode::DebugName() const {
  switch (kind()) {
    case kWasmToCapiWrapper:
      return "wasm-to-c";
    case kJumpTable:
      return "jump-table";
    case kWasmToJsWrapper:
      return "wasm-to-js";
#if V8_ENABLE_DRUMBRAKE
    case kInterpreterEntry:
      return "interpreter entry";
#endif  // V8_ENABLE_DRUMBRAKE
    case kWasmFunction:
      // Gets handled below
      break;
  }

  ModuleWireBytes wire_bytes(native_module()->wire_bytes());
  const WasmModule* module = native_module()->module();
  WireBytesRef name_ref =
      module->lazily_generated_names.LookupFunctionName(wire_bytes, index());
  WasmName name = wire_bytes.GetNameOrNull(name_ref);
  std::string name_buffer;
  if (name.empty()) {
    name_buffer.resize(32);
    name_buffer.resize(
        SNPrintF(base::VectorOf(&name_buffer.front(), name_buffer.size()),
                 "wasm-function[%d]", index()));
  } else {
    name_buffer.append(name.begin(), name.end());
  }
  return name_buffer;
}

void WasmCode::LogCode(Isolate* isolate, const char* source_url,
                       int script_id) const {
  DCHECK(ShouldBeLogged(isolate));
  if (IsAnonymous() && kind() != WasmCode::Kind::kWasmToJsWrapper) return;

  ModuleWireBytes wire_bytes(native_module_->wire_bytes());
  const WasmModule* module = native_module_->module();
  std::string fn_name = DebugName();
  WasmName name = base::VectorOf(fn_name);

  const WasmDebugSymbols& debug_symbols = module->debug_symbols;
  auto load_wasm_source_map = isolate->wasm_load_source_map_callback();
  auto source_map = native_module_->GetWasmSourceMap();
  if (!source_map && debug_symbols.type == WasmDebugSymbols::Type::SourceMap &&
      !debug_symbols.external_url.is_empty() && load_wasm_source_map) {
    WasmName external_url =
        wire_bytes.GetNameOrNull(debug_symbols.external_url);
    std::string external_url_string(external_url.data(), external_url.size());
    HandleScope scope(isolate);
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
    Local<v8::String> source_map_str =
        load_wasm_source_map(v8_isolate, external_url_string.c_str());
    native_module_->SetWasmSourceMap(
        std::make_unique<WasmModuleSourceMap>(v8_isolate, source_map_str));
  }

  // Record source positions before adding code, otherwise when code is added,
  // there are no source positions to associate with the added code.
  if (!source_positions().empty()) {
    LOG_CODE_EVENT(isolate, WasmCodeLinePosInfoRecordEvent(instruction_start(),
                                                           source_positions()));
  }

  int code_offset = 0;
  if (!IsAnonymous()) {
    code_offset = module->functions[index_].code.offset();
  }
  PROFILE(isolate, CodeCreateEvent(LogEventListener::CodeTag::kFunction, this,
                                   name, source_url, code_offset, script_id));
}

namespace {
bool ProtectedInstructionDataCompare(const ProtectedInstructionData& left,
                                     const ProtectedInstructionData& right) {
  return left.instr_offset < right.instr_offset;
}
}  // namespace

bool WasmCode::IsProtectedInstruction(Address pc) {
  base::Vector<const trap_handler::ProtectedInstructionData> instructions =
      protected_instructions();
  ProtectedInstructionData offset{
      static_cast<uint32_t>(pc - instruction_start())};
  return std::binary_search(instructions.begin(), instructions.end(), offset,
                            ProtectedInstructionDataCompare);
}

void WasmCode::Validate() const {
  // The packing strategy for {tagged_parameter_slots} only works if both the
  // max number of parameters and their max combined stack slot usage fits into
  // their respective half of the result value.
  static_assert(wasm::kV8MaxWasmFunctionParams <
                std::numeric_limits<uint16_t>::max());
  static constexpr int kMaxSlotsPerParam = 4;  // S128 on 32-bit platforms.
  static_assert(wasm::kV8MaxWasmFunctionParams * kMaxSlotsPerParam <
                std::numeric_limits<uint16_t>::max());

#ifdef DEBUG
  // Scope for foreign WasmCode pointers.
  WasmCodeRefScope code_ref_scope;
  // We expect certain relocation info modes to never appear in {WasmCode}
  // objects or to be restricted to a small set of valid values. Hence the
  // iteration below does not use a mask, but visits all relocation data.
  for (RelocIterator it(instructions(), reloc_info(), constant_pool());
       !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    switch (mode) {
      case RelocInfo::WASM_CALL: {
        Address target = it.rinfo()->wasm_call_address();
        WasmCode* code = native_module_->Lookup(target);
        CHECK_NOT_NULL(code);
        CHECK_EQ(WasmCode::kJumpTable, code->kind());
        CHECK(code->contains(target));
        break;
      }
      case RelocInfo::WASM_STUB_CALL: {
        Address target = it.rinfo()->wasm_stub_call_address();
        WasmCode* code = native_module_->Lookup(target);
        CHECK_NOT_NULL(code);
        CHECK_EQ(WasmCode::kJumpTable, code->kind());
        CHECK(code->contains(target));
        break;
      }
      case RelocInfo::WASM_CANONICAL_SIG_ID: {
        uint32_t sig_id = it.rinfo()->wasm_canonical_sig_id();
        CHECK_LE(sig_id, GetTypeCanonicalizer()->GetCurrentNumberOfTypes());
        break;
      }
      case RelocInfo::INTERNAL_REFERENCE:
      case RelocInfo::INTERNAL_REFERENCE_ENCODED: {
        Address target = it.rinfo()->target_internal_reference();
        CHECK(contains(target));
        break;
      }
      case RelocInfo::EXTERNAL_REFERENCE:
      case RelocInfo::CONST_POOL:
      case RelocInfo::VENEER_POOL:
        // These are OK to appear.
        break;
      default:
        FATAL("Unexpected mode: %d", mode);
    }
  }
#endif
}

void WasmCode::MaybePrint() const {
  // Determines whether flags want this code to be printed.
  bool function_index_matches =
      (!IsAnonymous() &&
       v8_flags.print_wasm_code_function_index == static_cast<int>(index()));
  if (v8_flags.print_code ||
      (kind() == kWasmFunction
           ? (v8_flags.print_wasm_code || function_index_matches)
           : v8_flags.print_wasm_stub_code.value())) {
    std::string name = DebugName();
    Print(name.c_str());
  }
}

void WasmCode::Print(const char* name) const {
  StdoutStream os;
  os << "--- WebAssembly code ---\n";
  Disassemble(name, os);
  if (native_module_->HasDebugInfo()) {
    if (auto* debug_side_table =
            native_module_->GetDebugInfo()->GetDebugSideTableIfExists(this)) {
      debug_side_table->Print(os);
    }
  }
  os << "--- End code ---\n";
}

void WasmCode::Disassemble(const char* name, std::ostream& os,
                           Address current_pc) const {
  if (name) os << "name: " << name << "\n";
  if (!IsAnonymous()) os << "index: " << index() << "\n";
  os << "kind: " << GetWasmCodeKindAsString(kind()) << "\n";
  if (kind() == kWasmFunction) {
    DCHECK(is_liftoff() || tier() == ExecutionTier::kTurbofan);
    const char* compiler =
        is_liftoff() ? (for_debugging() ? "Liftoff (debug)" : "Liftoff")
                     : "TurboFan";
    os << "compiler: " << compiler << "\n";
  }
  size_t padding = instructions().size() - unpadded_binary_size_;
  os << "Body (size = " << instructions().size() << " = "
     << unpadded_binary_size_ << " + " << padding << " padding)\n";

  int instruction_size = unpadded_binary_size_;
  if (constant_pool_offset_ < instruction_size) {
    instruction_size = constant_pool_offset_;
  }
  if (safepoint_table_offset_ && safepoint_table_offset_ < instruction_size) {
    instruction_size = safepoint_table_offset_;
  }
  if (handler_table_offset_ < instruction_size) {
    instruction_size = handler_table_offset_;
  }
  DCHECK_LT(0, instruction_size);

#ifdef ENABLE_DISASSEMBLER
  os << "Instructions (size = " << instruction_size << ")\n";
  Disassembler::Decode(nullptr, os, instructions().begin(),
                       instructions().begin() + instruction_size,
                       CodeReference(this), current_pc);
  os << "\n";

  if (handler_table_size() > 0) {
    HandlerTable table(this);
    os << "Exception Handler Table (size = " << table.NumberOfReturnEntries()
       << "):\n";
    table.HandlerTableReturnPrint(os);
    os << "\n";
  }

  if (protected_instructions_size_ > 0) {
    os << "Protected instructions:\n pc offset\n";
    for (auto& data : protected_instructions()) {
      os << std::setw(10) << std::hex << data.instr_offset << std::setw(10)
         << "\n";
    }
    os << "\n";
  }

  if (!source_positions().empty()) {
    os << "Source positions:\n pc offset  position\n";
    for (SourcePositionTableIterator it(source_positions()); !it.done();
         it.Advance()) {
      os << std::setw(10) << std::hex << it.code_offset() << std::dec
         << std::setw(10) << it.source_position().ScriptOffset()
         << (it.is_statement() ? "  statement" : "") << "\n";
    }
    os << "\n";
  }

  if (deopt_data_size_ > 0) {
    // TODO(mliedtke): It'd be more readable to format this as a table.
    WasmDeoptView view(deopt_data());
    const WasmDeoptData data = view.GetDeoptData();
    os << "Deopt exits (entries = " << data.entry_count
       << ", byte size = " << deopt_data_size_ << ")\n";
    uint32_t deopt_offset = data.deopt_exit_start_offset;
    for (uint32_t i = 0; i < data.entry_count; ++i) {
      WasmDeoptEntry entry = view.GetDeoptEntry(i);
      os << std::hex << deopt_offset << std::dec
         << ": function offset = " << entry.bytecode_offset
         << ", translation = " << entry.translation_index << '\n';
      deopt_offset += Deoptimizer::kEagerDeoptExitSize;
    }
    os << '\n';
  }

  if (safepoint_table_offset_ > 0) {
    SafepointTable table(this);
    table.Print(os);
    os << "\n";
  }

  os << "RelocInfo (size = " << reloc_info().size() << ")\n";
  for (RelocIterator it(instructions(), reloc_info(), constant_pool());
       !it.done(); it.next()) {
    it.rinfo()->Print(nullptr, os);
  }
  os << "\n";
#else   // !ENABLE_DISASSEMBLER
  os << "Instructions (size = " << instruction_size << ", "
     << static_cast<void*>(instructions().begin()) << "-"
     << static_cast<void*>(instructions().begin() + instruction_size) << ")\n";
#endif  // !ENABLE_DISASSEMBLER
}

const char* GetWasmCodeKindAsString(WasmCode::Kind kind) {
  switch (kind) {
    case WasmCode::kWasmFunction:
      return "wasm function";
    case WasmCode::kWasmToCapiWrapper:
      return "wasm-to-capi";
    case WasmCode::kWasmToJsWrapper:
      return "wasm-to-js";
#if V8_ENABLE_DRUMBRAKE
    case WasmCode::kInterpreterEntry:
      return "interpreter entry";
#endif  // V8_ENABLE_DRUMBRAKE
    case WasmCode::kJumpTable:
      return "jump table";
  }
  return "unknown kind";
}

WasmCode::~WasmCode() {
  if (has_trap_handler_index()) {
    trap_handler::ReleaseHandlerData(trap_handler_index());
  }
}

V8_WARN_UNUSED_RESULT bool WasmCode::DecRefOnPotentiallyDeadCode() {
  if (GetWasmEngine()->AddPotentiallyDeadCode(this)) {
    // The code just became potentially dead. The ref count we wanted to
    // decrement is now transferred to the set of potentially dead code, and
    // will be decremented when the next GC is run.
    return false;
  }
  // If we reach here, the code was already potentially dead. Decrement the ref
  // count, and return true if it drops to zero.
  return DecRefOnDeadCode();
}

// static
void WasmCode::DecrementRefCount(base::Vector<WasmCode* const> code_vec) {
  // Decrement the ref counter of all given code objects. Keep the ones whose
  // ref count drops to zero.
  WasmEngine::DeadCodeMap dead_code;
  for (WasmCode* code : code_vec) {
    if (!code->DecRef()) continue;  // Remaining references.
    dead_code[code->native_module()].push_back(code);
  }

  if (dead_code.empty()) return;

  GetWasmEngine()->FreeDeadCode(dead_code);
}

SourcePosition WasmCode::GetSourcePositionBefore(int code_offset) {
  SourcePosition position;
  for (SourcePositionTableIterator iterator(source_positions());
       !iterator.done() && iterator.code_offset() < code_offset;
       iterator.Advance()) {
    position = iterator.source_position();
  }
  return position;
}

int WasmCode::GetSourceOffsetBefore(int code_offset) {
  return GetSourcePositionBefore(code_offset).ScriptOffset();
}

std::tuple<int, bool, SourcePosition> WasmCode::GetInliningPosition(
    int inlining_id) const {
  const size_t elem_size = sizeof(int) + sizeof(bool) + sizeof(SourcePosition);
  const uint8_t* start = inlining_positions().begin() + elem_size * inlining_id;
  DCHECK_LE(start, inlining_positions().end());
  std::tuple<int, bool, SourcePosition> result;
  std::memcpy(&std::get<0>(result), start, sizeof std::get<0>(result));
  std::memcpy(&std::get<1>(result), start + sizeof std::get<0>(result),
              sizeof std::get<1>(result));
  std::memcpy(&std::get<2>(result),
              start + sizeof std::get<0>(result) + sizeof std::get<1>(result),
              sizeof std::get<2>(result));
  return result;
}

size_t WasmCode::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(WasmCode, 96);
  size_t result = sizeof(WasmCode);
  // For meta_data_.
  result += protected_instructions_size_ + reloc_info_size_ +
            source_positions_size_ + inlining_positions_size_ +
            deopt_data_size_;
  return result;
}

WasmCodeAllocator::WasmCodeAllocator(std::shared_ptr<Counters> async_counters)
    : async_counters_(std::move(async_counters)) {
  owned_code_space_.reserve(4);
}

WasmCodeAllocator::~WasmCodeAllocator() {
  GetWasmCodeManager()->FreeNativeModule(base::VectorOf(owned_code_space_),
                                         committed_code_space());
}

void WasmCodeAllocator::Init(VirtualMemory code_space) {
  DCHECK(owned_code_space_.empty());
  DCHECK(free_code_space_.IsEmpty());
  free_code_space_.Merge(code_space.region());
  owned_code_space_.emplace_back(std::move(code_space));
  async_counters_->wasm_module_num_code_spaces()->AddSample(1);
}

namespace {
// On Windows, we cannot commit a region that straddles different reservations
// of virtual memory. Because we bump-allocate, and because, if we need more
// memory, we append that memory at the end of the owned_code_space_ list, we
// traverse that list in reverse order to find the reservation(s) that guide how
// to chunk the region to commit.
#if V8_OS_WIN
constexpr bool kNeedsToSplitRangeByReservations = true;
#else
constexpr bool kNeedsToSplitRangeByReservations = false;
#endif

base::SmallVector<base::AddressRegion, 1> SplitRangeByReservationsIfNeeded(
    base::AddressRegion range,
    const std::vector<VirtualMemory>& owned_code_space) {
  if (!kNeedsToSplitRangeByReservations) return {range};

  base::SmallVector<base::AddressRegion, 1> split_ranges;
  size_t missing_begin = range.begin();
  size_t missing_end = range.end();
  for (auto& vmem : base::Reversed(owned_code_space)) {
    Address overlap_begin = std::max(missing_begin, vmem.address());
    Address overlap_end = std::min(missing_end, vmem.end());
    if (overlap_begin >= overlap_end) continue;
    split_ranges.emplace_back(overlap_begin, overlap_end - overlap_begin);
    // Opportunistically reduce the missing range. This might terminate the loop
    // early.
    if (missing_begin == overlap_begin) missing_begin = overlap_end;
    if (missing_end == overlap_end) missing_end = overlap_begin;
    if (missing_begin >= missing_end) break;
  }
#ifdef ENABLE_SLOW_DCHECKS
  // The returned vector should cover the full range.
  size_t total_split_size = 0;
  for (auto split : split_ranges) total_split_size += split.size();
  DCHECK_EQ(range.size(), total_split_size);
#endif
  return split_ranges;
}

int NumWasmFunctionsInFarJumpTable(uint32_t num_declared_functions) {
  return NativeModule::kNeedsFarJumpsBetweenCodeSpaces
             ? static_cast<int>(num_declared_functions)
             : 0;
}

// Returns an overapproximation of the code size overhead per new code space
// created by the jump tables.
size_t OverheadPerCodeSpace(uint32_t num_declared_functions) {
  // Overhead for the jump table.
  size_t overhead = RoundUp<kCodeAlignment>(
      JumpTableAssembler::SizeForNumberOfSlots(num_declared_functions));

#if defined(V8_OS_WIN64)
  // On Win64, we need to reserve some pages at the beginning of an executable
  // space. See {AddCodeSpace}.
  overhead += Heap::GetCodeRangeReservedAreaSize();
#endif  // V8_OS_WIN64

  // Overhead for the far jump table.
  overhead +=
      RoundUp<kCodeAlignment>(JumpTableAssembler::SizeForNumberOfFarJumpSlots(
          BuiltinLookup::BuiltinCount(),
          NumWasmFunctionsInFarJumpTable(num_declared_functions)));

  return overhead;
}

// Returns an estimate how much code space should be reserved. This can be
// smaller than the passed-in {code_size_estimate}, see comments in the code.
size_t ReservationSize(size_t code_size_estimate, int num_declared_functions,
                       size_t total_reserved) {
  size_t overhead = OverheadPerCodeSpace(num_declared_functions);

  // Reserve the maximum of
  //   a) needed size + overhead (this is the minimum needed)
  //   b) 2 * overhead (to not waste too much space by overhead)
  //   c) 1/4 of current total reservation size (to grow exponentially)
  // For the minimum size we only take the overhead into account and not the
  // code space estimate, for two reasons:
  //  - The code space estimate is only an estimate; we might actually need less
  //    space later.
  //  - When called at module construction time we pass the estimate for all
  //    code in the module; this can still be split up into multiple spaces
  //    later.
  size_t minimum_size = 2 * overhead;
  size_t suggested_size =
      std::max(std::max(RoundUp<kCodeAlignment>(code_size_estimate) + overhead,
                        minimum_size),
               total_reserved / 4);

  const size_t max_code_space_size =
      size_t{v8_flags.wasm_max_code_space_size_mb} * MB;
  if (V8_UNLIKELY(minimum_size > max_code_space_size)) {
    auto oom_detail = base::FormattedString{}
                      << "required reservation minimum (" << minimum_size
                      << ") is bigger than supported maximum ("
                      << max_code_space_size << ")";
    V8::FatalProcessOutOfMemory(nullptr,
                                "Exceeding maximum wasm code space size",
                                oom_detail.PrintToArray().data());
    UNREACHABLE();
  }

  // Limit by the maximum code space size.
  size_t reserve_size = std::min(max_code_space_size, suggested_size);

  return reserve_size;
}

// Sentinel value to be used for {AllocateForCodeInRegion} for specifying no
// restriction on the region to allocate in.
constexpr base::AddressRegion kUnrestrictedRegion{
    kNullAddress, std::numeric_limits<size_t>::max()};

}  // namespace

base::Vector<uint8_t> WasmCodeAllocator::AllocateForCode(
    NativeModule* native_module, size_t size) {
  return AllocateForCodeInRegion(native_module, size, kUnrestrictedRegion);
}

base::Vector<uint8_t> WasmCodeAllocator::AllocateForCodeInRegion(
    NativeModule* native_module, size_t size, base::AddressRegion region) {
  DCHECK_LT(0, size);
  auto* code_manager = GetWasmCodeManager();
  size = RoundUp<kCodeAlignment>(size);
  base::AddressRegion code_space =
      free_code_space_.AllocateInRegion(size, region);
  if (V8_UNLIKELY(code_space.is_empty())) {
    // Only allocations without a specific region are allowed to fail. Otherwise
    // the region must have been allocated big enough to hold all initial
    // allocations (jump tables etc).
    CHECK_EQ(kUnrestrictedRegion, region);

    size_t total_reserved = 0;
    for (auto& vmem : owned_code_space_) total_reserved += vmem.size();
    size_t reserve_size = ReservationSize(
        size, native_module->module()->num_declared_functions, total_reserved);
    if (reserve_size < size) {
      auto oom_detail = base::FormattedString{}
                        << "cannot reserve space for " << size
                        << "bytes of code (maximum reservation size is "
                        << reserve_size << ")";
      V8::FatalProcessOutOfMemory(nullptr, "Grow wasm code space",
                                  oom_detail.PrintToArray().data());
    }
    VirtualMemory new_mem = code_manager->TryAllocate(reserve_size);
    if (!new_mem.IsReserved()) {
      auto oom_detail = base::FormattedString{}
                        << "cannot allocate more code space (" << reserve_size
                        << " bytes, currently " << total_reserved << ")";
      V8::FatalProcessOutOfMemory(nullptr, "Grow wasm code space",
                                  oom_detail.PrintToArray().data());
      UNREACHABLE();
    }

    base::AddressRegion new_region = new_mem.region();
    code_manager->AssignRange(new_region, native_module);
    free_code_space_.Merge(new_region);
    owned_code_space_.emplace_back(std::move(new_mem));
    native_module->AddCodeSpaceLocked(new_region);

    code_space = free_code_space_.Allocate(size);
    CHECK(!code_space.is_empty());

    async_counters_->wasm_module_num_code_spaces()->AddSample(
        static_cast<int>(owned_code_space_.size()));
  }
  const Address commit_page_size = CommitPageSize();
  Address commit_start = RoundUp(code_space.begin(), commit_page_size);

  Address commit_end = RoundUp(code_space.end(), commit_page_size);
  // {commit_start} will be either code_space.start or the start of the next
  // page. {commit_end} will be the start of the page after the one in which
  // the allocation ends.
  // We start from an aligned start, and we know we allocated vmem in
  // page multiples.
  // We just need to commit what's not committed. The page in which we
  // start is already committed (or we start at the beginning of a page).
  // The end needs to be committed all through the end of the page.
  if (commit_start < commit_end) {
    for (base::AddressRegion split_range : SplitRangeByReservationsIfNeeded(
             {commit_start, commit_end - commit_start}, owned_code_space_)) {
      code_manager->Commit(split_range);
    }
    committed_code_space_.fetch_add(commit_end - commit_start);
    // Committed code cannot grow bigger than maximum code space size.
    DCHECK_LE(committed_code_space_.load(),
              v8_flags.wasm_max_committed_code_mb * MB);
  }
  DCHECK(IsAligned(code_space.begin(), kCodeAlignment));
  generated_code_size_.fetch_add(code_space.size(), std::memory_order_relaxed);

  TRACE_HEAP("Code alloc for %p: 0x%" PRIxPTR ",+%zu\n", this,
             code_space.begin(), size);
  return {reinterpret_cast<uint8_t*>(code_space.begin()), code_space.size()};
}

void WasmCodeAllocator::FreeCode(base::Vector<WasmCode* const> codes) {
  // Zap code area and collect freed code regions.
  DisjointAllocationPool freed_regions;
  size_t code_size = 0;
  for (WasmCode* code : codes) {
    code_size += code->instructions().size();
    freed_regions.Merge(base::AddressRegion{code->instruction_start(),
                                            code->instructions().size()});
    ThreadIsolation::UnregisterWasmAllocation(code->instruction_start(),
                                              code->instructions().size());
  }
  freed_code_size_.fetch_add(code_size);

  // Merge {freed_regions} into {freed_code_space_} and put all ranges of full
  // pages to decommit into {regions_to_decommit} (decommitting is expensive,
  // so try to merge regions before decommitting).
  DisjointAllocationPool regions_to_decommit;
  size_t commit_page_size = CommitPageSize();
  for (auto region : freed_regions.regions()) {
    auto merged_region = freed_code_space_.Merge(region);
    Address discard_start =
        std::max(RoundUp(merged_region.begin(), commit_page_size),
                 RoundDown(region.begin(), commit_page_size));
    Address discard_end =
        std::min(RoundDown(merged_region.end(), commit_page_size),
                 RoundUp(region.end(), commit_page_size));
    if (discard_start >= discard_end) continue;
    regions_to_decommit.Merge({discard_start, discard_end - discard_start});
  }

  auto* code_manager = GetWasmCodeManager();
  for (auto region : regions_to_decommit.regions()) {
    [[maybe_unused]] size_t old_committed =
        committed_code_space_.fetch_sub(region.size());
    DCHECK_GE(old_committed, region.size());
    for (base::AddressRegion split_range :
         SplitRangeByReservationsIfNeeded(region, owned_code_space_)) {
      code_manager->Decommit(split_range);
    }
  }
}

size_t WasmCodeAllocator::GetNumCodeSpaces() const {
  return owned_code_space_.size();
}

NativeModule::NativeModule(WasmEnabledFeatures enabled,
                           CompileTimeImports compile_imports,
                           DynamicTiering dynamic_tiering,
                           VirtualMemory code_space,
                           std::shared_ptr<const WasmModule> module,
                           std::shared_ptr<Counters> async_counters,
                           std::shared_ptr<NativeModule>* shared_this)
    : engine_scope_(
          GetWasmEngine()->GetBarrierForBackgroundCompile()->TryLock()),
      code_allocator_(async_counters),
      enabled_features_(enabled),
      compile_imports_(std::move(compile_imports)),
      module_(std::move(module)),
      fast_api_targets_(
          new std::atomic<Address>[module_->num_imported_functions]()),
      fast_api_signatures_(
          new std::atomic<
              const MachineSignature*>[module_->num_imported_functions]()) {
  DCHECK(engine_scope_);
  // We receive a pointer to an empty {std::shared_ptr}, and install ourselve
  // there.
  DCHECK_NOT_NULL(shared_this);
  DCHECK_NULL(*shared_this);
  shared_this->reset(this);
  compilation_state_ = CompilationState::New(
      *shared_this, std::move(async_counters), dynamic_tiering);
  compilation_state_->InitCompileJob();
  DCHECK_NOT_NULL(module_);
  if (module_->num_declared_functions > 0) {
    code_table_ =
        std::make_unique<WasmCode*[]>(module_->num_declared_functions);
    tiering_budgets_ = std::make_unique<std::atomic<uint32_t>[]>(
        module_->num_declared_functions);
    // The tiering budget is accessed directly from generated code.
    static_assert(sizeof(*tiering_budgets_.get()) == sizeof(uint32_t));

    std::fill_n(tiering_budgets_.get(), module_->num_declared_functions,
                v8_flags.wasm_tiering_budget);
  }

  if (v8_flags.wasm_jitless) return;

  // Even though there cannot be another thread using this object (since we
  // are just constructing it), we need to hold the mutex to fulfill the
  // precondition of {WasmCodeAllocator::Init}, which calls
  // {NativeModule::AddCodeSpaceLocked}.
  base::RecursiveMutexGuard guard{&allocation_mutex_};
  auto initial_region = code_space.region();
  code_allocator_.Init(std::move(code_space));
  AddCodeSpaceLocked(initial_region);
}

void NativeModule::ReserveCodeTableForTesting(uint32_t max_functions) {
  if (v8_flags.wasm_jitless) return;

  WasmCodeRefScope code_ref_scope;
  CHECK_LE(module_->num_declared_functions, max_functions);
  auto new_table = std::make_unique<WasmCode*[]>(max_functions);
  if (module_->num_declared_functions > 0) {
    memcpy(new_table.get(), code_table_.get(),
           module_->num_declared_functions * sizeof(WasmCode*));
  }
  code_table_ = std::move(new_table);

  base::RecursiveMutexGuard guard(&allocation_mutex_);
  CHECK_EQ(1, code_space_data_.size());
  base::AddressRegion single_code_space_region = code_space_data_[0].region;
  // Re-allocate the near and far jump tables.
  main_jump_table_ = CreateEmptyJumpTableInRegionLocked(
      JumpTableAssembler::SizeForNumberOfSlots(max_functions),
      single_code_space_region, JumpTableType::kJumpTable);
  CHECK(
      single_code_space_region.contains(main_jump_table_->instruction_start()));
  main_far_jump_table_ = CreateEmptyJumpTableInRegionLocked(
      JumpTableAssembler::SizeForNumberOfFarJumpSlots(
          BuiltinLookup::BuiltinCount(),
          NumWasmFunctionsInFarJumpTable(max_functions)),
      single_code_space_region, JumpTableType::kFarJumpTable);
  CHECK(single_code_space_region.contains(
      main_far_jump_table_->instruction_start()));
  code_space_data_[0].jump_table = main_jump_table_;
  InitializeJumpTableForLazyCompilation(max_functions);
}

void NativeModule::LogWasmCodes(Isolate* isolate, Tagged<Script> script) {
  DisallowGarbageCollection no_gc;
  if (!WasmCode::ShouldBeLogged(isolate)) return;

  TRACE_EVENT1("v8.wasm", "wasm.LogWasmCodes", "functions",
               module_->num_declared_functions);

  Tagged<Object> url_obj = script->name();
  DCHECK(IsString(url_obj) || IsUndefined(url_obj));
  std::unique_ptr<char[]> source_url =
      IsString(url_obj) ? Cast<String>(url_obj)->ToCString()
                        : std::unique_ptr<char[]>(new char[1]{'\0'});

  // Log all owned code, not just the current entries in the code table. This
  // will also include import wrappers.
  WasmCodeRefScope code_ref_scope;
  for (auto& code : SnapshotAllOwnedCode()) {
    code->LogCode(isolate, source_url.get(), script->id());
  }
}

WasmCode* NativeModule::AddCodeForTesting(DirectHandle<Code> code) {
  const size_t relocation_size = code->relocation_size();
  base::OwnedVector<uint8_t> reloc_info;
  if (relocation_size > 0) {
    reloc_info = base::OwnedVector<uint8_t>::Of(
        base::Vector<uint8_t>{code->relocation_start(), relocation_size});
  }
  DirectHandle<TrustedByteArray> source_pos_table(
      code->source_position_table(), code->instruction_stream()->GetIsolate());
  int source_pos_len = source_pos_table->length();
  auto source_pos = base::OwnedVector<uint8_t>::NewForOverwrite(source_pos_len);
  if (source_pos_len > 0) {
    MemCopy(source_pos.begin(), source_pos_table->begin(), source_pos_len);
  }

  static_assert(InstructionStream::kOnHeapBodyIsContiguous);
  base::Vector<const uint8_t> instructions(
      reinterpret_cast<uint8_t*>(code->body_start()),
      static_cast<size_t>(code->body_size()));
  const int stack_slots = code->stack_slots();

  // Metadata offsets in InstructionStream objects are relative to the start of
  // the metadata section, whereas WasmCode expects offsets relative to
  // instruction_start.
  const int base_offset = code->instruction_size();
  // TODO(jgruber,v8:8758): Remove this translation. It exists only because
  // InstructionStream objects contains real offsets but WasmCode expects an
  // offset of 0 to mean 'empty'.
  const int safepoint_table_offset =
      code->has_safepoint_table() ? base_offset + code->safepoint_table_offset()
                                  : 0;
  const int handler_table_offset = base_offset + code->handler_table_offset();
  const int constant_pool_offset = base_offset + code->constant_pool_offset();
  const int code_comments_offset = base_offset + code->code_comments_offset();

  base::RecursiveMutexGuard guard{&allocation_mutex_};
  base::Vector<uint8_t> dst_code_bytes =
      code_allocator_.AllocateForCode(this, instructions.size());
  {
    WritableJitAllocation jit_allocation =
        ThreadIsolation::RegisterJitAllocation(
            reinterpret_cast<Address>(dst_code_bytes.begin()),
            dst_code_bytes.size(),
            ThreadIsolation::JitAllocationType::kWasmCode);
    jit_allocation.CopyCode(0, instructions.begin(), instructions.size());

    // Apply the relocation delta by iterating over the RelocInfo.
    intptr_t delta = reinterpret_cast<Address>(dst_code_bytes.begin()) -
                     code->instruction_start();
    int mode_mask =
        RelocInfo::kApplyMask | RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL);
    auto jump_tables_ref =
        FindJumpTablesForRegionLocked(base::AddressRegionOf(dst_code_bytes));
    Address dst_code_addr = reinterpret_cast<Address>(dst_code_bytes.begin());
    Address constant_pool_start = dst_code_addr + constant_pool_offset;
    RelocIterator orig_it(*code, mode_mask);
    for (WritableRelocIterator it(jit_allocation, dst_code_bytes,
                                  reloc_info.as_vector(), constant_pool_start,
                                  mode_mask);
         !it.done(); it.next(), orig_it.next()) {
      RelocInfo::Mode mode = it.rinfo()->rmode();
      if (RelocInfo::IsWasmStubCall(mode)) {
        uint32_t stub_call_tag = orig_it.rinfo()->wasm_call_tag();
        DCHECK_LT(stub_call_tag,
                  static_cast<uint32_t>(Builtin::kFirstBytecodeHandler));
        Builtin builtin = static_cast<Builtin>(stub_call_tag);
        Address entry = GetJumpTableEntryForBuiltin(builtin, jump_tables_ref);
        it.rinfo()->set_wasm_stub_call_address(entry);
      } else {
        it.rinfo()->apply(delta);
      }
    }
  }

  // Flush the i-cache after relocation.
  FlushInstructionCache(dst_code_bytes.begin(), dst_code_bytes.size());

  std::unique_ptr<WasmCode> new_code{
      new WasmCode{this,                     // native_module
                   kAnonymousFuncIndex,      // index
                   dst_code_bytes,           // instructions
                   stack_slots,              // stack_slots
                   0,                        // ool_spills
                   0,                        // tagged_parameter_slots
                   safepoint_table_offset,   // safepoint_table_offset
                   handler_table_offset,     // handler_table_offset
                   constant_pool_offset,     // constant_pool_offset
                   code_comments_offset,     // code_comments_offset
                   instructions.length(),    // unpadded_binary_size
                   {},                       // protected_instructions
                   reloc_info.as_vector(),   // reloc_info
                   source_pos.as_vector(),   // source positions
                   {},                       // inlining positions
                   {},                       // deopt data
                   WasmCode::kWasmFunction,  // kind
                   ExecutionTier::kNone,     // tier
                   kNotForDebugging}};       // for_debugging
  new_code->MaybePrint();
  new_code->Validate();

  return PublishCodeLocked(std::move(new_code));
}

void NativeModule::InitializeJumpTableForLazyCompilation(
    uint32_t num_wasm_functions) {
  if (!num_wasm_functions) return;
  allocation_mutex_.AssertHeld();

  DCHECK_NULL(lazy_compile_table_);
  lazy_compile_table_ = CreateEmptyJumpTableLocked(
      JumpTableAssembler::SizeForNumberOfLazyFunctions(num_wasm_functions),
      JumpTableType::kLazyCompileTable);

  CHECK_EQ(1, code_space_data_.size());
  const CodeSpaceData& code_space_data = code_space_data_[0];
  DCHECK_NOT_NULL(code_space_data.jump_table);
  DCHECK_NOT_NULL(code_space_data.far_jump_table);

  Address compile_lazy_address =
      code_space_data.far_jump_table->instruction_start() +
      JumpTableAssembler::FarJumpSlotIndexToOffset(
          BuiltinLookup::JumptableIndexForBuiltin(Builtin::kWasmCompileLazy));

  JumpTableAssembler::GenerateLazyCompileTable(
      lazy_compile_table_->instruction_start(), num_wasm_functions,
      module_->num_imported_functions, compile_lazy_address);

  JumpTableAssembler::InitializeJumpsToLazyCompileTable(
      code_space_data.jump_table->instruction_start(), num_wasm_functions,
      lazy_compile_table_->instruction_start());
}

void NativeModule::UseLazyStubLocked(uint32_t func_index) {
  allocation_mutex_.AssertHeld();
  DCHECK_LE(module_->num_imported_functions, func_index);
  DCHECK_LT(func_index,
            module_->num_imported_functions + module_->num_declared_functions);
  // Avoid opening a new write scope per function. The caller should hold the
  // scope instead.

  DCHECK_NOT_NULL(lazy_compile_table_);

  // Add jump table entry for jump to the lazy compile stub.
  uint32_t slot_index = declared_function_index(module(), func_index);
  DCHECK_NULL(code_table_[slot_index]);
  Address lazy_compile_target =
      lazy_compile_table_->instruction_start() +
      JumpTableAssembler::LazyCompileSlotIndexToOffset(slot_index);
  PatchJumpTablesLocked(slot_index, lazy_compile_target);
}

std::unique_ptr<WasmCode> NativeModule::AddCode(
    int index, const CodeDesc& desc, int stack_slots, int ool_spill_count,
    uint32_t tagged_parameter_slots,
    base::Vector<const uint8_t> protected_instructions_data,
    base::Vector<const uint8_t> source_position_table,
    base::Vector<const uint8_t> inlining_positions,
    base::Vector<const uint8_t> deopt_data, WasmCode::Kind kind,
    ExecutionTier tier, ForDebugging for_debugging) {
  base::Vector<uint8_t> code_space;
  NativeModule::JumpTablesRef jump_table_ref;
  {
    base::RecursiveMutexGuard guard{&allocation_mutex_};
    code_space = code_allocator_.AllocateForCode(this, desc.instr_size);
    jump_table_ref =
        FindJumpTablesForRegionLocked(base::AddressRegionOf(code_space));
  }
  // Only Liftoff code can have the {frame_has_feedback_slot} bit set.
  DCHECK_NE(tier, ExecutionTier::kLiftoff);
  bool frame_has_feedback_slot = false;
  ThreadIsolation::RegisterJitAllocation(
      reinterpret_cast<Address>(code_space.begin()), code_space.size(),
      ThreadIsolation::JitAllocationType::kWasmCode);
  return AddCodeWithCodeSpace(
      index, desc, stack_slots, ool_spill_count, tagged_parameter_slots,
      protected_instructions_data, source_position_table, inlining_positions,
      deopt_data, kind, tier, for_debugging, frame_has_feedback_slot,
      code_space, jump_table_ref);
}

std::unique_ptr<WasmCode> NativeModule::AddCodeWithCodeSpace(
    int index, const CodeDesc& desc, int stack_slots, int ool_spill_count,
    uint32_t tagged_parameter_slots,
    base::Vector<const uint8_t> protected_instructions_data,
    base::Vector<const uint8_t> source_position_table,
    base::Vector<const uint8_t> inlining_positions,
    base::Vector<const uint8_t> deopt_data, WasmCode::Kind kind,
    ExecutionTier tier, ForDebugging for_debugging,
    bool frame_has_feedback_slot, base::Vector<uint8_t> dst_code_bytes,
    const JumpTablesRef& jump_tables) {
  base::Vector<uint8_t> reloc_info{
      desc.buffer + desc.buffer_size - desc.reloc_size,
      static_cast<size_t>(desc.reloc_size)};
  UpdateCodeSize(desc.instr_size, tier, for_debugging);

  // TODO(jgruber,v8:8758): Remove this translation. It exists only because
  // CodeDesc contains real offsets but WasmCode expects an offset of 0 to mean
  // 'empty'.
  const int safepoint_table_offset =
      desc.safepoint_table_size == 0 ? 0 : desc.safepoint_table_offset;
  const int handler_table_offset = desc.handler_table_offset;
  const int constant_pool_offset = desc.constant_pool_offset;
  const int code_comments_offset = desc.code_comments_offset;
  const int instr_size = desc.instr_size;

  {
    WritableJitAllocation jit_allocation = ThreadIsolation::LookupJitAllocation(
        reinterpret_cast<Address>(dst_code_bytes.begin()),
        dst_code_bytes.size(), ThreadIsolation::JitAllocationType::kWasmCode);
    jit_allocation.CopyCode(0, desc.buffer, desc.instr_size);

    // Apply the relocation delta by iterating over the RelocInfo.
    intptr_t delta = dst_code_bytes.begin() - desc.buffer;
    int mode_mask = RelocInfo::kApplyMask |
                    RelocInfo::ModeMask(RelocInfo::WASM_CALL) |
                    RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL);
    Address code_start = reinterpret_cast<Address>(dst_code_bytes.begin());
    Address constant_pool_start = code_start + constant_pool_offset;

    for (WritableRelocIterator it(jit_allocation, dst_code_bytes, reloc_info,
                                  constant_pool_start, mode_mask);
         !it.done(); it.next()) {
      RelocInfo::Mode mode = it.rinfo()->rmode();
      if (RelocInfo::IsWasmCall(mode)) {
        uint32_t call_tag = it.rinfo()->wasm_call_tag();
        Address target = GetNearCallTargetForFunction(call_tag, jump_tables);
        it.rinfo()->set_wasm_call_address(target);
      } else if (RelocInfo::IsWasmStubCall(mode)) {
        uint32_t stub_call_tag = it.rinfo()->wasm_call_tag();
        DCHECK_LT(stub_call_tag,
                  static_cast<uint32_t>(Builtin::kFirstBytecodeHandler));
        Builtin builtin = static_cast<Builtin>(stub_call_tag);
        Address entry = GetJumpTableEntryForBuiltin(builtin, jump_tables);
        it.rinfo()->set_wasm_stub_call_address(entry);
      } else {
        it.rinfo()->apply(delta);
      }
    }
  }

  // Flush the i-cache after relocation.
  FlushInstructionCache(dst_code_bytes.begin(), dst_code_bytes.size());

  // Liftoff code will not be relocated or serialized, thus do not store any
  // relocation information.
  if (tier == ExecutionTier::kLiftoff) reloc_info = {};

  std::unique_ptr<WasmCode> code{new WasmCode{this,
                                              index,
                                              dst_code_bytes,
                                              stack_slots,
                                              ool_spill_count,
                                              tagged_parameter_slots,
                                              safepoint_table_offset,
                                              handler_table_offset,
                                              constant_pool_offset,
                                              code_comments_offset,
                                              instr_size,
                                              protected_instructions_data,
                                              reloc_info,
                                              source_position_table,
                                              inlining_positions,
                                              deopt_data,
                                              kind,
                                              tier,
                                              for_debugging,
                                              frame_has_feedback_slot}};

  code->MaybePrint();
  code->Validate();

  return code;
}

WasmCode* NativeModule::PublishCode(std::unique_ptr<WasmCode> code,
                                    AssumptionsJournal* assumptions) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.PublishCode");
  base::RecursiveMutexGuard lock(&allocation_mutex_);
  if (assumptions != nullptr) {
    // Acquiring the lock is expensive, so callers should only pass non-empty
    // assumptions journals.
    DCHECK(!assumptions->empty());
    // Only Turbofan makes assumptions.
    DCHECK_EQ(ExecutionTier::kTurbofan, code->tier());
    WellKnownImportsList& current = module_->type_feedback.well_known_imports;
    base::MutexGuard wki_lock(current.mutex());
    for (auto [import_index, status] : assumptions->import_statuses()) {
      if (current.get(import_index) != status) {
        compilation_state_->AllowAnotherTopTierJob(code->index());
        return nullptr;
      }
    }
  }
  return PublishCodeLocked(std::move(code));
}

std::vector<WasmCode*> NativeModule::PublishCode(
    base::Vector<std::unique_ptr<WasmCode>> codes) {
  TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.PublishCode", "number", codes.size());
  std::vector<WasmCode*> published_code;
  published_code.reserve(codes.size());
  base::RecursiveMutexGuard lock(&allocation_mutex_);
  // The published code is put into the top-most surrounding {WasmCodeRefScope}.
  for (auto& code : codes) {
    published_code.push_back(PublishCodeLocked(std::move(code)));
  }
  return published_code;
}

WasmCode::Kind GetCodeKind(const WasmCompilationResult& result) {
  switch (result.kind) {
    case WasmCompilationResult::kWasmToJsWrapper:
      return WasmCode::Kind::kWasmToJsWrapper;
#if V8_ENABLE_DRUMBRAKE
    case WasmCompilationResult::kInterpreterEntry:
      return WasmCode::Kind::kInterpreterEntry;
#endif  // V8_ENABLE_DRUMBRAKE
    case WasmCompilationResult::kFunction:
      return WasmCode::Kind::kWasmFunction;
    default:
      UNREACHABLE();
  }
}

WasmCode* NativeModule::PublishCodeLocked(
    std::unique_ptr<WasmCode> owned_code) {
  allocation_mutex_.AssertHeld();

  WasmCode* code = owned_code.get();
  new_owned_code_.emplace_back(std::move(owned_code));

  // Add the code to the surrounding code ref scope, so the returned pointer is
  // guaranteed to be valid.
  WasmCodeRefScope::AddRef(code);

  if (code->index() < static_cast<int>(module_->num_imported_functions)) {
    return code;
  }

  DCHECK_LT(code->index(), num_functions());

  code->RegisterTrapHandlerData();

  // Assume an order of execution tiers that represents the quality of their
  // generated code.
  static_assert(ExecutionTier::kNone < ExecutionTier::kLiftoff &&
                    ExecutionTier::kLiftoff < ExecutionTier::kTurbofan,
                "Assume an order on execution tiers");

  uint32_t slot_idx = declared_function_index(module(), code->index());
  WasmCode* prior_code = code_table_[slot_idx];
  // If we are tiered down, install all debugging code (except for stepping
  // code, which is only used for a single frame and never installed in the
  // code table of jump table). Otherwise, install code if it was compiled
  // with a higher tier.
  static_assert(
      kForDebugging > kNotForDebugging && kWithBreakpoints > kForDebugging,
      "for_debugging is ordered");

  if (should_update_code_table(code, prior_code)) {
    code_table_[slot_idx] = code;
    if (prior_code) {
      WasmCodeRefScope::AddRef(prior_code);
      // The code is added to the current {WasmCodeRefScope}, hence the ref
      // count cannot drop to zero here.
      prior_code->DecRefOnLiveCode();
    }

    PatchJumpTablesLocked(slot_idx, code->instruction_start());
  } else {
    // The code tables does not hold a reference to the code, hence decrement
    // the initial ref count of 1. The code was added to the
    // {WasmCodeRefScope} though, so it cannot die here.
    code->DecRefOnLiveCode();
  }

  return code;
}

bool NativeModule::should_update_code_table(WasmCode* new_code,
                                            WasmCode* prior_code) const {
  if (new_code->for_debugging() == kForStepping) {
    // Never install stepping code.
    return false;
  }
  if (debug_state_ == kDebugging) {
    if (new_code->for_debugging() == kNotForDebugging) {
      // In debug state, only install debug code.
      return false;
    }
    if (prior_code && prior_code->for_debugging() > new_code->for_debugging()) {
      // In debug state, install breakpoints over normal debug code.
      return false;
    }
  }
  // In kNoDebugging:
  // Install if the tier is higher than before or we replace debugging code with
  // non-debugging code.
  // Also allow installing a lower tier if deopt support is enabled.
  if (prior_code && !prior_code->for_debugging() &&
      prior_code->tier() > new_code->tier() && !v8_flags.wasm_deopt) {
    return false;
  }
  return true;
}

void NativeModule::ReinstallDebugCode(WasmCode* code) {
  base::RecursiveMutexGuard lock(&allocation_mutex_);

  DCHECK_EQ(this, code->native_module());
  DCHECK_EQ(kWithBreakpoints, code->for_debugging());
  DCHECK(!code->IsAnonymous());
  DCHECK_LE(module_->num_imported_functions, code->index());
  DCHECK_LT(code->index(), num_functions());

  // If the module is tiered up by now, do not reinstall debug code.
  if (debug_state_ != kDebugging) return;

  uint32_t slot_idx = declared_function_index(module(), code->index());
  if (WasmCode* prior_code = code_table_[slot_idx]) {
    WasmCodeRefScope::AddRef(prior_code);
    // The code is added to the current {WasmCodeRefScope}, hence the ref
    // count cannot drop to zero here.
    prior_code->DecRefOnLiveCode();
  }
  code_table_[slot_idx] = code;
  code->IncRef();

  PatchJumpTablesLocked(slot_idx, code->instruction_start());
}

std::pair<base::Vector<uint8_t>, NativeModule::JumpTablesRef>
NativeModule::AllocateForDeserializedCode(size_t total_code_size) {
  base::RecursiveMutexGuard guard{&allocation_mutex_};
  base::Vector<uint8_t> code_space =
      code_allocator_.AllocateForCode(this, total_code_size);
  auto jump_tables =
      FindJumpTablesForRegionLocked(base::AddressRegionOf(code_space));
  return {code_space, jump_tables};
}

std::unique_ptr<WasmCode> NativeModule::AddDeserializedCode(
    int index, base::Vector<uint8_t> instructions, int stack_slots,
    int ool_spills, uint32_t tagged_parameter_slots, int safepoint_table_offset,
    int handler_table_offset, int constant_pool_offset,
    int code_comments_offset, int unpadded_binary_size,
    base::Vector<const uint8_t> protected_instructions_data,
    base::Vector<const uint8_t> reloc_info,
    base::Vector<const uint8_t> source_position_table,
    base::Vector<const uint8_t> inlining_positions,
    base::Vector<const uint8_t> deopt_data, WasmCode::Kind kind,
    ExecutionTier tier) {
  UpdateCodeSize(instructions.size(), tier, kNotForDebugging);

  return std::unique_ptr<WasmCode>{new WasmCode{
      this, index, instructions, stack_slots, ool_spills,
      tagged_parameter_slots, safepoint_table_offset, handler_table_offset,
      constant_pool_offset, code_comments_offset, unpadded_binary_size,
      protected_instructions_data, reloc_info, source_position_table,
      inlining_positions, deopt_data, kind, tier, kNotForDebugging}};
}

std::pair<std::vector<WasmCode*>, std::vector<WellKnownImport>>
NativeModule::SnapshotCodeTable() const {
  base::RecursiveMutexGuard lock(&allocation_mutex_);
  WasmCode** start = code_table_.get();
  WasmCode** end = start + module_->num_declared_functions;
  for (WasmCode* code : base::VectorOf(start, end - start)) {
    if (code) WasmCodeRefScope::AddRef(code);
  }
  std::vector<WellKnownImport> import_statuses(module_->num_imported_functions);
  for (uint32_t i = 0; i < module_->num_imported_functions; i++) {
    import_statuses[i] = module_->type_feedback.well_known_imports.get(i);
  }
  return {std::vector<WasmCode*>{start, end}, std::move(import_statuses)};
}

std::vector<WasmCode*> NativeModule::SnapshotAllOwnedCode() const {
  base::RecursiveMutexGuard lock(&allocation_mutex_);
  if (!new_owned_code_.empty()) TransferNewOwnedCodeLocked();

  std::vector<WasmCode*> all_code(owned_code_.size());
  std::transform(owned_code_.begin(), owned_code_.end(), all_code.begin(),
                 [](auto& entry) { return entry.second.get(); });
  std::for_each(all_code.begin(), all_code.end(), WasmCodeRefScope::AddRef);
  return all_code;
}

WasmCode* NativeModule::GetCode(uint32_t index) const {
  base::RecursiveMutexGuard guard(&allocation_mutex_);
  WasmCode* code = code_table_[declared_function_index(module(), index)];
  if (code) WasmCodeRefScope::AddRef(code);
  return code;
}

bool NativeModule::HasCode(uint32_t index) const {
  base::RecursiveMutexGuard guard(&allocation_mutex_);
  return code_table_[declared_function_index(module(), index)] != nullptr;
}

bool NativeModule::HasCodeWithTier(uint32_t index, ExecutionTier tier) const {
  base::RecursiveMutexGuard guard(&allocation_mutex_);
  return code_table_[declared_function_index(module(), index)] != nullptr &&
         code_table_[declared_function_index(module(), index)]->tier() == tier;
}

void NativeModule::SetWasmSourceMap(
    std::unique_ptr<WasmModuleSourceMap> source_map) {
  source_map_ = std::move(source_map);
}

WasmModuleSourceMap* NativeModule::GetWasmSourceMap() const {
  return source_map_.get();
}

WasmCode* NativeModule::CreateEmptyJumpTableLocked(int jump_table_size,
                                                   JumpTableType type) {
  return CreateEmptyJumpTableInRegionLocked(jump_table_size,
                                            kUnrestrictedRegion, type);
}

namespace {

ThreadIsolation::JitAllocationType ToAllocationType(
    v8::internal::wasm::NativeModule::JumpTableType type) {
  switch (type) {
    case NativeModule::JumpTableType::kJumpTable:
      return ThreadIsolation::JitAllocationType::kWasmJumpTable;
    case NativeModule::JumpTableType::kFarJumpTable:
      return ThreadIsolation::JitAllocationType::kWasmFarJumpTable;
    case NativeModule::JumpTableType::kLazyCompileTable:
      return ThreadIsolation::JitAllocationType::kWasmLazyCompileTable;
  }
}

}  // namespace

WasmCode* NativeModule::CreateEmptyJumpTableInRegionLocked(
    int jump_table_size, base::AddressRegion region, JumpTableType type) {
  allocation_mutex_.AssertHeld();
  // Only call this if we really need a jump table.
  DCHECK_LT(0, jump_table_size);
  base::Vector<uint8_t> code_space =
      code_allocator_.AllocateForCodeInRegion(this, jump_table_size, region);
  DCHECK(!code_space.empty());
  UpdateCodeSize(jump_table_size, ExecutionTier::kNone, kNotForDebugging);
  {
    WritableJitAllocation jit_allocation =
        ThreadIsolation::RegisterJitAllocation(
            reinterpret_cast<Address>(code_space.begin()), code_space.size(),
            ToAllocationType(type));
    jit_allocation.ClearBytes(0, code_space.size());
  }
  std::unique_ptr<WasmCode> code{
      new WasmCode{this,                  // native_module
                   kAnonymousFuncIndex,   // index
                   code_space,            // instructions
                   0,                     // stack_slots
                   0,                     // ool_spills
                   0,                     // tagged_parameter_slots
                   0,                     // safepoint_table_offset
                   jump_table_size,       // handler_table_offset
                   jump_table_size,       // constant_pool_offset
                   jump_table_size,       // code_comments_offset
                   jump_table_size,       // unpadded_binary_size
                   {},                    // protected_instructions
                   {},                    // reloc_info
                   {},                    // source_pos
                   {},                    // inlining pos
                   {},                    // deopt data
                   WasmCode::kJumpTable,  // kind
                   ExecutionTier::kNone,  // tier
                   kNotForDebugging}};    // for_debugging
  return PublishCodeLocked(std::move(code));
}

void NativeModule::UpdateCodeSize(size_t size, ExecutionTier tier,
                                  ForDebugging for_debugging) {
  if (for_debugging != kNotForDebugging) return;
  // Count jump tables (ExecutionTier::kNone) for both Liftoff and TurboFan as
  // this is shared code.
  if (tier != ExecutionTier::kTurbofan) liftoff_code_size_.fetch_add(size);
  if (tier != ExecutionTier::kLiftoff) turbofan_code_size_.fetch_add(size);
}

void NativeModule::PatchJumpTablesLocked(uint32_t slot_index, Address target) {
  allocation_mutex_.AssertHeld();

  for (auto& code_space_data : code_space_data_) {
    // TODO(sroettger): need to unlock both jump tables together
    DCHECK_IMPLIES(code_space_data.jump_table, code_space_data.far_jump_table);
    if (!code_space_data.jump_table) continue;
    WritableJumpTablePair writable_jump_tables =
        ThreadIsolation::LookupJumpTableAllocations(
            code_space_data.jump_table->instruction_start(),
            code_space_data.jump_table->instructions_size_,
            code_space_data.far_jump_table->instruction_start(),
            code_space_data.far_jump_table->instructions_size_);
    PatchJumpTableLocked(code_space_data, slot_index, target);
  }
}

void NativeModule::PatchJumpTableLocked(const CodeSpaceData& code_space_data,
                                        uint32_t slot_index, Address target) {
  allocation_mutex_.AssertHeld();

  DCHECK_NOT_NULL(code_space_data.jump_table);
  DCHECK_NOT_NULL(code_space_data.far_jump_table);

  DCHECK_LT(slot_index, module_->num_declared_functions);
  Address jump_table_slot =
      code_space_data.jump_table->instruction_start() +
      JumpTableAssembler::JumpSlotIndexToOffset(slot_index);
  uint32_t far_jump_table_offset = JumpTableAssembler::FarJumpSlotIndexToOffset(
      BuiltinLookup::BuiltinCount() + slot_index);
  // Only pass the far jump table start if the far jump table actually has a
  // slot for this function index (i.e. does not only contain runtime stubs).
  bool has_far_jump_slot =
      far_jump_table_offset <
      code_space_data.far_jump_table->instructions().size();
  Address far_jump_table_start =
      code_space_data.far_jump_table->instruction_start();
  Address far_jump_table_slot =
      has_far_jump_slot ? far_jump_table_start + far_jump_table_offset
                        : kNullAddress;
  JumpTableAssembler::PatchJumpTableSlot(jump_table_slot, far_jump_table_slot,
                                         target);
}

void NativeModule::AddCodeSpaceLocked(base::AddressRegion region) {
  allocation_mutex_.AssertHeld();

  // Each code space must be at least twice as large as the overhead per code
  // space. Otherwise, we are wasting too much memory.
  DCHECK_GE(region.size(),
            2 * OverheadPerCodeSpace(module()->num_declared_functions));

#if defined(V8_OS_WIN64)
  // On some platforms, specifically Win64, we need to reserve some pages at
  // the beginning of an executable space.
  // See src/heap/spaces.cc, MemoryAllocator::InitializeCodePageAllocator() and
  // https://cs.chromium.org/chromium/src/components/crash/content/app/crashpad_win.cc?rcl=fd680447881449fba2edcf0589320e7253719212&l=204
  // for details.
  if (WasmCodeManager::CanRegisterUnwindInfoForNonABICompliantCodeRange()) {
    size_t size = Heap::GetCodeRangeReservedAreaSize();
    DCHECK_LT(0, size);
    base::Vector<uint8_t> padding =
        code_allocator_.AllocateForCodeInRegion(this, size, region);
    CHECK_EQ(reinterpret_cast<Address>(padding.begin()), region.begin());
    win64_unwindinfo::RegisterNonABICompliantCodeRange(
        reinterpret_cast<void*>(region.begin()), region.size());
  }
#endif  // V8_OS_WIN64

  WasmCodeRefScope code_ref_scope;
  WasmCode* jump_table = nullptr;
  WasmCode* far_jump_table = nullptr;
  const uint32_t num_wasm_functions = module_->num_declared_functions;
  const bool is_first_code_space = code_space_data_.empty();
  // We always need a far jump table, because it contains the runtime stubs.
  const bool needs_far_jump_table =
      !FindJumpTablesForRegionLocked(region).is_valid();
  const bool needs_jump_table = num_wasm_functions > 0 && needs_far_jump_table;

  if (needs_jump_table) {
    // Allocate additional jump tables just as big as the first one.
    // This is in particular needed in cctests which add functions to the module
    // after the jump tables are already created (see https://crbug.com/v8/14213
    // and {NativeModule::ReserveCodeTableForTesting}.
    int jump_table_size =
        is_first_code_space
            ? JumpTableAssembler::SizeForNumberOfSlots(num_wasm_functions)
            : main_jump_table_->instructions_size_;
    jump_table = CreateEmptyJumpTableInRegionLocked(jump_table_size, region,
                                                    JumpTableType::kJumpTable);
    CHECK(region.contains(jump_table->instruction_start()));
  }

  if (needs_far_jump_table) {
    int num_function_slots = NumWasmFunctionsInFarJumpTable(num_wasm_functions);
    // See comment above for the size computation.
    int far_jump_table_size =
        is_first_code_space
            ? JumpTableAssembler::SizeForNumberOfFarJumpSlots(
                  BuiltinLookup::BuiltinCount(), num_function_slots)
            : main_far_jump_table_->instructions_size_;
    far_jump_table = CreateEmptyJumpTableInRegionLocked(
        far_jump_table_size, region, JumpTableType::kFarJumpTable);
    CHECK(region.contains(far_jump_table->instruction_start()));
    EmbeddedData embedded_data = EmbeddedData::FromBlob();
    static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
    Address builtin_addresses[BuiltinLookup::BuiltinCount()];
    for (int i = 0; i < BuiltinLookup::BuiltinCount(); ++i) {
      builtin_addresses[i] = embedded_data.InstructionStartOf(
          BuiltinLookup::BuiltinForJumptableIndex(i));
    }
    WritableJitAllocation jit_allocation = ThreadIsolation::LookupJitAllocation(
        far_jump_table->instruction_start(), far_jump_table->instructions_size_,
        ThreadIsolation::JitAllocationType::kWasmFarJumpTable);
    JumpTableAssembler::GenerateFarJumpTable(
        far_jump_table->instruction_start(), builtin_addresses,
        BuiltinLookup::BuiltinCount(), num_function_slots);
  }

  if (is_first_code_space) {
    // This can be updated and accessed without locks, since the addition of the
    // first code space happens during initialization of the {NativeModule},
    // where no concurrent accesses are possible.
    main_jump_table_ = jump_table;
    main_far_jump_table_ = far_jump_table;
  }

  code_space_data_.push_back(CodeSpaceData{region, jump_table, far_jump_table});

  if (is_first_code_space) {
    InitializeJumpTableForLazyCompilation(num_wasm_functions);
  }

  if (jump_table && !is_first_code_space) {
    // Patch the new jump table(s) with existing functions. If this is the first
    // code space, there cannot be any functions that have been compiled yet.
    const CodeSpaceData& new_code_space_data = code_space_data_.back();
    // TODO(sroettger): need to create two write scopes? Or have a write scope
    // for multiple allocations.
    WritableJumpTablePair writable_jump_tables =
        ThreadIsolation::LookupJumpTableAllocations(
            new_code_space_data.jump_table->instruction_start(),

            new_code_space_data.jump_table->instructions_size_,
            new_code_space_data.far_jump_table->instruction_start(),

            new_code_space_data.far_jump_table->instructions_size_);
    for (uint32_t slot_index = 0; slot_index < num_wasm_functions;
         ++slot_index) {
      if (code_table_[slot_index]) {
        PatchJumpTableLocked(new_code_space_data, slot_index,
                             code_table_[slot_index]->instruction_start());
      } else if (lazy_compile_table_) {
        Address lazy_compile_target =
            lazy_compile_table_->instruction_start() +
            JumpTableAssembler::LazyCompileSlotIndexToOffset(slot_index);
        PatchJumpTableLocked(new_code_space_data, slot_index,
                             lazy_compile_target);
      }
    }
  }
}

namespace {
class NativeModuleWireBytesStorage final : public WireBytesStorage {
 public:
  explicit NativeModuleWireBytesStorage(
      std::shared_ptr<base::OwnedVector<const uint8_t>> wire_bytes)
      : wire_bytes_(std::move(wire_bytes)) {}

  base::Vector<const uint8_t> GetCode(WireBytesRef ref) const final {
    return std::atomic_load(&wire_bytes_)
        ->as_vector()
        .SubVector(ref.offset(), ref.end_offset());
  }

  std::optional<ModuleWireBytes> GetModuleBytes() const final {
    return std::optional<ModuleWireBytes>(
        std::atomic_load(&wire_bytes_)->as_vector());
  }

 private:
  const std::shared_ptr<base::OwnedVector<const uint8_t>> wire_bytes_;
};
}  // namespace

void NativeModule::SetWireBytes(base::OwnedVector<const uint8_t> wire_bytes) {
  auto shared_wire_bytes =
      std::make_shared<base::OwnedVector<const uint8_t>>(std::move(wire_bytes));
  std::atomic_store(&wire_bytes_, shared_wire_bytes);
  if (!shared_wire_bytes->empty()) {
    compilation_state_->SetWireBytesStorage(
        std::make_shared<NativeModuleWireBytesStorage>(
            std::move(shared_wire_bytes)));
  }
}

void NativeModule::AddLazyCompilationTimeSample(int64_t sample_in_micro_sec) {
  num_lazy_compilations_.fetch_add(1, std::memory_order_relaxed);
  sum_lazy_compilation_time_in_micro_sec_.fetch_add(sample_in_micro_sec,
                                                    std::memory_order_relaxed);
  int64_t max =
      max_lazy_compilation_time_in_micro_sec_.load(std::memory_order_relaxed);
  while (sample_in_micro_sec > max &&
         !max_lazy_compilation_time_in_micro_sec_.compare_exchange_weak(
             max, sample_in_micro_sec, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
    // Repeat until we set the new maximum sucessfully.
  }
}

void NativeModule::TransferNewOwnedCodeLocked() const {
  allocation_mutex_.AssertHeld();
  DCHECK(!new_owned_code_.empty());
  // Sort the {new_owned_code_} vector reversed, such that the position of the
  // previously inserted element can be used as a hint for the next element. If
  // elements in {new_owned_code_} are adjacent, this will guarantee
  // constant-time insertion into the map.
  std::sort(new_owned_code_.begin(), new_owned_code_.end(),
            [](const std::unique_ptr<WasmCode>& a,
               const std::unique_ptr<WasmCode>& b) {
              return a->instruction_start() > b->instruction_start();
            });
  auto insertion_hint = owned_code_.end();
  for (auto& code : new_owned_code_) {
    DCHECK_EQ(0, owned_code_.count(code->instruction_start()));
    // Check plausibility of the insertion hint.
    DCHECK(insertion_hint == owned_code_.end() ||
           insertion_hint->first > code->instruction_start());
    insertion_hint = owned_code_.emplace_hint(
        insertion_hint, code->instruction_start(), std::move(code));
  }
  new_owned_code_.clear();
}

WasmCode* NativeModule::Lookup(Address pc) const {
  base::RecursiveMutexGuard lock(&allocation_mutex_);
  if (!new_owned_code_.empty()) TransferNewOwnedCodeLocked();
  auto iter = owned_code_.upper_bound(pc);
  if (iter == owned_code_.begin()) return nullptr;
  --iter;
  WasmCode* candidate = iter->second.get();
  DCHECK_EQ(candidate->instruction_start(), iter->first);
  if (!candidate->contains(pc)) return nullptr;
  WasmCodeRefScope::AddRef(candidate);
  return candidate;
}

NativeModule::JumpTablesRef NativeModule::FindJumpTablesForRegionLocked(
    base::AddressRegion code_region) const {
  allocation_mutex_.AssertHeld();
  auto jump_table_usable = [code_region](const WasmCode* jump_table) {
    // We only ever need to check for suitable jump tables if
    // {kNeedsFarJumpsBetweenCodeSpaces} is true.
    if constexpr (!kNeedsFarJumpsBetweenCodeSpaces) UNREACHABLE();
    Address table_start = jump_table->instruction_start();
    Address table_end = table_start + jump_table->instructions().size();
    // Compute the maximum distance from anywhere in the code region to anywhere
    // in the jump table, avoiding any underflow.
    size_t max_distance = std::max(
        code_region.end() > table_start ? code_region.end() - table_start : 0,
        table_end > code_region.begin() ? table_end - code_region.begin() : 0);
    // kDefaultMaxWasmCodeSpaceSizeMb is <= the maximum near call distance on
    // the current platform.
    // We can allow a max_distance that is equal to
    // kDefaultMaxWasmCodeSpaceSizeMb, because every call or jump will target an
    // address *within* the region, but never exactly the end of the region. So
    // all occuring offsets are actually smaller than max_distance.
    return max_distance <= kDefaultMaxWasmCodeSpaceSizeMb * MB;
  };

  for (auto& code_space_data : code_space_data_) {
    DCHECK_IMPLIES(code_space_data.jump_table, code_space_data.far_jump_table);
    if (!code_space_data.far_jump_table) continue;
    // Only return these jump tables if they are reachable from the whole
    // {code_region}.
    if (kNeedsFarJumpsBetweenCodeSpaces &&
        (!jump_table_usable(code_space_data.far_jump_table) ||
         (code_space_data.jump_table &&
          !jump_table_usable(code_space_data.jump_table)))) {
      continue;
    }
    return {code_space_data.jump_table
                ? code_space_data.jump_table->instruction_start()
                : kNullAddress,
            code_space_data.far_jump_table->instruction_start()};
  }
  return {};
}

Address NativeModule::GetNearCallTargetForFunction(
    uint32_t func_index, const JumpTablesRef& jump_tables) const {
  DCHECK(jump_tables.is_valid());
  uint32_t slot_offset = JumpTableOffset(module(), func_index);
  return jump_tables.jump_table_start + slot_offset;
}

Address NativeModule::GetJumpTableEntryForBuiltin(
    Builtin builtin, const JumpTablesRef& jump_tables) const {
  DCHECK(jump_tables.is_valid());
  int index = BuiltinLookup::JumptableIndexForBuiltin(builtin);

  auto offset = JumpTableAssembler::FarJumpSlotIndexToOffset(index);
  return jump_tables.far_jump_table_start + offset;
}

uint32_t NativeModule::GetFunctionIndexFromJumpTableSlot(
    Address slot_address) const {
  WasmCodeRefScope code_refs;
  WasmCode* code = Lookup(slot_address);
  DCHECK_NOT_NULL(code);
  DCHECK_EQ(WasmCode::kJumpTable, code->kind());
  uint32_t slot_offset =
      static_cast<uint32_t>(slot_address - code->instruction_start());
  uint32_t slot_idx = JumpTableAssembler::SlotOffsetToIndex(slot_offset);
  DCHECK_LT(slot_idx, module_->num_declared_functions);
  DCHECK_EQ(slot_address,
            code->instruction_start() +
                JumpTableAssembler::JumpSlotIndexToOffset(slot_idx));
  return module_->num_imported_functions + slot_idx;
}

Builtin NativeModule::GetBuiltinInJumptableSlot(Address target) const {
  base::RecursiveMutexGuard guard(&allocation_mutex_);

  for (auto& code_space_data : code_space_data_) {
    if (code_space_data.far_jump_table != nullptr &&
        code_space_data.far_jump_table->contains(target)) {
      uint32_t offset = static_cast<uint32_t>(
          target - code_space_data.far_jump_table->instruction_start());
      uint32_t index = JumpTableAssembler::FarJumpSlotOffsetToIndex(offset);
      if (index >= BuiltinLookup::BuiltinCount()) continue;
      if (JumpTableAssembler::FarJumpSlotIndexToOffset(index) != offset) {
        continue;
      }
      return BuiltinLookup::BuiltinForJumptableIndex(index);
    }
  }

  // Invalid address.
  return Builtin::kNoBuiltinId;
}

NativeModule::~NativeModule() {
  TRACE_HEAP("Deleting native module: %p\n", this);
  // Cancel all background compilation before resetting any field of the
  // NativeModule or freeing anything.
  compilation_state_->CancelCompilation();

  // Clear the import wrapper cache before releasing the {WasmCode} objects in
  // {owned_code_}. The {WasmImportWrapperCache} still needs to decrement
  // reference counts on the {WasmCode} objects.
  import_wrapper_cache_.clear();

  GetWasmEngine()->FreeNativeModule(this);

  // If experimental PGO support is enabled, serialize the PGO data now.
  if (V8_UNLIKELY(v8_flags.experimental_wasm_pgo_to_file)) {
    DumpProfileToFile(module_.get(), wire_bytes(), tiering_budgets_.get());
  }
}

WasmCodeManager::WasmCodeManager()
    : max_committed_code_space_(v8_flags.wasm_max_committed_code_mb * MB),
      critical_committed_code_space_(max_committed_code_space_ / 2),
      next_code_space_hint_(reinterpret_cast<Address>(
          GetPlatformPageAllocator()->GetRandomMmapAddr())) {
  // Check that --wasm-max-code-space-size-mb is not set bigger than the default
  // value. Otherwise we run into DCHECKs or other crashes later.
  CHECK_GE(kDefaultMaxWasmCodeSpaceSizeMb,
           v8_flags.wasm_max_code_space_size_mb);
}

WasmCodeManager::~WasmCodeManager() {
  // No more committed code space.
  DCHECK_EQ(0, total_committed_code_space_.load());
}

#if defined(V8_OS_WIN64)
// static
bool WasmCodeManager::CanRegisterUnwindInfoForNonABICompliantCodeRange() {
  return win64_unwindinfo::CanRegisterUnwindInfoForNonABICompliantCodeRange() &&
         v8_flags.win64_unwinding_info;
}
#endif  // V8_OS_WIN64

void WasmCodeManager::Commit(base::AddressRegion region) {
  DCHECK(IsAligned(region.begin(), CommitPageSize()));
  DCHECK(IsAligned(region.size(), CommitPageSize()));
  // Reserve the size. Use CAS loop to avoid overflow on
  // {total_committed_code_space_}.
  size_t old_value = total_committed_code_space_.load();
  while (true) {
    DCHECK_GE(max_committed_code_space_, old_value);
    if (region.size() > max_committed_code_space_ - old_value) {
      auto oom_detail = base::FormattedString{}
                        << "trying to commit " << region.size()
                        << ", already committed " << old_value;
      V8::FatalProcessOutOfMemory(nullptr,
                                  "Exceeding maximum wasm committed code space",
                                  oom_detail.PrintToArray().data());
      UNREACHABLE();
    }
    if (total_committed_code_space_.compare_exchange_weak(
            old_value, old_value + region.size())) {
      break;
    }
  }

  TRACE_HEAP("Setting rwx permissions for 0x%" PRIxPTR ":0x%" PRIxPTR "\n",
             region.begin(), region.end());
  bool success = GetPlatformPageAllocator()->RecommitPages(
      reinterpret_cast<void*>(region.begin()), region.size(),
      PageAllocator::kReadWriteExecute);

  if (V8_UNLIKELY(!success)) {
    auto oom_detail = base::FormattedString{} << "region size: "
                                              << region.size();
    V8::FatalProcessOutOfMemory(nullptr, "Commit wasm code space",
                                oom_detail.PrintToArray().data());
    UNREACHABLE();
  }
}

void WasmCodeManager::Decommit(base::AddressRegion region) {
  PageAllocator* allocator = GetPlatformPageAllocator();
  DCHECK(IsAligned(region.begin(), allocator->CommitPageSize()));
  DCHECK(IsAligned(region.size(), allocator->CommitPageSize()));
  [[maybe_unused]] size_t old_committed =
      total_committed_code_space_.fetch_sub(region.size());
  DCHECK_LE(region.size(), old_committed);
  TRACE_HEAP("Decommitting system pages 0x%" PRIxPTR ":0x%" PRIxPTR "\n",
             region.begin(), region.end());
  if (V8_UNLIKELY(!allocator->DecommitPages(
          reinterpret_cast<void*>(region.begin()), region.size()))) {
    // Decommit can fail in near-OOM situations.
    auto oom_detail = base::FormattedString{} << "region size: "
                                              << region.size();
    V8::FatalProcessOutOfMemory(nullptr, "Decommit Wasm code space",
                                oom_detail.PrintToArray().data());
  }
}

void WasmCodeManager::AssignRange(base::AddressRegion region,
                                  NativeModule* native_module) {
  base::MutexGuard lock(&native_modules_mutex_);
  lookup_map_.insert(std::make_pair(
      region.begin(), std::make_pair(region.end(), native_module)));
}

VirtualMemory WasmCodeManager::TryAllocate(size_t size) {
  v8::PageAllocator* page_allocator = GetPlatformPageAllocator();
  DCHECK_GT(size, 0);
  size_t allocate_page_size = page_allocator->AllocatePageSize();
  size = RoundUp(size, allocate_page_size);
  Address hint =
      next_code_space_hint_.fetch_add(size, std::memory_order_relaxed);

  // When we start exposing Wasm in jitless mode, then the jitless flag
  // will have to determine whether we set kMapAsJittable or not.
  DCHECK(!v8_flags.jitless);
  VirtualMemory mem(page_allocator, size, reinterpret_cast<void*>(hint),
                    allocate_page_size,
                    PageAllocator::Permission::kNoAccessWillJitLater);
  if (!mem.IsReserved()) {
    // Try resetting {next_code_space_hint_}, which might fail if another thread
    // bumped it in the meantime.
    Address bumped_hint = hint + size;
    next_code_space_hint_.compare_exchange_weak(bumped_hint, hint,
                                                std::memory_order_relaxed);
    return {};
  }
  TRACE_HEAP("VMem alloc: 0x%" PRIxPTR ":0x%" PRIxPTR " (%zu)\n", mem.address(),
             mem.end(), mem.size());

  if (mem.address() != hint) {
    // If the hint was ignored, just store the end of the new vmem area
    // unconditionally, potentially racing with other concurrent allocations (it
    // does not really matter which end pointer we keep in that case).
    next_code_space_hint_.store(mem.end(), std::memory_order_relaxed);
  }

  // Don't pre-commit the code cage on Windows since it uses memory and it's not
  // required for recommit.
#if !defined(V8_OS_WIN)
  if (MemoryProtectionKeysEnabled()) {
#if V8_HAS_PKU_JIT_WRITE_PROTECT
    if (ThreadIsolation::Enabled()) {
      CHECK(ThreadIsolation::MakeExecutable(mem.address(), mem.size()));
    } else {
      CHECK(base::MemoryProtectionKey::SetPermissionsAndKey(
          mem.region(), PageAllocator::kReadWriteExecute,
          RwxMemoryWriteScope::memory_protection_key()));
    }
#else
    UNREACHABLE();
#endif
  } else {
    CHECK(SetPermissions(GetPlatformPageAllocator(), mem.address(), mem.size(),
                         PageAllocator::kReadWriteExecute));
  }
  page_allocator->DiscardSystemPages(reinterpret_cast<void*>(mem.address()),
                                     mem.size());
#endif  // !defined(V8_OS_WIN)

  ThreadIsolation::RegisterJitPage(mem.address(), mem.size());

  return mem;
}

namespace {
// The numbers here are rough estimates, used to calculate the size of the
// initial code reservation and for estimating the amount of external memory
// reported to the GC.
// They do not need to be accurate. Choosing them too small will result in
// separate code spaces being allocated (compile time and runtime overhead),
// choosing them too large results in over-reservation (virtual address space
// only).
// In doubt, choose the numbers slightly too large on 64-bit systems (where
// {kNeedsFarJumpsBetweenCodeSpaces} is {true}). Over-reservation is less
// critical in a 64-bit address space, but separate code spaces cause overhead.
// On 32-bit systems (where {kNeedsFarJumpsBetweenCodeSpaces} is {false}), the
// opposite is true: Multiple code spaces are cheaper, and address space is
// scarce, hence choose numbers slightly too small.
//
// Numbers can be determined by running benchmarks with
// --trace-wasm-compilation-times, and piping the output through
// tools/wasm/code-size-factors.py.
#if V8_TARGET_ARCH_X64
constexpr size_t kTurbofanFunctionOverhead = 24;
constexpr size_t kTurbofanCodeSizeMultiplier = 3;
constexpr size_t kLiftoffFunctionOverhead = 56;
constexpr size_t kLiftoffCodeSizeMultiplier = 4;
constexpr size_t kImportSize = 640;
#elif V8_TARGET_ARCH_IA32
constexpr size_t kTurbofanFunctionOverhead = 20;
constexpr size_t kTurbofanCodeSizeMultiplier = 3;
constexpr size_t kLiftoffFunctionOverhead = 48;
constexpr size_t kLiftoffCodeSizeMultiplier = 3;
constexpr size_t kImportSize = 600;
#elif V8_TARGET_ARCH_ARM
constexpr size_t kTurbofanFunctionOverhead = 44;
constexpr size_t kTurbofanCodeSizeMultiplier = 3;
constexpr size_t kLiftoffFunctionOverhead = 96;
constexpr size_t kLiftoffCodeSizeMultiplier = 5;
constexpr size_t kImportSize = 550;
#elif V8_TARGET_ARCH_ARM64
constexpr size_t kTurbofanFunctionOverhead = 40;
constexpr size_t kTurbofanCodeSizeMultiplier = 3;
constexpr size_t kLiftoffFunctionOverhead = 68;
constexpr size_t kLiftoffCodeSizeMultiplier = 4;
constexpr size_t kImportSize = 750;
#else
// Other platforms should add their own estimates for best performance. Numbers
// below are the maximum of other architectures.
constexpr size_t kTurbofanFunctionOverhead = 44;
constexpr size_t kTurbofanCodeSizeMultiplier = 4;
constexpr size_t kLiftoffFunctionOverhead = 96;
constexpr size_t kLiftoffCodeSizeMultiplier = 5;
constexpr size_t kImportSize = 750;
#endif
}  // namespace

// static
size_t WasmCodeManager::EstimateLiftoffCodeSize(int body_size) {
  return kLiftoffFunctionOverhead + kCodeAlignment / 2 +
         body_size * kLiftoffCodeSizeMultiplier;
}

// static
size_t WasmCodeManager::EstimateNativeModuleCodeSize(
    const WasmModule* module, bool include_liftoff,
    DynamicTiering dynamic_tiering) {
  int num_functions = static_cast<int>(module->num_declared_functions);
  int num_imported_functions = static_cast<int>(module->num_imported_functions);
  int code_section_length = 0;
  if (num_functions > 0) {
    DCHECK_EQ(module->functions.size(), num_imported_functions + num_functions);
    auto* first_fn = &module->functions[module->num_imported_functions];
    auto* last_fn = &module->functions.back();
    code_section_length =
        static_cast<int>(last_fn->code.end_offset() - first_fn->code.offset());
  }
  return EstimateNativeModuleCodeSize(num_functions, num_imported_functions,
                                      code_section_length, include_liftoff,
                                      dynamic_tiering);
}

// static
size_t WasmCodeManager::EstimateNativeModuleCodeSize(
    int num_functions, int num_imported_functions, int code_section_length,
    bool include_liftoff, DynamicTiering dynamic_tiering) {
  // The size for the jump table and far jump table is added later, per code
  // space (see {OverheadPerCodeSpace}). We still need to add the overhead for
  // the lazy compile table once, though. There are configurations where we do
  // not need it (non-asm.js, no dynamic tiering and no lazy compilation), but
  // we ignore this here as most of the time we will need it.
  const size_t lazy_compile_table_size =
      JumpTableAssembler::SizeForNumberOfLazyFunctions(num_functions);

  const size_t size_of_imports = kImportSize * num_imported_functions;

  const size_t overhead_per_function_turbofan =
      kTurbofanFunctionOverhead + kCodeAlignment / 2;
  size_t size_of_turbofan = overhead_per_function_turbofan * num_functions +
                            kTurbofanCodeSizeMultiplier * code_section_length;

  const size_t overhead_per_function_liftoff =
      kLiftoffFunctionOverhead + kCodeAlignment / 2;
  const size_t size_of_liftoff =
      include_liftoff ? overhead_per_function_liftoff * num_functions +
                            kLiftoffCodeSizeMultiplier * code_section_length
                      : 0;

  // With dynamic tiering we don't expect to compile more than 25% with
  // TurboFan. If there is no liftoff though then all code will get generated
  // by TurboFan.
  if (include_liftoff && dynamic_tiering) size_of_turbofan /= 4;

  return lazy_compile_table_size + size_of_imports + size_of_liftoff +
         size_of_turbofan;
}

// static
size_t WasmCodeManager::EstimateNativeModuleMetaDataSize(
    const WasmModule* module) {
  size_t wasm_module_estimate = module->EstimateStoredSize();

  uint32_t num_wasm_functions = module->num_declared_functions;

  // TODO(wasm): Include wire bytes size.
  size_t native_module_estimate =
      sizeof(NativeModule) +                      // NativeModule struct
      (sizeof(WasmCode*) * num_wasm_functions) +  // code table size
      (sizeof(WasmCode) * num_wasm_functions);    // code object size

  size_t jump_table_size = RoundUp<kCodeAlignment>(
      JumpTableAssembler::SizeForNumberOfSlots(num_wasm_functions));
  size_t far_jump_table_size =
      RoundUp<kCodeAlignment>(JumpTableAssembler::SizeForNumberOfFarJumpSlots(
          BuiltinLookup::BuiltinCount(),
          NumWasmFunctionsInFarJumpTable(num_wasm_functions)));

  return wasm_module_estimate + native_module_estimate + jump_table_size +
         far_jump_table_size;
}

// static
bool WasmCodeManager::HasMemoryProtectionKeySupport() {
#if V8_HAS_PKU_JIT_WRITE_PROTECT
  return RwxMemoryWriteScope::IsSupported();
#else
  return false;
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT
}

// static
bool WasmCodeManager::MemoryProtectionKeysEnabled() {
  return HasMemoryProtectionKeySupport();
}

// static
bool WasmCodeManager::MemoryProtectionKeyWritable() {
#if V8_HAS_PKU_JIT_WRITE_PROTECT
  return RwxMemoryWriteScope::IsPKUWritable();
#else
  return false;
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT
}

std::shared_ptr<NativeModule> WasmCodeManager::NewNativeModule(
    Isolate* isolate, WasmEnabledFeatures enabled,
    CompileTimeImports compile_imports, size_t code_size_estimate,
    std::shared_ptr<const WasmModule> module) {
#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    VirtualMemory code_space;
    std::shared_ptr<NativeModule> ret;
    new NativeModule(enabled, compile_imports,
                     DynamicTiering{v8_flags.wasm_dynamic_tiering.value()},
                     std::move(code_space), std::move(module),
                     isolate->async_counters(), &ret);
    // The constructor initialized the shared_ptr.
    DCHECK_NOT_NULL(ret);
    TRACE_HEAP("New NativeModule (wasm-jitless) %p\n", ret.get());
    return ret;
  }
#endif  // V8_ENABLE_DRUMBRAKE

  if (total_committed_code_space_.load() >
      critical_committed_code_space_.load()) {
    // Flush Liftoff code and record the flushed code size.
    if (v8_flags.flush_liftoff_code) {
      auto [code_size, metadata_size] =
          wasm::GetWasmEngine()->FlushLiftoffCode();
      isolate->counters()->wasm_flushed_liftoff_code_size_bytes()->AddSample(
          static_cast<int>(code_size));
      isolate->counters()
          ->wasm_flushed_liftoff_metadata_size_bytes()
          ->AddSample(static_cast<int>(metadata_size));
    }
    (reinterpret_cast<v8::Isolate*>(isolate))
        ->MemoryPressureNotification(MemoryPressureLevel::kCritical);
    size_t committed = total_committed_code_space_.load();
    DCHECK_GE(max_committed_code_space_, committed);
    critical_committed_code_space_.store(
        committed + (max_committed_code_space_ - committed) / 2);
  }

  size_t code_vmem_size =
      ReservationSize(code_size_estimate, module->num_declared_functions, 0);

  // The '--wasm-max-initial-code-space-reservation' testing flag can be used to
  // reduce the maximum size of the initial code space reservation (in MB).
  if (v8_flags.wasm_max_initial_code_space_reservation > 0) {
    size_t flag_max_bytes =
        static_cast<size_t>(v8_flags.wasm_max_initial_code_space_reservation) *
        MB;
    if (flag_max_bytes < code_vmem_size) code_vmem_size = flag_max_bytes;
  }

  // Try up to two times; getting rid of dead JSArrayBuffer allocations might
  // require two GCs because the first GC maybe incremental and may have
  // floating garbage.
  static constexpr int kAllocationRetries = 2;
  VirtualMemory code_space;
  for (int retries = 0;; ++retries) {
    code_space = TryAllocate(code_vmem_size);
    if (code_space.IsReserved()) break;
    if (retries == kAllocationRetries) {
      auto oom_detail = base::FormattedString{}
                        << "NewNativeModule cannot allocate code space of "
                        << code_vmem_size << " bytes";
      V8::FatalProcessOutOfMemory(isolate, "Allocate initial wasm code space",
                                  oom_detail.PrintToArray().data());
      UNREACHABLE();
    }
    // Run one GC, then try the allocation again.
    isolate->heap()->MemoryPressureNotification(MemoryPressureLevel::kCritical,
                                                true);
  }

  Address start = code_space.address();
  size_t size = code_space.size();
  Address end = code_space.end();
  std::shared_ptr<NativeModule> ret;
  new NativeModule(enabled, std::move(compile_imports),
                   DynamicTiering{v8_flags.wasm_dynamic_tiering.value()},
                   std::move(code_space), std::move(module),
                   isolate->async_counters(), &ret);
  // The constructor initialized the shared_ptr.
  DCHECK_NOT_NULL(ret);
  TRACE_HEAP("New NativeModule %p: Mem: 0x%" PRIxPTR ",+%zu\n", ret.get(),
             start, size);

  base::MutexGuard lock(&native_modules_mutex_);
  lookup_map_.insert(std::make_pair(start, std::make_pair(end, ret.get())));
  return ret;
}

void NativeModule::SampleCodeSize(Counters* counters) const {
  size_t code_size = code_allocator_.committed_code_space();
  int code_size_mb = static_cast<int>(code_size / MB);
#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    base::MutexGuard lock(&module_->interpreter_mutex_);
    if (auto interpreter = module_->interpreter_.lock()) {
      code_size_mb = static_cast<int>(interpreter->TotalBytecodeSize() / MB);
    }
  }
#endif  // V8_ENABLE_DRUMBRAKE
  counters->wasm_module_code_size_mb()->AddSample(code_size_mb);
  int code_size_kb = static_cast<int>(code_size / KB);
  counters->wasm_module_code_size_kb()->AddSample(code_size_kb);
  // Record the size of metadata.
  Histogram* metadata_histogram = counters->wasm_module_metadata_size_kb();
  if (metadata_histogram->Enabled()) {
    // TODO(349610478): EstimateCurrentMemoryConsumption() acquires a large
    // amount of locks per NativeModule. This estimation is run on every
    // mark-compact GC. Reconsider whether this should be run less frequently.
    // (Probably incomplete) list of locks acquired:
    // - TypeFeedbackStorage::mutex
    // - LazilyGeneratedNames::mutex_
    // - CompilationStateImpl::mutex_
    // - CompilationUnitQueues::queues_mutex_
    //   - per queue: QueueImpl::mutex
    // - BigUnitsQueue::mutex
    // - WasmImportWrapperCache::mutex_
    // - NativeModule::allocation_mutex_
    // - LazilyGeneratedNames::mutex_
    // - DebugInfoImpl::debug_side_tables_mutex_
    // - DebugInfoImpl::mutex_
    int metadata_size_kb =
        static_cast<int>(EstimateCurrentMemoryConsumption() / KB);
    metadata_histogram->AddSample(metadata_size_kb);
  }
  // If this is a wasm module of >= 2MB, also sample the freed code size,
  // absolute and relative. Code GC does not happen on asm.js
  // modules, and small modules will never trigger GC anyway.
  size_t generated_size = code_allocator_.generated_code_size();
  if (generated_size >= 2 * MB && module()->origin == kWasmOrigin) {
    size_t freed_size = code_allocator_.freed_code_size();
    DCHECK_LE(freed_size, generated_size);
    int freed_percent = static_cast<int>(100 * freed_size / generated_size);
    counters->wasm_module_freed_code_size_percent()->AddSample(freed_percent);
  }
}

std::unique_ptr<WasmCode> NativeModule::AddCompiledCode(
    const WasmCompilationResult& result) {
  std::vector<std::unique_ptr<WasmCode>> code = AddCompiledCode({&result, 1});
  return std::move(code[0]);
}

std::vector<std::unique_ptr<WasmCode>> NativeModule::AddCompiledCode(
    base::Vector<const WasmCompilationResult> results) {
  TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.AddCompiledCode", "num", results.size());
  DCHECK(!results.empty());
  std::vector<std::unique_ptr<WasmCode>> generated_code;
  generated_code.reserve(results.size());

  // First, allocate code space for all the results.
  // Never add more than half of a code space at once. This leaves some space
  // for jump tables and other overhead. We could use {OverheadPerCodeSpace},
  // but that's only an approximation, so we are conservative here and never use
  // more than half a code space.
  size_t max_code_batch_size = v8_flags.wasm_max_code_space_size_mb * MB / 2;
  size_t total_code_space = 0;
  for (auto& result : results) {
    DCHECK(result.succeeded());
    size_t new_code_space =
        RoundUp<kCodeAlignment>(result.code_desc.instr_size);
    if (total_code_space + new_code_space > max_code_batch_size) {
      // Split off the first part of the {results} vector and process it
      // separately. This method then continues with the rest.
      size_t split_point = &result - results.begin();
      CHECK_WITH_MSG(
          split_point != 0,
          "A single code object needs more than half of the code space size");
      auto first_results = AddCompiledCode(results.SubVector(0, split_point));
      generated_code.insert(generated_code.end(),
                            std::make_move_iterator(first_results.begin()),
                            std::make_move_iterator(first_results.end()));
      // Continue processing the rest of the vector. This change to the
      // {results} vector does not invalidate iterators (which are just
      // pointers). In particular, the end pointer stays the same.
      results += split_point;
      total_code_space = 0;
    }
    total_code_space += new_code_space;
  }
  base::Vector<uint8_t> code_space;
  NativeModule::JumpTablesRef jump_tables;
  {
    base::RecursiveMutexGuard guard{&allocation_mutex_};
    code_space = code_allocator_.AllocateForCode(this, total_code_space);
    // Lookup the jump tables to use once, then use for all code objects.
    jump_tables =
        FindJumpTablesForRegionLocked(base::AddressRegionOf(code_space));
  }
  // If we happen to have a {total_code_space} which is bigger than
  // {kMaxCodeSpaceSize}, we would not find valid jump tables for the whole
  // region. If this ever happens, we need to handle this case (by splitting the
  // {results} vector in smaller chunks).
  CHECK(jump_tables.is_valid());

  std::vector<size_t> sizes;
  for (const auto& result : results) {
    sizes.emplace_back(RoundUp<kCodeAlignment>(result.code_desc.instr_size));
  }
  ThreadIsolation::RegisterJitAllocations(
      reinterpret_cast<Address>(code_space.begin()), sizes,
      ThreadIsolation::JitAllocationType::kWasmCode);

  // Now copy the generated code into the code space and relocate it.
  for (auto& result : results) {
    DCHECK_EQ(result.code_desc.buffer, result.instr_buffer->start());
    size_t code_size = RoundUp<kCodeAlignment>(result.code_desc.instr_size);
    base::Vector<uint8_t> this_code_space = code_space.SubVector(0, code_size);
    code_space += code_size;
    generated_code.emplace_back(AddCodeWithCodeSpace(
        result.func_index, result.code_desc, result.frame_slot_count,
        result.ool_spill_count, result.tagged_parameter_slots,
        result.protected_instructions_data.as_vector(),
        result.source_positions.as_vector(),
        result.inlining_positions.as_vector(), result.deopt_data.as_vector(),
        GetCodeKind(result), result.result_tier, result.for_debugging,
        result.frame_has_feedback_slot, this_code_space, jump_tables));
  }
  DCHECK_EQ(0, code_space.size());

  // Check that we added the expected amount of code objects, even if we split
  // the {results} vector.
  DCHECK_EQ(generated_code.capacity(), generated_code.size());

  return generated_code;
}

void NativeModule::SetDebugState(DebugState new_debug_state) {
  // Do not tier down asm.js (just never change the tiering state).
  if (module()->origin != kWasmOrigin) return;

  base::RecursiveMutexGuard lock(&allocation_mutex_);
  debug_state_ = new_debug_state;
}

namespace {
bool ShouldRemoveCode(WasmCode* code, NativeModule::RemoveFilter filter) {
  if (filter == NativeModule::RemoveFilter::kRemoveDebugCode &&
      !code->for_debugging()) {
    return false;
  }
  if (filter == NativeModule::RemoveFilter::kRemoveNonDebugCode &&
      code->for_debugging()) {
    return false;
  }
  if (filter == NativeModule::RemoveFilter::kRemoveLiftoffCode &&
      !code->is_liftoff()) {
    return false;
  }
  if (filter == NativeModule::RemoveFilter::kRemoveTurbofanCode &&
      !code->is_turbofan()) {
    return false;
  }
  return true;
}
}  // namespace

std::pair<size_t, size_t> NativeModule::RemoveCompiledCode(
    RemoveFilter filter) {
  const uint32_t num_imports = module_->num_imported_functions;
  const uint32_t num_functions = module_->num_declared_functions;
  base::RecursiveMutexGuard guard(&allocation_mutex_);
  size_t removed_codesize = 0;
  size_t removed_metadatasize = 0;
  for (uint32_t i = 0; i < num_functions; i++) {
    WasmCode* code = code_table_[i];
    if (code && ShouldRemoveCode(code, filter)) {
      removed_codesize += code->instructions_size();
      removed_metadatasize += code->EstimateCurrentMemoryConsumption();
      code_table_[i] = nullptr;
      // Add the code to the {WasmCodeRefScope}, so the ref count cannot drop to
      // zero here. It might in the {WasmCodeRefScope} destructor, though.
      WasmCodeRefScope::AddRef(code);
      code->DecRefOnLiveCode();
      uint32_t func_index = i + num_imports;
      UseLazyStubLocked(func_index);
    }
  }
  // When resuming optimized execution after a debugging session ends, or when
  // discarding optimized code that made outdated assumptions, allow another
  // tier-up task to get scheduled.
  if (filter == RemoveFilter::kRemoveDebugCode ||
      filter == RemoveFilter::kRemoveTurbofanCode) {
    compilation_state_->AllowAnotherTopTierJobForAllFunctions();
  }
  return std::make_pair(removed_codesize, removed_metadatasize);
}

size_t NativeModule::SumLiftoffCodeSizeForTesting() const {
  base::RecursiveMutexGuard guard(&allocation_mutex_);
  const uint32_t num_functions = module_->num_declared_functions;
  size_t codesize_liftoff = 0;
  for (uint32_t i = 0; i < num_functions; i++) {
    WasmCode* code = code_table_[i];
    if (code && code->is_liftoff()) {
      codesize_liftoff += code->instructions_size();
    }
  }
  return codesize_liftoff;
}

void NativeModule::FreeCode(base::Vector<WasmCode* const> codes) {
  base::RecursiveMutexGuard guard(&allocation_mutex_);
  // Free the code space.
  code_allocator_.FreeCode(codes);

  if (!new_owned_code_.empty()) TransferNewOwnedCodeLocked();
  DebugInfo* debug_info = debug_info_.get();
  // Free the {WasmCode} objects. This will also unregister trap handler data.
  for (WasmCode* code : codes) {
    DCHECK_EQ(1, owned_code_.count(code->instruction_start()));
    owned_code_.erase(code->instruction_start());
  }
  // Remove debug side tables for all removed code objects, after releasing our
  // lock. This is to avoid lock order inversion.
  if (debug_info) debug_info->RemoveDebugSideTables(codes);
}

size_t NativeModule::GetNumberOfCodeSpacesForTesting() const {
  base::RecursiveMutexGuard guard{&allocation_mutex_};
  return code_allocator_.GetNumCodeSpaces();
}

bool NativeModule::HasDebugInfo() const {
  base::RecursiveMutexGuard guard(&allocation_mutex_);
  return debug_info_ != nullptr;
}

DebugInfo* NativeModule::GetDebugInfo() {
  base::RecursiveMutexGuard guard(&allocation_mutex_);
  if (!debug_info_) debug_info_ = std::make_unique<DebugInfo>(this);
  return debug_info_.get();
}

NamesProvider* NativeModule::GetNamesProvider() {
  DCHECK(HasWireBytes());
  base::RecursiveMutexGuard guard(&allocation_mutex_);
  if (!names_provider_) {
    names_provider_ =
        std::make_unique<NamesProvider>(module_.get(), wire_bytes());
  }
  return names_provider_.get();
}

size_t NativeModule::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(NativeModule, 552);
  size_t result = sizeof(NativeModule);
  result += module_->EstimateCurrentMemoryConsumption();

  std::shared_ptr<base::OwnedVector<const uint8_t>> wire_bytes =
      std::atomic_load(&wire_bytes_);
  size_t wire_bytes_size = wire_bytes ? wire_bytes->size() : 0;
  result += wire_bytes_size;

  if (source_map_) {
    result += source_map_->EstimateCurrentMemoryConsumption();
  }
  result += compilation_state_->EstimateCurrentMemoryConsumption();
  result += import_wrapper_cache_.EstimateCurrentMemoryConsumption();
  // For {tiering_budgets_}.
  result += module_->num_declared_functions * sizeof(uint32_t);

  size_t external_storage = compile_imports_.constants_module().capacity();
  // This is an approximation: the actual number of inline-stored characters
  // is a little less than the result of `sizeof`.
  if (external_storage > sizeof(std::string)) {
    result += external_storage;
  }

  // For fast api call targets.
  result += module_->num_imported_functions *
            (sizeof(std::atomic<Address>) + sizeof(CFunctionInfo*));
  // We cannot hold the `allocation_mutex_` while calling
  // `debug_info_->EstimateCurrentMemoryConsumption`, as we would run into a
  // lock-order-inversion when acquiring the `mutex_`. The reverse order happens
  // when calling `WasmScript::SetBreakPointForFunction`.
  DebugInfo* debug_info;
  {
    base::RecursiveMutexGuard lock(&allocation_mutex_);
    result += ContentSize(owned_code_);
    for (auto& [address, unique_code_ptr] : owned_code_) {
      result += unique_code_ptr->EstimateCurrentMemoryConsumption();
    }
    result += ContentSize(new_owned_code_);
    for (std::unique_ptr<WasmCode>& code : new_owned_code_) {
      result += code->EstimateCurrentMemoryConsumption();
    }
    // For {code_table_}.
    result += module_->num_declared_functions * sizeof(void*);
    result += ContentSize(code_space_data_);
    debug_info = debug_info_.get();
    if (names_provider_) {
      result += names_provider_->EstimateCurrentMemoryConsumption();
    }
  }
  if (debug_info) {
    result += debug_info->EstimateCurrentMemoryConsumption();
  }

  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("NativeModule wire bytes: %zu\n", wire_bytes_size);
    PrintF("NativeModule: %zu\n", result);
  }
  return result;
}

void WasmCodeManager::FreeNativeModule(
    base::Vector<VirtualMemory> owned_code_space, size_t committed_size) {
  base::MutexGuard lock(&native_modules_mutex_);
  for (auto& code_space : owned_code_space) {
    DCHECK(code_space.IsReserved());
    TRACE_HEAP("VMem Release: 0x%" PRIxPTR ":0x%" PRIxPTR " (%zu)\n",
               code_space.address(), code_space.end(), code_space.size());

#if defined(V8_OS_WIN64)
    if (CanRegisterUnwindInfoForNonABICompliantCodeRange()) {
      win64_unwindinfo::UnregisterNonABICompliantCodeRange(
          reinterpret_cast<void*>(code_space.address()));
    }
#endif  // V8_OS_WIN64

    lookup_map_.erase(code_space.address());
    ThreadIsolation::UnregisterJitPage(code_space.address(), code_space.size());
    code_space.Free();
    DCHECK(!code_space.IsReserved());
  }

  DCHECK(IsAligned(committed_size, CommitPageSize()));
  [[maybe_unused]] size_t old_committed =
      total_committed_code_space_.fetch_sub(committed_size);
  DCHECK_LE(committed_size, old_committed);
}

NativeModule* WasmCodeManager::LookupNativeModule(Address pc) const {
  base::MutexGuard lock(&native_modules_mutex_);
  if (lookup_map_.empty()) return nullptr;

  auto iter = lookup_map_.upper_bound(pc);
  if (iter == lookup_map_.begin()) return nullptr;
  --iter;
  Address region_start = iter->first;
  Address region_end = iter->second.first;
  NativeModule* candidate = iter->second.second;

  DCHECK_NOT_NULL(candidate);
  return region_start <= pc && pc < region_end ? candidate : nullptr;
}

WasmCode* WasmCodeManager::LookupCode(Address pc) const {
  NativeModule* candidate = LookupNativeModule(pc);
  return candidate ? candidate->Lookup(pc) : nullptr;
}

WasmCode* WasmCodeManager::LookupCode(Isolate* isolate, Address pc) const {
  // Since kNullAddress is used as a sentinel value, we should not try
  // to look it up in the cache
  if (pc == kNullAddress) return nullptr;
  // If 'isolate' is nullptr, do not use a cache. This can happen when
  // called from function V8NameConverter::NameOfAddress
  if (isolate) {
    return isolate->wasm_code_look_up_cache()->GetCacheEntry(pc)->code;
  } else {
    wasm::WasmCodeRefScope code_ref_scope;
    return LookupCode(pc);
  }
}

std::pair<WasmCode*, SafepointEntry> WasmCodeManager::LookupCodeAndSafepoint(
    Isolate* isolate, Address pc) {
  auto* entry = isolate->wasm_code_look_up_cache()->GetCacheEntry(pc);
  WasmCode* code = entry->code;
  DCHECK_NOT_NULL(code);
  // For protected instructions we usually do not emit a safepoint because the
  // frame will be unwound anyway. The exception is debugging code, where the
  // frame might be inspected if "pause on exception" is set.
  // For those instructions, we thus need to explicitly return an empty
  // safepoint; using any previously registered safepoint can lead to crashes
  // when we try to visit spill slots that do not hold tagged values at this
  // point.
  // Evaluate this condition only on demand (the fast path does not need it).
  auto expect_safepoint = [code, pc]() {
    const bool is_protected_instruction = code->IsProtectedInstruction(
        pc - WasmFrameConstants::kProtectedInstructionReturnAddressOffset);
    return !is_protected_instruction || code->for_debugging();
  };
  if (!entry->safepoint_entry.is_initialized() && expect_safepoint()) {
    entry->safepoint_entry = SafepointTable{code}.TryFindEntry(pc);
    CHECK(entry->safepoint_entry.is_initialized());
  } else if (expect_safepoint()) {
    DCHECK_EQ(entry->safepoint_entry, SafepointTable{code}.TryFindEntry(pc));
  } else {
    DCHECK(!entry->safepoint_entry.is_initialized());
  }
  return std::make_pair(code, entry->safepoint_entry);
}

void WasmCodeManager::FlushCodeLookupCache(Isolate* isolate) {
  return isolate->wasm_code_look_up_cache()->Flush();
}

namespace {
thread_local WasmCodeRefScope* current_code_refs_scope = nullptr;
}  // namespace

WasmCodeRefScope::WasmCodeRefScope()
    : previous_scope_(current_code_refs_scope) {
  current_code_refs_scope = this;
}

WasmCodeRefScope::~WasmCodeRefScope() {
  DCHECK_EQ(this, current_code_refs_scope);
  current_code_refs_scope = previous_scope_;
  WasmCode::DecrementRefCount(base::VectorOf(code_ptrs_));
}

// static
void WasmCodeRefScope::AddRef(WasmCode* code) {
  DCHECK_NOT_NULL(code);
  WasmCodeRefScope* current_scope = current_code_refs_scope;
  DCHECK_NOT_NULL(current_scope);
  current_scope->code_ptrs_.push_back(code);
  code->IncRef();
}

void WasmCodeLookupCache::Flush() {
  for (int i = 0; i < kWasmCodeLookupCacheSize; i++)
    cache_[i].pc.store(kNullAddress, std::memory_order_release);
}

WasmCodeLookupCache::CacheEntry* WasmCodeLookupCache::GetCacheEntry(
    Address pc) {
  static_assert(base::bits::IsPowerOfTwo(kWasmCodeLookupCacheSize));
  DCHECK(pc != kNullAddress);
  uint32_t hash = ComputeAddressHash(pc);
  uint32_t index = hash & (kWasmCodeLookupCacheSize - 1);
  CacheEntry* entry = &cache_[index];
  if (entry->pc.load(std::memory_order_acquire) == pc) {
    // Code can be deallocated at two points:
    // - when the NativeModule that references it is garbage-
    //   collected;
    // - when it is no longer referenced by its NativeModule, nor from
    //   any stack.
    // The cache is cleared when a NativeModule is destroyed, and when
    // the isolate reports the set of code referenced from its stacks.
    // So, if the code is the cache, it is because it was live at some
    // point (when inserted in the cache), its native module is still
    // considered live, and it has not yet been reported as no longer
    // referenced from any stack. It thus cannot have been released
    // yet.
#ifdef DEBUG
    wasm::WasmCodeRefScope code_ref_scope;
    DCHECK_EQ(entry->code, wasm::GetWasmCodeManager()->LookupCode(pc));
#endif  // DEBUG
  } else {
    // For WebAssembly frames we perform a lookup in the handler table.
    // This code ref scope is here to avoid a check failure when looking up
    // the code. It's not actually necessary to keep the code alive as it's
    // currently being executed.
    wasm::WasmCodeRefScope code_ref_scope;
    entry->pc.store(pc, std::memory_order_release);
    entry->code = wasm::GetWasmCodeManager()->LookupCode(pc);
    entry->safepoint_entry.Reset();
  }
  return entry;
}
}  // namespace wasm
}  // namespace internal
}  // namespace v8
#undef TRACE_HEAP
