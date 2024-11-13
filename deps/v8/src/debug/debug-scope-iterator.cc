// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-scope-iterator.h"

#include "src/api/api-inl.h"
#include "src/execution/isolate.h"
#include "src/objects/js-generator-inl.h"

namespace v8 {

std::unique_ptr<debug::ScopeIterator> debug::ScopeIterator::CreateForFunction(
    v8::Isolate* v8_isolate, v8::Local<v8::Function> v8_func) {
  internal::DirectHandle<internal::JSReceiver> receiver =
      Utils::OpenDirectHandle(*v8_func);

  // Besides JSFunction and JSBoundFunction, {v8_func} could be an
  // ObjectTemplate with a CallAsFunctionHandler. We only handle plain
  // JSFunctions.
  if (!IsJSFunction(*receiver)) return nullptr;

  auto function = internal::Cast<internal::JSFunction>(receiver);

  CHECK(function->has_context());
  return std::unique_ptr<debug::ScopeIterator>(new internal::DebugScopeIterator(
      reinterpret_cast<internal::Isolate*>(v8_isolate), function));
}

std::unique_ptr<debug::ScopeIterator>
debug::ScopeIterator::CreateForGeneratorObject(
    v8::Isolate* v8_isolate, v8::Local<v8::Object> v8_generator) {
  internal::Handle<internal::Object> generator =
      Utils::OpenHandle(*v8_generator);
  DCHECK(IsJSGeneratorObject(*generator));
  return std::unique_ptr<debug::ScopeIterator>(new internal::DebugScopeIterator(
      reinterpret_cast<internal::Isolate*>(v8_isolate),
      internal::Cast<internal::JSGeneratorObject>(generator)));
}

namespace internal {

DebugScopeIterator::DebugScopeIterator(Isolate* isolate,
                                       FrameInspector* frame_inspector)
    : iterator_(
          isolate, frame_inspector,
          ::v8::internal::ScopeIterator::ReparseStrategy::kFunctionLiteral) {
  if (!Done() && ShouldIgnore()) Advance();
}

DebugScopeIterator::DebugScopeIterator(Isolate* isolate,
                                       DirectHandle<JSFunction> function)
    : iterator_(isolate, function) {
  if (!Done() && ShouldIgnore()) Advance();
}

DebugScopeIterator::DebugScopeIterator(Isolate* isolate,
                                       Handle<JSGeneratorObject> generator)
    : iterator_(isolate, generator) {
  if (!Done() && ShouldIgnore()) Advance();
}

bool DebugScopeIterator::Done() { return iterator_.Done(); }

void DebugScopeIterator::Advance() {
  DCHECK(!Done());
  iterator_.Next();
  while (!Done() && ShouldIgnore()) {
    iterator_.Next();
  }
}

bool DebugScopeIterator::ShouldIgnore() {
  if (GetType() == debug::ScopeIterator::ScopeTypeLocal) return false;
  return !iterator_.DeclaresLocals(i::ScopeIterator::Mode::ALL);
}

v8::debug::ScopeIterator::ScopeType DebugScopeIterator::GetType() {
  DCHECK(!Done());
  return static_cast<v8::debug::ScopeIterator::ScopeType>(iterator_.Type());
}

v8::Local<v8::Object> DebugScopeIterator::GetObject() {
  DCHECK(!Done());
  Handle<JSObject> value = iterator_.ScopeObject(i::ScopeIterator::Mode::ALL);
  return Utils::ToLocal(value);
}

int DebugScopeIterator::GetScriptId() {
  DCHECK(!Done());
  return iterator_.GetScript()->id();
}

v8::Local<v8::Value> DebugScopeIterator::GetFunctionDebugName() {
  DCHECK(!Done());
  Handle<Object> name = iterator_.GetFunctionDebugName();
  return Utils::ToLocal(name);
}

bool DebugScopeIterator::HasLocationInfo() {
  return iterator_.HasPositionInfo();
}

debug::Location DebugScopeIterator::GetStartLocation() {
  DCHECK(!Done());
  return ToApiHandle<v8::debug::Script>(iterator_.GetScript())
      ->GetSourceLocation(iterator_.start_position());
}

debug::Location DebugScopeIterator::GetEndLocation() {
  DCHECK(!Done());
  return ToApiHandle<v8::debug::Script>(iterator_.GetScript())
      ->GetSourceLocation(iterator_.end_position());
}

bool DebugScopeIterator::SetVariableValue(v8::Local<v8::String> name,
                                          v8::Local<v8::Value> value) {
  DCHECK(!Done());
  return iterator_.SetVariableValue(Utils::OpenHandle(*name),
                                    Utils::OpenHandle(*value));
}

}  // namespace internal
}  // namespace v8
