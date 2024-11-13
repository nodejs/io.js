// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_COMPILER_H_
#define V8_REGEXP_REGEXP_COMPILER_H_

#include <bitset>

#include "src/base/small-vector.h"
#include "src/base/strings.h"
#include "src/regexp/regexp-flags.h"
#include "src/regexp/regexp-nodes.h"

namespace v8 {
namespace internal {

class DynamicBitSet;
class Isolate;

namespace regexp_compiler_constants {

// The '2' variant is has inclusive from and exclusive to.
// This covers \s as defined in ECMA-262 5.1, 15.10.2.12,
// which include WhiteSpace (7.2) or LineTerminator (7.3) values.
constexpr base::uc32 kRangeEndMarker = 0x110000;
constexpr int kSpaceRanges[] = {
    '\t',   '\r' + 1, ' ',    ' ' + 1, 0x00A0, 0x00A1, 0x1680,
    0x1681, 0x2000,   0x200B, 0x2028,  0x202A, 0x202F, 0x2030,
    0x205F, 0x2060,   0x3000, 0x3001,  0xFEFF, 0xFF00, kRangeEndMarker};
constexpr int kSpaceRangeCount = arraysize(kSpaceRanges);

constexpr int kWordRanges[] = {'0',     '9' + 1, 'A',     'Z' + 1,        '_',
                               '_' + 1, 'a',     'z' + 1, kRangeEndMarker};
constexpr int kWordRangeCount = arraysize(kWordRanges);
constexpr int kDigitRanges[] = {'0', '9' + 1, kRangeEndMarker};
constexpr int kDigitRangeCount = arraysize(kDigitRanges);
constexpr int kSurrogateRanges[] = {kLeadSurrogateStart,
                                    kLeadSurrogateStart + 1, kRangeEndMarker};
constexpr int kSurrogateRangeCount = arraysize(kSurrogateRanges);
constexpr int kLineTerminatorRanges[] = {0x000A, 0x000B, 0x000D,         0x000E,
                                         0x2028, 0x202A, kRangeEndMarker};
constexpr int kLineTerminatorRangeCount = arraysize(kLineTerminatorRanges);

// More makes code generation slower, less makes V8 benchmark score lower.
constexpr int kMaxLookaheadForBoyerMoore = 8;
// In a 3-character pattern you can maximally step forwards 3 characters
// at a time, which is not always enough to pay for the extra logic.
constexpr int kPatternTooShortForBoyerMoore = 2;

}  // namespace regexp_compiler_constants

inline bool NeedsUnicodeCaseEquivalents(RegExpFlags flags) {
  // Both unicode (or unicode sets) and ignore_case flags are set. We need to
  // use ICU to find the closure over case equivalents.
  return IsEitherUnicode(flags) && IsIgnoreCase(flags);
}

// Details of a quick mask-compare check that can look ahead in the
// input stream.
class QuickCheckDetails {
 public:
  QuickCheckDetails()
      : characters_(0), mask_(0), value_(0), cannot_match_(false) {}
  explicit QuickCheckDetails(int characters)
      : characters_(characters), mask_(0), value_(0), cannot_match_(false) {}
  bool Rationalize(bool one_byte);
  // Merge in the information from another branch of an alternation.
  void Merge(QuickCheckDetails* other, int from_index);
  // Advance the current position by some amount.
  void Advance(int by, bool one_byte);
  void Clear();
  bool cannot_match() { return cannot_match_; }
  void set_cannot_match() { cannot_match_ = true; }
  struct Position {
    Position() : mask(0), value(0), determines_perfectly(false) {}
    base::uc32 mask;
    base::uc32 value;
    bool determines_perfectly;
  };
  int characters() { return characters_; }
  void set_characters(int characters) { characters_ = characters; }
  Position* positions(int index) {
    DCHECK_LE(0, index);
    DCHECK_GT(characters_, index);
    return positions_ + index;
  }
  uint32_t mask() { return mask_; }
  uint32_t value() { return value_; }

 private:
  // How many characters do we have quick check information from.  This is
  // the same for all branches of a choice node.
  int characters_;
  Position positions_[4];
  // These values are the condensate of the above array after Rationalize().
  uint32_t mask_;
  uint32_t value_;
  // If set to true, there is no way this quick check can match at all.
  // E.g., if it requires to be at the start of the input, and isn't.
  bool cannot_match_;
};

// Improve the speed that we scan for an initial point where a non-anchored
// regexp can match by using a Boyer-Moore-like table. This is done by
// identifying non-greedy non-capturing loops in the nodes that eat any
// character one at a time.  For example in the middle of the regexp
// /foo[\s\S]*?bar/ we find such a loop.  There is also such a loop implicitly
// inserted at the start of any non-anchored regexp.
//
// When we have found such a loop we look ahead in the nodes to find the set of
// characters that can come at given distances. For example for the regexp
// /.?foo/ we know that there are at least 3 characters ahead of us, and the
// sets of characters that can occur are [any, [f, o], [o]]. We find a range in
// the lookahead info where the set of characters is reasonably constrained. In
// our example this is from index 1 to 2 (0 is not constrained). We can now
// look 3 characters ahead and if we don't find one of [f, o] (the union of
// [f, o] and [o]) then we can skip forwards by the range size (in this case 2).
//
// For Unicode input strings we do the same, but modulo 128.
//
// We also look at the first string fed to the regexp and use that to get a hint
// of the character frequencies in the inputs. This affects the assessment of
// whether the set of characters is 'reasonably constrained'.
//
// We also have another lookahead mechanism (called quick check in the code),
// which uses a wide load of multiple characters followed by a mask and compare
// to determine whether a match is possible at this point.
enum ContainedInLattice {
  kNotYet = 0,
  kLatticeIn = 1,
  kLatticeOut = 2,
  kLatticeUnknown = 3  // Can also mean both in and out.
};

inline ContainedInLattice Combine(ContainedInLattice a, ContainedInLattice b) {
  return static_cast<ContainedInLattice>(a | b);
}

class BoyerMoorePositionInfo : public ZoneObject {
 public:
  bool at(int i) const { return map_[i]; }

  static constexpr int kMapSize = 128;
  static constexpr int kMask = kMapSize - 1;

  int map_count() const { return map_count_; }

  void Set(int character);
  void SetInterval(const Interval& interval);
  void SetAll();

  bool is_non_word() { return w_ == kLatticeOut; }
  bool is_word() { return w_ == kLatticeIn; }

  using Bitset = std::bitset<kMapSize>;
  Bitset raw_bitset() const { return map_; }

 private:
  Bitset map_;
  int map_count_ = 0;               // Number of set bits in the map.
  ContainedInLattice w_ = kNotYet;  // The \w character class.
};

class BoyerMooreLookahead : public ZoneObject {
 public:
  BoyerMooreLookahead(int length, RegExpCompiler* compiler, Zone* zone);

  int length() { return length_; }
  int max_char() { return max_char_; }
  RegExpCompiler* compiler() { return compiler_; }

  int Count(int map_number) { return bitmaps_->at(map_number)->map_count(); }

  BoyerMoorePositionInfo* at(int i) { return bitmaps_->at(i); }

  void Set(int map_number, int character) {
    if (character > max_char_) return;
    BoyerMoorePositionInfo* info = bitmaps_->at(map_number);
    info->Set(character);
  }

  void SetInterval(int map_number, const Interval& interval) {
    if (interval.from() > max_char_) return;
    BoyerMoorePositionInfo* info = bitmaps_->at(map_number);
    if (interval.to() > max_char_) {
      info->SetInterval(Interval(interval.from(), max_char_));
    } else {
      info->SetInterval(interval);
    }
  }

  void SetAll(int map_number) { bitmaps_->at(map_number)->SetAll(); }

  void SetRest(int from_map) {
    for (int i = from_map; i < length_; i++) SetAll(i);
  }
  void EmitSkipInstructions(RegExpMacroAssembler* masm);

 private:
  // This is the value obtained by EatsAtLeast.  If we do not have at least this
  // many characters left in the sample string then the match is bound to fail.
  // Therefore it is OK to read a character this far ahead of the current match
  // point.
  int length_;
  RegExpCompiler* compiler_;
  // 0xff for Latin1, 0xffff for UTF-16.
  int max_char_;
  ZoneList<BoyerMoorePositionInfo*>* bitmaps_;

  int GetSkipTable(int min_lookahead, int max_lookahead,
                   DirectHandle<ByteArray> boolean_skip_table);
  bool FindWorthwhileInterval(int* from, int* to);
  int FindBestInterval(int max_number_of_chars, int old_biggest_points,
                       int* from, int* to);
};

// There are many ways to generate code for a node.  This class encapsulates
// the current way we should be generating.  In other words it encapsulates
// the current state of the code generator.  The effect of this is that we
// generate code for paths that the matcher can take through the regular
// expression.  A given node in the regexp can be code-generated several times
// as it can be part of several traces.  For example for the regexp:
// /foo(bar|ip)baz/ the code to match baz will be generated twice, once as part
// of the foo-bar-baz trace and once as part of the foo-ip-baz trace.  The code
// to match foo is generated only once (the traces have a common prefix).  The
// code to store the capture is deferred and generated (twice) after the places
// where baz has been matched.
class Trace {
 public:
  // A value for a property that is either known to be true, know to be false,
  // or not known.
  enum TriBool { UNKNOWN = -1, FALSE_VALUE = 0, TRUE_VALUE = 1 };

  class DeferredAction {
   public:
    DeferredAction(ActionNode::ActionType action_type, int reg)
        : action_type_(action_type), reg_(reg), next_(nullptr) {}
    DeferredAction* next() { return next_; }
    bool Mentions(int reg);
    int reg() { return reg_; }
    ActionNode::ActionType action_type() { return action_type_; }

   private:
    ActionNode::ActionType action_type_;
    int reg_;
    DeferredAction* next_;
    friend class Trace;
  };

  class DeferredCapture : public DeferredAction {
   public:
    DeferredCapture(int reg, bool is_capture, Trace* trace)
        : DeferredAction(ActionNode::STORE_POSITION, reg),
          cp_offset_(trace->cp_offset()),
          is_capture_(is_capture) {}
    int cp_offset() { return cp_offset_; }
    bool is_capture() { return is_capture_; }

   private:
    int cp_offset_;
    bool is_capture_;
    void set_cp_offset(int cp_offset) { cp_offset_ = cp_offset; }
  };

  class DeferredSetRegisterForLoop : public DeferredAction {
   public:
    DeferredSetRegisterForLoop(int reg, int value)
        : DeferredAction(ActionNode::SET_REGISTER_FOR_LOOP, reg),
          value_(value) {}
    int value() { return value_; }

   private:
    int value_;
  };

  class DeferredClearCaptures : public DeferredAction {
   public:
    explicit DeferredClearCaptures(Interval range)
        : DeferredAction(ActionNode::CLEAR_CAPTURES, -1), range_(range) {}
    Interval range() { return range_; }

   private:
    Interval range_;
  };

  class DeferredIncrementRegister : public DeferredAction {
   public:
    explicit DeferredIncrementRegister(int reg)
        : DeferredAction(ActionNode::INCREMENT_REGISTER, reg) {}
  };

  Trace()
      : cp_offset_(0),
        actions_(nullptr),
        backtrack_(nullptr),
        stop_node_(nullptr),
        loop_label_(nullptr),
        characters_preloaded_(0),
        bound_checked_up_to_(0),
        flush_budget_(100),
        at_start_(UNKNOWN) {}

  // End the trace.  This involves flushing the deferred actions in the trace
  // and pushing a backtrack location onto the backtrack stack.  Once this is
  // done we can start a new trace or go to one that has already been
  // generated.
  void Flush(RegExpCompiler* compiler, RegExpNode* successor);
  int cp_offset() { return cp_offset_; }
  DeferredAction* actions() { return actions_; }
  // A trivial trace is one that has no deferred actions or other state that
  // affects the assumptions used when generating code.  There is no recorded
  // backtrack location in a trivial trace, so with a trivial trace we will
  // generate code that, on a failure to match, gets the backtrack location
  // from the backtrack stack rather than using a direct jump instruction.  We
  // always start code generation with a trivial trace and non-trivial traces
  // are created as we emit code for nodes or add to the list of deferred
  // actions in the trace.  The location of the code generated for a node using
  // a trivial trace is recorded in a label in the node so that gotos can be
  // generated to that code.
  bool is_trivial() {
    return backtrack_ == nullptr && actions_ == nullptr && cp_offset_ == 0 &&
           characters_preloaded_ == 0 && bound_checked_up_to_ == 0 &&
           quick_check_performed_.characters() == 0 && at_start_ == UNKNOWN;
  }
  TriBool at_start() { return at_start_; }
  void set_at_start(TriBool at_start) { at_start_ = at_start; }
  Label* backtrack() { return backtrack_; }
  Label* loop_label() { return loop_label_; }
  RegExpNode* stop_node() { return stop_node_; }
  int characters_preloaded() { return characters_preloaded_; }
  int bound_checked_up_to() { return bound_checked_up_to_; }
  int flush_budget() { return flush_budget_; }
  QuickCheckDetails* quick_check_performed() { return &quick_check_performed_; }
  bool mentions_reg(int reg);
  // Returns true if a deferred position store exists to the specified
  // register and stores the offset in the out-parameter.  Otherwise
  // returns false.
  bool GetStoredPosition(int reg, int* cp_offset);
  // These set methods and AdvanceCurrentPositionInTrace should be used only on
  // new traces - the intention is that traces are immutable after creation.
  void add_action(DeferredAction* new_action) {
    DCHECK(new_action->next_ == nullptr);
    new_action->next_ = actions_;
    actions_ = new_action;
  }
  void set_backtrack(Label* backtrack) { backtrack_ = backtrack; }
  void set_stop_node(RegExpNode* node) { stop_node_ = node; }
  void set_loop_label(Label* label) { loop_label_ = label; }
  void set_characters_preloaded(int count) { characters_preloaded_ = count; }
  void set_bound_checked_up_to(int to) { bound_checked_up_to_ = to; }
  void set_flush_budget(int to) { flush_budget_ = to; }
  void set_quick_check_performed(QuickCheckDetails* d) {
    quick_check_performed_ = *d;
  }
  void InvalidateCurrentCharacter();
  void AdvanceCurrentPositionInTrace(int by, RegExpCompiler* compiler);

 private:
  int FindAffectedRegisters(DynamicBitSet* affected_registers, Zone* zone);
  void PerformDeferredActions(RegExpMacroAssembler* macro, int max_register,
                              const DynamicBitSet& affected_registers,
                              DynamicBitSet* registers_to_pop,
                              DynamicBitSet* registers_to_clear, Zone* zone);
  void RestoreAffectedRegisters(RegExpMacroAssembler* macro, int max_register,
                                const DynamicBitSet& registers_to_pop,
                                const DynamicBitSet& registers_to_clear);
  int cp_offset_;
  DeferredAction* actions_;
  Label* backtrack_;
  RegExpNode* stop_node_;
  Label* loop_label_;
  int characters_preloaded_;
  int bound_checked_up_to_;
  QuickCheckDetails quick_check_performed_;
  int flush_budget_;
  TriBool at_start_;
};

class GreedyLoopState {
 public:
  explicit GreedyLoopState(bool not_at_start);

  Label* label() { return &label_; }
  Trace* counter_backtrack_trace() { return &counter_backtrack_trace_; }

 private:
  Label label_;
  Trace counter_backtrack_trace_;
};

struct PreloadState {
  static const int kEatsAtLeastNotYetInitialized = -1;
  bool preload_is_current_;
  bool preload_has_checked_bounds_;
  int preload_characters_;
  int eats_at_least_;
  void init() { eats_at_least_ = kEatsAtLeastNotYetInitialized; }
};

// Analysis performs assertion propagation and computes eats_at_least_ values.
// See the comments on AssertionPropagator and EatsAtLeastPropagator for more
// details.
RegExpError AnalyzeRegExp(Isolate* isolate, bool is_one_byte, RegExpFlags flags,
                          RegExpNode* node);

class FrequencyCollator {
 public:
  FrequencyCollator() : total_samples_(0) {
    for (int i = 0; i < RegExpMacroAssembler::kTableSize; i++) {
      frequencies_[i] = CharacterFrequency(i);
    }
  }

  void CountCharacter(int character) {
    int index = (character & RegExpMacroAssembler::kTableMask);
    frequencies_[index].Increment();
    total_samples_++;
  }

  // Does not measure in percent, but rather per-128 (the table size from the
  // regexp macro assembler).
  int Frequency(int in_character) {
    DCHECK((in_character & RegExpMacroAssembler::kTableMask) == in_character);
    if (total_samples_ < 1) return 1;  // Division by zero.
    int freq_in_per128 =
        (frequencies_[in_character].counter() * 128) / total_samples_;
    return freq_in_per128;
  }

 private:
  class CharacterFrequency {
   public:
    CharacterFrequency() : counter_(0), character_(-1) {}
    explicit CharacterFrequency(int character)
        : counter_(0), character_(character) {}

    void Increment() { counter_++; }
    int counter() { return counter_; }
    int character() { return character_; }

   private:
    int counter_;
    int character_;
  };

 private:
  CharacterFrequency frequencies_[RegExpMacroAssembler::kTableSize];
  int total_samples_;
};

class RegExpCompiler {
 public:
  RegExpCompiler(Isolate* isolate, Zone* zone, int capture_count,
                 RegExpFlags flags, bool is_one_byte);

  int AllocateRegister() {
    if (next_register_ >= RegExpMacroAssembler::kMaxRegister) {
      reg_exp_too_big_ = true;
      return next_register_;
    }
    return next_register_++;
  }

  // Lookarounds to match lone surrogates for unicode character class matches
  // are never nested. We can therefore reuse registers.
  int UnicodeLookaroundStackRegister() {
    if (unicode_lookaround_stack_register_ == kNoRegister) {
      unicode_lookaround_stack_register_ = AllocateRegister();
    }
    return unicode_lookaround_stack_register_;
  }

  int UnicodeLookaroundPositionRegister() {
    if (unicode_lookaround_position_register_ == kNoRegister) {
      unicode_lookaround_position_register_ = AllocateRegister();
    }
    return unicode_lookaround_position_register_;
  }

  struct CompilationResult final {
    explicit CompilationResult(RegExpError err) : error(err) {}
    CompilationResult(Handle<Object> code, int registers)
        : code(code), num_registers(registers) {}

    static CompilationResult RegExpTooBig() {
      return CompilationResult(RegExpError::kTooLarge);
    }

    bool Succeeded() const { return error == RegExpError::kNone; }

    const RegExpError error = RegExpError::kNone;
    Handle<Object> code;
    int num_registers = 0;
  };

  CompilationResult Assemble(Isolate* isolate, RegExpMacroAssembler* assembler,
                             RegExpNode* start, int capture_count,
                             Handle<String> pattern);

  // Preprocessing is the final step of node creation before analysis
  // and assembly. It includes:
  // - Wrapping the body of the regexp in capture 0.
  // - Inserting the implicit .* before/after the regexp if necessary.
  // - If the input is a one-byte string, filtering out nodes that can't match.
  // - Fixing up regexp matches that start within a surrogate pair.
  RegExpNode* PreprocessRegExp(RegExpCompileData* data, bool is_one_byte);

  // If the regexp matching starts within a surrogate pair, step back to the
  // lead surrogate and start matching from there.
  RegExpNode* OptionallyStepBackToLeadSurrogate(RegExpNode* on_success);

  inline void AddWork(RegExpNode* node) {
    if (!node->on_work_list() && !node->label()->is_bound()) {
      node->set_on_work_list(true);
      work_list_->push_back(node);
    }
  }

  static const int kImplementationOffset = 0;
  static const int kNumberOfRegistersOffset = 0;
  static const int kCodeOffset = 1;

  RegExpMacroAssembler* macro_assembler() { return macro_assembler_; }
  EndNode* accept() { return accept_; }

  static const int kMaxRecursion = 100;
  inline int recursion_depth() { return recursion_depth_; }
  inline void IncrementRecursionDepth() { recursion_depth_++; }
  inline void DecrementRecursionDepth() { recursion_depth_--; }

  inline RegExpFlags flags() const { return flags_; }
  inline void set_flags(RegExpFlags flags) { flags_ = flags; }

  void SetRegExpTooBig() { reg_exp_too_big_ = true; }

  inline bool one_byte() { return one_byte_; }
  inline bool optimize() { return optimize_; }
  inline void set_optimize(bool value) { optimize_ = value; }
  inline bool limiting_recursion() { return limiting_recursion_; }
  inline void set_limiting_recursion(bool value) {
    limiting_recursion_ = value;
  }
  bool read_backward() { return read_backward_; }
  void set_read_backward(bool value) { read_backward_ = value; }
  FrequencyCollator* frequency_collator() { return &frequency_collator_; }

  int current_expansion_factor() { return current_expansion_factor_; }
  void set_current_expansion_factor(int value) {
    current_expansion_factor_ = value;
  }

  // The recursive nature of ToNode node generation means we may run into stack
  // overflow issues. We introduce periodic checks to detect these, and the
  // tick counter helps limit overhead of these checks.
  // TODO(jgruber): This is super hacky and should be replaced by an abort
  // mechanism or iterative node generation.
  void ToNodeMaybeCheckForStackOverflow() {
    if ((to_node_overflow_check_ticks_++ % 16 == 0)) {
      ToNodeCheckForStackOverflow();
    }
  }
  void ToNodeCheckForStackOverflow();

  Isolate* isolate() const { return isolate_; }
  Zone* zone() const { return zone_; }

  static const int kNoRegister = -1;

 private:
  EndNode* accept_;
  int next_register_;
  int unicode_lookaround_stack_register_;
  int unicode_lookaround_position_register_;
  ZoneVector<RegExpNode*>* work_list_;
  int recursion_depth_;
  RegExpFlags flags_;
  RegExpMacroAssembler* macro_assembler_;
  bool one_byte_;
  bool reg_exp_too_big_;
  bool limiting_recursion_;
  int to_node_overflow_check_ticks_ = 0;
  bool optimize_;
  bool read_backward_;
  int current_expansion_factor_;
  FrequencyCollator frequency_collator_;
  Isolate* isolate_;
  Zone* zone_;
};

// Categorizes character ranges into BMP, non-BMP, lead, and trail surrogates.
class UnicodeRangeSplitter {
 public:
  V8_EXPORT_PRIVATE UnicodeRangeSplitter(ZoneList<CharacterRange>* base);

  static constexpr int kInitialSize = 8;
  using CharacterRangeVector = base::SmallVector<CharacterRange, kInitialSize>;

  const CharacterRangeVector* bmp() const { return &bmp_; }
  const CharacterRangeVector* lead_surrogates() const {
    return &lead_surrogates_;
  }
  const CharacterRangeVector* trail_surrogates() const {
    return &trail_surrogates_;
  }
  const CharacterRangeVector* non_bmp() const { return &non_bmp_; }

 private:
  void AddRange(CharacterRange range);

  CharacterRangeVector bmp_;
  CharacterRangeVector lead_surrogates_;
  CharacterRangeVector trail_surrogates_;
  CharacterRangeVector non_bmp_;
};

// We need to check for the following characters: 0x39C 0x3BC 0x178.
// TODO(jgruber): Move to CharacterRange.
bool RangeContainsLatin1Equivalents(CharacterRange range);

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_COMPILER_H_
