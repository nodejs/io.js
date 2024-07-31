// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_HANDLER_TABLE_H_
#define V8_CODEGEN_HANDLER_TABLE_H_

#include "src/base/bit-field.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class Assembler;
class TrustedByteArray;
class BytecodeArray;
class InstructionStream;
class Code;

namespace wasm {
class WasmCode;
}  // namespace wasm

// HandlerTable is a byte array containing entries for exception handlers in
// the code object it is associated with. The tables come in two flavors:
// 1) Based on ranges: Used for unoptimized code. Stored in a
//   {TrustedByteArray} that is attached to each {BytecodeArray}. Contains one
//   entry per exception handler and a range representing the try-block covered
//   by that handler. Layout looks as follows:
//      [ range-start , range-end , handler-offset , handler-data ]
// 2) Based on return addresses: Used for turbofanned code. Stored directly in
//    the instruction stream of the {InstructionStream} object. Contains one
//    entry per call-site that could throw an exception. Layout looks as
//    follows:
//      [ return-address-offset , handler-offset ]
class V8_EXPORT_PRIVATE HandlerTable {
 public:
  // Conservative prediction whether a given handler will locally catch an
  // exception or cause a re-throw to outside the code boundary. Since this is
  // undecidable it is merely an approximation (e.g. useful for debugger).
  enum CatchPrediction {
    UNCAUGHT,     // The handler will (likely) rethrow the exception.
    CAUGHT,       // The exception will be caught by the handler.
    PROMISE,      // The exception will be caught and cause a promise rejection.
    ASYNC_AWAIT,  // The exception will be caught and cause a promise rejection
                  // in the desugaring of an async function, so special
                  // async/await handling in the debugger can take place.
    UNCAUGHT_ASYNC_AWAIT,  // The exception will be caught and cause a promise
                           // rejection in the desugaring of an async REPL
                           // script. The corresponding message object needs to
                           // be kept alive on the Isolate though.
  };

  enum EncodingMode { kRangeBasedEncoding, kReturnAddressBasedEncoding };

  // Constructors for the various encodings.
  explicit HandlerTable(Tagged<InstructionStream> code);
  explicit HandlerTable(Tagged<Code> code);
  explicit HandlerTable(Tagged<TrustedByteArray> byte_array);
#if V8_ENABLE_WEBASSEMBLY
  explicit HandlerTable(const wasm::WasmCode* code);
#endif  // V8_ENABLE_WEBASSEMBLY
  explicit HandlerTable(Tagged<BytecodeArray> bytecode_array);
  HandlerTable(Address handler_table, int handler_table_size,
               EncodingMode encoding_mode);

  // Getters for handler table based on ranges.
  int GetRangeStart(int index) const;
  int GetRangeEnd(int index) const;
  int GetRangeHandler(int index) const;
  int GetRangeData(int index) const;

  // Setters for handler table based on ranges.
  void SetRangeStart(int index, int value);
  void SetRangeEnd(int index, int value);
  void SetRangeHandler(int index, int offset, CatchPrediction pred);
  void SetRangeData(int index, int value);

  // Returns the required length of the underlying byte array.
  static int LengthForRange(int entries);

  // Emitters for handler table based on return addresses.
  static int EmitReturnTableStart(Assembler* masm);
  static void EmitReturnEntry(Assembler* masm, int offset, int handler);

  // Lookup handler in a table based on ranges. The {pc_offset} is an offset to
  // the start of the potentially throwing instruction (using return addresses
  // for this value would be invalid).
  int LookupRange(int pc_offset, int* data, CatchPrediction* prediction);

  // Lookup handler in a table based on return addresses.
  int LookupReturn(int pc_offset);

  // Returns the number of entries in the table.
  int NumberOfRangeEntries() const;
  int NumberOfReturnEntries() const;

#ifdef ENABLE_DISASSEMBLER
  void HandlerTableRangePrint(std::ostream& os);
  void HandlerTableReturnPrint(std::ostream& os);
#endif

 private:
  // Getters for handler table based on ranges.
  CatchPrediction GetRangePrediction(int index) const;

  // Gets entry size based on mode.
  static int EntrySizeFromMode(EncodingMode mode);

  // Getters for handler table based on return addresses.
  int GetReturnOffset(int index) const;
  int GetReturnHandler(int index) const;

  // Number of entries in the loaded handler table.
  const int number_of_entries_;

#ifdef DEBUG
  // The encoding mode of the table. Mostly useful for debugging to check that
  // used accessors and constructors fit together.
  const EncodingMode mode_;
#endif

  // Direct pointer into the encoded data. This pointer potentially points into
  // objects on the GC heap (either {TrustedByteArray} or {InstructionStream})
  // and could become stale during a collection. Hence we disallow any
  // allocation.
  const Address raw_encoded_data_;
  DISALLOW_GARBAGE_COLLECTION(no_gc_)

  // Layout description for handler table based on ranges.
  static const int kRangeStartIndex = 0;
  static const int kRangeEndIndex = 1;
  static const int kRangeHandlerIndex = 2;
  static const int kRangeDataIndex = 3;
  static const int kRangeEntrySize = 4;

  // Layout description for handler table based on return addresses.
  static const int kReturnOffsetIndex = 0;
  static const int kReturnHandlerIndex = 1;
  static const int kReturnEntrySize = 2;

  // Encoding of the {handler} field.
  using HandlerPredictionField = base::BitField<CatchPrediction, 0, 3>;
  using HandlerOffsetField = base::BitField<int, 3, 29>;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_HANDLER_TABLE_H_
