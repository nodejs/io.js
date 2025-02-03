#ifndef SRC_NODE_CONTEXT_DATA_H_
#define SRC_NODE_CONTEXT_DATA_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "util.h"
#include "v8.h"

namespace node {

// Pick an index that's hopefully out of the way when we're embedded inside
// another application. Performance-wise or memory-wise it doesn't matter:
// Context::SetAlignedPointerInEmbedderData() is backed by a FixedArray,
// worst case we pay a one-time penalty for resizing the array.
#ifndef NODE_CONTEXT_EMBEDDER_DATA_INDEX
#define NODE_CONTEXT_EMBEDDER_DATA_INDEX 32
#endif

#ifndef NODE_CONTEXT_SANDBOX_OBJECT_INDEX
#define NODE_CONTEXT_SANDBOX_OBJECT_INDEX 33
#endif

#ifndef NODE_CONTEXT_ALLOW_WASM_CODE_GENERATION_INDEX
#define NODE_CONTEXT_ALLOW_WASM_CODE_GENERATION_INDEX 34
#endif

#ifndef NODE_BINDING_DATA_STORE_INDEX
#define NODE_BINDING_DATA_STORE_INDEX 35
#endif

#ifndef NODE_CONTEXT_ALLOW_CODE_GENERATION_FROM_STRINGS_INDEX
#define NODE_CONTEXT_ALLOW_CODE_GENERATION_FROM_STRINGS_INDEX 36
#endif

#ifndef NODE_CONTEXT_CONTEXTIFY_CONTEXT_INDEX
#define NODE_CONTEXT_CONTEXTIFY_CONTEXT_INDEX 37
#endif

#ifndef NODE_CONTEXT_REALM_INDEX
#define NODE_CONTEXT_REALM_INDEX 38
#endif

// NODE_CONTEXT_TAG must be greater than any embedder indexes so that a single
// check on the number of embedder data fields can assure the presence of all
// embedder indexes.
#ifndef NODE_CONTEXT_TAG
#define NODE_CONTEXT_TAG 39
#endif

/**
 * These are indexes used to set Node.js-specific data in the V8 contexts
 * via v8::Context::SetAlignedPointerInEmbedderData() (for pointers) and
 * v8::Context::SetEmbedderData() (for v8::Values).
 *
 * There are five types of contexts in Node.js:
 * 1. Default V8 context, with nothing Node.js-specific. This is normally only
 *    created by the embedders that uses v8::Context APIs directly and is not
 *    available through the Node.js JS APIs. Its context snapshot in the
 *    built-in v8 startup snapshot is stored using
 *    v8::SnapshotCreator::SetDefaultContext() - effectively at index 0.
 * 2. Default Node.js main context. When Node.js is launched typically there is
 *    a main thread on which a main node::Environment is created. The
 *    Environment is associated with its own v8::Isolate (env->isolate()) and a
 *    main v8::Context (env->context()). The corresponding data structure is
 *    node::NodeMainInstance and the Environment is created in
 *    node::NodeMainInstance::Run(). Its context snapshot in the built-in V8
 *    startup snapshot is stored at node::SnapshotData::kNodeMainContextIndex.
 * 3. Node.js worker context. When a Worker instance is created via
 *    new worker_threads.Worker(), it gets its own OS thread, its
 *    own node::Environment, its own v8::Isolate and its own v8::Context.
 *    The corresponding data structure is node::Worker and the Environment
 *    is created in node::Worker::Run().
 *    Its context snapshot in the built-in V8 startup snapshot is stored at
 *    node::SnapshotData::kNodeBaseContextIndex.
 * 4. Contextified vm context: When a contextified context is created via
 *    vm.createContext() or other related vm APIs, the vm.Context instance
 *    gets its own v8::Context. It shares the thread, the v8::Isolate and
 *    the node::Environment with the context where the vm APIs are called.
 *    The corresponding data structure is node::ContextifyContext and
 *    the initialization code is in node::ContextifyContext::New().
 *    Its context snapshot in the built-in V8 startup snapshot is stored at
 *    node::SnapshotData::kNodeVMContextIndex.
 * 5. ShadowRealm context: When a JS ShadowRealm is created via new ShadowRealm,
 *    it gets its own v8::Context. It also shares the thread, the v8::Isolate
 *    and the node::Environment with the context where the ShadowRealm
 *    constructor is called.  The corresponding data structure is
 *    node::ShadowRealm and the initialization code is in
 *    node::ShadowRealm::New().
 */
enum ContextEmbedderIndex {
  // Pointer to the node::Environment associated with the context. Only set for
  // context type 2-5. Used by Environment::GetCurrent(context) to retrieve
  // the node::Environment associated with any Node.js context in the
  // V8 callbacks.
  kEnvironment = NODE_CONTEXT_EMBEDDER_DATA_INDEX,
  // A v8::Value which is the sandbox object used to create the contextified vm
  // context. For example the first argument of vm.createContext().
  // Only set for context type 4 and used in ContextifyContext methods.
  kSandboxObject = NODE_CONTEXT_SANDBOX_OBJECT_INDEX,
  // A v8::Value indicating whether the context allows WebAssembly code
  // generation.
  // Only set for context type 2-5, and for them the default is v8::True.
  // For context type 4 it's configurable via options.codeGeneration.wasm in the
  // vm APIs. Used in the default v8::AllowWasmCodeGenerationCallback.
  kAllowWasmCodeGeneration = NODE_CONTEXT_ALLOW_WASM_CODE_GENERATION_INDEX,
  // A v8::Value indicating whether the context allows code generation via
  // eval() or new Function().
  // Only set for context type 2-5, and for them the default is v8::True.
  // For context type 4 it's configurable via options.codeGeneration.strings in
  // the
  // vm APIs. Used in the default v8::AllowCodeGenerationFromStringsCallback.
  kAllowCodeGenerationFromStrings =
      NODE_CONTEXT_ALLOW_CODE_GENERATION_FROM_STRINGS_INDEX,
  // Pointer to the node::ContextifyContext associated with vm.Contexts.
  // Only set for context type 4. Used by ContextifyContext::Get() to get
  // to the ContextifyContext from the property interceptors.
  kContextifyContext = NODE_CONTEXT_CONTEXTIFY_CONTEXT_INDEX,
  // Pointer to the node::Realm associated with the context.
  // Only set for context type 2-3 and 5. For context type 2-3, it points to a
  // node::PrincipalRealm. For context type 5 it points to a node::ShadowRealm.
  // Used by Realm::GetCurrent(context) to retrieve the associated node::Realm
  // with any Node.js context in the V8 callbacks.
  kRealm = NODE_CONTEXT_REALM_INDEX,
  // Pointer to a constant address which is
  // node::ContextEmbedderTag::kNodeContextTagPtr.
  // Only set for context type 2-5. Used by ContextEmbedderTag::IsNodeContext()
  // to check whether a context is a Node.js context.
  kContextTag = NODE_CONTEXT_TAG,
};

class ContextEmbedderTag {
 public:
  static inline void TagNodeContext(v8::Local<v8::Context> context) {
    // Used by ContextEmbedderTag::IsNodeContext to know that we are on a node
    // context.
    context->SetAlignedPointerInEmbedderData(
        ContextEmbedderIndex::kContextTag,
        ContextEmbedderTag::kNodeContextTagPtr);
  }

  static inline bool IsNodeContext(v8::Local<v8::Context> context) {
    if (context.IsEmpty()) [[unlikely]] {
      return false;
    }
    if (context->GetNumberOfEmbedderDataFields() <=
        ContextEmbedderIndex::kContextTag) [[unlikely]] {
      return false;
    }
    if (context->GetAlignedPointerFromEmbedderData(
            ContextEmbedderIndex::kContextTag) !=
        ContextEmbedderTag::kNodeContextTagPtr) [[unlikely]] {
      return false;
    }
    return true;
  }

 private:
  static void* const kNodeContextTagPtr;
  static int const kNodeContextTag;

  ContextEmbedderTag() = delete;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_CONTEXT_DATA_H_
