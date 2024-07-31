// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_DECODER_H_
#define V8_WASM_DECODER_H_

#include <cinttypes>
#include <cstdarg>
#include <memory>

#include "src/base/compiler-specific.h"
#include "src/base/memory.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/flags/flags.h"
#include "src/utils/utils.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-result.h"

namespace v8 {
namespace internal {
namespace wasm {

#define TRACE(...)                                        \
  do {                                                    \
    if (v8_flags.trace_wasm_decoder) PrintF(__VA_ARGS__); \
  } while (false)
#define TRACE_IF(cond, ...)                                         \
  do {                                                              \
    if (v8_flags.trace_wasm_decoder && (cond)) PrintF(__VA_ARGS__); \
  } while (false)

// A {DecodeResult} only stores the failure / success status, but no data.
using DecodeResult = VoidResult;

struct WasmFunction;

class ITracer {
 public:
  static constexpr ITracer* NoTrace = nullptr;

  // Hooks for extracting byte offsets of things.
  virtual void TypeOffset(uint32_t offset) = 0;
  virtual void ImportOffset(uint32_t offset) = 0;
  virtual void ImportsDone(const WasmModule* module) = 0;
  virtual void TableOffset(uint32_t offset) = 0;
  virtual void MemoryOffset(uint32_t offset) = 0;
  virtual void TagOffset(uint32_t offset) = 0;
  virtual void GlobalOffset(uint32_t offset) = 0;
  virtual void StartOffset(uint32_t offset) = 0;
  virtual void ElementOffset(uint32_t offset) = 0;
  virtual void DataOffset(uint32_t offset) = 0;
  virtual void StringOffset(uint32_t offset) = 0;
  virtual void RecGroupOffset(uint32_t offset, uint32_t group_size) = 0;

  // Hooks for annotated hex dumps.
  virtual void Bytes(const uint8_t* start, uint32_t count) = 0;

  virtual void Description(const char* desc) = 0;
  virtual void Description(const char* desc, size_t length) = 0;
  virtual void Description(uint32_t number) = 0;
  virtual void Description(ValueType type) = 0;
  virtual void Description(HeapType type) = 0;
  virtual void Description(const FunctionSig* sig) = 0;

  virtual void NextLine() = 0;
  virtual void NextLineIfFull() = 0;
  virtual void NextLineIfNonEmpty() = 0;

  virtual void InitializerExpression(const uint8_t* start, const uint8_t* end,
                                     ValueType expected_type) = 0;
  virtual void FunctionBody(const WasmFunction* func, const uint8_t* start) = 0;
  virtual void FunctionName(uint32_t func_index) = 0;
  virtual void NameSection(const uint8_t* start, const uint8_t* end,
                           uint32_t offset) = 0;

  virtual ~ITracer() = default;
};

// A helper utility to decode bytes, integers, fields, varints, etc, from
// a buffer of bytes.
class Decoder {
 public:
  // Don't run validation, assume valid input.
  static constexpr struct NoValidationTag {
    static constexpr bool validate = false;
    static constexpr bool full_validation = false;
  } kNoValidation = {};
  // Run validation but only store a generic error.
  static constexpr struct BooleanValidationTag {
    static constexpr bool validate = true;
    static constexpr bool full_validation = false;
  } kBooleanValidation = {};
  // Run full validation with error message and location.
  static constexpr struct FullValidationTag {
    static constexpr bool validate = true;
    static constexpr bool full_validation = true;
  } kFullValidation = {};

  struct NoName {
    constexpr NoName(const char*) {}
    operator const char*() const { UNREACHABLE(); }
  };
  // Pass a {NoName} if we know statically that we do not use it anyway (we are
  // not tracing (in release mode) and not running full validation).
#ifdef DEBUG
  template <typename ValidationTag>
  using Name = const char*;
#else
  template <typename ValidationTag>
  using Name =
      std::conditional_t<ValidationTag::full_validation, const char*, NoName>;
#endif

  enum TraceFlag : bool { kTrace = true, kNoTrace = false };

  Decoder(const uint8_t* start, const uint8_t* end, uint32_t buffer_offset = 0)
      : Decoder(start, start, end, buffer_offset) {}
  explicit Decoder(const base::Vector<const uint8_t> bytes,
                   uint32_t buffer_offset = 0)
      : Decoder(bytes.begin(), bytes.begin() + bytes.length(), buffer_offset) {}
  Decoder(const uint8_t* start, const uint8_t* pc, const uint8_t* end,
          uint32_t buffer_offset = 0)
      : start_(start), pc_(pc), end_(end), buffer_offset_(buffer_offset) {
    DCHECK_LE(start, pc);
    DCHECK_LE(pc, end);
    DCHECK_EQ(static_cast<uint32_t>(end - start), end - start);
  }

  virtual ~Decoder() = default;

  // Reads an 8-bit unsigned integer.
  template <typename ValidationTag>
  uint8_t read_u8(const uint8_t* pc,
                  Name<ValidationTag> msg = "expected 1 byte") {
    return read_little_endian<uint8_t, ValidationTag>(pc, msg);
  }

  // Reads a 16-bit unsigned integer (little endian).
  template <typename ValidationTag>
  uint16_t read_u16(const uint8_t* pc,
                    Name<ValidationTag> msg = "expected 2 bytes") {
    return read_little_endian<uint16_t, ValidationTag>(pc, msg);
  }

  // Reads a 32-bit unsigned integer (little endian).
  template <typename ValidationTag>
  uint32_t read_u32(const uint8_t* pc,
                    Name<ValidationTag> msg = "expected 4 bytes") {
    return read_little_endian<uint32_t, ValidationTag>(pc, msg);
  }

  // Reads a 64-bit unsigned integer (little endian).
  template <typename ValidationTag>
  uint64_t read_u64(const uint8_t* pc,
                    Name<ValidationTag> msg = "expected 8 bytes") {
    return read_little_endian<uint64_t, ValidationTag>(pc, msg);
  }

  // Reads a variable-length unsigned integer (little endian). Returns the read
  // value and the number of bytes read.
  template <typename ValidationTag>
  std::pair<uint32_t, uint32_t> read_u32v(const uint8_t* pc,
                                          Name<ValidationTag> name = "LEB32") {
    return read_leb<uint32_t, ValidationTag, kNoTrace>(pc, name);
  }

  // Reads a variable-length signed integer (little endian). Returns the read
  // value and the number of bytes read.
  template <typename ValidationTag>
  std::pair<int32_t, uint32_t> read_i32v(
      const uint8_t* pc, Name<ValidationTag> name = "signed LEB32") {
    return read_leb<int32_t, ValidationTag, kNoTrace>(pc, name);
  }

  // Reads a variable-length unsigned integer (little endian). Returns the read
  // value and the number of bytes read.
  template <typename ValidationTag>
  std::pair<uint64_t, uint32_t> read_u64v(const uint8_t* pc,
                                          Name<ValidationTag> name = "LEB64") {
    return read_leb<uint64_t, ValidationTag, kNoTrace>(pc, name);
  }

  // Reads a variable-length signed integer (little endian). Returns the read
  // value and the number of bytes read.
  template <typename ValidationTag>
  std::pair<int64_t, uint32_t> read_i64v(
      const uint8_t* pc, Name<ValidationTag> name = "signed LEB64") {
    return read_leb<int64_t, ValidationTag, kNoTrace>(pc, name);
  }

  // Reads a variable-length 33-bit signed integer (little endian). Returns the
  // read value and the number of bytes read.
  template <typename ValidationTag>
  std::pair<int64_t, uint32_t> read_i33v(
      const uint8_t* pc, Name<ValidationTag> name = "signed LEB33") {
    return read_leb<int64_t, ValidationTag, kNoTrace, 33>(pc, name);
  }

  // Reads a prefixed-opcode, possibly with variable-length index.
  // Returns the read opcode and the number of bytes that make up this opcode,
  // *including* the prefix byte. For most opcodes, it will be 2.
  template <typename ValidationTag>
  std::pair<WasmOpcode, uint32_t> read_prefixed_opcode(
      const uint8_t* pc, Name<ValidationTag> name = "prefixed opcode") {
    // Prefixed opcodes all use LEB128 encoding.
    auto [index, index_length] =
        read_u32v<ValidationTag>(pc + 1, "prefixed opcode index");
    uint32_t length = index_length + 1;  // 1 for prefix byte.
    // Only support opcodes that go up to 0xFFF (when decoded). Anything
    // bigger will need more than 2 bytes, and the '<< 12' below will be wrong.
    if (ValidationTag::validate && V8_UNLIKELY(index > 0xfff)) {
      errorf(pc, "Invalid prefixed opcode %d", index);
      // On validation failure we return "unreachable" (opcode 0).
      static_assert(kExprUnreachable == 0);
      return {kExprUnreachable, 0};
    }

    if (index > 0xff) {
      return {static_cast<WasmOpcode>((*pc) << 12 | index), length};
    }

    return {static_cast<WasmOpcode>((*pc) << 8 | index), length};
  }

  // Reads a 8-bit unsigned integer (byte) and advances {pc_}.
  uint8_t consume_u8(const char* name = "uint8_t") {
    return consume_little_endian<uint8_t, kTrace>(name);
  }
  uint8_t consume_u8(const char* name, ITracer* tracer) {
    if (tracer) {
      tracer->Bytes(pc_, sizeof(uint8_t));
      tracer->Description(name);
    }
    return consume_little_endian<uint8_t, kNoTrace>(name);
  }

  // Reads a 16-bit unsigned integer (little endian) and advances {pc_}.
  uint16_t consume_u16(const char* name = "uint16_t") {
    return consume_little_endian<uint16_t, kTrace>(name);
  }

  // Reads a single 32-bit unsigned integer (little endian) and advances {pc_}.
  uint32_t consume_u32(const char* name, ITracer* tracer) {
    if (tracer) {
      tracer->Bytes(pc_, sizeof(uint32_t));
      tracer->Description(name);
    }
    return consume_little_endian<uint32_t, kNoTrace>(name);
  }

  // Reads a LEB128 variable-length unsigned 32-bit integer and advances {pc_}.
  uint32_t consume_u32v(const char* name = "var_uint32") {
    auto [result, length] =
        read_leb<uint32_t, FullValidationTag, kTrace>(pc_, name);
    pc_ += length;
    return result;
  }
  uint32_t consume_u32v(const char* name, ITracer* tracer) {
    auto [result, length] =
        read_leb<uint32_t, FullValidationTag, kNoTrace>(pc_, name);
    if (tracer) {
      tracer->Bytes(pc_, length);
      tracer->Description(name);
    }
    pc_ += length;
    return result;
  }

  // Reads a LEB128 variable-length signed 32-bit integer and advances {pc_}.
  int32_t consume_i32v(const char* name = "var_int32") {
    auto [result, length] =
        read_leb<int32_t, FullValidationTag, kTrace>(pc_, name);
    pc_ += length;
    return result;
  }

  // Reads a LEB128 variable-length unsigned 64-bit integer and advances {pc_}.
  uint64_t consume_u64v(const char* name, ITracer* tracer) {
    auto [result, length] =
        read_leb<uint64_t, FullValidationTag, kNoTrace>(pc_, name);
    if (tracer) {
      tracer->Bytes(pc_, length);
      tracer->Description(name);
    }
    pc_ += length;
    return result;
  }

  // Reads a LEB128 variable-length signed 64-bit integer and advances {pc_}.
  int64_t consume_i64v(const char* name = "var_int64") {
    auto [result, length] =
        read_leb<int64_t, FullValidationTag, kTrace>(pc_, name);
    pc_ += length;
    return result;
  }

  // Consume {size} bytes and send them to the bit bucket, advancing {pc_}.
  void consume_bytes(uint32_t size, const char* name = "skip") {
    // Only trace if the name is not null.
    TRACE_IF(name, "  +%u  %-20s: %u bytes\n", pc_offset(), name, size);
    if (checkAvailable(size)) {
      pc_ += size;
    } else {
      pc_ = end_;
    }
  }
  void consume_bytes(uint32_t size, const char* name, ITracer* tracer) {
    if (tracer) {
      tracer->Bytes(pc_, size);
      tracer->Description(name);
    }
    consume_bytes(size, nullptr);
  }

  uint32_t available_bytes() const {
    DCHECK_LE(pc_, end_);
    DCHECK_GE(kMaxUInt32, end_ - pc_);
    return static_cast<uint32_t>(end_ - pc_);
  }

  // Check that at least {size} bytes exist between {pc_} and {end_}.
  bool checkAvailable(uint32_t size) {
    if (V8_UNLIKELY(size > available_bytes())) {
      errorf(pc_, "expected %u bytes, fell off end", size);
      return false;
    }
    return true;
  }

  // Use this for "boolean validation", i.e. if the error message is not used
  // anyway.
  void V8_NOINLINE V8_PRESERVE_MOST MarkError() {
    if (!ok()) return;
    error_ = {0, "validation failed"};
    onFirstError();
  }

  // Do not inline error methods. This has measurable impact on validation time,
  // see https://crbug.com/910432.
  void V8_NOINLINE V8_PRESERVE_MOST error(const char* msg) {
    errorf(pc_offset(), "%s", msg);
  }
  void V8_NOINLINE V8_PRESERVE_MOST error(const uint8_t* pc, const char* msg) {
    errorf(pc_offset(pc), "%s", msg);
  }
  void V8_NOINLINE V8_PRESERVE_MOST error(uint32_t offset, const char* msg) {
    errorf(offset, "%s", msg);
  }

  template <typename... Args>
  void V8_NOINLINE V8_PRESERVE_MOST errorf(const char* format, Args... args) {
    errorf(pc_offset(), format, args...);
  }

  template <typename... Args>
  void V8_NOINLINE V8_PRESERVE_MOST errorf(const uint8_t* pc,
                                           const char* format, Args... args) {
    errorf(pc_offset(pc), format, args...);
  }

  template <typename... Args>
  void V8_NOINLINE V8_PRESERVE_MOST errorf(uint32_t offset, const char* format,
                                           Args... args) {
    static_assert(
        sizeof...(Args) > 0,
        "Use error instead of errorf if the format string has no placeholders");
    verrorf(offset, format, args...);
  }

  // Behavior triggered on first error, overridden in subclasses.
  virtual void onFirstError() {}

  // Debugging helper to print a bytes range as hex bytes.
  void traceByteRange(const uint8_t* start, const uint8_t* end) {
    DCHECK_LE(start, end);
    for (const uint8_t* p = start; p < end; ++p) TRACE("%02x ", *p);
  }

  // Debugging helper to print bytes up to the end.
  void traceOffEnd() {
    traceByteRange(pc_, end_);
    TRACE("<end>\n");
  }

  // Converts the given value to a {Result}, copying the error if necessary.
  template <typename T, typename R = std::decay_t<T>>
  Result<R> toResult(T&& val) {
    if (failed()) {
      TRACE("Result error: %s\n", error_.message().c_str());
      return Result<R>{error_};
    }
    return Result<R>{std::forward<T>(val)};
  }

  // Resets the boundaries of this decoder.
  void Reset(const uint8_t* start, const uint8_t* end,
             uint32_t buffer_offset = 0) {
    DCHECK_LE(start, end);
    DCHECK_EQ(static_cast<uint32_t>(end - start), end - start);
    start_ = start;
    pc_ = start;
    end_ = end;
    buffer_offset_ = buffer_offset;
    error_ = {};
  }

  void Reset(base::Vector<const uint8_t> bytes, uint32_t buffer_offset = 0) {
    Reset(bytes.begin(), bytes.end(), buffer_offset);
  }

  bool ok() const { return !failed(); }
  bool failed() const { return error_.has_error(); }
  bool more() const { return pc_ < end_; }
  const WasmError& error() const { return error_; }

  const uint8_t* start() const { return start_; }
  const uint8_t* pc() const { return pc_; }
  uint32_t V8_INLINE position() const {
    return static_cast<uint32_t>(pc_ - start_);
  }
  // This needs to be inlined for performance (see https://crbug.com/910432).
  uint32_t V8_INLINE pc_offset(const uint8_t* pc) const {
    DCHECK_LE(start_, pc);
    DCHECK_GE(kMaxUInt32 - buffer_offset_, pc - start_);
    return static_cast<uint32_t>(pc - start_) + buffer_offset_;
  }
  uint32_t pc_offset() const { return pc_offset(pc_); }
  uint32_t buffer_offset() const { return buffer_offset_; }
  // Takes an offset relative to the module start and returns an offset relative
  // to the current buffer of the decoder.
  uint32_t GetBufferRelativeOffset(uint32_t offset) const {
    DCHECK_LE(buffer_offset_, offset);
    return offset - buffer_offset_;
  }
  const uint8_t* end() const { return end_; }
  void set_end(const uint8_t* end) { end_ = end; }

  // Check if the uint8_t at {offset} from the current pc equals {expected}.
  bool lookahead(int offset, uint8_t expected) {
    DCHECK_LE(pc_, end_);
    return end_ - pc_ > offset && pc_[offset] == expected;
  }

 protected:
  const uint8_t* start_;
  const uint8_t* pc_;
  const uint8_t* end_;
  // The offset of the current buffer in the module. Needed for streaming.
  uint32_t buffer_offset_;
  WasmError error_;

 private:
  void V8_NOINLINE PRINTF_FORMAT(3, 4)
      verrorf(uint32_t offset, const char* format, ...) {
    // Only report the first error.
    if (!ok()) return;
    constexpr int kMaxErrorMsg = 256;
    base::EmbeddedVector<char, kMaxErrorMsg> buffer;
    va_list args;
    va_start(args, format);
    int len = base::VSNPrintF(buffer, format, args);
    va_end(args);
    CHECK_LT(0, len);
    error_ = {offset, {buffer.begin(), static_cast<size_t>(len)}};
    onFirstError();
  }

  template <typename IntType, typename ValidationTag>
  IntType read_little_endian(const uint8_t* pc, Name<ValidationTag> msg) {
    DCHECK_LE(start_, pc);

    if (!ValidationTag::validate) {
      DCHECK_LE(pc, end_);
      DCHECK_LE(sizeof(IntType), end_ - pc);
    } else if (V8_UNLIKELY(ptrdiff_t{sizeof(IntType)} > end_ - pc)) {
      if (ValidationTag::full_validation) {
        error(pc, msg);
      } else {
        MarkError();
      }
      return 0;
    }
    return base::ReadLittleEndianValue<IntType>(reinterpret_cast<Address>(pc));
  }

  template <typename IntType, TraceFlag trace>
  IntType consume_little_endian(const char* name) {
    TRACE_IF(trace, "  +%u  %-20s: ", pc_offset(), name);
    if (!checkAvailable(sizeof(IntType))) {
      traceOffEnd();
      pc_ = end_;
      return IntType{0};
    }
    IntType val = read_little_endian<IntType, NoValidationTag>(pc_, name);
    traceByteRange(pc_, pc_ + sizeof(IntType));
    TRACE_IF(trace, "= %d\n", val);
    pc_ += sizeof(IntType);
    return val;
  }

  // The implementation of LEB-decoding; returns the value and the number of
  // bytes read.
  template <typename IntType, typename ValidationTag, TraceFlag trace,
            size_t size_in_bits = 8 * sizeof(IntType)>
  V8_INLINE std::pair<IntType, uint32_t> read_leb(
      const uint8_t* pc, Name<ValidationTag> name = "varint") {
    static_assert(size_in_bits <= 8 * sizeof(IntType),
                  "leb does not fit in type");
    TRACE_IF(trace, "  +%u  %-20s: ", pc_offset(),
             implicit_cast<const char*>(name));
    // Fast path for single-byte integers.
    if (V8_LIKELY((!ValidationTag::validate || pc < end_) && !(*pc & 0x80))) {
      TRACE_IF(trace, "%02x ", *pc);
      IntType result = *pc;
      if (std::is_signed<IntType>::value) {
        // Perform sign extension.
        constexpr int sign_ext_shift = int{8 * sizeof(IntType)} - 7;
        result = (result << sign_ext_shift) >> sign_ext_shift;
        TRACE_IF(trace, "= %" PRIi64 "\n", static_cast<int64_t>(result));
      } else {
        TRACE_IF(trace, "= %" PRIu64 "\n", static_cast<uint64_t>(result));
      }
      return {result, 1};
    }
    auto [result, length] =
        read_leb_slowpath<IntType, ValidationTag, trace, size_in_bits>(pc,
                                                                       name);
    V8_ASSUME(length >= 0 && length <= (size_in_bits + 6) / 7);
    V8_ASSUME(ValidationTag::validate || length >= 1);
    return {result, length};
  }

  template <typename IntType, typename ValidationTag, TraceFlag trace,
            size_t size_in_bits = 8 * sizeof(IntType)>
  V8_NOINLINE V8_PRESERVE_MOST std::pair<IntType, uint32_t> read_leb_slowpath(
      const uint8_t* pc, Name<ValidationTag> name) {
    // Create an unrolled LEB decoding function per integer type.
    return read_leb_tail<IntType, ValidationTag, trace, size_in_bits, 0>(
        pc, name, 0);
  }

  template <typename IntType, typename ValidationTag, TraceFlag trace,
            size_t size_in_bits, int byte_index>
  V8_INLINE std::pair<IntType, uint32_t> read_leb_tail(
      const uint8_t* pc, Name<ValidationTag> name,
      IntType intermediate_result) {
    constexpr bool is_signed = std::is_signed<IntType>::value;
    constexpr int kMaxLength = (size_in_bits + 6) / 7;
    static_assert(byte_index < kMaxLength, "invalid template instantiation");
    constexpr int shift = byte_index * 7;
    constexpr bool is_last_byte = byte_index == kMaxLength - 1;
    const bool at_end = ValidationTag::validate && pc >= end_;
    uint8_t b = 0;
    if (V8_LIKELY(!at_end)) {
      DCHECK_LT(pc, end_);
      b = *pc;
      TRACE_IF(trace, "%02x ", b);
      using Unsigned = typename std::make_unsigned<IntType>::type;
      intermediate_result |=
          (static_cast<Unsigned>(static_cast<IntType>(b) & 0x7f) << shift);
    }
    if (!is_last_byte && (b & 0x80)) {
      // Make sure that we only instantiate the template for valid byte indexes.
      // Compilers are not smart enough to figure out statically that the
      // following call is unreachable if is_last_byte is false.
      constexpr int next_byte_index = byte_index + (is_last_byte ? 0 : 1);
      return read_leb_tail<IntType, ValidationTag, trace, size_in_bits,
                           next_byte_index>(pc + 1, name, intermediate_result);
    }
    if (ValidationTag::validate && V8_UNLIKELY(at_end || (b & 0x80))) {
      TRACE_IF(trace, at_end ? "<end> " : "<length overflow> ");
      if constexpr (ValidationTag::full_validation) {
        errorf(pc, "%s while decoding %s",
               at_end ? "reached end" : "length overflow", name);
      } else {
        MarkError();
      }
      return {0, 0};
    }
    if constexpr (is_last_byte) {
      // A signed-LEB128 must sign-extend the final byte, excluding its
      // most-significant bit; e.g. for a 32-bit LEB128:
      //   kExtraBits = 4  (== 32 - (5-1) * 7)
      // For unsigned values, the extra bits must be all zero.
      // For signed values, the extra bits *plus* the most significant bit must
      // either be 0, or all ones.
      constexpr int kExtraBits = size_in_bits - ((kMaxLength - 1) * 7);
      constexpr int kSignExtBits = kExtraBits - (is_signed ? 1 : 0);
      const uint8_t checked_bits = b & (0xFF << kSignExtBits);
      constexpr uint8_t kSignExtendedExtraBits = 0x7f & (0xFF << kSignExtBits);
      const bool valid_extra_bits =
          checked_bits == 0 ||
          (is_signed && checked_bits == kSignExtendedExtraBits);
      if (!ValidationTag::validate) {
        DCHECK(valid_extra_bits);
      } else if (V8_UNLIKELY(!valid_extra_bits)) {
        if (ValidationTag::full_validation) {
          error(pc, "extra bits in varint");
        } else {
          MarkError();
        }
        return {0, 0};
      }
    }
    constexpr int sign_ext_shift =
        is_signed ? std::max(0, int{8 * sizeof(IntType)} - shift - 7) : 0;
    // Perform sign extension.
    intermediate_result =
        (intermediate_result << sign_ext_shift) >> sign_ext_shift;
    if (trace && is_signed) {
      TRACE("= %" PRIi64 "\n", static_cast<int64_t>(intermediate_result));
    } else if (trace) {
      TRACE("= %" PRIu64 "\n", static_cast<uint64_t>(intermediate_result));
    }
    const uint32_t length = byte_index + 1;
    return {intermediate_result, length};
  }
};

#undef TRACE
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_DECODER_H_
