// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_C_API_H_
#define V8_WASM_C_API_H_

#if !defined(BUILDING_V8_SHARED) && !defined(USING_V8_SHARED)
#define LIBWASM_STATIC 1
#endif

#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "third_party/wasm-api/wasm.hh"

namespace v8 {
namespace internal {

class JSWeakMap;

}  // namespace internal
}  // namespace v8

namespace wasm {

class StoreImpl {
 public:
  ~StoreImpl();

  void destroy();

  v8::Isolate* isolate() const { return isolate_; }
  i::Isolate* i_isolate() const {
    return reinterpret_cast<i::Isolate*>(isolate_);
  }

  v8::Local<v8::Context> context() const { return context_.Get(isolate_); }

  static StoreImpl* get(i::Isolate* isolate) {
    return static_cast<StoreImpl*>(
        reinterpret_cast<v8::Isolate*>(isolate)->GetData(0));
  }

  void SetHostInfo(i::DirectHandle<i::Object> object, void* info,
                   void (*finalizer)(void*));
  void* GetHostInfo(i::DirectHandle<i::Object> key);

 private:
  friend own<Store> Store::make(Engine*);

  StoreImpl() = default;

  v8::Isolate::CreateParams create_params_;
  v8::Isolate* isolate_ = nullptr;
  v8::Eternal<v8::Context> context_;
  i::Handle<i::JSWeakMap> host_info_map_;
};

}  // namespace wasm

#endif  // V8_WASM_C_API_H_
