// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

#ifdef V8_CODEGEN_CODE_STUB_ASSEMBLER_MACROS_DEFINED
#error \
    "Assembler macros already defined. Did you forget to #include \"src/codegen/undef-code-stub-assembler-macros.inc\" in a previous file?"
#endif

#define V8_CODEGEN_CODE_STUB_ASSEMBLER_MACROS_DEFINED 1

#ifdef DEBUG
#define CSA_CHECK(csa, x) \
  (csa)->Check([&]() -> TNode<BoolT> { return x; }, #x, __FILE__, __LINE__)
#else
#define CSA_CHECK(csa, x) (csa)->FastCheck(x)
#endif

// This is a check that always calls into the runtime if it aborts.
// This also exits silently when --hole-fuzzing is enabled.
#define CSA_HOLE_SECURITY_CHECK(csa, x) \
  (csa)->Check([&]() -> TNode<BoolT> { return x; }, #x, __FILE__, __LINE__)

#ifdef DEBUG
// CSA_DCHECK_ARGS generates an
// std::initializer_list<CodeStubAssembler::ExtraNode> from __VA_ARGS__. It
// currently supports between 0 and 2 arguments.

#define CSA_DCHECK_0_ARGS(...) {}
#define CSA_DCHECK_1_ARG(a, ...) {{a, #a}}
#define CSA_DCHECK_2_ARGS(a, b, ...) {{a, #a}, {b, #b}}
#define SWITCH_CSA_DCHECK_ARGS(a, b, FUNC, ...) FUNC(a, b)
#define CSA_DCHECK_ARGS(...)                                           \
  SWITCH_CSA_DCHECK_ARGS(__VA_ARGS__ __VA_OPT__(, ) CSA_DCHECK_2_ARGS, \
                         CSA_DCHECK_1_ARG, CSA_DCHECK_0_ARGS)

// CSA_DCHECK(csa, <condition>, <extra values to print...>)

#define CSA_DCHECK(csa, condition_node, ...)                         \
  (csa)->Dcheck(condition_node, #condition_node, __FILE__, __LINE__, \
                CSA_DCHECK_ARGS(__VA_ARGS__))

#define CSA_DCHECK_JS_ARGC_EQ(csa, expected)                            \
  (csa)->Dcheck(                                                        \
      [&]() -> TNode<BoolT> {                                           \
        const TNode<Word32T> argc = (csa)->UncheckedParameter<Word32T>( \
            Descriptor::kJSActualArgumentsCount);                       \
        return (csa)->Word32Equal(                                      \
            argc, (csa)->Int32Constant(i::JSParameterCount(expected))); \
      },                                                                \
      "argc == " #expected, __FILE__, __LINE__,                         \
      {{SmiFromInt32((csa)->UncheckedParameter<Int32T>(                 \
            Descriptor::kJSActualArgumentsCount)),                      \
        "argc"}})

#define CSA_DEBUG_INFO(name) \
  { #name, __FILE__, __LINE__ }
#define BIND(label) Bind(label, CSA_DEBUG_INFO(label))
#define TYPED_VARIABLE_DEF(type, name, ...) \
  TVariable<type> name(CSA_DEBUG_INFO(name), __VA_ARGS__)
#define TYPED_VARIABLE_CONSTRUCTOR(name, ...) \
  name(CSA_DEBUG_INFO(name), __VA_ARGS__)
#else  // DEBUG
#define CSA_DCHECK(csa, ...) ((void)0)
#define CSA_DCHECK_JS_ARGC_EQ(csa, expected) ((void)0)
#define BIND(label) Bind(label)
#define TYPED_VARIABLE_DEF(type, name, ...) TVariable<type> name(__VA_ARGS__)
#define TYPED_VARIABLE_CONSTRUCTOR(name, ...) name(__VA_ARGS__)
#endif  // DEBUG

#define TVARIABLE(...) EXPAND(TYPED_VARIABLE_DEF(__VA_ARGS__, this))
#define TVARIABLE_CONSTRUCTOR(...) \
  EXPAND(TYPED_VARIABLE_CONSTRUCTOR(__VA_ARGS__, this))

#ifdef ENABLE_SLOW_DCHECKS
#define CSA_SLOW_DCHECK(csa, ...)     \
  if (v8_flags.enable_slow_asserts) { \
    CSA_DCHECK(csa, __VA_ARGS__);     \
  }
#else
#define CSA_SLOW_DCHECK(csa, ...) ((void)0)
#endif

// Similar to SBXCHECK in C++, these become a CSA_CHECK in sandbox-enabled
// builds, otherwise a CSA_DCHECK.
#ifdef V8_ENABLE_SANDBOX
#define CSA_SBXCHECK(csa, ...) CSA_CHECK(csa, __VA_ARGS__)
#else
#define CSA_SBXCHECK(csa, ...) CSA_DCHECK(csa, __VA_ARGS__)
#endif
