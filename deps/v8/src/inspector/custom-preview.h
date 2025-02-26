// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_CUSTOM_PREVIEW_H_
#define V8_INSPECTOR_CUSTOM_PREVIEW_H_

#include <memory>

#include "src/inspector/protocol/Protocol.h"
#include "src/inspector/protocol/Runtime.h"

namespace v8_inspector {

const int kMaxCustomPreviewDepth = 20;

void generateCustomPreview(
    v8::Isolate* isolate, int sessionId, const String16& groupName,
    v8::Local<v8::Object> object, v8::MaybeLocal<v8::Value> config,
    int maxDepth, std::unique_ptr<protocol::Runtime::CustomPreview>* preview);

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_CUSTOM_PREVIEW_H_
