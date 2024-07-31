// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_KEYED_STORE_GENERIC_H_
#define V8_IC_KEYED_STORE_GENERIC_H_

#include "src/common/globals.h"
#include "src/compiler/code-assembler.h"

namespace v8 {
namespace internal {

class KeyedStoreMegamorphicGenerator {
 public:
  static void Generate(compiler::CodeAssemblerState* state);
};

class KeyedStoreGenericGenerator {
 public:
  static void Generate(compiler::CodeAssemblerState* state);

  // Building block for fast path of Object.assign implementation.
  static void SetProperty(compiler::CodeAssemblerState* state,
                          TNode<Context> context, TNode<JSReceiver> receiver,
                          TNode<BoolT> is_simple_receiver, TNode<Name> name,
                          TNode<Object> value, LanguageMode language_mode);

  // Same as above but more generic. I.e. the receiver can by anything and the
  // key does not have to be unique. Essentially the same as KeyedStoreGeneric.
  static void SetProperty(compiler::CodeAssemblerState* state,
                          TNode<Context> context, TNode<Object> receiver,
                          TNode<Object> key, TNode<Object> value,
                          LanguageMode language_mode);

  static void CreateDataProperty(compiler::CodeAssemblerState* state,
                                 TNode<Context> context,
                                 TNode<JSObject> receiver, TNode<Object> key,
                                 TNode<Object> value);
};

class DefineKeyedOwnGenericGenerator {
 public:
  static void Generate(compiler::CodeAssemblerState* state);
};

class StoreICNoFeedbackGenerator {
 public:
  static void Generate(compiler::CodeAssemblerState* state);
};

class DefineNamedOwnICNoFeedbackGenerator {
 public:
  static void Generate(compiler::CodeAssemblerState* state);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_KEYED_STORE_GENERIC_H_
