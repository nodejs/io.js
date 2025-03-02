// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_PROFILE_GENERATOR_H_
#define V8_PROFILER_PROFILE_GENERATOR_H_

#include <atomic>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "include/v8-profiler.h"
#include "src/base/platform/time.h"
#include "src/builtins/builtins.h"
#include "src/execution/vm-state.h"
#include "src/logging/code-events.h"
#include "src/profiler/output-stream-writer.h"
#include "src/profiler/strings-storage.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

struct TickSample;

// Provides a mapping from the offsets within generated code or a bytecode array
// to the source line and inlining id.
class V8_EXPORT_PRIVATE SourcePositionTable : public Malloced {
 public:
  SourcePositionTable() = default;
  SourcePositionTable(const SourcePositionTable&) = delete;
  SourcePositionTable& operator=(const SourcePositionTable&) = delete;

  void SetPosition(int pc_offset, int line, int inlining_id);
  int GetSourceLineNumber(int pc_offset) const;
  int GetInliningId(int pc_offset) const;

  size_t Size() const;
  void print() const;

 private:
  struct SourcePositionTuple {
    bool operator<(const SourcePositionTuple& other) const {
      return pc_offset < other.pc_offset;
    }
    int pc_offset;
    int line_number;
    int inlining_id;
  };
  // This is logically a map, but we store it as a vector of tuples, sorted by
  // the pc offset, so that we can save space and look up items using binary
  // search.
  std::vector<SourcePositionTuple> pc_offsets_to_lines_;
};

struct CodeEntryAndLineNumber;

class CodeEntry {
 public:
  enum class CodeType { JS, WASM, OTHER };

  // CodeEntry may reference strings (|name|, |resource_name|) managed by a
  // StringsStorage instance. These must be freed via ReleaseStrings.
  inline CodeEntry(LogEventListener::CodeTag tag, const char* name,
                   const char* resource_name = CodeEntry::kEmptyResourceName,
                   int line_number = v8::CpuProfileNode::kNoLineNumberInfo,
                   int column_number = v8::CpuProfileNode::kNoColumnNumberInfo,
                   std::unique_ptr<SourcePositionTable> line_info = nullptr,
                   bool is_shared_cross_origin = false,
                   CodeType code_type = CodeType::JS);
  CodeEntry(const CodeEntry&) = delete;
  CodeEntry& operator=(const CodeEntry&) = delete;
  ~CodeEntry() {
    // No alive handles should be associated with the CodeEntry at time of
    // destruction.
    DCHECK(!heap_object_location_);
    DCHECK_EQ(ref_count_, 0UL);
  }

  const char* name() const { return name_; }
  const char* resource_name() const { return resource_name_; }
  int line_number() const { return line_number_; }
  int column_number() const { return column_number_; }
  const SourcePositionTable* line_info() const { return line_info_.get(); }
  int script_id() const { return script_id_; }
  void set_script_id(int script_id) { script_id_ = script_id; }
  int position() const { return position_; }
  void set_position(int position) { position_ = position; }
  void set_bailout_reason(const char* bailout_reason) {
    EnsureRareData()->bailout_reason_ = bailout_reason;
  }
  const char* bailout_reason() const {
    return rare_data_ ? rare_data_->bailout_reason_ : kEmptyBailoutReason;
  }

  void set_deopt_info(const char* deopt_reason, int deopt_id,
                      std::vector<CpuProfileDeoptFrame> inlined_frames);

  size_t EstimatedSize() const;
  CpuProfileDeoptInfo GetDeoptInfo();
  bool has_deopt_info() const {
    return rare_data_ && rare_data_->deopt_id_ != kNoDeoptimizationId;
  }
  void clear_deopt_info() {
    if (!rare_data_) return;
    // TODO(alph): Clear rare_data_ if that was the only field in use.
    rare_data_->deopt_reason_ = kNoDeoptReason;
    rare_data_->deopt_id_ = kNoDeoptimizationId;
  }

  const char* code_type_string() const {
    switch (CodeTypeField::decode(bit_field_)) {
      case CodeType::JS:
        return "JS";
      case CodeType::WASM:
        return "wasm";
      case CodeType::OTHER:
        return "other";
    }
  }

  // Returns the start address of the instruction segment represented by this
  // CodeEntry. Used as a key in the containing InstructionStreamMap.
  Address instruction_start() const { return instruction_start_; }
  void set_instruction_start(Address address) { instruction_start_ = address; }

  Address** heap_object_location_address() { return &heap_object_location_; }

  void FillFunctionInfo(Tagged<SharedFunctionInfo> shared);

  void SetBuiltinId(Builtin id);
  Builtin builtin() const { return BuiltinField::decode(bit_field_); }

  bool is_shared_cross_origin() const {
    return SharedCrossOriginField::decode(bit_field_);
  }

  // Returns whether or not the lifetime of this CodeEntry is reference
  // counted, and managed by an InstructionStreamMap.
  bool is_ref_counted() const { return RefCountedField::decode(bit_field_); }

  uint32_t GetHash() const;
  bool IsSameFunctionAs(const CodeEntry* entry) const;

  int GetSourceLine(int pc_offset) const;

  struct Equals {
    bool operator()(const CodeEntry* lhs, const CodeEntry* rhs) const {
      return lhs->IsSameFunctionAs(rhs);
    }
  };
  struct Hasher {
    std::size_t operator()(CodeEntry* e) const { return e->GetHash(); }
  };

  void SetInlineStacks(
      std::unordered_set<CodeEntry*, Hasher, Equals> inline_entries,
      std::unordered_map<int, std::vector<CodeEntryAndLineNumber>>
          inline_stacks);
  const std::vector<CodeEntryAndLineNumber>* GetInlineStack(
      int pc_offset) const;

  LogEventListener::Event event() const {
    return EventField::decode(bit_field_);
  }

  LogEventListener::CodeTag code_tag() const {
    return CodeTagField::decode(bit_field_);
  }

  V8_EXPORT_PRIVATE static const char* const kEmptyResourceName;
  static const char* const kEmptyBailoutReason;
  static const char* const kNoDeoptReason;

  V8_EXPORT_PRIVATE static const char* const kProgramEntryName;
  V8_EXPORT_PRIVATE static const char* const kIdleEntryName;
  V8_EXPORT_PRIVATE static const char* const kGarbageCollectorEntryName;
  // Used to represent frames for which we have no reliable way to
  // detect function.
  V8_EXPORT_PRIVATE static const char* const kUnresolvedFunctionName;
  V8_EXPORT_PRIVATE static const char* const kRootEntryName;

  V8_EXPORT_PRIVATE static CodeEntry* program_entry();
  V8_EXPORT_PRIVATE static CodeEntry* idle_entry();
  V8_EXPORT_PRIVATE static CodeEntry* gc_entry();
  V8_EXPORT_PRIVATE static CodeEntry* unresolved_entry();
  V8_EXPORT_PRIVATE static CodeEntry* root_entry();

  // Releases strings owned by this CodeEntry, which may be allocated in the
  // provided StringsStorage instance. This instance is not stored directly
  // with the CodeEntry in order to reduce memory footprint.
  // Called before every destruction.
  void ReleaseStrings(StringsStorage& strings);

  void print() const;

 private:
  friend class CodeEntryStorage;

  struct RareData {
    const char* deopt_reason_ = kNoDeoptReason;
    const char* bailout_reason_ = kEmptyBailoutReason;
    int deopt_id_ = kNoDeoptimizationId;
    std::unordered_map<int, std::vector<CodeEntryAndLineNumber>> inline_stacks_;
    std::unordered_set<CodeEntry*, Hasher, Equals> inline_entries_;
    std::vector<CpuProfileDeoptFrame> deopt_inlined_frames_;
  };

  RareData* EnsureRareData();

  void mark_ref_counted() {
    bit_field_ = RefCountedField::update(bit_field_, true);
    ref_count_ = 1;
  }

  size_t AddRef() {
    DCHECK(is_ref_counted());
    DCHECK_LT(ref_count_, std::numeric_limits<size_t>::max());
    ref_count_++;
    return ref_count_;
  }

  size_t DecRef() {
    DCHECK(is_ref_counted());
    DCHECK_GT(ref_count_, 0UL);
    ref_count_--;
    return ref_count_;
  }

  using EventField = base::BitField<LogEventListener::Event, 0, 4>;
  using CodeTagField = base::BitField<LogEventListener::CodeTag, 0, 4>;
  using BuiltinField = base::BitField<Builtin, 8, 20>;
  static_assert(Builtins::kBuiltinCount <= BuiltinField::kNumValues,
                "builtin_count exceeds size of bitfield");
  using RefCountedField = base::BitField<bool, 28, 1>;
  using CodeTypeField = base::BitField<CodeType, 29, 2>;
  using SharedCrossOriginField = base::BitField<bool, 31, 1>;

  std::uint32_t bit_field_;
  std::atomic<std::size_t> ref_count_ = {0};
  const char* name_;
  const char* resource_name_;
  int line_number_;
  int column_number_;
  int script_id_;
  int position_;
  std::unique_ptr<SourcePositionTable> line_info_;
  std::unique_ptr<RareData> rare_data_;
  Address instruction_start_ = kNullAddress;
  Address* heap_object_location_ = nullptr;
};

struct CodeEntryAndLineNumber {
  CodeEntry* code_entry;
  int line_number;
};

using ProfileStackTrace = std::vector<CodeEntryAndLineNumber>;

// Filters stack frames from sources other than a target native context.
class ContextFilter {
 public:
  explicit ContextFilter(Address native_context_address = kNullAddress)
      : native_context_address_(native_context_address) {}

  // Invoked when a native context has changed address.
  void OnMoveEvent(Address from_address, Address to_address);

  bool Accept(Address native_context_address) const {
    if (native_context_address_ == kNullAddress) return true;
    return (native_context_address & ~kHeapObjectTag) ==
           native_context_address_;
  }

  // Update the context's tracked address based on VM-thread events.
  void set_native_context_address(Address address) {
    native_context_address_ = address;
  }
  Address native_context_address() const { return native_context_address_; }

 private:
  Address native_context_address_;
};

class ProfileTree;

class V8_EXPORT_PRIVATE ProfileNode {
 public:
  inline ProfileNode(ProfileTree* tree, CodeEntry* entry, ProfileNode* parent,
                     int line_number = 0);
  ~ProfileNode();
  ProfileNode(const ProfileNode&) = delete;
  ProfileNode& operator=(const ProfileNode&) = delete;

  ProfileNode* FindChild(
      CodeEntry* entry,
      int line_number = v8::CpuProfileNode::kNoLineNumberInfo);
  ProfileNode* FindOrAddChild(CodeEntry* entry, int line_number = 0);
  void IncrementSelfTicks() { ++self_ticks_; }
  void IncreaseSelfTicks(unsigned amount) { self_ticks_ += amount; }
  void IncrementLineTicks(int src_line);

  CodeEntry* entry() const { return entry_; }
  unsigned self_ticks() const { return self_ticks_; }
  const std::vector<ProfileNode*>* children() const { return &children_list_; }
  unsigned id() const { return id_; }
  ProfileNode* parent() const { return parent_; }
  int line_number() const {
    return line_number_ != 0 ? line_number_ : entry_->line_number();
  }
  CpuProfileNode::SourceType source_type() const;

  unsigned int GetHitLineCount() const {
    return static_cast<unsigned int>(line_ticks_.size());
  }
  bool GetLineTicks(v8::CpuProfileNode::LineTick* entries,
                    unsigned int length) const;
  void CollectDeoptInfo(CodeEntry* entry);
  const std::vector<CpuProfileDeoptInfo>& deopt_infos() const {
    return deopt_infos_;
  }
  Isolate* isolate() const;

  void Print(int indent) const;

 private:
  struct Equals {
    bool operator()(CodeEntryAndLineNumber lhs,
                    CodeEntryAndLineNumber rhs) const {
      return lhs.code_entry->IsSameFunctionAs(rhs.code_entry) &&
             lhs.line_number == rhs.line_number;
    }
  };
  struct Hasher {
    std::size_t operator()(CodeEntryAndLineNumber pair) const {
      return pair.code_entry->GetHash() ^ ComputeUnseededHash(pair.line_number);
    }
  };

  ProfileTree* tree_;
  CodeEntry* entry_;
  unsigned self_ticks_;
  std::unordered_map<CodeEntryAndLineNumber, ProfileNode*, Hasher, Equals>
      children_;
  int line_number_;
  std::vector<ProfileNode*> children_list_;
  ProfileNode* parent_;
  unsigned id_;
  // maps line number --> number of ticks
  std::unordered_map<int, int> line_ticks_;

  std::vector<CpuProfileDeoptInfo> deopt_infos_;
};

class CodeEntryStorage;

class V8_EXPORT_PRIVATE ProfileTree {
 public:
  explicit ProfileTree(Isolate* isolate, CodeEntryStorage* storage = nullptr);
  ~ProfileTree();
  ProfileTree(const ProfileTree&) = delete;
  ProfileTree& operator=(const ProfileTree&) = delete;

  using ProfilingMode = v8::CpuProfilingMode;

  ProfileNode* AddPathFromEnd(
      const std::vector<CodeEntry*>& path,
      int src_line = v8::CpuProfileNode::kNoLineNumberInfo,
      bool update_stats = true);
  ProfileNode* AddPathFromEnd(
      const ProfileStackTrace& path,
      int src_line = v8::CpuProfileNode::kNoLineNumberInfo,
      bool update_stats = true,
      ProfilingMode mode = ProfilingMode::kLeafNodeLineNumbers);
  ProfileNode* root() const { return root_; }
  unsigned next_node_id() { return next_node_id_++; }

  void Print() const { root_->Print(0); }

  Isolate* isolate() const { return isolate_; }

  void EnqueueNode(const ProfileNode* node) { pending_nodes_.push_back(node); }
  size_t pending_nodes_count() const { return pending_nodes_.size(); }
  std::vector<const ProfileNode*> TakePendingNodes() {
    return std::move(pending_nodes_);
  }

  CodeEntryStorage* code_entries() { return code_entries_; }

 private:
  template <typename Callback>
  void TraverseDepthFirst(Callback* callback);

  std::vector<const ProfileNode*> pending_nodes_;

  unsigned next_node_id_;
  Isolate* isolate_;
  CodeEntryStorage* const code_entries_;
  ProfileNode* root_;
};

class CpuProfiler;

class CpuProfile {
 public:
  struct SampleInfo {
    ProfileNode* node;
    base::TimeTicks timestamp;
    int line;
    StateTag state_tag;
    EmbedderStateTag embedder_state_tag;
    const std::optional<uint64_t> trace_id;
  };

  V8_EXPORT_PRIVATE CpuProfile(
      CpuProfiler* profiler, ProfilerId id, const char* title,
      CpuProfilingOptions options,
      std::unique_ptr<DiscardedSamplesDelegate> delegate = nullptr);
  CpuProfile(const CpuProfile&) = delete;
  CpuProfile& operator=(const CpuProfile&) = delete;

  // Checks whether or not the given TickSample should be (sub)sampled, given
  // the sampling interval of the profiler that recorded it (in microseconds).
  V8_EXPORT_PRIVATE bool CheckSubsample(base::TimeDelta sampling_interval);
  // Add pc -> ... -> main() call path to the profile.
  void AddPath(base::TimeTicks timestamp, const ProfileStackTrace& path,
               int src_line, bool update_stats,
               base::TimeDelta sampling_interval, StateTag state,
               EmbedderStateTag embedder_state,
               const std::optional<uint64_t> trace_id = std::nullopt);
  void FinishProfile();

  const char* title() const { return title_; }
  const ProfileTree* top_down() const { return &top_down_; }

  int samples_count() const { return static_cast<int>(samples_.size()); }
  const SampleInfo& sample(int index) const { return samples_[index]; }

  int64_t sampling_interval_us() const {
    return options_.sampling_interval_us();
  }

  base::TimeTicks start_time() const { return start_time_; }
  base::TimeTicks end_time() const { return end_time_; }
  CpuProfiler* cpu_profiler() const { return profiler_; }
  ContextFilter& context_filter() { return context_filter_; }
  ProfilerId id() const { return id_; }

  void UpdateTicksScale();

  V8_EXPORT_PRIVATE void Print() const;

 private:
  void StreamPendingTraceEvents();

  const char* title_;
  const CpuProfilingOptions options_;
  std::unique_ptr<DiscardedSamplesDelegate> delegate_;
  ContextFilter context_filter_;
  base::TimeTicks start_time_;
  base::TimeTicks end_time_;
  std::deque<SampleInfo> samples_;
  ProfileTree top_down_;
  CpuProfiler* const profiler_;
  size_t streaming_next_sample_;
  const ProfilerId id_;
  // Number of microseconds worth of profiler ticks that should elapse before
  // the next sample is recorded.
  base::TimeDelta next_sample_delta_;
};

class CpuProfileMaxSamplesCallbackTask : public v8::Task {
 public:
  explicit CpuProfileMaxSamplesCallbackTask(
      std::unique_ptr<DiscardedSamplesDelegate> delegate)
      : delegate_(std::move(delegate)) {}

  void Run() override { delegate_->Notify(); }

 private:
  std::unique_ptr<DiscardedSamplesDelegate> delegate_;
};

class V8_EXPORT_PRIVATE InstructionStreamMap {
 public:
  explicit InstructionStreamMap(CodeEntryStorage& storage);
  ~InstructionStreamMap();
  InstructionStreamMap(const InstructionStreamMap&) = delete;
  InstructionStreamMap& operator=(const InstructionStreamMap&) = delete;

  // Adds the given CodeEntry to the InstructionStreamMap. The
  // InstructionStreamMap takes ownership of the CodeEntry.
  void AddCode(Address addr, CodeEntry* entry, unsigned size);
  void MoveCode(Address from, Address to);
  // Attempts to remove the given CodeEntry from the InstructionStreamMap.
  // Returns true iff the entry was found and removed.
  bool RemoveCode(CodeEntry*);
  void ClearCodesInRange(Address start, Address end);
  CodeEntry* FindEntry(Address addr, Address* out_instruction_start = nullptr);
  void Print();
  size_t size() const { return code_map_.size(); }

  size_t GetEstimatedMemoryUsage() const;

  CodeEntryStorage& code_entries() { return code_entries_; }

  void Clear();

 private:
  struct CodeEntryMapInfo {
    CodeEntry* entry;
    unsigned size;
  };

  std::multimap<Address, CodeEntryMapInfo> code_map_;
  CodeEntryStorage& code_entries_;
};

// Manages the lifetime of CodeEntry objects, and stores shared resources
// between them.
class V8_EXPORT_PRIVATE CodeEntryStorage {
 public:
  template <typename... Args>
  static CodeEntry* Create(Args&&... args) {
    CodeEntry* const entry = new CodeEntry(std::forward<Args>(args)...);
    entry->mark_ref_counted();
    return entry;
  }

  void AddRef(CodeEntry*);
  void DecRef(CodeEntry*);

  StringsStorage& strings() { return function_and_resource_names_; }

 private:
  StringsStorage function_and_resource_names_;
};

class V8_EXPORT_PRIVATE CpuProfilesCollection {
 public:
  explicit CpuProfilesCollection(Isolate* isolate);
  CpuProfilesCollection(const CpuProfilesCollection&) = delete;
  CpuProfilesCollection& operator=(const CpuProfilesCollection&) = delete;

  void set_cpu_profiler(CpuProfiler* profiler) { profiler_ = profiler; }
  CpuProfilingResult StartProfiling(
      const char* title = nullptr, CpuProfilingOptions options = {},
      std::unique_ptr<DiscardedSamplesDelegate> delegate = nullptr);

  // This Method is only visible for testing
  CpuProfilingResult StartProfilingForTesting(ProfilerId id);
  CpuProfile* StopProfiling(ProfilerId id);
  bool IsLastProfileLeft(ProfilerId id);
  CpuProfile* Lookup(const char* title);

  std::vector<std::unique_ptr<CpuProfile>>* profiles() {
    return &finished_profiles_;
  }
  const char* GetName(Tagged<Name> name) {
    return resource_names_.GetName(name);
  }
  void RemoveProfile(CpuProfile* profile);

  // Finds a common sampling interval dividing each CpuProfile's interval,
  // rounded up to the nearest multiple of the CpuProfiler's sampling interval.
  // Returns 0 if no profiles are attached.
  base::TimeDelta GetCommonSamplingInterval();

  // Called from profile generator thread.
  void AddPathToCurrentProfiles(
      base::TimeTicks timestamp, const ProfileStackTrace& path, int src_line,
      bool update_stats, base::TimeDelta sampling_interval, StateTag state,
      EmbedderStateTag embedder_state_tag,
      Address native_context_address = kNullAddress,
      Address native_embedder_context_address = kNullAddress,
      const std::optional<uint64_t> trace_id = std::nullopt);

  // Called from profile generator thread.
  void UpdateNativeContextAddressForCurrentProfiles(Address from, Address to);

  // Limits the number of profiles that can be simultaneously collected.
  static const int kMaxSimultaneousProfiles = 100;

 private:
  CpuProfilingResult StartProfiling(
      ProfilerId id, const char* title = nullptr,
      CpuProfilingOptions options = {},
      std::unique_ptr<DiscardedSamplesDelegate> delegate = nullptr);
  StringsStorage resource_names_;
  std::vector<std::unique_ptr<CpuProfile>> finished_profiles_;
  CpuProfiler* profiler_;

  // Accessed by VM thread and profile generator thread.
  std::vector<std::unique_ptr<CpuProfile>> current_profiles_;
  base::RecursiveMutex current_profiles_mutex_;
  static std::atomic<ProfilerId> last_id_;
  Isolate* isolate_;
};

class CpuProfileJSONSerializer {
 public:
  explicit CpuProfileJSONSerializer(CpuProfile* profile)
      : profile_(profile), writer_(nullptr) {}
  CpuProfileJSONSerializer(const CpuProfileJSONSerializer&) = delete;
  CpuProfileJSONSerializer& operator=(const CpuProfileJSONSerializer&) = delete;
  void Serialize(v8::OutputStream* stream);

 private:
  void SerializePositionTicks(const v8::CpuProfileNode* node, int lineCount);
  void SerializeCallFrame(const v8::CpuProfileNode* node);
  void SerializeChildren(const v8::CpuProfileNode* node, int childrenCount);
  void SerializeNode(const v8::CpuProfileNode* node);
  void SerializeNodes();
  void SerializeSamples();
  void SerializeTimeDeltas();
  void SerializeImpl();

  static const int kEdgeFieldsCount;
  static const int kNodeFieldsCount;

  CpuProfile* profile_;
  OutputStreamWriter* writer_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_PROFILE_GENERATOR_H_
