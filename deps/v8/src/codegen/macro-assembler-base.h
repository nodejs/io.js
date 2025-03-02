// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_MACRO_ASSEMBLER_BASE_H_
#define V8_CODEGEN_MACRO_ASSEMBLER_BASE_H_

#include <memory>

#include "src/base/template-utils.h"
#include "src/builtins/builtins.h"
#include "src/codegen/assembler-arch.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

// Common base class for platform-specific MacroAssemblers containing
// platform-independent bits.
// TODO(victorgomes): We should use LocalIsolate instead of Isolate in the
// methods of this class.
class V8_EXPORT_PRIVATE MacroAssemblerBase : public Assembler {
 public:
  // Constructors are declared public to inherit them in derived classes
  // with `using` directive.
  MacroAssemblerBase(Isolate* isolate, CodeObjectRequired create_code_object,
                     std::unique_ptr<AssemblerBuffer> buffer = {})
      : MacroAssemblerBase(isolate, AssemblerOptions::Default(isolate),
                           create_code_object, std::move(buffer)) {}
  MacroAssemblerBase(Isolate* isolate, MaybeAssemblerZone zone,
                     CodeObjectRequired create_code_object,
                     std::unique_ptr<AssemblerBuffer> buffer = {})
      : MacroAssemblerBase(isolate, zone, AssemblerOptions::Default(isolate),
                           create_code_object, std::move(buffer)) {}

  MacroAssemblerBase(Isolate* isolate, const AssemblerOptions& options,
                     CodeObjectRequired create_code_object,
                     std::unique_ptr<AssemblerBuffer> buffer = {});
  MacroAssemblerBase(Isolate* isolate, MaybeAssemblerZone zone,
                     AssemblerOptions options,
                     CodeObjectRequired create_code_object,
                     std::unique_ptr<AssemblerBuffer> buffer = {});
  // For isolate-less users.
  MacroAssemblerBase(MaybeAssemblerZone zone, AssemblerOptions options,
                     CodeObjectRequired create_code_object,
                     std::unique_ptr<AssemblerBuffer> buffer = {})
      : MacroAssemblerBase(nullptr, zone, options, create_code_object,
                           std::move(buffer)) {}

  Isolate* isolate() const { return isolate_; }

  IndirectHandle<HeapObject> CodeObject() const {
    DCHECK(!code_object_.is_null());
    return code_object_;
  }

  bool root_array_available() const { return root_array_available_; }
  void set_root_array_available(bool v) { root_array_available_ = v; }

  bool should_abort_hard() const { return hard_abort_; }
  void set_abort_hard(bool v) { hard_abort_ = v; }

  void set_builtin(Builtin builtin) { maybe_builtin_ = builtin; }
  Builtin builtin() const { return maybe_builtin_; }

  void set_has_frame(bool v) { has_frame_ = v; }
  bool has_frame() const { return has_frame_; }

  // Loads the given constant or external reference without embedding its direct
  // pointer. The produced code is isolate-independent.
  void IndirectLoadConstant(Register destination, Handle<HeapObject> object);
  void IndirectLoadExternalReference(Register destination,
                                     ExternalReference reference);

  Address BuiltinEntry(Builtin builtin);

  virtual void LoadFromConstantsTable(Register destination,
                                      int constant_index) = 0;

  // Corresponds to: destination = kRootRegister + offset.
  virtual void LoadRootRegisterOffset(Register destination,
                                      intptr_t offset) = 0;

  // Corresponds to: destination = [kRootRegister + offset].
  virtual void LoadRootRelative(Register destination, int32_t offset) = 0;
  virtual void StoreRootRelative(int32_t offset, Register value) = 0;

  static constexpr bool CanBeImmediate(RootIndex index) {
    return V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index);
  }
  Tagged_t ReadOnlyRootPtr(RootIndex index);
  static Tagged_t ReadOnlyRootPtr(RootIndex index, Isolate* isolate);
  virtual void LoadRoot(Register destination, RootIndex index) = 0;

  static int32_t RootRegisterOffsetForRootIndex(RootIndex root_index);
  static int32_t RootRegisterOffsetForBuiltin(Builtin builtin);

  // Returns the root-relative offset to reference.address().
  static intptr_t RootRegisterOffsetForExternalReference(
      Isolate* isolate, const ExternalReference& reference);

  // Returns the root-relative offset to the external reference table entry,
  // which itself contains reference.address().
  static int32_t RootRegisterOffsetForExternalReferenceTableEntry(
      Isolate* isolate, const ExternalReference& reference);

  // An address is addressable through kRootRegister if it is located within
  // isolate->root_register_addressable_region().
  static bool IsAddressableThroughRootRegister(
      Isolate* isolate, const ExternalReference& reference);

#if defined(V8_TARGET_OS_WIN) || defined(V8_TARGET_OS_MACOS)
  // Minimum page size. We must touch memory once per page when expanding the
  // stack, to avoid access violations.
  static constexpr int kStackPageSize = 4 * KB;
#endif

  V8_INLINE std::string CommentForOffHeapTrampoline(const char* prefix,
                                                    Builtin builtin) {
    if (!v8_flags.code_comments) return "";
    std::ostringstream str;
    str << "Inlined  Trampoline for " << prefix << " to "
        << Builtins::name(builtin);
    return str.str();
  }

  enum class RecordWriteCallMode { kDefault, kWasm };

 protected:
  Isolate* const isolate_ = nullptr;

  // This handle will be patched with the code object on installation.
  IndirectHandle<HeapObject> code_object_;

  // Whether kRootRegister has been initialized.
  bool root_array_available_ = true;

  // Emit a C call to abort instead of a runtime call.
  bool hard_abort_ = false;

  // May be set while generating builtins.
  Builtin maybe_builtin_ = Builtin::kNoBuiltinId;

  bool has_frame_ = false;

  int comment_depth_ = 0;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MacroAssemblerBase);
};

// Avoids emitting calls to the {Builtin::kAbort} builtin when emitting
// debug code during the lifetime of this scope object.
class V8_NODISCARD HardAbortScope {
 public:
  explicit HardAbortScope(MacroAssemblerBase* assembler)
      : assembler_(assembler), old_value_(assembler->should_abort_hard()) {
    assembler_->set_abort_hard(true);
  }
  ~HardAbortScope() { assembler_->set_abort_hard(old_value_); }

 private:
  MacroAssemblerBase* assembler_;
  bool old_value_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_MACRO_ASSEMBLER_BASE_H_
