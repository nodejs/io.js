// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TOOLS_V8WINDBG_SRC_JS_STACK_H_
#define V8_TOOLS_V8WINDBG_SRC_JS_STACK_H_

#include <crtdbg.h>
#include <wrl/implements.h>

#include <string>
#include <vector>

#include "tools/v8windbg/base/utilities.h"
#include "tools/v8windbg/src/v8-debug-helper-interop.h"
#include "tools/v8windbg/src/v8windbg-extension.h"

class JSStackAlias
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IModelMethod> {
 public:
  IFACEMETHOD(Call)
  (IModelObject* p_context_object, ULONG64 arg_count,
   _In_reads_(arg_count) IModelObject** pp_arguments, IModelObject** pp_result,
   IKeyStore** pp_metadata);
};

struct FrameData {
  FrameData();
  ~FrameData();
  FrameData(const FrameData&);
  FrameData(FrameData&&);
  FrameData& operator=(const FrameData&);
  FrameData& operator=(FrameData&&);
  WRL::ComPtr<IModelObject> script_name;
  WRL::ComPtr<IModelObject> script_source;
  WRL::ComPtr<IModelObject> function_name;
  WRL::ComPtr<IModelObject> function_character_offset;
};

class StackFrameIterator
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IModelIterator> {
 public:
  StackFrameIterator(WRL::ComPtr<IDebugHostContext>& host_context);
  ~StackFrameIterator() override;

  HRESULT PopulateFrameData();

  IFACEMETHOD(Reset)();

  IFACEMETHOD(GetNext)
  (IModelObject** object, ULONG64 dimensions, IModelObject** indexers,
   IKeyStore** metadata);

  HRESULT GetAt(uint64_t index, IModelObject** result) const;

 private:
  ULONG position_ = 0;
  std::vector<FrameData> frames_;
  WRL::ComPtr<IDebugHostContext> sp_ctx_;
};

class StackFrames
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IIndexableConcept, IIterableConcept> {
 public:
  StackFrames();
  ~StackFrames() override;

  // IIndexableConcept members
  IFACEMETHOD(GetDimensionality)
  (IModelObject* context_object, ULONG64* dimensionality);

  IFACEMETHOD(GetAt)
  (IModelObject* context_object, ULONG64 indexer_count, IModelObject** indexers,
   IModelObject** object, IKeyStore** metadata);

  IFACEMETHOD(SetAt)
  (IModelObject* context_object, ULONG64 indexer_count, IModelObject** indexers,
   IModelObject* value);

  // IIterableConcept
  IFACEMETHOD(GetDefaultIndexDimensionality)
  (IModelObject* context_object, ULONG64* dimensionality);

  IFACEMETHOD(GetIterator)
  (IModelObject* context_object, IModelIterator** iterator);

 private:
  WRL::ComPtr<StackFrameIterator> opt_frames_;
};

#endif  // V8_TOOLS_V8WINDBG_SRC_JS_STACK_H_
