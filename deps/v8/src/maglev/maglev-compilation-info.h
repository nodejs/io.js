// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_COMPILATION_INFO_H_
#define V8_MAGLEV_MAGLEV_COMPILATION_INFO_H_

#include <memory>

#include "src/base/optional.h"
#include "src/handles/handles.h"
#include "src/handles/maybe-handles.h"

namespace v8 {

namespace base {
class DefaultAllocationPolicy;
}

namespace internal {

class Isolate;
class PersistentHandles;
class SharedFunctionInfo;
class TranslationArrayBuilder;
class Zone;

namespace compiler {
class JSHeapBroker;
}

namespace maglev {

class MaglevCompilationUnit;
class MaglevGraphLabeller;
class MaglevCodeGenerator;

// A list of v8_flag values copied into the MaglevCompilationInfo for
// guaranteed {immutable,threadsafe} access.
#define MAGLEV_COMPILATION_FLAG_LIST(V) \
  V(code_comments)                      \
  V(maglev)                             \
  V(print_maglev_code)                  \
  V(print_maglev_graph)                 \
  V(trace_maglev_regalloc)

class MaglevCompilationInfo final {
 public:
  static std::unique_ptr<MaglevCompilationInfo> New(
      Isolate* isolate, compiler::JSHeapBroker* broker,
      Handle<JSFunction> function, BytecodeOffset osr_offset) {
    // Doesn't use make_unique due to the private ctor.
    return std::unique_ptr<MaglevCompilationInfo>(
        new MaglevCompilationInfo(isolate, function, osr_offset, broker));
  }
  static std::unique_ptr<MaglevCompilationInfo> New(Isolate* isolate,
                                                    Handle<JSFunction> function,
                                                    BytecodeOffset osr_offset) {
    // Doesn't use make_unique due to the private ctor.
    return std::unique_ptr<MaglevCompilationInfo>(
        new MaglevCompilationInfo(isolate, function, osr_offset));
  }
  ~MaglevCompilationInfo();

  Zone* zone() { return &zone_; }
  compiler::JSHeapBroker* broker() const { return broker_; }
  MaglevCompilationUnit* toplevel_compilation_unit() const {
    return toplevel_compilation_unit_;
  }
  Handle<JSFunction> toplevel_function() const { return toplevel_function_; }
  BytecodeOffset toplevel_osr_offset() const { return osr_offset_; }
  bool toplevel_is_osr() const { return osr_offset_ != BytecodeOffset::None(); }
  void set_code(Handle<Code> code) {
    DCHECK(code_.is_null());
    code_ = code;
  }
  MaybeHandle<Code> get_code() { return code_; }

  bool has_graph_labeller() const { return !!graph_labeller_; }
  void set_graph_labeller(MaglevGraphLabeller* graph_labeller);
  MaglevGraphLabeller* graph_labeller() const {
    DCHECK(has_graph_labeller());
    return graph_labeller_.get();
  }

#ifdef V8_ENABLE_MAGLEV
  void set_code_generator(std::unique_ptr<MaglevCodeGenerator> code_generator);
  MaglevCodeGenerator* code_generator() const { return code_generator_.get(); }
#endif

  // Flag accessors (for thread-safe access to global flags).
  // TODO(v8:7700): Consider caching these.
#define V(Name) \
  bool Name() const { return Name##_; }
  MAGLEV_COMPILATION_FLAG_LIST(V)
#undef V
  bool collect_source_positions() const { return collect_source_positions_; }

  bool specialize_to_function_context() const {
    return specialize_to_function_context_;
  }

  // Must be called from within a MaglevCompilationHandleScope. Transfers owned
  // handles (e.g. shared_, function_) to the new scope.
  void ReopenAndCanonicalizeHandlesInNewScope(Isolate* isolate);

  // Persistent and canonical handles are passed back and forth between the
  // Isolate, this info, and the LocalIsolate.
  void set_persistent_handles(
      std::unique_ptr<PersistentHandles>&& persistent_handles);
  std::unique_ptr<PersistentHandles> DetachPersistentHandles();
  void set_canonical_handles(
      std::unique_ptr<CanonicalHandlesMap>&& canonical_handles);
  std::unique_ptr<CanonicalHandlesMap> DetachCanonicalHandles();

  bool is_detached();

 private:
  MaglevCompilationInfo(
      Isolate* isolate, Handle<JSFunction> function, BytecodeOffset osr_offset,
      base::Optional<compiler::JSHeapBroker*> broker = base::nullopt);

  // Storing the raw pointer to the CanonicalHandlesMap is generally not safe.
  // Use DetachCanonicalHandles() to transfer ownership instead.
  // We explicitly allow the JSHeapBroker to store the raw pointer as it is
  // guaranteed that the MaglevCompilationInfo's lifetime exceeds the lifetime
  // of the broker.
  CanonicalHandlesMap* canonical_handles() { return canonical_handles_.get(); }
  friend compiler::JSHeapBroker;

  Zone zone_;
  compiler::JSHeapBroker* broker_;
  // Must be initialized late since it requires an initialized heap broker.
  MaglevCompilationUnit* toplevel_compilation_unit_ = nullptr;
  Handle<JSFunction> toplevel_function_;
  Handle<Code> code_;
  BytecodeOffset osr_offset_;

  // True if this MaglevCompilationInfo owns its broker and false otherwise. In
  // particular, when used as Turboshaft front-end, this will use Turboshaft's
  // broker.
  bool owns_broker_ = true;

  std::unique_ptr<MaglevGraphLabeller> graph_labeller_;

#ifdef V8_ENABLE_MAGLEV
  // Produced off-thread during ExecuteJobImpl.
  std::unique_ptr<MaglevCodeGenerator> code_generator_;
#endif

#define V(Name) const bool Name##_;
  MAGLEV_COMPILATION_FLAG_LIST(V)
#undef V
  bool collect_source_positions_;

  // If enabled, the generated code can rely on the function context to be a
  // constant (known at compile-time). This opens new optimization
  // opportunities, but prevents code sharing between different function
  // contexts.
  const bool specialize_to_function_context_;

  // 1) PersistentHandles created via PersistentHandlesScope inside of
  //    CompilationHandleScope.
  // 2) Owned by MaglevCompilationInfo.
  // 3) Owned by the broker's LocalHeap when entering the LocalHeapScope.
  // 4) Back to MaglevCompilationInfo when exiting the LocalHeapScope.
  //
  // TODO(jgruber,v8:7700): Update this comment:
  //
  // In normal execution it gets destroyed when PipelineData gets destroyed.
  // There is a special case in GenerateCodeForTesting where the JSHeapBroker
  // will not be retired in that same method. In this case, we need to re-attach
  // the PersistentHandles container to the JSHeapBroker.
  std::unique_ptr<PersistentHandles> ph_;

  // Canonical handles follow the same path as described by the persistent
  // handles above. The only difference is that is created in the
  // CanonicalHandleScope(i.e step 1) is different).
  std::unique_ptr<CanonicalHandlesMap> canonical_handles_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_COMPILATION_INFO_H_
