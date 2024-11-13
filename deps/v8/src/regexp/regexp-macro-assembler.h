// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_MACRO_ASSEMBLER_H_
#define V8_REGEXP_REGEXP_MACRO_ASSEMBLER_H_

#include "src/base/strings.h"
#include "src/execution/frame-constants.h"
#include "src/objects/fixed-array.h"
#include "src/regexp/regexp-ast.h"
#include "src/regexp/regexp.h"

namespace v8 {
namespace internal {

class ByteArray;
class JSRegExp;
class Label;
class String;

static const base::uc32 kLeadSurrogateStart = 0xd800;
static const base::uc32 kLeadSurrogateEnd = 0xdbff;
static const base::uc32 kTrailSurrogateStart = 0xdc00;
static const base::uc32 kTrailSurrogateEnd = 0xdfff;
static const base::uc32 kNonBmpStart = 0x10000;
static const base::uc32 kNonBmpEnd = 0x10ffff;

class RegExpMacroAssembler {
 public:
  // The implementation must be able to handle at least:
  static constexpr int kMaxRegisterCount = (1 << 16);
  static constexpr int kMaxRegister = kMaxRegisterCount - 1;
  static constexpr int kMaxCaptures = (kMaxRegister - 1) / 2;
  static constexpr int kMaxCPOffset = (1 << 15) - 1;
  static constexpr int kMinCPOffset = -(1 << 15);

  static constexpr int kTableSizeBits = 7;
  static constexpr int kTableSize = 1 << kTableSizeBits;
  static constexpr int kTableMask = kTableSize - 1;

  static constexpr int kUseCharactersValue = -1;

  RegExpMacroAssembler(Isolate* isolate, Zone* zone);
  virtual ~RegExpMacroAssembler() = default;

  virtual Handle<HeapObject> GetCode(Handle<String> source) = 0;

  // This function is called when code generation is aborted, so that
  // the assembler could clean up internal data structures.
  virtual void AbortedCodeGeneration() {}
  // The maximal number of pushes between stack checks. Users must supply
  // kCheckStackLimit flag to push operations (instead of kNoStackLimitCheck)
  // at least once for every stack_limit() pushes that are executed.
  virtual int stack_limit_slack() = 0;
  virtual bool CanReadUnaligned() const = 0;

  virtual void AdvanceCurrentPosition(int by) = 0;  // Signed cp change.
  virtual void AdvanceRegister(int reg, int by) = 0;  // r[reg] += by.
  // Continues execution from the position pushed on the top of the backtrack
  // stack by an earlier PushBacktrack(Label*).
  virtual void Backtrack() = 0;
  virtual void Bind(Label* label) = 0;
  // Dispatch after looking the current character up in a 2-bits-per-entry
  // map.  The destinations vector has up to 4 labels.
  virtual void CheckCharacter(unsigned c, Label* on_equal) = 0;
  // Bitwise and the current character with the given constant and then
  // check for a match with c.
  virtual void CheckCharacterAfterAnd(unsigned c,
                                      unsigned and_with,
                                      Label* on_equal) = 0;
  virtual void CheckCharacterGT(base::uc16 limit, Label* on_greater) = 0;
  virtual void CheckCharacterLT(base::uc16 limit, Label* on_less) = 0;
  virtual void CheckGreedyLoop(Label* on_tos_equals_current_position) = 0;
  virtual void CheckAtStart(int cp_offset, Label* on_at_start) = 0;
  virtual void CheckNotAtStart(int cp_offset, Label* on_not_at_start) = 0;
  virtual void CheckNotBackReference(int start_reg, bool read_backward,
                                     Label* on_no_match) = 0;
  virtual void CheckNotBackReferenceIgnoreCase(int start_reg,
                                               bool read_backward, bool unicode,
                                               Label* on_no_match) = 0;
  // Check the current character for a match with a literal character.  If we
  // fail to match then goto the on_failure label.  End of input always
  // matches.  If the label is nullptr then we should pop a backtrack address
  // off the stack and go to that.
  virtual void CheckNotCharacter(unsigned c, Label* on_not_equal) = 0;
  virtual void CheckNotCharacterAfterAnd(unsigned c,
                                         unsigned and_with,
                                         Label* on_not_equal) = 0;
  // Subtract a constant from the current character, then and with the given
  // constant and then check for a match with c.
  virtual void CheckNotCharacterAfterMinusAnd(base::uc16 c, base::uc16 minus,
                                              base::uc16 and_with,
                                              Label* on_not_equal) = 0;
  virtual void CheckCharacterInRange(base::uc16 from,
                                     base::uc16 to,  // Both inclusive.
                                     Label* on_in_range) = 0;
  virtual void CheckCharacterNotInRange(base::uc16 from,
                                        base::uc16 to,  // Both inclusive.
                                        Label* on_not_in_range) = 0;
  // Returns true if the check was emitted, false otherwise.
  virtual bool CheckCharacterInRangeArray(
      const ZoneList<CharacterRange>* ranges, Label* on_in_range) = 0;
  virtual bool CheckCharacterNotInRangeArray(
      const ZoneList<CharacterRange>* ranges, Label* on_not_in_range) = 0;

  // The current character (modulus the kTableSize) is looked up in the byte
  // array, and if the found byte is non-zero, we jump to the on_bit_set label.
  virtual void CheckBitInTable(Handle<ByteArray> table, Label* on_bit_set) = 0;

  // Checks whether the given offset from the current position is before
  // the end of the string.  May overwrite the current character.
  virtual void CheckPosition(int cp_offset, Label* on_outside_input);
  // Check whether a standard/default character class matches the current
  // character. Returns false if the type of special character class does
  // not have custom support.
  // May clobber the current loaded character.
  virtual bool CheckSpecialClassRanges(StandardCharacterSet type,
                                       Label* on_no_match) {
    return false;
  }

  // Control-flow integrity:
  // Define a jump target and bind a label.
  virtual void BindJumpTarget(Label* label) { Bind(label); }

  virtual void Fail() = 0;
  virtual void GoTo(Label* label) = 0;
  // Check whether a register is >= a given constant and go to a label if it
  // is.  Backtracks instead if the label is nullptr.
  virtual void IfRegisterGE(int reg, int comparand, Label* if_ge) = 0;
  // Check whether a register is < a given constant and go to a label if it is.
  // Backtracks instead if the label is nullptr.
  virtual void IfRegisterLT(int reg, int comparand, Label* if_lt) = 0;
  // Check whether a register is == to the current position and go to a
  // label if it is.
  virtual void IfRegisterEqPos(int reg, Label* if_eq) = 0;
  V8_EXPORT_PRIVATE void LoadCurrentCharacter(
      int cp_offset, Label* on_end_of_input, bool check_bounds = true,
      int characters = 1, int eats_at_least = kUseCharactersValue);
  virtual void LoadCurrentCharacterImpl(int cp_offset, Label* on_end_of_input,
                                        bool check_bounds, int characters,
                                        int eats_at_least) = 0;
  virtual void PopCurrentPosition() = 0;
  virtual void PopRegister(int register_index) = 0;
  // Pushes the label on the backtrack stack, so that a following Backtrack
  // will go to this label. Always checks the backtrack stack limit.
  virtual void PushBacktrack(Label* label) = 0;
  virtual void PushCurrentPosition() = 0;
  enum StackCheckFlag { kNoStackLimitCheck = false, kCheckStackLimit = true };
  virtual void PushRegister(int register_index,
                            StackCheckFlag check_stack_limit) = 0;
  virtual void ReadCurrentPositionFromRegister(int reg) = 0;
  virtual void ReadStackPointerFromRegister(int reg) = 0;
  virtual void SetCurrentPositionFromEnd(int by) = 0;
  virtual void SetRegister(int register_index, int to) = 0;
  // Return whether the matching (with a global regexp) will be restarted.
  virtual bool Succeed() = 0;
  virtual void WriteCurrentPositionToRegister(int reg, int cp_offset) = 0;
  virtual void ClearRegisters(int reg_from, int reg_to) = 0;
  virtual void WriteStackPointerToRegister(int reg) = 0;

  // Check that we are not in the middle of a surrogate pair.
  void CheckNotInSurrogatePair(int cp_offset, Label* on_failure);

#define IMPLEMENTATIONS_LIST(V) \
  V(IA32)                       \
  V(ARM)                        \
  V(ARM64)                      \
  V(MIPS)                       \
  V(LOONG64)                    \
  V(RISCV)                      \
  V(RISCV32)                    \
  V(S390)                       \
  V(PPC)                        \
  V(X64)                        \
  V(Bytecode)

  enum IrregexpImplementation {
#define V(Name) k##Name##Implementation,
    IMPLEMENTATIONS_LIST(V)
#undef V
  };

  inline const char* ImplementationToString(IrregexpImplementation impl) {
    static const char* const kNames[] = {
#define V(Name) #Name,
        IMPLEMENTATIONS_LIST(V)
#undef V
    };
    return kNames[impl];
  }
#undef IMPLEMENTATIONS_LIST
  virtual IrregexpImplementation Implementation() = 0;

  // Compare two-byte strings case insensitively.
  //
  // Called from generated code.
  static int CaseInsensitiveCompareNonUnicode(Address byte_offset1,
                                              Address byte_offset2,
                                              size_t byte_length,
                                              Isolate* isolate);
  static int CaseInsensitiveCompareUnicode(Address byte_offset1,
                                           Address byte_offset2,
                                           size_t byte_length,
                                           Isolate* isolate);

  // `raw_byte_array` is a ByteArray containing a set of character ranges,
  // where ranges are encoded as uint16_t elements:
  //
  //  [from0, to0, from1, to1, ..., fromN, toN], or
  //  [from0, to0, from1, to1, ..., fromN]  (open-ended last interval).
  //
  // fromN is inclusive, toN is exclusive. Returns zero if not in a range,
  // non-zero otherwise.
  //
  // Called from generated code.
  static uint32_t IsCharacterInRangeArray(uint32_t current_char,
                                          Address raw_byte_array);

  // Controls the generation of large inlined constants in the code.
  void set_slow_safe(bool ssc) { slow_safe_compiler_ = ssc; }
  bool slow_safe() const { return slow_safe_compiler_; }

  // Controls after how many backtracks irregexp should abort execution.  If it
  // can fall back to the experimental engine (see `set_can_fallback`), it will
  // return the appropriate error code, otherwise it will return the number of
  // matches found so far (perhaps none).
  void set_backtrack_limit(uint32_t backtrack_limit) {
    backtrack_limit_ = backtrack_limit;
  }

  // Set whether or not irregexp can fall back to the experimental engine on
  // excessive backtracking.  The number of backtracks considered excessive can
  // be controlled with set_backtrack_limit.
  void set_can_fallback(bool val) { can_fallback_ = val; }

  enum GlobalMode {
    NOT_GLOBAL,
    GLOBAL_NO_ZERO_LENGTH_CHECK,
    GLOBAL,
    GLOBAL_UNICODE
  };
  // Set whether the regular expression has the global flag.  Exiting due to
  // a failure in a global regexp may still mean success overall.
  inline void set_global_mode(GlobalMode mode) { global_mode_ = mode; }
  inline bool global() const { return global_mode_ != NOT_GLOBAL; }
  inline bool global_with_zero_length_check() const {
    return global_mode_ == GLOBAL || global_mode_ == GLOBAL_UNICODE;
  }
  inline bool global_unicode() const { return global_mode_ == GLOBAL_UNICODE; }

  Isolate* isolate() const { return isolate_; }
  Zone* zone() const { return zone_; }

 protected:
  bool has_backtrack_limit() const;
  uint32_t backtrack_limit() const { return backtrack_limit_; }

  bool can_fallback() const { return can_fallback_; }

 private:
  bool slow_safe_compiler_;
  uint32_t backtrack_limit_;
  bool can_fallback_ = false;
  GlobalMode global_mode_;
  Isolate* const isolate_;
  Zone* const zone_;
};

class NativeRegExpMacroAssembler: public RegExpMacroAssembler {
 public:
  // Type of input string to generate code for.
  enum Mode { LATIN1 = 1, UC16 = 2 };

  // Result of calling generated native RegExp code.
  // RETRY: Something significant changed during execution, and the matching
  //        should be retried from scratch.
  // EXCEPTION: Something failed during execution. If no exception has been
  //            thrown, it's an internal out-of-memory, and the caller should
  //            throw the exception.
  // FAILURE: Matching failed.
  // SUCCESS: Matching succeeded, and the output array has been filled with
  //          capture positions.
  // FALLBACK_TO_EXPERIMENTAL: Execute the regexp on this subject using the
  //                           experimental engine instead.
  enum Result {
    FAILURE = RegExp::kInternalRegExpFailure,
    SUCCESS = RegExp::kInternalRegExpSuccess,
    EXCEPTION = RegExp::kInternalRegExpException,
    RETRY = RegExp::kInternalRegExpRetry,
    FALLBACK_TO_EXPERIMENTAL = RegExp::kInternalRegExpFallbackToExperimental,
    SMALLEST_REGEXP_RESULT = RegExp::kInternalRegExpSmallestResult,
  };

  NativeRegExpMacroAssembler(Isolate* isolate, Zone* zone)
      : RegExpMacroAssembler(isolate, zone), range_array_cache_(zone) {}
  ~NativeRegExpMacroAssembler() override = default;

  // Returns a {Result} sentinel, or the number of successful matches.
  static int Match(DirectHandle<IrRegExpData> regexp_data,
                   DirectHandle<String> subject, int* offsets_vector,
                   int offsets_vector_length, int previous_index,
                   Isolate* isolate);

  V8_EXPORT_PRIVATE static int ExecuteForTesting(
      Tagged<String> input, int start_offset, const uint8_t* input_start,
      const uint8_t* input_end, int* output, int output_size, Isolate* isolate,
      Tagged<JSRegExp> regexp);

  bool CanReadUnaligned() const override;

  void LoadCurrentCharacterImpl(int cp_offset, Label* on_end_of_input,
                                bool check_bounds, int characters,
                                int eats_at_least) override;
  // Load a number of characters at the given offset from the
  // current position, into the current-character register.
  virtual void LoadCurrentCharacterUnchecked(int cp_offset,
                                             int character_count) = 0;

  // Called from RegExp if the backtrack stack limit is hit. Tries to expand
  // the stack. Returns the new stack-pointer if successful, or returns 0 if
  // unable to grow the stack.
  // This function must not trigger a garbage collection.
  //
  // Called from generated code.
  static Address GrowStack(Isolate* isolate);

  // Called from generated code.
  static int CheckStackGuardState(Isolate* isolate, int start_index,
                                  RegExp::CallOrigin call_origin,
                                  Address* return_address,
                                  Tagged<InstructionStream> re_code,
                                  Address* subject, const uint8_t** input_start,
                                  const uint8_t** input_end, uintptr_t gap);

  static Address word_character_map_address() {
    return reinterpret_cast<Address>(&word_character_map[0]);
  }

 protected:
  // Byte map of one byte characters with a 0xff if the character is a word
  // character (digit, letter or underscore) and 0x00 otherwise.
  // Used by generated RegExp code.
  static const uint8_t word_character_map[256];

  Handle<ByteArray> GetOrAddRangeArray(const ZoneList<CharacterRange>* ranges);

 private:
  // Returns a {Result} sentinel, or the number of successful matches.
  static int Execute(Tagged<String> input, int start_offset,
                     const uint8_t* input_start, const uint8_t* input_end,
                     int* output, int output_size, Isolate* isolate,
                     Tagged<IrRegExpData> regexp_data);

  ZoneUnorderedMap<uint32_t, Handle<FixedUInt16Array>> range_array_cache_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_MACRO_ASSEMBLER_H_
