// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_FUNCTION_BODY_DECODER_H_
#define V8_WASM_FUNCTION_BODY_DECODER_H_

#include "src/base/compiler-specific.h"
#include "src/base/iterator.h"
#include "src/common/globals.h"
#include "src/wasm/decoder.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-result.h"

namespace v8::internal {
class AccountingAllocator;
class BitVector;
class Zone;
}  // namespace v8::internal

namespace v8::internal::wasm {

class WasmDetectedFeatures;
class WasmEnabledFeatures;
struct WasmModule;  // forward declaration of module interface.

// A wrapper around the signature and bytes of a function.
struct FunctionBody {
  const FunctionSig* sig;  // function signature
  uint32_t offset;         // offset in the module bytes, for error reporting
  const uint8_t* start;    // start of the function body
  const uint8_t* end;      // end of the function body
  bool is_shared;          // whether this is a shared function

  FunctionBody(const FunctionSig* sig, uint32_t offset, const uint8_t* start,
               const uint8_t* end, bool is_shared)
      : sig(sig),
        offset(offset),
        start(start),
        end(end),
        is_shared(is_shared) {}
};

enum class LoadTransformationKind : uint8_t { kSplat, kExtend, kZeroExtend };

V8_EXPORT_PRIVATE DecodeResult ValidateFunctionBody(
    Zone* zone, WasmEnabledFeatures enabled, const WasmModule* module,
    WasmDetectedFeatures* detected, const FunctionBody& body);

struct BodyLocalDecls {
  // The size of the encoded declarations.
  uint32_t encoded_size = 0;  // size of encoded declarations

  uint32_t num_locals = 0;
  ValueType* local_types = nullptr;
};

// Decode locals; validation is not performed.
V8_EXPORT_PRIVATE void DecodeLocalDecls(WasmEnabledFeatures enabled,
                                        BodyLocalDecls* decls,
                                        const uint8_t* start,
                                        const uint8_t* end, Zone* zone);

// Decode locals, including validation.
V8_EXPORT_PRIVATE bool ValidateAndDecodeLocalDeclsForTesting(
    WasmEnabledFeatures enabled, BodyLocalDecls* decls,
    const WasmModule* module, bool is_shared, const uint8_t* start,
    const uint8_t* end, Zone* zone);

V8_EXPORT_PRIVATE BitVector* AnalyzeLoopAssignmentForTesting(
    Zone* zone, uint32_t num_locals, const uint8_t* start, const uint8_t* end,
    bool* loop_is_innermost);

// Computes the length of the opcode at the given address.
V8_EXPORT_PRIVATE unsigned OpcodeLength(const uint8_t* pc, const uint8_t* end);

// Checks if the underlying hardware supports the Wasm SIMD proposal.
V8_EXPORT_PRIVATE bool CheckHardwareSupportsSimd();

// A simple forward iterator for bytecodes.
class V8_EXPORT_PRIVATE BytecodeIterator : public NON_EXPORTED_BASE(Decoder) {
  // Base class for both iterators defined below.
  class iterator_base {
   public:
    iterator_base& operator++() {
      DCHECK_LT(ptr_, end_);
      ptr_ += OpcodeLength(ptr_, end_);
      return *this;
    }
    bool operator==(const iterator_base& that) const {
      return this->ptr_ == that.ptr_;
    }
    bool operator!=(const iterator_base& that) const {
      return this->ptr_ != that.ptr_;
    }

   protected:
    const uint8_t* ptr_;
    const uint8_t* end_;
    iterator_base(const uint8_t* ptr, const uint8_t* end)
        : ptr_(ptr), end_(end) {}
  };

 public:
  // If one wants to iterate over the bytecode without looking at {pc_offset()}.
  class opcode_iterator
      : public iterator_base,
        public base::iterator<std::input_iterator_tag, WasmOpcode> {
   public:
    WasmOpcode operator*() {
      DCHECK_LT(ptr_, end_);
      return static_cast<WasmOpcode>(*ptr_);
    }

   private:
    friend class BytecodeIterator;
    opcode_iterator(const uint8_t* ptr, const uint8_t* end)
        : iterator_base(ptr, end) {}
  };
  // If one wants to iterate over the instruction offsets without looking at
  // opcodes.
  class offset_iterator
      : public iterator_base,
        public base::iterator<std::input_iterator_tag, uint32_t> {
   public:
    uint32_t operator*() {
      DCHECK_LT(ptr_, end_);
      return static_cast<uint32_t>(ptr_ - start_);
    }

   private:
    const uint8_t* start_;
    friend class BytecodeIterator;
    offset_iterator(const uint8_t* start, const uint8_t* ptr,
                    const uint8_t* end)
        : iterator_base(ptr, end), start_(start) {}
  };

  // Create a new {BytecodeIterator}, starting after the locals declarations.
  BytecodeIterator(const uint8_t* start, const uint8_t* end);

  // Create a new {BytecodeIterator}, starting with locals declarations.
  BytecodeIterator(const uint8_t* start, const uint8_t* end,
                   BodyLocalDecls* decls, Zone* zone);

  base::iterator_range<opcode_iterator> opcodes() const {
    return base::iterator_range<opcode_iterator>(opcode_iterator(pc_, end_),
                                                 opcode_iterator(end_, end_));
  }

  base::iterator_range<offset_iterator> offsets() const {
    return base::iterator_range<offset_iterator>(
        offset_iterator(start_, pc_, end_),
        offset_iterator(start_, end_, end_));
  }

  WasmOpcode current() {
    return static_cast<WasmOpcode>(
        read_u8<Decoder::NoValidationTag>(pc_, "expected bytecode"));
  }

  void next() {
    if (pc_ < end_) {
      pc_ += OpcodeLength(pc_, end_);
      if (pc_ >= end_) pc_ = end_;
    }
  }

  bool has_next() const { return pc_ < end_; }

  WasmOpcode prefixed_opcode() {
    auto [opcode, length] = read_prefixed_opcode<Decoder::NoValidationTag>(pc_);
    return opcode;
  }

  const uint8_t* pc() const { return pc_; }
};

}  // namespace v8::internal::wasm

#endif  // V8_WASM_FUNCTION_BODY_DECODER_H_
