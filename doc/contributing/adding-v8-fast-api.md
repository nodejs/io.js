# Adding V8 Fast API

Node.js uses [V8](https://v8.dev/) as its JavaScript engine.
Embedding functions implemented in C++ incur a high overhead, so V8
provides an API to implement native functions which may be invoked directly
from JIT-ed code. These functions also come with additional constraints,
for example, they may not trigger garbage collection.

## Limitations

* Fast API functions may not trigger garbage collection. This means by proxy
  that JavaScript execution and heap allocation are also forbidden, including
  `v8::Array::Get()` or `v8::Number::New()`.
* Throwing errors is not available from within a fast API call, but can be done
  through the fallback to the slow API.
* Not all parameter and return types are supported in fast API calls.
  For a full list, please look into
  [`v8-fast-api-calls.h`](../../deps/v8/include/v8-fast-api-calls.h).

## Requirements

* Any function passed to `CFunction::Make`, including fast API function
  declarations, should have their signature registered in
  [`node_external_reference.h`](../../src/node_external_reference.h) file.
  Although, it would not start failing or crashing until the function ends up
  in a snapshot (either the built-in or a user-land one). Please refer to the
  [binding functions documentation](../../src/README.md#binding-functions) for more
  information.
* To test fast APIs, make sure to run the tests in a loop with a decent
  iterations count to trigger relevant optimizations that prefer the fast API
  over the slow one.
* In debug mode (`--debug` or `--debug-node` flags), the fast API calls can be
  tracked using the `TRACK_V8_FAST_API_CALL("key")` macro. This can be used to
  count how many times fast paths are taken during tests. The key is a global
  identifier and should be unique across the codebase.
  Use `"binding_name.function_name"` or `"binding_name.function_name.suffix"` to
  ensure uniqueness.
* The fast callback must be idempotent up to the point where error and fallback
  conditions are checked, because otherwise executing the slow callback might
  produce visible side effects twice.
* If the receiver is used in the callback, it must be passed as a second argument,
  leaving the first one unused, to prevent the JS land from accidentally omitting the receiver when
  invoking the fast API method.

  ```cpp
  // Instead of invoking the method as `receiver.internalModuleStat(input)`, the JS land should
  // invoke it as `internalModuleStat(binding, input)` to make sure the binding is available to
  // the native land.
  static int32_t FastInternalModuleStat(
      Local<Object> unused,
      Local<Object> recv,
      const FastOneByteString& input,
      FastApiCallbackOptions& options) {
    Environment* env = Environment::GetCurrent(recv->GetCreationContextChecked());
    // More code
  }
  ```

## Fallback to slow path

Fast API supports fallback to slow path for when it is desirable to do so,
for example, when throwing a custom error or executing JavaScript code is
needed. The fallback mechanism can be enabled and changed from the C++
implementation of the fast API function declaration.

Passing `true` to the `fallback` option will force V8 to run the slow path
with the same arguments.

In V8, the options fallback is defined as `FastApiCallbackOptions` inside
[`v8-fast-api-calls.h`](../../deps/v8/include/v8-fast-api-calls.h).

* C++ land

  Example of a conditional fast path on C++

  ```cpp
  // Anywhere in the execution flow, you can set fallback and stop the execution.
  static double divide(const int32_t a,
                       const int32_t b,
                       v8::FastApiCallbackOptions& options) {
    if (b == 0) {
      options.fallback = true;
      return 0;
    } else {
      return a / b;
    }
  }
  ```

## Example

A typical function that communicates between JavaScript and C++ is as follows.

* On the JavaScript side:

  ```js
  const { divide } = internalBinding('custom_namespace');
  ```

* On the C++ side:

  ```cpp
  #include "node_debug.h"
  #include "v8-fast-api-calls.h"

  namespace node {
  namespace custom_namespace {

  static void SlowDivide(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    CHECK_GE(args.Length(), 2);
    CHECK(args[0]->IsInt32());
    CHECK(args[1]->IsInt32());
    auto a = args[0].As<v8::Int32>();
    auto b = args[1].As<v8::Int32>();

    if (b->Value() == 0) {
      return node::THROW_ERR_INVALID_STATE(env, "Error");
    }

    double result = a->Value() / b->Value();
    args.GetReturnValue().Set(v8::Number::New(env->isolate(), result));
  }

  static double FastDivide(const int32_t a,
                           const int32_t b,
                           v8::FastApiCallbackOptions& options) {
    if (b == 0) {
      TRACK_V8_FAST_API_CALL("custom_namespace.divide.error");
      options.fallback = true;
      return 0;
    } else {
      TRACK_V8_FAST_API_CALL("custom_namespace.divide.ok");
      return a / b;
    }
  }

  CFunction fast_divide_(CFunction::Make(FastDivide));

  static void Initialize(Local<Object> target,
                         Local<Value> unused,
                         Local<Context> context,
                         void* priv) {
    SetFastMethod(context, target, "divide", SlowDivide, &fast_divide_);
  }

  void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
    registry->Register(SlowDivide);
    registry->Register(FastDivide);
    registry->Register(fast_divide_.GetTypeInfo());
  }

  } // namespace custom_namespace
  } // namespace node

  NODE_BINDING_CONTEXT_AWARE_INTERNAL(custom_namespace,
                                      node::custom_namespace::Initialize);
  NODE_BINDING_EXTERNAL_REFERENCE(
                        custom_namespace,
                        node::custom_namespace::RegisterExternalReferences);
  ```

* Update external references ([`node_external_reference.h`](../../src/node_external_reference.h))

  Since our implementation used
  `double(const int32_t a, const int32_t b, v8::FastApiCallbackOptions& options)`
  signature, we need to add it to external references and in
  `ALLOWED_EXTERNAL_REFERENCE_TYPES`.

  Example declaration:

  ```cpp
  using CFunctionCallbackReturningDouble = double (*)(const int32_t a,
                                                      const int32_t b,
                                                      v8::FastApiCallbackOptions& options);
  ```

* In the unit tests:

  Since the fast API function uses `TRACK_V8_FAST_API_CALL`, we can ensure that
  the fast paths are taken and test them by writing tests that force
  V8 optimizations and check the counters.

  ```js
  // Flags: --expose-internals --no-warnings --allow-natives-syntax
  'use strict';
  const common = require('../common');

  const { internalBinding } = require('internal/test/binding');
  // We could also require a function that uses the internal binding internally.
  const { divide } = internalBinding('custom_namespace');

  // The function that will be optimized. It has to be a function written in
  // JavaScript. Since `divide` comes from the C++ side, we need to wrap it.
  function testFastPath(a, b) {
    return divide(a, b);
  }

  eval('%PrepareFunctionForOptimization(testFastPath)');
  // This call will let V8 know about the argument types that the function expects.
  assert.strictEqual(testFastPath(6, 3), 2);

  eval('%OptimizeFunctionOnNextCall(testFastPath)');
  assert.strictEqual(testFastPath(8, 2), 4);
  assert.throws(() => testFastPath(1, 0), {
    code: 'ERR_INVALID_STATE',
  });

  if (common.isDebug) {
    const { getV8FastApiCallCount } = internalBinding('debug');
    assert.strictEqual(getV8FastApiCallCount('custom_namespace.divide.ok'), 1);
    assert.strictEqual(getV8FastApiCallCount('custom_namespace.divide.error'), 1);
  }
  ```
