// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_NODES_H_
#define V8_REGEXP_REGEXP_NODES_H_

#include "src/codegen/label.h"
#include "src/regexp/regexp-macro-assembler.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class AlternativeGenerationList;
class BoyerMooreLookahead;
class GreedyLoopState;
class NodeVisitor;
class QuickCheckDetails;
class RegExpCompiler;
class Trace;
struct PreloadState;
class ChoiceNode;

#define FOR_EACH_NODE_TYPE(VISIT) \
  VISIT(End)                      \
  VISIT(Action)                   \
  VISIT(Choice)                   \
  VISIT(LoopChoice)               \
  VISIT(NegativeLookaroundChoice) \
  VISIT(BackReference)            \
  VISIT(Assertion)                \
  VISIT(Text)

struct NodeInfo final {
  NodeInfo()
      : being_analyzed(false),
        been_analyzed(false),
        follows_word_interest(false),
        follows_newline_interest(false),
        follows_start_interest(false),
        at_end(false),
        visited(false),
        replacement_calculated(false) {}

  // Returns true if the interests and assumptions of this node
  // matches the given one.
  bool Matches(NodeInfo* that) {
    return (at_end == that->at_end) &&
           (follows_word_interest == that->follows_word_interest) &&
           (follows_newline_interest == that->follows_newline_interest) &&
           (follows_start_interest == that->follows_start_interest);
  }

  // Updates the interests of this node given the interests of the
  // node preceding it.
  void AddFromPreceding(NodeInfo* that) {
    at_end |= that->at_end;
    follows_word_interest |= that->follows_word_interest;
    follows_newline_interest |= that->follows_newline_interest;
    follows_start_interest |= that->follows_start_interest;
  }

  bool HasLookbehind() {
    return follows_word_interest || follows_newline_interest ||
           follows_start_interest;
  }

  // Sets the interests of this node to include the interests of the
  // following node.
  void AddFromFollowing(NodeInfo* that) {
    follows_word_interest |= that->follows_word_interest;
    follows_newline_interest |= that->follows_newline_interest;
    follows_start_interest |= that->follows_start_interest;
  }

  void ResetCompilationState() {
    being_analyzed = false;
    been_analyzed = false;
  }

  bool being_analyzed : 1;
  bool been_analyzed : 1;

  // These bits are set of this node has to know what the preceding
  // character was.
  bool follows_word_interest : 1;
  bool follows_newline_interest : 1;
  bool follows_start_interest : 1;

  bool at_end : 1;
  bool visited : 1;
  bool replacement_calculated : 1;
};

struct EatsAtLeastInfo final {
  EatsAtLeastInfo() : EatsAtLeastInfo(0) {}
  explicit EatsAtLeastInfo(uint8_t eats)
      : eats_at_least_from_possibly_start(eats),
        eats_at_least_from_not_start(eats) {}
  void SetMin(const EatsAtLeastInfo& other) {
    if (other.eats_at_least_from_possibly_start <
        eats_at_least_from_possibly_start) {
      eats_at_least_from_possibly_start =
          other.eats_at_least_from_possibly_start;
    }
    if (other.eats_at_least_from_not_start < eats_at_least_from_not_start) {
      eats_at_least_from_not_start = other.eats_at_least_from_not_start;
    }
  }

  bool IsZero() const {
    return eats_at_least_from_possibly_start == 0 &&
           eats_at_least_from_not_start == 0;
  }

  // Any successful match starting from the current node will consume at least
  // this many characters. This does not necessarily mean that there is a
  // possible match with exactly this many characters, but we generally try to
  // get this number as high as possible to allow for early exit on failure.
  uint8_t eats_at_least_from_possibly_start;

  // Like eats_at_least_from_possibly_start, but with the additional assumption
  // that start-of-string assertions (^) can't match. This value is greater than
  // or equal to eats_at_least_from_possibly_start.
  uint8_t eats_at_least_from_not_start;
};

class RegExpNode : public ZoneObject {
 public:
  explicit RegExpNode(Zone* zone)
      : replacement_(nullptr),
        on_work_list_(false),
        trace_count_(0),
        zone_(zone) {
    bm_info_[0] = bm_info_[1] = nullptr;
  }
  virtual ~RegExpNode();
  virtual void Accept(NodeVisitor* visitor) = 0;
  // Generates a goto to this node or actually generates the code at this point.
  virtual void Emit(RegExpCompiler* compiler, Trace* trace) = 0;
  // How many characters must this node consume at a minimum in order to
  // succeed.  The not_at_start argument is used to indicate that we know we are
  // not at the start of the input.  In this case anchored branches will always
  // fail and can be ignored when determining how many characters are consumed
  // on success.  If this node has not been analyzed yet, EatsAtLeast returns 0.
  int EatsAtLeast(bool not_at_start);
  // Returns how many characters this node must consume in order to succeed,
  // given that this is a LoopChoiceNode whose counter register is in a
  // newly-initialized state at the current position in the generated code. For
  // example, consider /a{6,8}/. Absent any extra information, the
  // LoopChoiceNode for the repetition must report that it consumes at least
  // zero characters, because it may have already looped several times. However,
  // with a newly-initialized counter, it can report that it consumes at least
  // six characters.
  virtual EatsAtLeastInfo EatsAtLeastFromLoopEntry();
  // Emits some quick code that checks whether the preloaded characters match.
  // Falls through on certain failure, jumps to the label on possible success.
  // If the node cannot make a quick check it does nothing and returns false.
  bool EmitQuickCheck(RegExpCompiler* compiler, Trace* bounds_check_trace,
                      Trace* trace, bool preload_has_checked_bounds,
                      Label* on_possible_success,
                      QuickCheckDetails* details_return,
                      bool fall_through_on_failure, ChoiceNode* predecessor);
  // For a given number of characters this returns a mask and a value.  The
  // next n characters are anded with the mask and compared with the value.
  // A comparison failure indicates the node cannot match the next n characters.
  // A comparison success indicates the node may match.
  virtual void GetQuickCheckDetails(QuickCheckDetails* details,
                                    RegExpCompiler* compiler,
                                    int characters_filled_in,
                                    bool not_at_start) = 0;
  // Fills in quick check details for this node, given that this is a
  // LoopChoiceNode whose counter register is in a newly-initialized state at
  // the current position in the generated code. For example, consider /a{6,8}/.
  // Absent any extra information, the LoopChoiceNode for the repetition cannot
  // generate any useful quick check because a match might be the (empty)
  // continuation node. However, with a newly-initialized counter, it can
  // generate a quick check for several 'a' characters at once.
  virtual void GetQuickCheckDetailsFromLoopEntry(QuickCheckDetails* details,
                                                 RegExpCompiler* compiler,
                                                 int characters_filled_in,
                                                 bool not_at_start);
  static const int kNodeIsTooComplexForGreedyLoops = kMinInt;
  virtual int GreedyLoopTextLength() { return kNodeIsTooComplexForGreedyLoops; }
  // Only returns the successor for a text node of length 1 that matches any
  // character and that has no guards on it.
  virtual RegExpNode* GetSuccessorOfOmnivorousTextNode(
      RegExpCompiler* compiler) {
    return nullptr;
  }

  // Collects information on the possible code units (mod 128) that can match if
  // we look forward.  This is used for a Boyer-Moore-like string searching
  // implementation.  TODO(erikcorry):  This should share more code with
  // EatsAtLeast, GetQuickCheckDetails.  The budget argument is used to limit
  // the number of nodes we are willing to look at in order to create this data.
  static const int kRecursionBudget = 200;
  bool KeepRecursing(RegExpCompiler* compiler);
  virtual void FillInBMInfo(Isolate* isolate, int offset, int budget,
                            BoyerMooreLookahead* bm, bool not_at_start) {
    UNREACHABLE();
  }

  // If we know that the input is one-byte then there are some nodes that can
  // never match.  This method returns a node that can be substituted for
  // itself, or nullptr if the node can never match.
  virtual RegExpNode* FilterOneByte(int depth, RegExpFlags flags) {
    return this;
  }
  // Helper for FilterOneByte.
  RegExpNode* replacement() {
    DCHECK(info()->replacement_calculated);
    return replacement_;
  }
  RegExpNode* set_replacement(RegExpNode* replacement) {
    info()->replacement_calculated = true;
    replacement_ = replacement;
    return replacement;  // For convenience.
  }

  // We want to avoid recalculating the lookahead info, so we store it on the
  // node.  Only info that is for this node is stored.  We can tell that the
  // info is for this node when offset == 0, so the information is calculated
  // relative to this node.
  void SaveBMInfo(BoyerMooreLookahead* bm, bool not_at_start, int offset) {
    if (offset == 0) set_bm_info(not_at_start, bm);
  }

  Label* label() { return &label_; }
  // If non-generic code is generated for a node (i.e. the node is not at the
  // start of the trace) then it cannot be reused.  This variable sets a limit
  // on how often we allow that to happen before we insist on starting a new
  // trace and generating generic code for a node that can be reused by flushing
  // the deferred actions in the current trace and generating a goto.
  static const int kMaxCopiesCodeGenerated = 10;

  bool on_work_list() { return on_work_list_; }
  void set_on_work_list(bool value) { on_work_list_ = value; }

  NodeInfo* info() { return &info_; }
  const EatsAtLeastInfo* eats_at_least_info() const { return &eats_at_least_; }
  void set_eats_at_least_info(const EatsAtLeastInfo& eats_at_least) {
    eats_at_least_ = eats_at_least;
  }

  // TODO(v8:10441): This is a hacky way to avoid exponential code size growth
  // for very large choice nodes that can be generated by unicode property
  // escapes. In order to avoid inlining (i.e. trace recursion), we pretend to
  // have generated the maximum count of code copies already.
  // We should instead fix this properly, e.g. by using the code size budget
  // (flush_budget) or by generating property escape matches as calls to a C
  // function.
  void SetDoNotInline() { trace_count_ = kMaxCopiesCodeGenerated; }

  BoyerMooreLookahead* bm_info(bool not_at_start) {
    return bm_info_[not_at_start ? 1 : 0];
  }

  Zone* zone() const { return zone_; }

 protected:
  enum LimitResult { DONE, CONTINUE };
  RegExpNode* replacement_;

  LimitResult LimitVersions(RegExpCompiler* compiler, Trace* trace);

  void set_bm_info(bool not_at_start, BoyerMooreLookahead* bm) {
    bm_info_[not_at_start ? 1 : 0] = bm;
  }

 private:
  static const int kFirstCharBudget = 10;
  Label label_;
  bool on_work_list_;
  NodeInfo info_;

  // Saved values for EatsAtLeast results, to avoid recomputation. Filled in
  // during analysis (valid if info_.been_analyzed is true).
  EatsAtLeastInfo eats_at_least_;

  // This variable keeps track of how many times code has been generated for
  // this node (in different traces).  We don't keep track of where the
  // generated code is located unless the code is generated at the start of
  // a trace, in which case it is generic and can be reused by flushing the
  // deferred operations in the current trace and generating a goto.
  int trace_count_;
  BoyerMooreLookahead* bm_info_[2];

  Zone* zone_;
};

class SeqRegExpNode : public RegExpNode {
 public:
  explicit SeqRegExpNode(RegExpNode* on_success)
      : RegExpNode(on_success->zone()), on_success_(on_success) {}
  RegExpNode* on_success() { return on_success_; }
  void set_on_success(RegExpNode* node) { on_success_ = node; }
  RegExpNode* FilterOneByte(int depth, RegExpFlags flags) override;
  void FillInBMInfo(Isolate* isolate, int offset, int budget,
                    BoyerMooreLookahead* bm, bool not_at_start) override {
    on_success_->FillInBMInfo(isolate, offset, budget - 1, bm, not_at_start);
    if (offset == 0) set_bm_info(not_at_start, bm);
  }

 protected:
  RegExpNode* FilterSuccessor(int depth, RegExpFlags flags);

 private:
  RegExpNode* on_success_;
};

class ActionNode : public SeqRegExpNode {
 public:
  enum ActionType {
    SET_REGISTER_FOR_LOOP,
    INCREMENT_REGISTER,
    STORE_POSITION,
    BEGIN_POSITIVE_SUBMATCH,
    BEGIN_NEGATIVE_SUBMATCH,
    POSITIVE_SUBMATCH_SUCCESS,
    EMPTY_MATCH_CHECK,
    CLEAR_CAPTURES,
    MODIFY_FLAGS
  };
  static ActionNode* SetRegisterForLoop(int reg, int val,
                                        RegExpNode* on_success);
  static ActionNode* IncrementRegister(int reg, RegExpNode* on_success);
  static ActionNode* StorePosition(int reg, bool is_capture,
                                   RegExpNode* on_success);
  static ActionNode* ClearCaptures(Interval range, RegExpNode* on_success);
  static ActionNode* BeginPositiveSubmatch(int stack_pointer_reg,
                                           int position_reg,
                                           RegExpNode* on_success);
  static ActionNode* BeginNegativeSubmatch(int stack_pointer_reg,
                                           int position_reg,
                                           RegExpNode* on_success);
  static ActionNode* PositiveSubmatchSuccess(int stack_pointer_reg,
                                             int restore_reg,
                                             int clear_capture_count,
                                             int clear_capture_from,
                                             RegExpNode* on_success);
  static ActionNode* EmptyMatchCheck(int start_register,
                                     int repetition_register,
                                     int repetition_limit,
                                     RegExpNode* on_success);
  static ActionNode* ModifyFlags(RegExpFlags flags, RegExpNode* on_success);
  void Accept(NodeVisitor* visitor) override;
  void Emit(RegExpCompiler* compiler, Trace* trace) override;
  void GetQuickCheckDetails(QuickCheckDetails* details,
                            RegExpCompiler* compiler, int filled_in,
                            bool not_at_start) override;
  void FillInBMInfo(Isolate* isolate, int offset, int budget,
                    BoyerMooreLookahead* bm, bool not_at_start) override;
  ActionType action_type() { return action_type_; }
  // TODO(erikcorry): We should allow some action nodes in greedy loops.
  int GreedyLoopTextLength() override {
    return kNodeIsTooComplexForGreedyLoops;
  }
  RegExpFlags flags() {
    DCHECK_EQ(action_type(), MODIFY_FLAGS);
    return RegExpFlags{data_.u_modify_flags.flags};
  }

 private:
  union {
    struct {
      int reg;
      int value;
    } u_store_register;
    struct {
      int reg;
    } u_increment_register;
    struct {
      int reg;
      bool is_capture;
    } u_position_register;
    struct {
      int stack_pointer_register;
      int current_position_register;
      int clear_register_count;
      int clear_register_from;
    } u_submatch;
    struct {
      int start_register;
      int repetition_register;
      int repetition_limit;
    } u_empty_match_check;
    struct {
      int range_from;
      int range_to;
    } u_clear_captures;
    struct {
      int flags;
    } u_modify_flags;
  } data_;
  ActionNode(ActionType action_type, RegExpNode* on_success)
      : SeqRegExpNode(on_success), action_type_(action_type) {}

  ActionType action_type_;
  friend class DotPrinterImpl;
  friend Zone;
};

class TextNode : public SeqRegExpNode {
 public:
  TextNode(ZoneList<TextElement>* elms, bool read_backward,
           RegExpNode* on_success)
      : SeqRegExpNode(on_success), elms_(elms), read_backward_(read_backward) {}
  TextNode(RegExpClassRanges* that, bool read_backward, RegExpNode* on_success)
      : SeqRegExpNode(on_success),
        elms_(zone()->New<ZoneList<TextElement>>(1, zone())),
        read_backward_(read_backward) {
    elms_->Add(TextElement::ClassRanges(that), zone());
  }
  // Create TextNode for a single character class for the given ranges.
  static TextNode* CreateForCharacterRanges(Zone* zone,
                                            ZoneList<CharacterRange>* ranges,
                                            bool read_backward,
                                            RegExpNode* on_success);
  // Create TextNode for a surrogate pair (i.e. match a sequence of two uc16
  // code unit ranges).
  static TextNode* CreateForSurrogatePair(
      Zone* zone, CharacterRange lead, ZoneList<CharacterRange>* trail_ranges,
      bool read_backward, RegExpNode* on_success);
  static TextNode* CreateForSurrogatePair(Zone* zone,
                                          ZoneList<CharacterRange>* lead_ranges,
                                          CharacterRange trail,
                                          bool read_backward,
                                          RegExpNode* on_success);
  void Accept(NodeVisitor* visitor) override;
  void Emit(RegExpCompiler* compiler, Trace* trace) override;
  void GetQuickCheckDetails(QuickCheckDetails* details,
                            RegExpCompiler* compiler, int characters_filled_in,
                            bool not_at_start) override;
  ZoneList<TextElement>* elements() { return elms_; }
  bool read_backward() { return read_backward_; }
  void MakeCaseIndependent(Isolate* isolate, bool is_one_byte,
                           RegExpFlags flags);
  int GreedyLoopTextLength() override;
  RegExpNode* GetSuccessorOfOmnivorousTextNode(
      RegExpCompiler* compiler) override;
  void FillInBMInfo(Isolate* isolate, int offset, int budget,
                    BoyerMooreLookahead* bm, bool not_at_start) override;
  void CalculateOffsets();
  RegExpNode* FilterOneByte(int depth, RegExpFlags flags) override;
  int Length();

 private:
  enum TextEmitPassType {
    NON_LATIN1_MATCH,            // Check for characters that can't match.
    SIMPLE_CHARACTER_MATCH,      // Case-dependent single character check.
    NON_LETTER_CHARACTER_MATCH,  // Check characters that have no case equivs.
    CASE_CHARACTER_MATCH,        // Case-independent single character check.
    CHARACTER_CLASS_MATCH        // Character class.
  };
  static bool SkipPass(TextEmitPassType pass, bool ignore_case);
  static const int kFirstRealPass = SIMPLE_CHARACTER_MATCH;
  static const int kLastPass = CHARACTER_CLASS_MATCH;
  void TextEmitPass(RegExpCompiler* compiler, TextEmitPassType pass,
                    bool preloaded, Trace* trace, bool first_element_checked,
                    int* checked_up_to);
  ZoneList<TextElement>* elms_;
  bool read_backward_;
};

class AssertionNode : public SeqRegExpNode {
 public:
  enum AssertionType {
    AT_END,
    AT_START,
    AT_BOUNDARY,
    AT_NON_BOUNDARY,
    AFTER_NEWLINE
  };
  static AssertionNode* AtEnd(RegExpNode* on_success) {
    return on_success->zone()->New<AssertionNode>(AT_END, on_success);
  }
  static AssertionNode* AtStart(RegExpNode* on_success) {
    return on_success->zone()->New<AssertionNode>(AT_START, on_success);
  }
  static AssertionNode* AtBoundary(RegExpNode* on_success) {
    return on_success->zone()->New<AssertionNode>(AT_BOUNDARY, on_success);
  }
  static AssertionNode* AtNonBoundary(RegExpNode* on_success) {
    return on_success->zone()->New<AssertionNode>(AT_NON_BOUNDARY, on_success);
  }
  static AssertionNode* AfterNewline(RegExpNode* on_success) {
    return on_success->zone()->New<AssertionNode>(AFTER_NEWLINE, on_success);
  }
  void Accept(NodeVisitor* visitor) override;
  void Emit(RegExpCompiler* compiler, Trace* trace) override;
  void GetQuickCheckDetails(QuickCheckDetails* details,
                            RegExpCompiler* compiler, int filled_in,
                            bool not_at_start) override;
  void FillInBMInfo(Isolate* isolate, int offset, int budget,
                    BoyerMooreLookahead* bm, bool not_at_start) override;
  AssertionType assertion_type() { return assertion_type_; }

 private:
  friend Zone;

  void EmitBoundaryCheck(RegExpCompiler* compiler, Trace* trace);
  enum IfPrevious { kIsNonWord, kIsWord };
  void BacktrackIfPrevious(RegExpCompiler* compiler, Trace* trace,
                           IfPrevious backtrack_if_previous);
  AssertionNode(AssertionType t, RegExpNode* on_success)
      : SeqRegExpNode(on_success), assertion_type_(t) {}
  AssertionType assertion_type_;
};

class BackReferenceNode : public SeqRegExpNode {
 public:
  BackReferenceNode(int start_reg, int end_reg, bool read_backward,
                    RegExpNode* on_success)
      : SeqRegExpNode(on_success),
        start_reg_(start_reg),
        end_reg_(end_reg),
        read_backward_(read_backward) {}
  void Accept(NodeVisitor* visitor) override;
  int start_register() { return start_reg_; }
  int end_register() { return end_reg_; }
  bool read_backward() { return read_backward_; }
  void Emit(RegExpCompiler* compiler, Trace* trace) override;
  void GetQuickCheckDetails(QuickCheckDetails* details,
                            RegExpCompiler* compiler, int characters_filled_in,
                            bool not_at_start) override {
    return;
  }
  void FillInBMInfo(Isolate* isolate, int offset, int budget,
                    BoyerMooreLookahead* bm, bool not_at_start) override;

 private:
  int start_reg_;
  int end_reg_;
  bool read_backward_;
};

class EndNode : public RegExpNode {
 public:
  enum Action { ACCEPT, BACKTRACK, NEGATIVE_SUBMATCH_SUCCESS };
  EndNode(Action action, Zone* zone) : RegExpNode(zone), action_(action) {}
  void Accept(NodeVisitor* visitor) override;
  void Emit(RegExpCompiler* compiler, Trace* trace) override;
  void GetQuickCheckDetails(QuickCheckDetails* details,
                            RegExpCompiler* compiler, int characters_filled_in,
                            bool not_at_start) override {
    // Returning 0 from EatsAtLeast should ensure we never get here.
    UNREACHABLE();
  }
  void FillInBMInfo(Isolate* isolate, int offset, int budget,
                    BoyerMooreLookahead* bm, bool not_at_start) override {
    // Returning 0 from EatsAtLeast should ensure we never get here.
    UNREACHABLE();
  }

 private:
  Action action_;
};

class NegativeSubmatchSuccess : public EndNode {
 public:
  NegativeSubmatchSuccess(int stack_pointer_reg, int position_reg,
                          int clear_capture_count, int clear_capture_start,
                          Zone* zone)
      : EndNode(NEGATIVE_SUBMATCH_SUCCESS, zone),
        stack_pointer_register_(stack_pointer_reg),
        current_position_register_(position_reg),
        clear_capture_count_(clear_capture_count),
        clear_capture_start_(clear_capture_start) {}
  void Emit(RegExpCompiler* compiler, Trace* trace) override;

 private:
  int stack_pointer_register_;
  int current_position_register_;
  int clear_capture_count_;
  int clear_capture_start_;
};

class Guard : public ZoneObject {
 public:
  enum Relation { LT, GEQ };
  Guard(int reg, Relation op, int value) : reg_(reg), op_(op), value_(value) {}
  int reg() { return reg_; }
  Relation op() { return op_; }
  int value() { return value_; }

 private:
  int reg_;
  Relation op_;
  int value_;
};

class GuardedAlternative {
 public:
  explicit GuardedAlternative(RegExpNode* node)
      : node_(node), guards_(nullptr) {}
  void AddGuard(Guard* guard, Zone* zone);
  RegExpNode* node() { return node_; }
  void set_node(RegExpNode* node) { node_ = node; }
  ZoneList<Guard*>* guards() { return guards_; }

 private:
  RegExpNode* node_;
  ZoneList<Guard*>* guards_;
};

class AlternativeGeneration;

class ChoiceNode : public RegExpNode {
 public:
  explicit ChoiceNode(int expected_size, Zone* zone)
      : RegExpNode(zone),
        alternatives_(
            zone->New<ZoneList<GuardedAlternative>>(expected_size, zone)),
        not_at_start_(false),
        being_calculated_(false) {}
  void Accept(NodeVisitor* visitor) override;
  void AddAlternative(GuardedAlternative node) {
    alternatives()->Add(node, zone());
  }
  ZoneList<GuardedAlternative>* alternatives() { return alternatives_; }
  void Emit(RegExpCompiler* compiler, Trace* trace) override;
  void GetQuickCheckDetails(QuickCheckDetails* details,
                            RegExpCompiler* compiler, int characters_filled_in,
                            bool not_at_start) override;
  void FillInBMInfo(Isolate* isolate, int offset, int budget,
                    BoyerMooreLookahead* bm, bool not_at_start) override;

  bool being_calculated() { return being_calculated_; }
  bool not_at_start() { return not_at_start_; }
  void set_not_at_start() { not_at_start_ = true; }
  void set_being_calculated(bool b) { being_calculated_ = b; }
  virtual bool try_to_emit_quick_check_for_alternative(bool is_first) {
    return true;
  }
  RegExpNode* FilterOneByte(int depth, RegExpFlags flags) override;
  virtual bool read_backward() { return false; }

 protected:
  int GreedyLoopTextLengthForAlternative(GuardedAlternative* alternative);
  ZoneList<GuardedAlternative>* alternatives_;

 private:
  template <typename...>
  friend class Analysis;

  void GenerateGuard(RegExpMacroAssembler* macro_assembler, Guard* guard,
                     Trace* trace);
  int CalculatePreloadCharacters(RegExpCompiler* compiler, int eats_at_least);
  void EmitOutOfLineContinuation(RegExpCompiler* compiler, Trace* trace,
                                 GuardedAlternative alternative,
                                 AlternativeGeneration* alt_gen,
                                 int preload_characters,
                                 bool next_expects_preload);
  void SetUpPreLoad(RegExpCompiler* compiler, Trace* current_trace,
                    PreloadState* preloads);
  void AssertGuardsMentionRegisters(Trace* trace);
  int EmitOptimizedUnanchoredSearch(RegExpCompiler* compiler, Trace* trace);
  Trace* EmitGreedyLoop(RegExpCompiler* compiler, Trace* trace,
                        AlternativeGenerationList* alt_gens,
                        PreloadState* preloads,
                        GreedyLoopState* greedy_loop_state, int text_length);
  void EmitChoices(RegExpCompiler* compiler,
                   AlternativeGenerationList* alt_gens, int first_choice,
                   Trace* trace, PreloadState* preloads);

  // If true, this node is never checked at the start of the input.
  // Allows a new trace to start with at_start() set to false.
  bool not_at_start_;
  bool being_calculated_;
};

class NegativeLookaroundChoiceNode : public ChoiceNode {
 public:
  explicit NegativeLookaroundChoiceNode(GuardedAlternative this_must_fail,
                                        GuardedAlternative then_do_this,
                                        Zone* zone)
      : ChoiceNode(2, zone) {
    AddAlternative(this_must_fail);
    AddAlternative(then_do_this);
  }
  void GetQuickCheckDetails(QuickCheckDetails* details,
                            RegExpCompiler* compiler, int characters_filled_in,
                            bool not_at_start) override;
  void FillInBMInfo(Isolate* isolate, int offset, int budget,
                    BoyerMooreLookahead* bm, bool not_at_start) override {
    continue_node()->FillInBMInfo(isolate, offset, budget - 1, bm,
                                  not_at_start);
    if (offset == 0) set_bm_info(not_at_start, bm);
  }
  static constexpr int kLookaroundIndex = 0;
  static constexpr int kContinueIndex = 1;
  RegExpNode* lookaround_node() {
    return alternatives()->at(kLookaroundIndex).node();
  }
  RegExpNode* continue_node() {
    return alternatives()->at(kContinueIndex).node();
  }
  // For a negative lookahead we don't emit the quick check for the
  // alternative that is expected to fail.  This is because quick check code
  // starts by loading enough characters for the alternative that takes fewest
  // characters, but on a negative lookahead the negative branch did not take
  // part in that calculation (EatsAtLeast) so the assumptions don't hold.
  bool try_to_emit_quick_check_for_alternative(bool is_first) override {
    return !is_first;
  }
  void Accept(NodeVisitor* visitor) override;
  RegExpNode* FilterOneByte(int depth, RegExpFlags flags) override;
};

class LoopChoiceNode : public ChoiceNode {
 public:
  LoopChoiceNode(bool body_can_be_zero_length, bool read_backward,
                 int min_loop_iterations, Zone* zone)
      : ChoiceNode(2, zone),
        loop_node_(nullptr),
        continue_node_(nullptr),
        body_can_be_zero_length_(body_can_be_zero_length),
        read_backward_(read_backward),
        traversed_loop_initialization_node_(false),
        min_loop_iterations_(min_loop_iterations) {}
  void AddLoopAlternative(GuardedAlternative alt);
  void AddContinueAlternative(GuardedAlternative alt);
  void Emit(RegExpCompiler* compiler, Trace* trace) override;
  void GetQuickCheckDetails(QuickCheckDetails* details,
                            RegExpCompiler* compiler, int characters_filled_in,
                            bool not_at_start) override;
  void GetQuickCheckDetailsFromLoopEntry(QuickCheckDetails* details,
                                         RegExpCompiler* compiler,
                                         int characters_filled_in,
                                         bool not_at_start) override;
  void FillInBMInfo(Isolate* isolate, int offset, int budget,
                    BoyerMooreLookahead* bm, bool not_at_start) override;
  EatsAtLeastInfo EatsAtLeastFromLoopEntry() override;
  RegExpNode* loop_node() { return loop_node_; }
  RegExpNode* continue_node() { return continue_node_; }
  bool body_can_be_zero_length() { return body_can_be_zero_length_; }
  int min_loop_iterations() const { return min_loop_iterations_; }
  bool read_backward() override { return read_backward_; }
  void Accept(NodeVisitor* visitor) override;
  RegExpNode* FilterOneByte(int depth, RegExpFlags flags) override;

 private:
  // AddAlternative is made private for loop nodes because alternatives
  // should not be added freely, we need to keep track of which node
  // goes back to the node itself.
  void AddAlternative(GuardedAlternative node) {
    ChoiceNode::AddAlternative(node);
  }

  RegExpNode* loop_node_;
  RegExpNode* continue_node_;
  bool body_can_be_zero_length_;
  bool read_backward_;

  // Temporary marker set only while generating quick check details. Represents
  // whether GetQuickCheckDetails traversed the initialization node for this
  // loop's counter. If so, we may be able to generate stricter quick checks
  // because we know the loop node must match at least min_loop_iterations_
  // times before the continuation node can match.
  bool traversed_loop_initialization_node_;

  // The minimum number of times the loop_node_ must match before the
  // continue_node_ might be considered. This value can be temporarily decreased
  // while generating quick check details, to represent the remaining iterations
  // after the completed portion of the quick check details.
  int min_loop_iterations_;

  friend class IterationDecrementer;
  friend class LoopInitializationMarker;
};

class NodeVisitor {
 public:
  virtual ~NodeVisitor() = default;
#define DECLARE_VISIT(Type) virtual void Visit##Type(Type##Node* that) = 0;
  FOR_EACH_NODE_TYPE(DECLARE_VISIT)
#undef DECLARE_VISIT
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_NODES_H_
