// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Tests for heap profiler

#include <ctype.h>

#include <memory>
#include <optional>
#include <vector>

#include "include/v8-function.h"
#include "include/v8-json.h"
#include "include/v8-profiler.h"
#include "src/api/api-inl.h"
#include "src/base/hashmap.h"
#include "src/base/logging.h"
#include "src/base/strings.h"
#include "src/codegen/assembler-inl.h"
#include "src/debug/debug.h"
#include "src/handles/global-handles.h"
#include "src/heap/heap-inl.h"
#include "src/heap/pretenuring-handler.h"
#include "src/objects/objects-inl.h"
#include "src/profiler/allocation-tracker.h"
#include "src/profiler/heap-profiler.h"
#include "src/profiler/heap-snapshot-generator-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/collector.h"
#include "test/cctest/heap/heap-utils.h"
#include "test/cctest/jsonstream-helper.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-module-builder.h"
#endif

using i::AllocationTraceNode;
using i::AllocationTraceTree;
using i::AllocationTracker;
using i::EntrySourceLocation;
using i::heap::GrowNewSpaceToMaximumCapacity;
using std::optional;
using v8::base::ArrayVector;
using v8::base::OS;
using v8::base::Vector;
using v8::base::VectorOf;

namespace {

class NamedEntriesDetector {
 public:
  NamedEntriesDetector()
      : has_A2(false), has_B2(false), has_C2(false) {
  }

  void CheckEntry(i::HeapEntry* entry) {
    if (strcmp(entry->name(), "A2") == 0) has_A2 = true;
    if (strcmp(entry->name(), "B2") == 0) has_B2 = true;
    if (strcmp(entry->name(), "C2") == 0) has_C2 = true;
  }

  void CheckAllReachables(i::HeapEntry* root) {
    v8::base::HashMap visited;
    std::vector<i::HeapEntry*> list;
    list.push_back(root);
    CheckEntry(root);
    while (!list.empty()) {
      i::HeapEntry* heap_entry = list.back();
      list.pop_back();
      for (int i = 0; i < heap_entry->children_count(); ++i) {
        i::HeapGraphEdge* edge = heap_entry->child(i);
        if (edge->type() == i::HeapGraphEdge::kShortcut) continue;
        i::HeapEntry* child = edge->to();
        v8::base::HashMap::Entry* entry = visited.LookupOrInsert(
            reinterpret_cast<void*>(child),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(child)));
        if (entry->value)
          continue;
        entry->value = reinterpret_cast<void*>(1);
        list.push_back(child);
        CheckEntry(child);
      }
    }
  }

  bool has_A2;
  bool has_B2;
  bool has_C2;
};

}  // namespace


static const v8::HeapGraphNode* GetGlobalObject(
    const v8::HeapSnapshot* snapshot) {
  // The 0th-child is (GC Roots), 1st is the user root.
  const v8::HeapGraphNode* global_obj =
      snapshot->GetRoot()->GetChild(1)->GetToNode();
  CHECK_EQ(0, strncmp("Object", const_cast<i::HeapEntry*>(
      reinterpret_cast<const i::HeapEntry*>(global_obj))->name(), 6));
  return global_obj;
}

static const char* GetName(const v8::HeapGraphNode* node) {
  return const_cast<i::HeapEntry*>(reinterpret_cast<const i::HeapEntry*>(node))
      ->name();
}

static const char* GetName(const v8::HeapGraphEdge* edge) {
  return const_cast<i::HeapGraphEdge*>(
             reinterpret_cast<const i::HeapGraphEdge*>(edge))
      ->name();
}

static size_t GetSize(const v8::HeapGraphNode* node) {
  return const_cast<i::HeapEntry*>(reinterpret_cast<const i::HeapEntry*>(node))
      ->self_size();
}

static const v8::HeapGraphNode* GetChildByName(const v8::HeapGraphNode* node,
                                               const char* name) {
  for (int i = 0, count = node->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphNode* child = node->GetChild(i)->GetToNode();
    if (!strcmp(name, GetName(child))) {
      return child;
    }
  }
  return nullptr;
}

static const v8::HeapGraphEdge* GetEdgeByChildName(
    const v8::HeapGraphNode* node, const char* name) {
  for (int i = 0, count = node->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* edge = node->GetChild(i);
    const v8::HeapGraphNode* child = edge->GetToNode();
    if (!strcmp(name, GetName(child))) {
      return edge;
    }
  }
  return nullptr;
}

static const v8::HeapGraphNode* GetRootChild(const v8::HeapSnapshot* snapshot,
                                             const char* name) {
  return GetChildByName(snapshot->GetRoot(), name);
}

static optional<EntrySourceLocation> GetLocation(
    const v8::HeapSnapshot* s, const v8::HeapGraphNode* node) {
  const i::HeapSnapshot* snapshot = reinterpret_cast<const i::HeapSnapshot*>(s);
  const std::vector<EntrySourceLocation>& locations = snapshot->locations();
  const i::HeapEntry* entry = reinterpret_cast<const i::HeapEntry*>(node);
  for (const auto& loc : locations) {
    if (loc.entry_index == entry->index()) {
      return optional<EntrySourceLocation>(loc);
    }
  }

  return optional<EntrySourceLocation>();
}

static const v8::HeapGraphNode* GetProperty(v8::Isolate* isolate,
                                            const v8::HeapGraphNode* node,
                                            v8::HeapGraphEdge::Type type,
                                            const char* name) {
  for (int i = 0, count = node->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = node->GetChild(i);
    v8::String::Utf8Value prop_name(isolate, prop->GetName());
    if (prop->GetType() == type && strcmp(name, *prop_name) == 0)
      return prop->GetToNode();
  }
  return nullptr;
}

// The following functions are not Wasm-specific, but are only used in a
// Wasm-specific test. As long as this is the case we only define them if Wasm
// is enabled to avoid warnings about unused functions.
#if V8_ENABLE_WEBASSEMBLY
static const std::vector<std::string> GetProperties(
    v8::Isolate* isolate, const v8::HeapGraphNode* node) {
  int num_children = node->GetChildrenCount();
  std::vector<std::string> properties(num_children);
  for (int i = 0; i < num_children; ++i) {
    const v8::HeapGraphEdge* prop = node->GetChild(i);
    v8::String::Utf8Value prop_name(isolate, prop->GetName());
    properties[i] = *prop_name;
  }
  std::sort(properties.begin(), properties.end());
  return properties;
}

static void CheckProperties(
    v8::Isolate* isolate, const v8::HeapGraphNode* node,
    std::initializer_list<std::string> expected_properties) {
  std::vector<std::string> properties = GetProperties(isolate, node);
  if (VectorOf(properties) == VectorOf(expected_properties)) return;

  std::ostringstream full_error;
  full_error << "Expected properties: "
             << i::PrintCollection(expected_properties) << "\n";
  full_error << "Found properties:    " << i::PrintCollection(properties)
             << "\n";
  OS::PrintError("%s\n", full_error.str().c_str());
  FATAL("Mismatch in properties");
}
#endif  // V8_ENABLE_WEBASSEMBLY

static bool HasString(v8::Isolate* isolate, const v8::HeapGraphNode* node,
                      const char* contents) {
  for (int i = 0, count = node->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = node->GetChild(i);
    const v8::HeapGraphNode* dest_node = prop->GetToNode();
    if (dest_node->GetType() == v8::HeapGraphNode::kString) {
      v8::String::Utf8Value node_name(isolate, dest_node->GetName());
      if (strcmp(contents, *node_name) == 0) return true;
    }
  }
  return false;
}

static void EnsureNoUninstrumentedInternals(v8::Isolate* isolate,
                                            const v8::HeapGraphNode* node) {
  for (int i = 0; i < 20; ++i) {
    v8::base::ScopedVector<char> buffer(10);
    const v8::HeapGraphNode* internal =
        GetProperty(isolate, node, v8::HeapGraphEdge::kInternal,
                    i::IntToCString(i, buffer));
    CHECK(!internal);
  }
}

// Check that snapshot has no unretained entries except root.
static bool ValidateSnapshot(const v8::HeapSnapshot* snapshot, int depth = 3) {
  i::HeapSnapshot* heap_snapshot = const_cast<i::HeapSnapshot*>(
      reinterpret_cast<const i::HeapSnapshot*>(snapshot));

  v8::base::HashMap visited;
  std::deque<i::HeapGraphEdge>& edges = heap_snapshot->edges();
  for (size_t i = 0; i < edges.size(); ++i) {
    v8::base::HashMap::Entry* entry = visited.LookupOrInsert(
        reinterpret_cast<void*>(edges[i].to()),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(edges[i].to())));
    uint32_t ref_count = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(entry->value));
    entry->value = reinterpret_cast<void*>(ref_count + 1);
  }
  uint32_t unretained_entries_count = 0;
  std::deque<i::HeapEntry>& entries = heap_snapshot->entries();
  for (i::HeapEntry& entry : entries) {
    v8::base::HashMap::Entry* map_entry = visited.Lookup(
        reinterpret_cast<void*>(&entry),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&entry)));
    if (!map_entry && entry.id() != 1) {
      entry.Print("entry with no retainer", "", depth, 0);
      ++unretained_entries_count;
    }
  }
  return unretained_entries_count == 0;
}

bool EndsWith(const char* a, const char* b) {
  size_t length_a = strlen(a);
  size_t length_b = strlen(b);
  return (length_a >= length_b) && !strcmp(a + length_a - length_b, b);
}

TEST(HeapSnapshot) {
  LocalContext env2;
  v8::HandleScope scope(env2->GetIsolate());
  v8::HeapProfiler* heap_profiler = env2->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "function A2() {}\n"
      "function B2(x) { return function() { return typeof x; }; }\n"
      "function C2(x) { this.x1 = x; this.x2 = x; this[1] = x; }\n"
      "var a2 = new A2();\n"
      "var b2_1 = new B2(a2), b2_2 = new B2(a2);\n"
      "var c2 = new C2(a2);");
  const v8::HeapSnapshot* snapshot_env2 = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot_env2));
  const v8::HeapGraphNode* global_env2 = GetGlobalObject(snapshot_env2);

  // Verify, that JS global object of env2 has '..2' properties.
  const v8::HeapGraphNode* a2_node = GetProperty(
      env2->GetIsolate(), global_env2, v8::HeapGraphEdge::kProperty, "a2");
  CHECK(a2_node);
  CHECK(GetProperty(env2->GetIsolate(), global_env2,
                    v8::HeapGraphEdge::kProperty, "b2_1"));
  CHECK(GetProperty(env2->GetIsolate(), global_env2,
                    v8::HeapGraphEdge::kProperty, "b2_2"));
  CHECK(GetProperty(env2->GetIsolate(), global_env2,
                    v8::HeapGraphEdge::kProperty, "c2"));

  NamedEntriesDetector det;
  det.CheckAllReachables(const_cast<i::HeapEntry*>(
      reinterpret_cast<const i::HeapEntry*>(global_env2)));
  CHECK(det.has_A2);
  CHECK(det.has_B2);
  CHECK(det.has_C2);
}

TEST(HeapSnapshotLocations) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "function X(a) { return function() { return a; } }\n"
      "function* getid() { yield 1; }\n"
      "class A {}\n"
      "var x = X(1);\n"
      "var g = getid();\n"
      "var o = new A();");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* x =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "x");
  CHECK(x);

  optional<EntrySourceLocation> x_loc = GetLocation(snapshot, x);
  CHECK(x_loc);
  CHECK_EQ(0, x_loc->line);
  CHECK_EQ(31, x_loc->col);

  const v8::HeapGraphNode* g =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "g");
  CHECK(x);

  optional<EntrySourceLocation> g_loc = GetLocation(snapshot, g);
  CHECK(g_loc);
  CHECK_EQ(1, g_loc->line);
  CHECK_EQ(15, g_loc->col);

  const v8::HeapGraphNode* o =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "o");
  CHECK(x);

  optional<EntrySourceLocation> o_loc = GetLocation(snapshot, o);
  CHECK(o_loc);
  CHECK_EQ(2, o_loc->line);
  CHECK_EQ(0, o_loc->col);
}

TEST(HeapSnapshotObjectSizes) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  //   -a-> X1 --a
  // x -b-> X2 <-|
  CompileRun(
      "function X(a, b) { this.a = a; this.b = b; }\n"
      "x = new X(new X(), new X());\n"
      "dummy = new X();\n"
      "(function() { x.a.a = x.b; })();");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* x =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "x");
  CHECK(x);
  const v8::HeapGraphNode* x1 =
      GetProperty(env->GetIsolate(), x, v8::HeapGraphEdge::kProperty, "a");
  CHECK(x1);
  const v8::HeapGraphNode* x2 =
      GetProperty(env->GetIsolate(), x, v8::HeapGraphEdge::kProperty, "b");
  CHECK(x2);

  // Test sizes.
  CHECK_NE(0, static_cast<int>(x->GetShallowSize()));
  CHECK_NE(0, static_cast<int>(x1->GetShallowSize()));
  CHECK_NE(0, static_cast<int>(x2->GetShallowSize()));
}


TEST(BoundFunctionInSnapshot) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun(
      "function myFunction(a, b) { this.a = a; this.b = b; }\n"
      "function AAAAA() {}\n"
      "boundFunction = myFunction.bind(new AAAAA(), 20, new Number(12)); \n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* f = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "boundFunction");
  CHECK(f);
  CHECK(v8_str("native_bind")->Equals(env.local(), f->GetName()).FromJust());
  const v8::HeapGraphNode* bindings = GetProperty(
      env->GetIsolate(), f, v8::HeapGraphEdge::kInternal, "bindings");
  CHECK(bindings);
  CHECK_EQ(v8::HeapGraphNode::kArray, bindings->GetType());
  CHECK_EQ(1, bindings->GetChildrenCount());

  const v8::HeapGraphNode* bound_this = GetProperty(
      env->GetIsolate(), f, v8::HeapGraphEdge::kInternal, "bound_this");
  CHECK(bound_this);
  CHECK_EQ(v8::HeapGraphNode::kObject, bound_this->GetType());

  const v8::HeapGraphNode* bound_function = GetProperty(
      env->GetIsolate(), f, v8::HeapGraphEdge::kInternal, "bound_function");
  CHECK(bound_function);
  CHECK_EQ(v8::HeapGraphNode::kClosure, bound_function->GetType());

  const v8::HeapGraphNode* bound_argument = GetProperty(
      env->GetIsolate(), f, v8::HeapGraphEdge::kShortcut, "bound_argument_1");
  CHECK(bound_argument);
  CHECK_EQ(v8::HeapGraphNode::kObject, bound_argument->GetType());
}


TEST(HeapSnapshotEntryChildren) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "function A() { }\n"
      "a = new A;");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  for (int i = 0, count = global->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = global->GetChild(i);
    CHECK_EQ(global, prop->GetFromNode());
  }
  const v8::HeapGraphNode* a =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "a");
  CHECK(a);
  for (int i = 0, count = a->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = a->GetChild(i);
    CHECK_EQ(a, prop->GetFromNode());
  }
}


TEST(HeapSnapshotCodeObjects) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "function lazy(x) { return x - 1; }\n"
      "function compiled(x) { ()=>x; return x + 1; }\n"
      "var anonymous = (function() { return function() { return 0; } })();\n"
      "compiled(1)");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* compiled = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "compiled");
  CHECK(compiled);
  CHECK_EQ(v8::HeapGraphNode::kClosure, compiled->GetType());
  const v8::HeapGraphNode* lazy = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "lazy");
  CHECK(lazy);
  CHECK_EQ(v8::HeapGraphNode::kClosure, lazy->GetType());
  const v8::HeapGraphNode* anonymous = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "anonymous");
  CHECK(anonymous);
  CHECK_EQ(v8::HeapGraphNode::kClosure, anonymous->GetType());
  v8::String::Utf8Value anonymous_name(env->GetIsolate(), anonymous->GetName());
  CHECK_EQ(0, strcmp("", *anonymous_name));

  // Find references to shared function info.
  const v8::HeapGraphNode* compiled_sfi = GetProperty(
      env->GetIsolate(), compiled, v8::HeapGraphEdge::kInternal, "shared");
  CHECK(compiled_sfi);
  const v8::HeapGraphNode* lazy_sfi = GetProperty(
      env->GetIsolate(), lazy, v8::HeapGraphEdge::kInternal, "shared");
  CHECK(lazy_sfi);

  // TODO(leszeks): Check that there's bytecode on the compiled function, but
  // not the lazy function.

  // Verify that non-compiled function doesn't contain references to "x"
  // literal, while compiled function does. The scope info is stored in
  // ScopeInfo objects attached to the SharedFunctionInfo.
  bool compiled_references_x = false, lazy_references_x = false;
  for (int i = 0, count = compiled_sfi->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = compiled_sfi->GetChild(i);
    const v8::HeapGraphNode* node = prop->GetToNode();
    if (node->GetType() == v8::HeapGraphNode::kCode &&
        !strcmp("system / ScopeInfo", GetName(node))) {
      if (HasString(env->GetIsolate(), node, "x")) {
        compiled_references_x = true;
        break;
      }
    }
  }
  for (int i = 0, count = lazy_sfi->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = lazy_sfi->GetChild(i);
    const v8::HeapGraphNode* node = prop->GetToNode();
    if (node->GetType() == v8::HeapGraphNode::kCode &&
        !strcmp("system / ScopeInfo", GetName(node))) {
      if (HasString(env->GetIsolate(), node, "x")) {
        lazy_references_x = true;
        break;
      }
    }
  }
  CHECK(compiled_references_x);
  if (i::v8_flags.lazy) {
    CHECK(!lazy_references_x);
  }
}


TEST(HeapSnapshotHeapNumbers) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun(
      "a = 1;    // a is Smi\n"
      "b = 2.5;  // b is HeapNumber");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(!GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty,
                     "a"));
  const v8::HeapGraphNode* b =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "b");
  CHECK(b);
  CHECK_EQ(v8::HeapGraphNode::kHeapNumber, b->GetType());
}

TEST(HeapSnapshotHeapNumbersCaptureNumericValue) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun(
      "a = 1;    // a is Smi\n"
      "b = 2.5;  // b is HeapNumber");
  const v8::HeapSnapshot* snapshot =
      heap_profiler->TakeHeapSnapshot(nullptr, nullptr, true, true);
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* a =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "a");
  CHECK(a);
  CHECK_EQ(1, a->GetChildrenCount());
  v8::String::Utf8Value value_a(CcTest::isolate(),
                                a->GetChild(0)->GetToNode()->GetName());
  CHECK_EQ(0, strcmp("1", *value_a));

  const v8::HeapGraphNode* b =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "b");
  CHECK(b);
  CHECK_EQ(2, b->GetChildrenCount());
  v8::String::Utf8Value value_b(CcTest::isolate(),
                                b->GetChild(0)->GetToNode()->GetName());
  CHECK_EQ(0, strcmp("2.5", *value_b));
}

TEST(HeapSnapshotHeapBigInts) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun(
      "a = 1n;"
      "b = Object(BigInt(2))");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* a =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "a");
  CHECK(a);
  CHECK_EQ(v8::HeapGraphNode::kBigInt, a->GetType());
  const v8::HeapGraphNode* b =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "b");
  CHECK(b);
  CHECK_EQ(v8::HeapGraphNode::kObject, b->GetType());
}

TEST(HeapSnapshotSlicedString) {
  if (!i::v8_flags.string_slices) return;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun(
      "parent_string = \"123456789.123456789.123456789.123456789.123456789."
      "123456789.123456789.123456789.123456789.123456789."
      "123456789.123456789.123456789.123456789.123456789."
      "123456789.123456789.123456789.123456789.123456789."
      "123456789.123456789.123456789.123456789.123456789."
      "123456789.123456789.123456789.123456789.123456789."
      "123456789.123456789.123456789.123456789.123456789."
      "123456789.123456789.123456789.123456789.123456789.\";"
      "child_string = parent_string.slice(100);");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* parent_string = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "parent_string");
  CHECK(parent_string);
  const v8::HeapGraphNode* child_string = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "child_string");
  CHECK(child_string);
  CHECK_EQ(v8::HeapGraphNode::kSlicedString, child_string->GetType());
  const v8::HeapGraphNode* parent = GetProperty(
      env->GetIsolate(), child_string, v8::HeapGraphEdge::kInternal, "parent");
  CHECK_EQ(parent_string, parent);
  heap_profiler->DeleteAllHeapSnapshots();
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
START_ALLOW_USE_DEPRECATED()

TEST(HeapSnapshotConsString) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> global_template =
      v8::ObjectTemplate::New(isolate);
  global_template->SetInternalFieldCount(1);
  LocalContext env(nullptr, global_template);
  v8::Local<v8::Object> global_proxy = env->Global();
  CHECK_EQ(1, global_proxy->InternalFieldCount());
  v8::Local<v8::Object> global = global_proxy->GetPrototype().As<v8::Object>();
  CHECK_EQ(1, global->InternalFieldCount());

  i::Factory* factory = CcTest::i_isolate()->factory();
  i::Handle<i::String> first = factory->NewStringFromStaticChars("0123456789");
  i::Handle<i::String> second = factory->NewStringFromStaticChars("0123456789");
  i::DirectHandle<i::String> cons_string =
      factory->NewConsString(first, second).ToHandleChecked();

  global_proxy->SetInternalField(0, v8::ToApiHandle<v8::String>(cons_string));
  global->SetInternalField(0, v8::ToApiHandle<v8::String>(cons_string));

  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global_node = GetGlobalObject(snapshot);

  const v8::HeapGraphNode* string_node =
      GetProperty(isolate, global_node, v8::HeapGraphEdge::kInternal, "0");
  CHECK(string_node);
  CHECK_EQ(v8::HeapGraphNode::kConsString, string_node->GetType());

  const v8::HeapGraphNode* first_node =
      GetProperty(isolate, string_node, v8::HeapGraphEdge::kInternal, "first");
  CHECK_EQ(v8::HeapGraphNode::kString, first_node->GetType());

  const v8::HeapGraphNode* second_node =
      GetProperty(isolate, string_node, v8::HeapGraphEdge::kInternal, "second");
  CHECK_EQ(v8::HeapGraphNode::kString, second_node->GetType());

  heap_profiler->DeleteAllHeapSnapshots();
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
END_ALLOW_USE_DEPRECATED()

TEST(HeapSnapshotSymbol) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun("a = Symbol('mySymbol');\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* a =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "a");
  CHECK(a);
  CHECK_EQ(a->GetType(), v8::HeapGraphNode::kSymbol);
  CHECK(v8_str("symbol")->Equals(env.local(), a->GetName()).FromJust());
  const v8::HeapGraphNode* name =
      GetProperty(env->GetIsolate(), a, v8::HeapGraphEdge::kInternal, "name");
  CHECK(name);
  CHECK(v8_str("mySymbol")->Equals(env.local(), name->GetName()).FromJust());
}

TEST(HeapSnapshotWeakCollection) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "k = {}; v = {}; s = 'str';\n"
      "ws = new WeakSet(); ws.add(k); ws.add(v); ws[s] = s;\n"
      "wm = new WeakMap(); wm.set(k, v); wm[s] = s;\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* k =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "k");
  CHECK(k);
  const v8::HeapGraphNode* v =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "v");
  CHECK(v);
  const v8::HeapGraphNode* s =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "s");
  CHECK(s);

  const v8::HeapGraphNode* ws = GetProperty(env->GetIsolate(), global,
                                            v8::HeapGraphEdge::kProperty, "ws");
  CHECK(ws);
  CHECK_EQ(v8::HeapGraphNode::kObject, ws->GetType());
  CHECK(v8_str("WeakSet")->Equals(env.local(), ws->GetName()).FromJust());

  const v8::HeapGraphNode* ws_table =
      GetProperty(env->GetIsolate(), ws, v8::HeapGraphEdge::kInternal, "table");
  CHECK_EQ(v8::HeapGraphNode::kArray, ws_table->GetType());
  CHECK_GT(ws_table->GetChildrenCount(), 0);
  int weak_entries = 0;
  for (int i = 0, count = ws_table->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = ws_table->GetChild(i);
    if (prop->GetType() != v8::HeapGraphEdge::kWeak) continue;
    if (k->GetId() == prop->GetToNode()->GetId()) {
      ++weak_entries;
    }
  }
  CHECK_EQ(1, weak_entries);
  const v8::HeapGraphNode* ws_s =
      GetProperty(env->GetIsolate(), ws, v8::HeapGraphEdge::kProperty, "str");
  CHECK(ws_s);
  CHECK_EQ(s->GetId(), ws_s->GetId());

  const v8::HeapGraphNode* wm = GetProperty(env->GetIsolate(), global,
                                            v8::HeapGraphEdge::kProperty, "wm");
  CHECK(wm);
  CHECK_EQ(v8::HeapGraphNode::kObject, wm->GetType());
  CHECK(v8_str("WeakMap")->Equals(env.local(), wm->GetName()).FromJust());

  const v8::HeapGraphNode* wm_table =
      GetProperty(env->GetIsolate(), wm, v8::HeapGraphEdge::kInternal, "table");
  CHECK_EQ(v8::HeapGraphNode::kArray, wm_table->GetType());
  CHECK_GT(wm_table->GetChildrenCount(), 0);
  weak_entries = 0;
  for (int i = 0, count = wm_table->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = wm_table->GetChild(i);
    if (prop->GetType() != v8::HeapGraphEdge::kWeak) continue;
    const v8::SnapshotObjectId to_node_id = prop->GetToNode()->GetId();
    if (to_node_id == k->GetId() || to_node_id == v->GetId()) {
      ++weak_entries;
    }
  }
  CHECK_EQ(2, weak_entries);  // Key and value are weak.
  const v8::HeapGraphNode* wm_s =
      GetProperty(env->GetIsolate(), wm, v8::HeapGraphEdge::kProperty, "str");
  CHECK(wm_s);
  CHECK_EQ(s->GetId(), wm_s->GetId());
}


TEST(HeapSnapshotCollection) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "k = {}; v = {}; s = 'str';\n"
      "set = new Set(); set.add(k); set.add(v); set[s] = s;\n"
      "map = new Map(); map.set(k, v); map[s] = s;\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* k =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "k");
  CHECK(k);
  const v8::HeapGraphNode* v =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "v");
  CHECK(v);
  const v8::HeapGraphNode* s =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "s");
  CHECK(s);

  const v8::HeapGraphNode* set = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "set");
  CHECK(set);
  CHECK_EQ(v8::HeapGraphNode::kObject, set->GetType());
  CHECK(v8_str("Set")->Equals(env.local(), set->GetName()).FromJust());

  const v8::HeapGraphNode* set_table = GetProperty(
      env->GetIsolate(), set, v8::HeapGraphEdge::kInternal, "table");
  CHECK_EQ(v8::HeapGraphNode::kArray, set_table->GetType());
  CHECK_GT(set_table->GetChildrenCount(), 0);
  int entries = 0;
  for (int i = 0, count = set_table->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = set_table->GetChild(i);
    const v8::SnapshotObjectId to_node_id = prop->GetToNode()->GetId();
    if (to_node_id == k->GetId() || to_node_id == v->GetId()) {
      ++entries;
    }
  }
  CHECK_EQ(2, entries);
  const v8::HeapGraphNode* set_s =
      GetProperty(env->GetIsolate(), set, v8::HeapGraphEdge::kProperty, "str");
  CHECK(set_s);
  CHECK_EQ(s->GetId(), set_s->GetId());

  const v8::HeapGraphNode* map = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "map");
  CHECK(map);
  CHECK_EQ(v8::HeapGraphNode::kObject, map->GetType());
  CHECK(v8_str("Map")->Equals(env.local(), map->GetName()).FromJust());

  const v8::HeapGraphNode* map_table = GetProperty(
      env->GetIsolate(), map, v8::HeapGraphEdge::kInternal, "table");
  CHECK_EQ(v8::HeapGraphNode::kArray, map_table->GetType());
  CHECK_GT(map_table->GetChildrenCount(), 0);
  entries = 0;
  for (int i = 0, count = map_table->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = map_table->GetChild(i);
    const v8::SnapshotObjectId to_node_id = prop->GetToNode()->GetId();
    if (to_node_id == k->GetId() || to_node_id == v->GetId()) {
      ++entries;
    }
  }
  CHECK_EQ(2, entries);
  const v8::HeapGraphNode* map_s =
      GetProperty(env->GetIsolate(), map, v8::HeapGraphEdge::kProperty, "str");
  CHECK(map_s);
  CHECK_EQ(s->GetId(), map_s->GetId());
}

TEST(HeapSnapshotMap) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "function Z() { this.foo = {}; this.bar = 0; }\n"
      "z = new Z();\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* z =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "z");
  CHECK(z);
  const v8::HeapGraphNode* map =
      GetProperty(env->GetIsolate(), z, v8::HeapGraphEdge::kInternal, "map");
  CHECK(map);
  CHECK(
      GetProperty(env->GetIsolate(), map, v8::HeapGraphEdge::kInternal, "map"));
  CHECK(GetProperty(env->GetIsolate(), map, v8::HeapGraphEdge::kInternal,
                    "prototype"));
  const v8::HeapGraphNode* parent_map = GetProperty(
      env->GetIsolate(), map, v8::HeapGraphEdge::kInternal, "back_pointer");
  CHECK(parent_map);

  CHECK(GetProperty(env->GetIsolate(), map, v8::HeapGraphEdge::kInternal,
                    "back_pointer"));
  CHECK(GetProperty(env->GetIsolate(), map, v8::HeapGraphEdge::kInternal,
                    "descriptors"));
  CHECK(GetProperty(env->GetIsolate(), parent_map, v8::HeapGraphEdge::kWeak,
                    "transition"));
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
START_ALLOW_USE_DEPRECATED()

TEST(HeapSnapshotInternalReferences) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> global_template =
      v8::ObjectTemplate::New(isolate);
  global_template->SetInternalFieldCount(2);
  LocalContext env(nullptr, global_template);
  v8::Local<v8::Object> global_proxy = env->Global();
  v8::Local<v8::Object> global = global_proxy->GetPrototype().As<v8::Object>();
  CHECK_EQ(2, global->InternalFieldCount());
  v8::Local<v8::Object> obj = v8::Object::New(isolate);
  global->SetInternalField(0, v8_num(17));
  global->SetInternalField(1, obj);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global_node = GetGlobalObject(snapshot);
  // The first reference will not present, because it's a Smi.
  CHECK(!GetProperty(env->GetIsolate(), global_node,
                     v8::HeapGraphEdge::kInternal, "0"));
  // The second reference is to an object.
  CHECK(GetProperty(env->GetIsolate(), global_node,
                    v8::HeapGraphEdge::kInternal, "1"));
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
END_ALLOW_USE_DEPRECATED()

TEST(HeapSnapshotEphemeron) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "class KeyClass{};\n"
      "class ValueClass{};\n"
      "var wm = new WeakMap();\n"
      "function foo(key) { wm.set(key, new ValueClass()); }\n"
      "var key = new KeyClass();\n"
      "foo(key);");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);

  const v8::HeapGraphNode* key = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "key");
  CHECK(key);
  const v8::HeapGraphNode* weakmap = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "wm");
  CHECK(weakmap);
  const v8::HeapGraphNode* weakmap_table = GetProperty(
      env->GetIsolate(), weakmap, v8::HeapGraphEdge::kInternal, "table");
  CHECK(weakmap_table);
  bool success = false;
  for (int i = 0, count = key->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* edge = key->GetChild(i);
    const v8::HeapGraphNode* child = edge->GetToNode();
    if (!strcmp("ValueClass", GetName(child))) {
      v8::String::Utf8Value edge_name(CcTest::isolate(), edge->GetName());
      std::stringstream end_of_label;
      end_of_label << "/ part of key (KeyClass @" << key->GetId()
                   << ") -> value (ValueClass @" << child->GetId()
                   << ") pair in WeakMap (table @" << weakmap_table->GetId()
                   << ")";
      CHECK(EndsWith(*edge_name, end_of_label.str().c_str()));
      success = true;
      break;
    }
  }
  CHECK(success);
}

TEST(HeapSnapshotAddressReuse) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "function A() {}\n"
      "var a = [];\n"
      "for (var i = 0; i < 10000; ++i)\n"
      "  a[i] = new A();\n");
  const v8::HeapSnapshot* snapshot1 = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot1));
  v8::SnapshotObjectId maxId1 = snapshot1->GetMaxSnapshotJSObjectId();

  CompileRun(
      "for (var i = 0; i < 10000; ++i)\n"
      "  a[i] = new A();\n");
  i::heap::InvokeMajorGC(CcTest::heap());

  const v8::HeapSnapshot* snapshot2 = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot2));
  const v8::HeapGraphNode* global2 = GetGlobalObject(snapshot2);

  const v8::HeapGraphNode* array_node = GetProperty(
      env->GetIsolate(), global2, v8::HeapGraphEdge::kProperty, "a");
  CHECK(array_node);
  int wrong_count = 0;
  for (int i = 0, count = array_node->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = array_node->GetChild(i);
    if (prop->GetType() != v8::HeapGraphEdge::kElement)
      continue;
    v8::SnapshotObjectId id = prop->GetToNode()->GetId();
    if (id < maxId1)
      ++wrong_count;
  }
  CHECK_EQ(0, wrong_count);
}


TEST(HeapEntryIdsAndArrayShift) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "function AnObject() {\n"
      "    this.first = 'first';\n"
      "    this.second = 'second';\n"
      "}\n"
      "var a = new Array();\n"
      "for (var i = 0; i < 10; ++i)\n"
      "  a.push(new AnObject());\n");
  const v8::HeapSnapshot* snapshot1 = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot1));

  CompileRun(
      "for (var i = 0; i < 1; ++i)\n"
      "  a.shift();\n");

  i::heap::InvokeMajorGC(CcTest::heap());

  const v8::HeapSnapshot* snapshot2 = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot2));

  const v8::HeapGraphNode* global1 = GetGlobalObject(snapshot1);
  const v8::HeapGraphNode* global2 = GetGlobalObject(snapshot2);
  CHECK_NE(0u, global1->GetId());
  CHECK_EQ(global1->GetId(), global2->GetId());

  const v8::HeapGraphNode* a1 = GetProperty(env->GetIsolate(), global1,
                                            v8::HeapGraphEdge::kProperty, "a");
  CHECK(a1);
  const v8::HeapGraphNode* k1 = GetProperty(
      env->GetIsolate(), a1, v8::HeapGraphEdge::kInternal, "elements");
  CHECK(k1);
  const v8::HeapGraphNode* a2 = GetProperty(env->GetIsolate(), global2,
                                            v8::HeapGraphEdge::kProperty, "a");
  CHECK(a2);
  const v8::HeapGraphNode* k2 = GetProperty(
      env->GetIsolate(), a2, v8::HeapGraphEdge::kInternal, "elements");
  CHECK(k2);

  CHECK_EQ(a1->GetId(), a2->GetId());
  CHECK_EQ(k1->GetId(), k2->GetId());
}


TEST(HeapEntryIdsAndGC) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "function A() {}\n"
      "function B(x) { this.x = x; }\n"
      "var a = new A();\n"
      "var b = new B(a);");
  const v8::HeapSnapshot* snapshot1 = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot1));

  i::heap::InvokeMajorGC(CcTest::heap());

  const v8::HeapSnapshot* snapshot2 = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot2));

  CHECK_GT(snapshot1->GetMaxSnapshotJSObjectId(), 7000u);
  CHECK(snapshot1->GetMaxSnapshotJSObjectId() <=
        snapshot2->GetMaxSnapshotJSObjectId());

  const v8::HeapGraphNode* global1 = GetGlobalObject(snapshot1);
  const v8::HeapGraphNode* global2 = GetGlobalObject(snapshot2);
  CHECK_NE(0u, global1->GetId());
  CHECK_EQ(global1->GetId(), global2->GetId());
  const v8::HeapGraphNode* A1 = GetProperty(env->GetIsolate(), global1,
                                            v8::HeapGraphEdge::kProperty, "A");
  CHECK(A1);
  const v8::HeapGraphNode* A2 = GetProperty(env->GetIsolate(), global2,
                                            v8::HeapGraphEdge::kProperty, "A");
  CHECK(A2);
  CHECK_NE(0u, A1->GetId());
  CHECK_EQ(A1->GetId(), A2->GetId());
  const v8::HeapGraphNode* B1 = GetProperty(env->GetIsolate(), global1,
                                            v8::HeapGraphEdge::kProperty, "B");
  CHECK(B1);
  const v8::HeapGraphNode* B2 = GetProperty(env->GetIsolate(), global2,
                                            v8::HeapGraphEdge::kProperty, "B");
  CHECK(B2);
  CHECK_NE(0u, B1->GetId());
  CHECK_EQ(B1->GetId(), B2->GetId());
  const v8::HeapGraphNode* a1 = GetProperty(env->GetIsolate(), global1,
                                            v8::HeapGraphEdge::kProperty, "a");
  CHECK(a1);
  const v8::HeapGraphNode* a2 = GetProperty(env->GetIsolate(), global2,
                                            v8::HeapGraphEdge::kProperty, "a");
  CHECK(a2);
  CHECK_NE(0u, a1->GetId());
  CHECK_EQ(a1->GetId(), a2->GetId());
  const v8::HeapGraphNode* b1 = GetProperty(env->GetIsolate(), global1,
                                            v8::HeapGraphEdge::kProperty, "b");
  CHECK(b1);
  const v8::HeapGraphNode* b2 = GetProperty(env->GetIsolate(), global2,
                                            v8::HeapGraphEdge::kProperty, "b");
  CHECK(b2);
  CHECK_NE(0u, b1->GetId());
  CHECK_EQ(b1->GetId(), b2->GetId());
}

TEST(HeapSnapshotJSONSerialization) {
  v8::Isolate* isolate = CcTest::isolate();
  LocalContext env;
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

#define STRING_LITERAL_FOR_TEST \
  "\"String \\n\\r\\u0008\\u0081\\u0101\\u0801\\u8001\""
  CompileRun(
      "function A(s) { this.s = s; }\n"
      "function B(x) { this.x = x; }\n"
      "var a = new A(" STRING_LITERAL_FOR_TEST ");\n"
      "var b = new B(a);");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  v8::internal::TestJSONStream stream;
  snapshot->Serialize(&stream, v8::HeapSnapshot::kJSON);
  CHECK_GT(stream.size(), 0);
  CHECK_EQ(1, stream.eos_signaled());
  v8::base::ScopedVector<char> json(stream.size());
  stream.WriteTo(json);

  // Verify that snapshot string is valid JSON.
  v8::internal::OneByteResource* json_res =
      new v8::internal::OneByteResource(json);
  v8::Local<v8::String> json_string =
      v8::String::NewExternalOneByte(env->GetIsolate(), json_res)
          .ToLocalChecked();
  v8::Local<v8::Context> context = v8::Context::New(env->GetIsolate());
  v8::Local<v8::Value> snapshot_parse_result =
      v8::JSON::Parse(context, json_string).ToLocalChecked();
  CHECK(snapshot_parse_result->IsObject());

  // Verify that snapshot object has required fields.
  v8::Local<v8::Object> parsed_snapshot =
      snapshot_parse_result.As<v8::Object>();
  CHECK(parsed_snapshot->Get(env.local(), v8_str("snapshot"))
            .ToLocalChecked()
            ->IsObject());
  CHECK(parsed_snapshot->Get(env.local(), v8_str("nodes"))
            .ToLocalChecked()
            ->IsArray());
  CHECK(parsed_snapshot->Get(env.local(), v8_str("edges"))
            .ToLocalChecked()
            ->IsArray());
  CHECK(parsed_snapshot->Get(env.local(), v8_str("locations"))
            .ToLocalChecked()
            ->IsArray());
  CHECK(parsed_snapshot->Get(env.local(), v8_str("strings"))
            .ToLocalChecked()
            ->IsArray());

  // Get node and edge "member" offsets.
  env->Global()->Set(env.local(), v8_str("parsed"), parsed_snapshot).FromJust();
  v8::Local<v8::Value> meta_analysis_result = CompileRun(
      "var meta = parsed.snapshot.meta;\n"
      "var edge_count_offset = meta.node_fields.indexOf('edge_count');\n"
      "var node_fields_count = meta.node_fields.length;\n"
      "var edge_fields_count = meta.edge_fields.length;\n"
      "var edge_type_offset = meta.edge_fields.indexOf('type');\n"
      "var edge_name_offset = meta.edge_fields.indexOf('name_or_index');\n"
      "var edge_to_node_offset = meta.edge_fields.indexOf('to_node');\n"
      "var property_type ="
      "    meta.edge_types[edge_type_offset].indexOf('property');\n"
      "var shortcut_type ="
      "    meta.edge_types[edge_type_offset].indexOf('shortcut');\n"
      "var node_count = parsed.nodes.length / node_fields_count;\n"
      "var first_edge_indexes = parsed.first_edge_indexes = [];\n"
      "for (var i = 0, first_edge_index = 0; i < node_count; ++i) {\n"
      "  first_edge_indexes[i] = first_edge_index;\n"
      "  first_edge_index += edge_fields_count *\n"
      "      parsed.nodes[i * node_fields_count + edge_count_offset];\n"
      "}\n"
      "first_edge_indexes[node_count] = first_edge_index;\n");
  CHECK(!meta_analysis_result.IsEmpty());

  // A helper function for processing encoded nodes.
  CompileRun(
      "function GetChildPosByProperty(pos, prop_name, prop_type) {\n"
      "  var nodes = parsed.nodes;\n"
      "  var edges = parsed.edges;\n"
      "  var strings = parsed.strings;\n"
      "  var node_ordinal = pos / node_fields_count;\n"
      "  for (var i = parsed.first_edge_indexes[node_ordinal],\n"
      "      count = parsed.first_edge_indexes[node_ordinal + 1];\n"
      "      i < count; i += edge_fields_count) {\n"
      "    if (edges[i + edge_type_offset] === prop_type\n"
      "        && strings[edges[i + edge_name_offset]] === prop_name)\n"
      "      return edges[i + edge_to_node_offset];\n"
      "  }\n"
      "  return null;\n"
      "}\n");
  // Get the string index using the path: <root> -> <global>.b.x.s
  v8::Local<v8::Value> string_obj_pos_val = CompileRun(
      "GetChildPosByProperty(\n"
      "  GetChildPosByProperty(\n"
      "    GetChildPosByProperty("
      "      parsed.edges[edge_fields_count + edge_to_node_offset],"
      "      \"b\", property_type),\n"
      "    \"x\", property_type),"
      "  \"s\", property_type)");
  CHECK(!string_obj_pos_val.IsEmpty());
  int string_obj_pos = static_cast<int>(
      string_obj_pos_val->ToNumber(env.local()).ToLocalChecked()->Value());
  v8::Local<v8::Object> nodes_array =
      parsed_snapshot->Get(env.local(), v8_str("nodes"))
          .ToLocalChecked()
          ->ToObject(env.local())
          .ToLocalChecked();
  int string_index =
      static_cast<int>(nodes_array->Get(env.local(), string_obj_pos + 1)
                           .ToLocalChecked()
                           ->ToNumber(env.local())
                           .ToLocalChecked()
                           ->Value());
  CHECK_GT(string_index, 0);
  v8::Local<v8::Object> strings_array =
      parsed_snapshot->Get(env.local(), v8_str("strings"))
          .ToLocalChecked()
          ->ToObject(env.local())
          .ToLocalChecked();
  v8::Local<v8::String> string = strings_array->Get(env.local(), string_index)
                                     .ToLocalChecked()
                                     ->ToString(env.local())
                                     .ToLocalChecked();
  v8::Local<v8::String> ref_string = CompileRun(STRING_LITERAL_FOR_TEST)
                                         ->ToString(env.local())
                                         .ToLocalChecked();
#undef STRING_LITERAL_FOR_TEST
  CHECK_EQ(0, strcmp(*v8::String::Utf8Value(env->GetIsolate(), ref_string),
                     *v8::String::Utf8Value(env->GetIsolate(), string)));
}


TEST(HeapSnapshotJSONSerializationAborting) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  v8::internal::TestJSONStream stream(5);
  snapshot->Serialize(&stream, v8::HeapSnapshot::kJSON);
  CHECK_GT(stream.size(), 0);
  CHECK_EQ(0, stream.eos_signaled());
}

namespace {

class TestStatsStream : public v8::OutputStream {
 public:
  TestStatsStream()
    : eos_signaled_(0),
      updates_written_(0),
      entries_count_(0),
      entries_size_(0),
      intervals_count_(0),
      first_interval_index_(-1) { }
  TestStatsStream(const TestStatsStream& stream) V8_NOEXCEPT = default;
  ~TestStatsStream() override = default;
  void EndOfStream() override { ++eos_signaled_; }
  WriteResult WriteAsciiChunk(char* buffer, int chars_written) override {
    UNREACHABLE();
  }
  WriteResult WriteHeapStatsChunk(v8::HeapStatsUpdate* buffer,
                                  int updates_written) override {
    ++intervals_count_;
    CHECK(updates_written);
    updates_written_ += updates_written;
    entries_count_ = 0;
    if (first_interval_index_ == -1 && updates_written != 0)
      first_interval_index_ = buffer[0].index;
    for (int i = 0; i < updates_written; ++i) {
      entries_count_ += buffer[i].count;
      entries_size_ += buffer[i].size;
    }

    return kContinue;
  }
  int eos_signaled() { return eos_signaled_; }
  int updates_written() { return updates_written_; }
  uint32_t entries_count() const { return entries_count_; }
  uint32_t entries_size() const { return entries_size_; }
  int intervals_count() const { return intervals_count_; }
  int first_interval_index() const { return first_interval_index_; }

 private:
  int eos_signaled_;
  int updates_written_;
  uint32_t entries_count_;
  uint32_t entries_size_;
  int intervals_count_;
  int first_interval_index_;
};

}  // namespace

static TestStatsStream GetHeapStatsUpdate(
    v8::HeapProfiler* heap_profiler,
    v8::SnapshotObjectId* object_id = nullptr) {
  TestStatsStream stream;
  int64_t timestamp = -1;
  v8::SnapshotObjectId last_seen_id =
      heap_profiler->GetHeapStats(&stream, &timestamp);
  if (object_id)
    *object_id = last_seen_id;
  CHECK_NE(-1, timestamp);
  CHECK_EQ(1, stream.eos_signaled());
  return stream;
}


TEST(HeapSnapshotObjectsStats) {
  // Concurrent allocation and conservative stack scanning might break results.
  i::v8_flags.stress_concurrent_allocation = false;
  i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      CcTest::heap());

  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  heap_profiler->StartTrackingHeapObjects();
  // We have to call GC 6 times. In other case the garbage will be
  // the reason of flakiness.
  for (int i = 0; i < 6; ++i) {
    i::heap::InvokeMajorGC(CcTest::heap());
  }

  v8::SnapshotObjectId initial_id;
  {
    // Single chunk of data expected in update. Initial data.
    TestStatsStream stats_update = GetHeapStatsUpdate(heap_profiler,
                                                      &initial_id);
    CHECK_EQ(1, stats_update.intervals_count());
    CHECK_EQ(1, stats_update.updates_written());
    CHECK_LT(0u, stats_update.entries_size());
    CHECK_EQ(0, stats_update.first_interval_index());
  }

  // No data expected in update because nothing has happened.
  v8::SnapshotObjectId same_id;
  CHECK_EQ(0, GetHeapStatsUpdate(heap_profiler, &same_id).updates_written());
  CHECK_EQ(initial_id, same_id);

  {
    v8::SnapshotObjectId additional_string_id;
    v8::HandleScope inner_scope_1(isolate);
    v8_str("string1");
    {
      // Single chunk of data with one new entry expected in update.
      TestStatsStream stats_update = GetHeapStatsUpdate(heap_profiler,
                                                        &additional_string_id);
      CHECK_LT(same_id, additional_string_id);
      CHECK_EQ(1, stats_update.intervals_count());
      CHECK_EQ(1, stats_update.updates_written());
      CHECK_LT(0u, stats_update.entries_size());
      CHECK_EQ(1u, stats_update.entries_count());
      CHECK_EQ(2, stats_update.first_interval_index());
    }

    // No data expected in update because nothing happened.
    v8::SnapshotObjectId last_id;
    CHECK_EQ(0, GetHeapStatsUpdate(heap_profiler, &last_id).updates_written());
    CHECK_EQ(additional_string_id, last_id);

    {
      v8::HandleScope inner_scope_2(isolate);
      v8_str("string2");

      uint32_t entries_size;
      {
        v8::HandleScope inner_scope_3(isolate);
        v8_str("string3");
        v8_str("string4");

        {
          // Single chunk of data with three new entries expected in update.
          TestStatsStream stats_update = GetHeapStatsUpdate(heap_profiler);
          CHECK_EQ(1, stats_update.intervals_count());
          CHECK_EQ(1, stats_update.updates_written());
          CHECK_LT(0u, entries_size = stats_update.entries_size());
          CHECK_EQ(3u, stats_update.entries_count());
          CHECK_EQ(4, stats_update.first_interval_index());
        }
      }

      {
        // Single chunk of data with two left entries expected in update.
        TestStatsStream stats_update = GetHeapStatsUpdate(heap_profiler);
        CHECK_EQ(1, stats_update.intervals_count());
        CHECK_EQ(1, stats_update.updates_written());
        CHECK_GT(entries_size, stats_update.entries_size());
        CHECK_EQ(1u, stats_update.entries_count());
        // Two strings from forth interval were released.
        CHECK_EQ(4, stats_update.first_interval_index());
      }
    }

    {
      // Single chunk of data with 0 left entries expected in update.
      TestStatsStream stats_update = GetHeapStatsUpdate(heap_profiler);
      CHECK_EQ(1, stats_update.intervals_count());
      CHECK_EQ(1, stats_update.updates_written());
      CHECK_EQ(0u, stats_update.entries_size());
      CHECK_EQ(0u, stats_update.entries_count());
      // The last string from forth interval was released.
      CHECK_EQ(4, stats_update.first_interval_index());
    }
  }
  {
    // Single chunk of data with 0 left entries expected in update.
    TestStatsStream stats_update = GetHeapStatsUpdate(heap_profiler);
    CHECK_EQ(1, stats_update.intervals_count());
    CHECK_EQ(1, stats_update.updates_written());
    CHECK_EQ(0u, stats_update.entries_size());
    CHECK_EQ(0u, stats_update.entries_count());
    // The only string from the second interval was released.
    CHECK_EQ(2, stats_update.first_interval_index());
  }

  // With conservative stack scanning disabled and with direct locals, a
  // v8::Local<v8::Array> here would be reclaimed by GetHeapStatsUpdate.
  v8::Persistent<v8::Array> array(isolate, v8::Array::New(isolate));
  CHECK_EQ(0u, array.Get(isolate)->Length());
  // Force array's buffer allocation.
  array.Get(isolate)->Set(env.local(), 2, v8_num(7)).FromJust();

  uint32_t entries_size;
  {
    // Single chunk of data with 2 entries expected in update.
    TestStatsStream stats_update = GetHeapStatsUpdate(heap_profiler);
    CHECK_EQ(1, stats_update.intervals_count());
    CHECK_EQ(1, stats_update.updates_written());
    CHECK_LT(0u, entries_size = stats_update.entries_size());
    // They are the array and its buffer.
    CHECK_EQ(2u, stats_update.entries_count());
    CHECK_EQ(8, stats_update.first_interval_index());
  }

  for (int i = 0; i < 100; ++i)
    array.Get(isolate)->Set(env.local(), i, v8_num(i)).FromJust();

  {
    // Single chunk of data with 1 entry expected in update.
    TestStatsStream stats_update = GetHeapStatsUpdate(heap_profiler);
    CHECK_EQ(1, stats_update.intervals_count());
    // The first interval was changed because old buffer was collected.
    // The second interval was changed because new buffer was allocated.
    CHECK_EQ(2, stats_update.updates_written());
    CHECK_LT(entries_size, stats_update.entries_size());
    CHECK_EQ(2u, stats_update.entries_count());
    CHECK_EQ(8, stats_update.first_interval_index());
  }

  heap_profiler->StopTrackingHeapObjects();
}


TEST(HeapObjectIds) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  const int kLength = 10;
  v8::Local<v8::Object> objects[kLength];
  v8::SnapshotObjectId ids[kLength];

  heap_profiler->StartTrackingHeapObjects(false);

  for (int i = 0; i < kLength; i++) {
    objects[i] = v8::Object::New(isolate);
  }
  GetHeapStatsUpdate(heap_profiler);

  for (int i = 0; i < kLength; i++) {
    v8::SnapshotObjectId id = heap_profiler->GetObjectId(objects[i]);
    CHECK_NE(v8::HeapProfiler::kUnknownObjectId, id);
    ids[i] = id;
  }

  heap_profiler->StopTrackingHeapObjects();
  i::heap::InvokeMemoryReducingMajorGCs(CcTest::heap());

  for (int i = 0; i < kLength; i++) {
    v8::SnapshotObjectId id = heap_profiler->GetObjectId(objects[i]);
    CHECK_EQ(ids[i], id);
    v8::Local<v8::Value> obj = heap_profiler->FindObjectById(ids[i]);
    CHECK(objects[i]->Equals(env.local(), obj).FromJust());
  }

  heap_profiler->ClearObjectIds();
  for (int i = 0; i < kLength; i++) {
    v8::SnapshotObjectId id = heap_profiler->GetObjectId(objects[i]);
    CHECK_EQ(v8::HeapProfiler::kUnknownObjectId, id);
    v8::Local<v8::Value> obj = heap_profiler->FindObjectById(ids[i]);
    CHECK(obj.IsEmpty());
  }
}


static void CheckChildrenIds(const v8::HeapSnapshot* snapshot,
                             const v8::HeapGraphNode* node,
                             int level, int max_level) {
  if (level > max_level) return;
  CHECK_EQ(node, snapshot->GetNodeById(node->GetId()));
  for (int i = 0, count = node->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = node->GetChild(i);
    const v8::HeapGraphNode* child =
        snapshot->GetNodeById(prop->GetToNode()->GetId());
    CHECK_EQ(prop->GetToNode()->GetId(), child->GetId());
    CHECK_EQ(prop->GetToNode(), child);
    CheckChildrenIds(snapshot, child, level + 1, max_level);
  }
}


TEST(HeapSnapshotGetNodeById) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* root = snapshot->GetRoot();
  CheckChildrenIds(snapshot, root, 0, 3);
  // Check a big id, which should not exist yet.
  CHECK(!snapshot->GetNodeById(0x1000000UL));
}


TEST(HeapSnapshotGetSnapshotObjectId) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun("globalObject = {};\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* global_object = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "globalObject");
  CHECK(global_object);

  v8::Local<v8::Value> globalObjectHandle =
      env->Global()->Get(env.local(), v8_str("globalObject")).ToLocalChecked();
  CHECK(!globalObjectHandle.IsEmpty());
  CHECK(globalObjectHandle->IsObject());

  v8::SnapshotObjectId id = heap_profiler->GetObjectId(globalObjectHandle);
  CHECK_NE(v8::HeapProfiler::kUnknownObjectId, id);
  CHECK_EQ(id, global_object->GetId());
}


TEST(HeapSnapshotUnknownSnapshotObjectId) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun("globalObject = {};\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* node =
      snapshot->GetNodeById(v8::HeapProfiler::kUnknownObjectId);
  CHECK(!node);
}


namespace {

class TestActivityControl : public v8::ActivityControl {
 public:
  explicit TestActivityControl(int abort_count)
      : done_(0),
        total_(0),
        abort_count_(abort_count),
        reported_finish_(false) {}
  ControlOption ReportProgressValue(uint32_t done, uint32_t total) override {
    done_ = done;
    total_ = total;
    CHECK_LE(done_, total_);
    if (done_ == total_) {
      CHECK(!reported_finish_);
      reported_finish_ = true;
    }
    return --abort_count_ != 0 ? kContinue : kAbort;
  }
  int done() { return done_; }
  int total() { return total_; }

 private:
  int done_;
  int total_;
  int abort_count_;
  bool reported_finish_;
};

}  // namespace


TEST(TakeHeapSnapshotAborting) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  const int snapshots_count = heap_profiler->GetSnapshotCount();
  TestActivityControl aborting_control(1);
  const v8::HeapSnapshot* no_snapshot =
      heap_profiler->TakeHeapSnapshot(&aborting_control);
  CHECK(!no_snapshot);
  CHECK_EQ(snapshots_count, heap_profiler->GetSnapshotCount());
  CHECK_GT(aborting_control.total(), aborting_control.done());

  TestActivityControl control(-1);  // Don't abort.
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot(&control);
  CHECK(ValidateSnapshot(snapshot));

  CHECK(snapshot);
  CHECK_EQ(snapshots_count + 1, heap_profiler->GetSnapshotCount());
  CHECK_EQ(control.total(), control.done());
  CHECK_GT(control.total(), 0);
}

TEST(TakeHeapSnapshotReportFinishOnce) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  TestActivityControl control(-1);
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot(&control);
  CHECK(ValidateSnapshot(snapshot));
  CHECK_EQ(control.total(), control.done());
  CHECK_GT(control.total(), 0);
}

namespace {

class EmbedderGraphBuilder : public v8::PersistentHandleVisitor {
 public:
  class Node : public v8::EmbedderGraph::Node {
   public:
    Node(const char* name, size_t size) : name_(name), size_(size) {}
    // v8::EmbedderGraph::Node
    const char* Name() override { return name_; }
    size_t SizeInBytes() override { return size_; }

   private:
    const char* name_;
    size_t size_;
  };

  class Group : public Node {
   public:
    explicit Group(const char* name) : Node(name, 0) {}
    // v8::EmbedderGraph::EmbedderNode
    bool IsRootNode() override { return true; }
  };

  EmbedderGraphBuilder(v8::Isolate* isolate, v8::EmbedderGraph* graph)
      : isolate_(isolate), graph_(graph) {
    classid_to_group_[0] = nullptr;
    classid_to_group_[1] =
        graph->AddNode(std::unique_ptr<Group>(new Group("aaa-group")));
    classid_to_group_[2] =
        graph->AddNode(std::unique_ptr<Group>(new Group("ccc-group")));
  }

  static void BuildEmbedderGraph(v8::Isolate* isolate, v8::EmbedderGraph* graph,
                                 void* data) {
    EmbedderGraphBuilder builder(isolate, graph);
    reinterpret_cast<i::Isolate*>(isolate)
        ->global_handles()
        ->IterateAllRootsForTesting(&builder);
  }

  void VisitPersistentHandle(v8::Persistent<v8::Value>* value,
                             uint16_t class_id) override {
    v8::Local<v8::Value> wrapper = v8::Local<v8::Value>::New(
        isolate_, v8::Persistent<v8::Value>::Cast(*value));
    if (class_id == 1) {
      if (wrapper->IsString()) {
        v8::String::Utf8Value utf8(CcTest::isolate(), wrapper);
        DCHECK(!strcmp(*utf8, "AAA") || !strcmp(*utf8, "BBB"));
        v8::EmbedderGraph::Node* node = graph_->V8Node(wrapper);
        v8::EmbedderGraph::Node* group = classid_to_group_[1];
        graph_->AddEdge(node, group);
        graph_->AddEdge(group, node);
      }
    } else if (class_id == 2) {
      if (wrapper->IsString()) {
        v8::String::Utf8Value utf8(CcTest::isolate(), wrapper);
        DCHECK(!strcmp(*utf8, "CCC"));
        v8::EmbedderGraph::Node* node = graph_->V8Node(wrapper);
        v8::EmbedderGraph::Node* group = classid_to_group_[2];
        graph_->AddEdge(node, group);
        graph_->AddEdge(group, node);
      }
    }
  }

 private:
  v8::Isolate* isolate_;
  v8::EmbedderGraph* graph_;
  v8::EmbedderGraph::Node* classid_to_group_[3];
};

}  // namespace


static const v8::HeapGraphNode* GetNode(const v8::HeapGraphNode* parent,
                                        v8::HeapGraphNode::Type type,
                                        const char* name) {
  for (int i = 0, count = parent->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphNode* node = parent->GetChild(i)->GetToNode();
    if (node->GetType() == type && strcmp(name,
               const_cast<i::HeapEntry*>(
                   reinterpret_cast<const i::HeapEntry*>(node))->name()) == 0) {
      return node;
    }
  }
  return nullptr;
}


TEST(HeapSnapshotRetainedObjectInfo) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  heap_profiler->AddBuildEmbedderGraphCallback(
      EmbedderGraphBuilder::BuildEmbedderGraph, nullptr);
  v8::Persistent<v8::String> p_AAA(isolate, v8_str("AAA"));
  p_AAA.SetWrapperClassId(1);
  v8::Persistent<v8::String> p_BBB(isolate, v8_str("BBB"));
  p_BBB.SetWrapperClassId(1);
  v8::Persistent<v8::String> p_CCC(isolate, v8_str("CCC"));
  p_CCC.SetWrapperClassId(2);
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  const v8::HeapGraphNode* native_group_aaa =
      GetNode(snapshot->GetRoot(), v8::HeapGraphNode::kSynthetic, "aaa-group");
  CHECK_NOT_NULL(native_group_aaa);
  const v8::HeapGraphNode* native_group_ccc =
      GetNode(snapshot->GetRoot(), v8::HeapGraphNode::kSynthetic, "ccc-group");
  CHECK_NOT_NULL(native_group_ccc);

  const v8::HeapGraphNode* n_AAA =
      GetNode(native_group_aaa, v8::HeapGraphNode::kString, "AAA");
  CHECK(n_AAA);
  const v8::HeapGraphNode* n_BBB =
      GetNode(native_group_aaa, v8::HeapGraphNode::kString, "BBB");
  CHECK(n_BBB);
  const v8::HeapGraphNode* n_CCC =
      GetNode(native_group_ccc, v8::HeapGraphNode::kString, "CCC");
  CHECK(n_CCC);

  CHECK_EQ(native_group_aaa, GetChildByName(n_AAA, "aaa-group"));
  CHECK_EQ(native_group_aaa, GetChildByName(n_BBB, "aaa-group"));
  CHECK_EQ(native_group_ccc, GetChildByName(n_CCC, "ccc-group"));
}

namespace {

class EmbedderGraphBuilderForNativeSnapshotObjectId final {
 public:
  class RegularNode : public v8::EmbedderGraph::Node {
   public:
    RegularNode(v8::NativeObject native_object, const char* name, size_t size,
                Node* wrapper_node)
        : name_(name),
          size_(size),
          native_object_(native_object),
          wrapper_node_(wrapper_node) {}
    // v8::EmbedderGraph::Node
    const char* Name() override { return name_; }
    size_t SizeInBytes() override { return size_; }
    Node* WrapperNode() override { return wrapper_node_; }
    v8::NativeObject GetNativeObject() override {
      return native_object_ ? native_object_ : this;
    }

   private:
    const char* name_;
    size_t size_;
    v8::NativeObject native_object_;
    Node* wrapper_node_;
  };

  class RootNode : public RegularNode {
   public:
    explicit RootNode(const char* name)
        : RegularNode(nullptr, name, 0, nullptr) {}
    // v8::EmbedderGraph::EmbedderNode
    bool IsRootNode() override { return true; }
  };

  struct BuildParameter {
    v8::Persistent<v8::String>* wrapper;
    void* native1;
    void* native2;
  };

  static void BuildEmbedderGraph(v8::Isolate* isolate, v8::EmbedderGraph* graph,
                                 void* data) {
    BuildParameter* parameter = reinterpret_cast<BuildParameter*>(data);
    v8::Local<v8::String> local_str =
        v8::Local<v8::String>::New(isolate, *(parameter->wrapper));
    auto* v8_node = graph->V8Node(local_str.As<v8::Value>());
    CHECK(!v8_node->IsEmbedderNode());
    auto* root_node =
        graph->AddNode(std::unique_ptr<RootNode>(new RootNode("root")));
    auto* non_merged_node = graph->AddNode(std::unique_ptr<RegularNode>(
        new RegularNode(parameter->native1, "non-merged", 0, nullptr)));
    auto* merged_node = graph->AddNode(std::unique_ptr<RegularNode>(
        new RegularNode(parameter->native2, "merged", 0, v8_node)));
    graph->AddEdge(root_node, non_merged_node);
    graph->AddEdge(root_node, merged_node);
  }
};

}  // namespace

TEST(NativeSnapshotObjectId) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  v8::Persistent<v8::String> wrapper(isolate, v8_str("wrapper"));
  int native1;
  int native2;

  EmbedderGraphBuilderForNativeSnapshotObjectId::BuildParameter parameter{
      &wrapper, &native1, &native2};
  heap_profiler->AddBuildEmbedderGraphCallback(
      EmbedderGraphBuilderForNativeSnapshotObjectId::BuildEmbedderGraph,
      &parameter);
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  v8::SnapshotObjectId non_merged_id = heap_profiler->GetObjectId(&native1);
  CHECK_NE(v8::HeapProfiler::kUnknownObjectId, non_merged_id);
  v8::SnapshotObjectId merged_id = heap_profiler->GetObjectId(&native2);
  CHECK_NE(v8::HeapProfiler::kUnknownObjectId, merged_id);
  CHECK_NE(non_merged_id, merged_id);
  const v8::HeapGraphNode* non_merged_node =
      snapshot->GetNodeById(non_merged_id);
  CHECK_NOT_NULL(non_merged_node);
  const v8::HeapGraphNode* merged_node = snapshot->GetNodeById(merged_id);
  CHECK_NOT_NULL(merged_node);

  heap_profiler->ClearObjectIds();
  CHECK_EQ(v8::HeapProfiler::kUnknownObjectId,
           heap_profiler->GetObjectId(&native1));
  CHECK_EQ(v8::HeapProfiler::kUnknownObjectId,
           heap_profiler->GetObjectId(&native2));
}

TEST(NativeSnapshotObjectIdMoving) {
  if (i::v8_flags.enable_third_party_heap) return;
  // Required to allow moving specific objects.
  i::ManualGCScope manual_gc_scope;
  i::heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  // Concurrent allocation writes page flags in a racy way.
  i::v8_flags.stress_concurrent_allocation = false;

  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();
  heap_profiler->StartTrackingHeapObjects(true);

  v8::Persistent<v8::String> wrapper(isolate, v8_str("wrapper"));
  int native1;
  int native2;

  EmbedderGraphBuilderForNativeSnapshotObjectId::BuildParameter parameter{
      &wrapper, &native1, &native2};
  heap_profiler->AddBuildEmbedderGraphCallback(
      EmbedderGraphBuilderForNativeSnapshotObjectId::BuildEmbedderGraph,
      &parameter);
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  v8::SnapshotObjectId non_merged_id = heap_profiler->GetObjectId(&native1);
  CHECK_NE(v8::HeapProfiler::kUnknownObjectId, non_merged_id);
  v8::SnapshotObjectId merged_id = heap_profiler->GetObjectId(&native2);
  CHECK_NE(v8::HeapProfiler::kUnknownObjectId, merged_id);
  CHECK_NE(non_merged_id, merged_id);
  const v8::HeapGraphNode* non_merged_node =
      snapshot->GetNodeById(non_merged_id);
  CHECK_NOT_NULL(non_merged_node);
  const v8::HeapGraphNode* merged_node = snapshot->GetNodeById(merged_id);
  CHECK_NOT_NULL(merged_node);

  {
    v8::HandleScope inner_scope(isolate);
    auto local = v8::Local<v8::String>::New(isolate, wrapper);
    i::DirectHandle<i::String> internal = i::Cast<i::String>(
        v8::Utils::OpenDirectHandle(*v8::Local<v8::String>::Cast(local)));
    i::heap::ForceEvacuationCandidate(
        i::PageMetadata::FromHeapObject(*internal));
  }
  i::heap::InvokeMajorGC(CcTest::heap());

  non_merged_id = heap_profiler->GetObjectId(&native1);
  CHECK_NE(v8::HeapProfiler::kUnknownObjectId, non_merged_id);
  merged_id = heap_profiler->GetObjectId(&native2);
  CHECK_NE(v8::HeapProfiler::kUnknownObjectId, merged_id);
  CHECK_NE(non_merged_id, merged_id);

  heap_profiler->StopTrackingHeapObjects();
}

TEST(DeleteAllHeapSnapshots) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CHECK_EQ(0, heap_profiler->GetSnapshotCount());
  heap_profiler->DeleteAllHeapSnapshots();
  CHECK_EQ(0, heap_profiler->GetSnapshotCount());
  CHECK(heap_profiler->TakeHeapSnapshot());
  CHECK_EQ(1, heap_profiler->GetSnapshotCount());
  heap_profiler->DeleteAllHeapSnapshots();
  CHECK_EQ(0, heap_profiler->GetSnapshotCount());
  CHECK(heap_profiler->TakeHeapSnapshot());
  CHECK(heap_profiler->TakeHeapSnapshot());
  CHECK_EQ(2, heap_profiler->GetSnapshotCount());
  heap_profiler->DeleteAllHeapSnapshots();
  CHECK_EQ(0, heap_profiler->GetSnapshotCount());
}


static bool FindHeapSnapshot(v8::HeapProfiler* profiler,
                             const v8::HeapSnapshot* snapshot) {
  int length = profiler->GetSnapshotCount();
  for (int i = 0; i < length; i++) {
    if (snapshot == profiler->GetHeapSnapshot(i)) return true;
  }
  return false;
}


TEST(DeleteHeapSnapshot) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CHECK_EQ(0, heap_profiler->GetSnapshotCount());
  const v8::HeapSnapshot* s1 = heap_profiler->TakeHeapSnapshot();

  CHECK(s1);
  CHECK_EQ(1, heap_profiler->GetSnapshotCount());
  CHECK(FindHeapSnapshot(heap_profiler, s1));
  const_cast<v8::HeapSnapshot*>(s1)->Delete();
  CHECK_EQ(0, heap_profiler->GetSnapshotCount());
  CHECK(!FindHeapSnapshot(heap_profiler, s1));

  const v8::HeapSnapshot* s2 = heap_profiler->TakeHeapSnapshot();
  CHECK(s2);
  CHECK_EQ(1, heap_profiler->GetSnapshotCount());
  CHECK(FindHeapSnapshot(heap_profiler, s2));
  const v8::HeapSnapshot* s3 = heap_profiler->TakeHeapSnapshot();
  CHECK(s3);
  CHECK_EQ(2, heap_profiler->GetSnapshotCount());
  CHECK_NE(s2, s3);
  CHECK(FindHeapSnapshot(heap_profiler, s3));
  const_cast<v8::HeapSnapshot*>(s2)->Delete();
  CHECK_EQ(1, heap_profiler->GetSnapshotCount());
  CHECK(!FindHeapSnapshot(heap_profiler, s2));
  CHECK(FindHeapSnapshot(heap_profiler, s3));
  const_cast<v8::HeapSnapshot*>(s3)->Delete();
  CHECK_EQ(0, heap_profiler->GetSnapshotCount());
  CHECK(!FindHeapSnapshot(heap_profiler, s3));
}


class NameResolver : public v8::HeapProfiler::ObjectNameResolver {
 public:
  const char* GetName(v8::Local<v8::Object> object) override {
    return "Global object name";
  }
};


TEST(GlobalObjectName) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun("document = { URL:\"abcdefgh\" };");

  NameResolver name_resolver;
  const v8::HeapSnapshot* snapshot =
      heap_profiler->TakeHeapSnapshot(nullptr, &name_resolver);
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(global);
  CHECK_EQ(0,
           strcmp("Object / Global object name",
                  const_cast<i::HeapEntry*>(
                      reinterpret_cast<const i::HeapEntry*>(global))->name()));
}


TEST(GlobalObjectFields) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun("obj = {};");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* global_proxy = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kInternal, "global_proxy");
  CHECK(global_proxy);
}


TEST(NoHandleLeaks) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun("document = { URL:\"abcdefgh\" };");

  i::Isolate* isolate = CcTest::i_isolate();
  int count_before = i::HandleScope::NumberOfHandles(isolate);
  heap_profiler->TakeHeapSnapshot();
  int count_after = i::HandleScope::NumberOfHandles(isolate);
  CHECK_EQ(count_before, count_after);
}


TEST(NodesIteration) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(global);
  // Verify that we can find this object by iteration.
  const int nodes_count = snapshot->GetNodesCount();
  int count = 0;
  for (int i = 0; i < nodes_count; ++i) {
    if (snapshot->GetNode(i) == global)
      ++count;
  }
  CHECK_EQ(1, count);
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
START_ALLOW_USE_DEPRECATED()

TEST(GetHeapValueForNode) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun("a = { s_prop: \'value\', n_prop: \'value2\' };");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(heap_profiler->FindObjectById(global->GetId())->IsObject());
  v8::Local<v8::Object> js_global =
      env->Global()->GetPrototype().As<v8::Object>();
  CHECK_EQ(js_global, heap_profiler->FindObjectById(global->GetId()));
  const v8::HeapGraphNode* obj =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "a");
  CHECK(heap_profiler->FindObjectById(obj->GetId())->IsObject());
  v8::Local<v8::Object> js_obj = js_global->Get(env.local(), v8_str("a"))
                                     .ToLocalChecked()
                                     .As<v8::Object>();
  CHECK_EQ(js_obj, heap_profiler->FindObjectById(obj->GetId()));
  const v8::HeapGraphNode* s_prop = GetProperty(
      env->GetIsolate(), obj, v8::HeapGraphEdge::kProperty, "s_prop");
  v8::Local<v8::String> js_s_prop = js_obj->Get(env.local(), v8_str("s_prop"))
                                        .ToLocalChecked()
                                        .As<v8::String>();
  CHECK_EQ(js_s_prop, heap_profiler->FindObjectById(s_prop->GetId()));
  const v8::HeapGraphNode* n_prop = GetProperty(
      env->GetIsolate(), obj, v8::HeapGraphEdge::kProperty, "n_prop");
  v8::Local<v8::String> js_n_prop = js_obj->Get(env.local(), v8_str("n_prop"))
                                        .ToLocalChecked()
                                        .As<v8::String>();
  CHECK_EQ(js_n_prop, heap_profiler->FindObjectById(n_prop->GetId()));
}


TEST(GetHeapValueForDeletedObject) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  // It is impossible to delete a global property, so we are about to delete a
  // property of the "a" object. Also, the "p" object can't be an empty one
  // because the empty object is static and isn't actually deleted.
  CompileRun("a = { p: { r: {} } };");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* obj =
      GetProperty(env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "a");
  const v8::HeapGraphNode* prop =
      GetProperty(env->GetIsolate(), obj, v8::HeapGraphEdge::kProperty, "p");
  {
    // Perform the check inside a nested local scope to avoid creating a
    // reference to the object we are deleting.
    v8::HandleScope inner_scope(env->GetIsolate());
    CHECK(heap_profiler->FindObjectById(prop->GetId())->IsObject());
  }
  CompileRun("delete a.p;");
  {
    // Exclude the stack during object finding, so that conservative stack
    // scanning may not accidentally mark the object as reachable.
    i::Heap* heap = reinterpret_cast<i::Isolate*>(env->GetIsolate())->heap();
    i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    CHECK(heap_profiler->FindObjectById(prop->GetId()).IsEmpty());
  }
}

static int StringCmp(const char* ref, i::Tagged<i::String> act) {
  std::unique_ptr<char[]> s_act = act->ToCString();
  int result = strcmp(ref, s_act.get());
  if (result != 0)
    fprintf(stderr, "Expected: \"%s\", Actual: \"%s\"\n", ref, s_act.get());
  return result;
}

TEST(GetConstructor) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());

  CompileRun(
      "function Constructor1() {};\n"
      "var obj1 = new Constructor1();\n"
      "var Constructor2 = function() {};\n"
      "var obj2 = new Constructor2();\n"
      "var obj3 = {};\n"
      "obj3.__proto__ = { constructor: function Constructor3() {} };\n"
      "var obj4 = {};\n"
      "// Slow properties\n"
      "for (var i=0; i<2000; ++i) obj4[\"p\" + i] = i;\n"
      "obj4.__proto__ = { constructor: function Constructor4() {} };\n"
      "var obj5 = {};\n"
      "var obj6 = {};\n"
      "obj6.constructor = 6;");
  v8::Local<v8::Object> js_global =
      env->Global()->GetPrototype().As<v8::Object>();
  v8::Local<v8::Object> obj1 = js_global->Get(env.local(), v8_str("obj1"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj1 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj1));
  CHECK(!i::V8HeapExplorer::GetConstructor(i_isolate, *js_obj1).is_null());
  v8::Local<v8::Object> obj2 = js_global->Get(env.local(), v8_str("obj2"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj2 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj2));
  CHECK(!i::V8HeapExplorer::GetConstructor(i_isolate, *js_obj2).is_null());
  v8::Local<v8::Object> obj3 = js_global->Get(env.local(), v8_str("obj3"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj3 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj3));
  CHECK(!i::V8HeapExplorer::GetConstructor(i_isolate, *js_obj3).is_null());
  v8::Local<v8::Object> obj4 = js_global->Get(env.local(), v8_str("obj4"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj4 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj4));
  CHECK(!i::V8HeapExplorer::GetConstructor(i_isolate, *js_obj4).is_null());
  v8::Local<v8::Object> obj5 = js_global->Get(env.local(), v8_str("obj5"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj5 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj5));
  CHECK(i::V8HeapExplorer::GetConstructor(i_isolate, *js_obj5).is_null());
  v8::Local<v8::Object> obj6 = js_global->Get(env.local(), v8_str("obj6"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj6 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj6));
  CHECK(i::V8HeapExplorer::GetConstructor(i_isolate, *js_obj6).is_null());
}

TEST(GetConstructorName) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());

  CompileRun(
      "function Constructor1() {};\n"
      "var obj1 = new Constructor1();\n"
      "var Constructor2 = function() {};\n"
      "var obj2 = new Constructor2();\n"
      "var obj3 = {};\n"
      "obj3.__proto__ = { constructor: function Constructor3() {} };\n"
      "var obj4 = {};\n"
      "// Slow properties\n"
      "for (var i=0; i<2000; ++i) obj4[\"p\" + i] = i;\n"
      "obj4.__proto__ = { constructor: function Constructor4() {} };\n"
      "var obj5 = {};\n"
      "var obj6 = {};\n"
      "obj6.constructor = 6;");
  v8::Local<v8::Object> js_global =
      env->Global()->GetPrototype().As<v8::Object>();
  v8::Local<v8::Object> obj1 = js_global->Get(env.local(), v8_str("obj1"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj1 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj1));
  CHECK_EQ(0, StringCmp("Constructor1", i::V8HeapExplorer::GetConstructorName(
                                            i_isolate, *js_obj1)));
  v8::Local<v8::Object> obj2 = js_global->Get(env.local(), v8_str("obj2"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj2 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj2));
  CHECK_EQ(0, StringCmp("Constructor2", i::V8HeapExplorer::GetConstructorName(
                                            i_isolate, *js_obj2)));
  v8::Local<v8::Object> obj3 = js_global->Get(env.local(), v8_str("obj3"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj3 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj3));
  CHECK_EQ(0, StringCmp("Constructor3", i::V8HeapExplorer::GetConstructorName(
                                            i_isolate, *js_obj3)));
  v8::Local<v8::Object> obj4 = js_global->Get(env.local(), v8_str("obj4"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj4 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj4));
  CHECK_EQ(0, StringCmp("Constructor4", i::V8HeapExplorer::GetConstructorName(
                                            i_isolate, *js_obj4)));
  v8::Local<v8::Object> obj5 = js_global->Get(env.local(), v8_str("obj5"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj5 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj5));
  CHECK_EQ(0, StringCmp("Object", i::V8HeapExplorer::GetConstructorName(
                                      i_isolate, *js_obj5)));
  v8::Local<v8::Object> obj6 = js_global->Get(env.local(), v8_str("obj6"))
                                   .ToLocalChecked()
                                   .As<v8::Object>();
  i::DirectHandle<i::JSObject> js_obj6 =
      i::Cast<i::JSObject>(v8::Utils::OpenDirectHandle(*obj6));
  CHECK_EQ(0, StringCmp("Object", i::V8HeapExplorer::GetConstructorName(
                                      i_isolate, *js_obj6)));
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
END_ALLOW_USE_DEPRECATED()

TEST(FastCaseAccessors) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun("var obj1 = {};\n"
             "obj1.__defineGetter__('propWithGetter', function Y() {\n"
             "  return 42;\n"
             "});\n"
             "obj1.__defineSetter__('propWithSetter', function Z(value) {\n"
             "  return this.value_ = value;\n"
             "});\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(global);
  const v8::HeapGraphNode* obj1 = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "obj1");
  CHECK(obj1);
  const v8::HeapGraphNode* func;
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "get propWithGetter");
  CHECK(func);
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "set propWithGetter");
  CHECK(!func);
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "set propWithSetter");
  CHECK(func);
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "get propWithSetter");
  CHECK(!func);
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
START_ALLOW_USE_DEPRECATED()

TEST(FastCaseRedefinedAccessors) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "var obj1 = {};\n"
      "Object.defineProperty(obj1, 'prop', { "
      "  get: function() { return 42; },\n"
      "  set: function(value) { return this.prop_ = value; },\n"
      "  configurable: true,\n"
      "  enumerable: true,\n"
      "});\n"
      "Object.defineProperty(obj1, 'prop', { "
      "  get: function() { return 153; },\n"
      "  set: function(value) { return this.prop_ = value; },\n"
      "  configurable: true,\n"
      "  enumerable: true,\n"
      "});\n");
  v8::Local<v8::Object> js_global =
      env->Global()->GetPrototype().As<v8::Object>();
  i::Handle<i::JSReceiver> js_obj1 =
      v8::Utils::OpenHandle(*js_global->Get(env.local(), v8_str("obj1"))
                                 .ToLocalChecked()
                                 .As<v8::Object>());
  USE(js_obj1);

  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(global);
  const v8::HeapGraphNode* obj1 = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "obj1");
  CHECK(obj1);
  const v8::HeapGraphNode* func;
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "get prop");
  CHECK(func);
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "set prop");
  CHECK(func);
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
END_ALLOW_USE_DEPRECATED()

TEST(SlowCaseAccessors) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun("var obj1 = {};\n"
             "for (var i = 0; i < 100; ++i) obj1['z' + i] = {};"
             "obj1.__defineGetter__('propWithGetter', function Y() {\n"
             "  return 42;\n"
             "});\n"
             "obj1.__defineSetter__('propWithSetter', function Z(value) {\n"
             "  return this.value_ = value;\n"
             "});\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(global);
  const v8::HeapGraphNode* obj1 = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "obj1");
  CHECK(obj1);
  const v8::HeapGraphNode* func;
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "get propWithGetter");
  CHECK(func);
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "set propWithGetter");
  CHECK(!func);
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "set propWithSetter");
  CHECK(func);
  func = GetProperty(env->GetIsolate(), obj1, v8::HeapGraphEdge::kProperty,
                     "get propWithSetter");
  CHECK(!func);
}


TEST(HiddenPropertiesFastCase) {
  v8::Isolate* isolate = CcTest::isolate();
  LocalContext env;
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  CompileRun(
      "function C(x) { this.a = this; this.b = x; }\n"
      "c = new C(2012);\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* c =
      GetProperty(isolate, global, v8::HeapGraphEdge::kProperty, "c");
  CHECK(c);
  const v8::HeapGraphNode* hidden_props =
      GetProperty(isolate, c, v8::HeapGraphEdge::kProperty, "<symbol key>");
  CHECK(!hidden_props);

  v8::Local<v8::Value> cHandle =
      env->Global()->Get(env.local(), v8_str("c")).ToLocalChecked();
  CHECK(!cHandle.IsEmpty() && cHandle->IsObject());
  cHandle->ToObject(env.local())
      .ToLocalChecked()
      ->SetPrivate(env.local(),
                   v8::Private::ForApi(env->GetIsolate(), v8_str("key")),
                   v8_str("val"))
      .FromJust();

  snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  global = GetGlobalObject(snapshot);
  c = GetProperty(isolate, global, v8::HeapGraphEdge::kProperty, "c");
  CHECK(c);
  hidden_props =
      GetProperty(isolate, c, v8::HeapGraphEdge::kProperty, "<symbol key>");
  CHECK(hidden_props);
}

TEST(SymbolsAndPrivateClassFields) {
  v8::Isolate* isolate = CcTest::isolate();
  LocalContext env;
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  CompileRun(
      "class C { #private = this; [Symbol('MySymbol')] = this; };\n"
      "c = new C;\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* c =
      GetProperty(isolate, global, v8::HeapGraphEdge::kProperty, "c");
  CHECK(c);
  const v8::HeapGraphNode* prop;
  prop = GetProperty(isolate, c, v8::HeapGraphEdge::kProperty, "#private");
  CHECK(prop);
  prop = GetProperty(isolate, c, v8::HeapGraphEdge::kProperty,
                     "<symbol MySymbol>");
  CHECK(prop);
}

TEST(AccessorInfo) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun("function foo(x) { }\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* foo = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "foo");
  CHECK(foo);
  const v8::HeapGraphNode* map =
      GetProperty(env->GetIsolate(), foo, v8::HeapGraphEdge::kInternal, "map");
  CHECK(map);
  const v8::HeapGraphNode* descriptors = GetProperty(
      env->GetIsolate(), map, v8::HeapGraphEdge::kInternal, "descriptors");
  CHECK(descriptors);
  const v8::HeapGraphNode* length_name = GetProperty(
      env->GetIsolate(), descriptors, v8::HeapGraphEdge::kInternal, "0");
  CHECK(length_name);
  CHECK_EQ(0, strcmp("length", *v8::String::Utf8Value(env->GetIsolate(),
                                                      length_name->GetName())));
  const v8::HeapGraphNode* length_accessor = GetProperty(
      env->GetIsolate(), descriptors, v8::HeapGraphEdge::kInternal, "2");
  CHECK(length_accessor);
  CHECK_EQ(0, strcmp("system / AccessorInfo",
                     *v8::String::Utf8Value(env->GetIsolate(),
                                            length_accessor->GetName())));
  const v8::HeapGraphNode* name = GetProperty(
      env->GetIsolate(), length_accessor, v8::HeapGraphEdge::kInternal, "name");
  CHECK(name);
}

TEST(JSGeneratorObject) {
  v8::Isolate* isolate = CcTest::isolate();
  LocalContext env;
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  CompileRun(
      "function* foo() { yield 1; }\n"
      "g = foo();\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* g =
      GetProperty(isolate, global, v8::HeapGraphEdge::kProperty, "g");
  CHECK(g);
  const v8::HeapGraphNode* function = GetProperty(
      env->GetIsolate(), g, v8::HeapGraphEdge::kInternal, "function");
  CHECK(function);
  const v8::HeapGraphNode* context = GetProperty(
      env->GetIsolate(), g, v8::HeapGraphEdge::kInternal, "context");
  CHECK(context);
  const v8::HeapGraphNode* receiver = GetProperty(
      env->GetIsolate(), g, v8::HeapGraphEdge::kInternal, "receiver");
  CHECK(receiver);
  const v8::HeapGraphNode* parameters_and_registers =
      GetProperty(env->GetIsolate(), g, v8::HeapGraphEdge::kInternal,
                  "parameters_and_registers");
  CHECK(parameters_and_registers);
}

bool HasWeakEdge(const v8::HeapGraphNode* node) {
  for (int i = 0; i < node->GetChildrenCount(); ++i) {
    const v8::HeapGraphEdge* handle_edge = node->GetChild(i);
    if (handle_edge->GetType() == v8::HeapGraphEdge::kWeak) return true;
  }
  return false;
}


bool HasWeakGlobalHandle() {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* gc_roots = GetNode(
      snapshot->GetRoot(), v8::HeapGraphNode::kSynthetic, "(GC roots)");
  CHECK(gc_roots);
  const v8::HeapGraphNode* global_handles = GetNode(
      gc_roots, v8::HeapGraphNode::kSynthetic, "(Global handles)");
  CHECK(global_handles);
  return HasWeakEdge(global_handles);
}


TEST(WeakGlobalHandle) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  CHECK(!HasWeakGlobalHandle());

  v8::Global<v8::Object> handle;

  handle.Reset(env->GetIsolate(), v8::Object::New(env->GetIsolate()));
  handle.SetWeak();

  CHECK(HasWeakGlobalHandle());
}


TEST(SfiAndJsFunctionWeakRefs) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun(
      "fun = (function (x) { return function () { return x + 1; } })(1);");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(global);
  const v8::HeapGraphNode* fun = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "fun");
  CHECK(!HasWeakEdge(fun));
  const v8::HeapGraphNode* shared = GetProperty(
      env->GetIsolate(), fun, v8::HeapGraphEdge::kInternal, "shared");
  CHECK(!HasWeakEdge(shared));
}


TEST(AllStrongGcRootsHaveNames) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun("foo = {};");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* gc_roots = GetNode(
      snapshot->GetRoot(), v8::HeapGraphNode::kSynthetic, "(GC roots)");
  CHECK(gc_roots);
  const v8::HeapGraphNode* strong_roots = GetNode(
      gc_roots, v8::HeapGraphNode::kSynthetic, "(Strong roots)");
  CHECK(strong_roots);
  for (int i = 0; i < strong_roots->GetChildrenCount(); ++i) {
    const v8::HeapGraphEdge* edge = strong_roots->GetChild(i);
    CHECK_EQ(v8::HeapGraphEdge::kInternal, edge->GetType());
    v8::String::Utf8Value name(env->GetIsolate(), edge->GetName());
    CHECK(isalpha(**name));
  }
}


TEST(NoRefsToNonEssentialEntries) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun("global_object = {};\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* global_object = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "global_object");
  CHECK(global_object);
  const v8::HeapGraphNode* properties =
      GetProperty(env->GetIsolate(), global_object,
                  v8::HeapGraphEdge::kInternal, "properties");
  CHECK(!properties);
  const v8::HeapGraphNode* elements =
      GetProperty(env->GetIsolate(), global_object,
                  v8::HeapGraphEdge::kInternal, "elements");
  CHECK(!elements);
}


TEST(MapHasDescriptorsAndTransitions) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun("obj = { a: 10 };\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* global_object = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "obj");
  CHECK(global_object);

  const v8::HeapGraphNode* map = GetProperty(
      env->GetIsolate(), global_object, v8::HeapGraphEdge::kInternal, "map");
  CHECK(map);
  const v8::HeapGraphNode* own_descriptors = GetProperty(
      env->GetIsolate(), map, v8::HeapGraphEdge::kInternal, "descriptors");
  CHECK(own_descriptors);
  const v8::HeapGraphNode* own_transitions = GetProperty(
      env->GetIsolate(), map, v8::HeapGraphEdge::kInternal, "transitions");
  CHECK(!own_transitions);
}


TEST(ManyLocalsInSharedContext) {
  // This test gets very slow with slow asserts (18 minutes instead of 1:30,
  // as of November 2018).
#ifdef ENABLE_SLOW_DCHECKS
  i::v8_flags.enable_slow_asserts = false;
#endif
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  int num_objects = 6000;
  CompileRun(
      "var n = 6000;"
      "var result = [];"
      "result.push('(function outer() {');"
      "for (var i = 0; i < n; i++) {"
      "    var f = 'function f_' + i + '() { ';"
      "    if (i > 0)"
      "        f += 'f_' + (i - 1) + '();';"
      "    f += ' }';"
      "    result.push(f);"
      "}"
      "result.push('return f_' + (n - 1) + ';');"
      "result.push('})()');"
      "var ok = eval(result.join('\\n'));");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(global);
  const v8::HeapGraphNode* ok_object = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "ok");
  CHECK(ok_object);
  const v8::HeapGraphNode* context_object = GetProperty(
      env->GetIsolate(), ok_object, v8::HeapGraphEdge::kInternal, "context");
  CHECK(context_object);
  // Check the objects are not duplicated in the context.
  CHECK_EQ(i::Context::MIN_CONTEXT_EXTENDED_SLOTS + num_objects - 1,
           context_object->GetChildrenCount());
  // Check all the objects have got their names.
  // ... well check just every 15th because otherwise it's too slow in debug.
  for (int i = 0; i < num_objects - 1; i += 15) {
    v8::base::EmbeddedVector<char, 100> var_name;
    v8::base::SNPrintF(var_name, "f_%d", i);
    const v8::HeapGraphNode* f_object =
        GetProperty(env->GetIsolate(), context_object,
                    v8::HeapGraphEdge::kContextVariable, var_name.begin());
    CHECK(f_object);
  }
}

TEST(AllocationSitesAreVisible) {
  i::v8_flags.lazy_feedback_allocation = false;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();
  CompileRun(
      "fun = function () { var a = [3, 2, 1]; return a; }\n"
      "fun();");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK(global);
  const v8::HeapGraphNode* fun_code = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "fun");
  CHECK(fun_code);
  const v8::HeapGraphNode* feedback_cell =
      GetProperty(env->GetIsolate(), fun_code, v8::HeapGraphEdge::kInternal,
                  "feedback_cell");
  CHECK(feedback_cell);
  const v8::HeapGraphNode* vector = GetProperty(
      env->GetIsolate(), feedback_cell, v8::HeapGraphEdge::kInternal, "value");
  CHECK_EQ(v8::HeapGraphNode::kCode, vector->GetType());
  CHECK_EQ(5, vector->GetChildrenCount());

  // The last value in the feedback vector should be the boilerplate,
  // found in AllocationSite.transition_info.
  const v8::HeapGraphEdge* prop = vector->GetChild(4);
  const v8::HeapGraphNode* allocation_site = prop->GetToNode();
  v8::String::Utf8Value name(env->GetIsolate(), allocation_site->GetName());
  CHECK_EQ(0, strcmp("system / AllocationSite", *name));
  const v8::HeapGraphNode* transition_info =
      GetProperty(env->GetIsolate(), allocation_site,
                  v8::HeapGraphEdge::kInternal, "transition_info");
  CHECK(transition_info);

  const v8::HeapGraphNode* elements =
      GetProperty(env->GetIsolate(), transition_info,
                  v8::HeapGraphEdge::kInternal, "elements");
  CHECK(elements);
  CHECK_EQ(v8::HeapGraphNode::kCode, elements->GetType());
  CHECK_EQ(i::FixedArray::SizeFor(3),
           static_cast<int>(elements->GetShallowSize()));

  v8::Local<v8::Value> array_val =
      heap_profiler->FindObjectById(transition_info->GetId());
  CHECK(array_val->IsArray());
  v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(array_val);
  // Verify the array is "a" in the code above.
  CHECK_EQ(3u, array->Length());
  CHECK(v8::Integer::New(isolate, 3)
            ->Equals(env.local(),
                     array->Get(env.local(), v8::Integer::New(isolate, 0))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8::Integer::New(isolate, 2)
            ->Equals(env.local(),
                     array->Get(env.local(), v8::Integer::New(isolate, 1))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8::Integer::New(isolate, 1)
            ->Equals(env.local(),
                     array->Get(env.local(), v8::Integer::New(isolate, 2))
                         .ToLocalChecked())
            .FromJust());
}


TEST(JSFunctionHasCodeLink) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun("function foo(x, y) { return x + y; }\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* foo_func = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "foo");
  CHECK(foo_func);
  const v8::HeapGraphNode* code = GetProperty(
      env->GetIsolate(), foo_func, v8::HeapGraphEdge::kInternal, "code");
  CHECK(code);
}

static const v8::HeapGraphNode* GetNodeByPath(v8::Isolate* isolate,
                                              const v8::HeapSnapshot* snapshot,
                                              const char* path[], int depth) {
  const v8::HeapGraphNode* node = snapshot->GetRoot();
  for (int current_depth = 0; current_depth < depth; ++current_depth) {
    int i, count = node->GetChildrenCount();
    for (i = 0; i < count; ++i) {
      const v8::HeapGraphEdge* edge = node->GetChild(i);
      const v8::HeapGraphNode* to_node = edge->GetToNode();
      v8::String::Utf8Value edge_name(isolate, edge->GetName());
      v8::String::Utf8Value node_name(isolate, to_node->GetName());
      v8::base::EmbeddedVector<char, 100> name;
      v8::base::SNPrintF(name, "%s::%s", *edge_name, *node_name);
      if (strstr(name.begin(), path[current_depth])) {
        node = to_node;
        break;
      }
    }
    if (i == count) return nullptr;
  }
  return node;
}


TEST(CheckCodeNames) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun("var a = 1.1;");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  const char* builtin_path1[] = {
      "::(GC roots)",
      "::(Builtins)",
      "::(KeyedLoadIC_PolymorphicName builtin code)",
  };
  const v8::HeapGraphNode* node = GetNodeByPath(
      env->GetIsolate(), snapshot, builtin_path1, arraysize(builtin_path1));
  CHECK(node);

  const char* builtin_path2[] = {
      "::(GC roots)",
      "::(Builtins)",
      "::(CompileLazy builtin code)",
  };
  node = GetNodeByPath(env->GetIsolate(), snapshot, builtin_path2,
                       arraysize(builtin_path2));
  CHECK(node);
  v8::String::Utf8Value node_name(env->GetIsolate(), node->GetName());
  CHECK_EQ(0, strcmp("(CompileLazy builtin code)", *node_name));
}


static const char* record_trace_tree_source =
"var topFunctions = [];\n"
"var global = this;\n"
"function generateFunctions(width, depth) {\n"
"  var script = [];\n"
"  for (var i = 0; i < width; i++) {\n"
"    for (var j = 0; j < depth; j++) {\n"
"      script.push('function f_' + i + '_' + j + '(x) {\\n');\n"
"      script.push('  try {\\n');\n"
"      if (j < depth-2) {\n"
"        script.push('    return f_' + i + '_' + (j+1) + '(x+1);\\n');\n"
"      } else if (j == depth - 2) {\n"
"        script.push('    return new f_' + i + '_' + (depth - 1) + '();\\n');\n"
"      } else if (j == depth - 1) {\n"
"        script.push('    this.ts = Date.now();\\n');\n"
"      }\n"
"      script.push('  } catch (e) {}\\n');\n"
"      script.push('}\\n');\n"
"      \n"
"    }\n"
"  }\n"
"  var script = script.join('');\n"
"  // throw script;\n"
"  global.eval(script);\n"
"  for (var i = 0; i < width; i++) {\n"
"    topFunctions.push(this['f_' + i + '_0']);\n"
"  }\n"
"}\n"
"\n"
"var width = 3;\n"
"var depth = 3;\n"
"generateFunctions(width, depth);\n"
"var instances = [];\n"
"function start() {\n"
"  for (var i = 0; i < width; i++) {\n"
"    instances.push(topFunctions[i](0));\n"
"  }\n"
"}\n"
"\n"
"for (var i = 0; i < 100; i++) start();\n";

static AllocationTraceNode* FindNode(AllocationTracker* tracker,
                                     v8::base::Vector<const char*> names) {
  AllocationTraceNode* node = tracker->trace_tree()->root();
  for (int i = 0; node != nullptr && i < names.length(); i++) {
    const char* name = names[i];
    const std::vector<AllocationTraceNode*>& children = node->children();
    node = nullptr;
    for (AllocationTraceNode* child : children) {
      unsigned index = child->function_info_index();
      AllocationTracker::FunctionInfo* info =
          tracker->function_info_list()[index];
      if (info && strcmp(info->name, name) == 0) {
        node = child;
        break;
      }
    }
  }
  return node;
}

TEST(ArrayGrowLeftTrim) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  heap_profiler->StartTrackingHeapObjects(true);

  CompileRun(
    "var a = [];\n"
    "for (var i = 0; i < 5; ++i)\n"
    "    a[i] = i;\n"
    "for (var i = 0; i < 3; ++i)\n"
    "    a.shift();\n");

  const char* names[] = {""};
  AllocationTracker* tracker =
      reinterpret_cast<i::HeapProfiler*>(heap_profiler)->allocation_tracker();
  CHECK(tracker);
  // Print for better diagnostics in case of failure.
  tracker->trace_tree()->Print(tracker);

  AllocationTraceNode* node = FindNode(tracker, v8::base::ArrayVector(names));
  CHECK(node);
  CHECK_GE(node->allocation_count(), 2u);
  CHECK_GE(node->allocation_size(), 4u * 5u);
  heap_profiler->StopTrackingHeapObjects();
}

TEST(TrackHeapAllocationsWithInlining) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;

  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  heap_profiler->StartTrackingHeapObjects(true);

  CompileRun(record_trace_tree_source);

  AllocationTracker* tracker =
      reinterpret_cast<i::HeapProfiler*>(heap_profiler)->allocation_tracker();
  CHECK(tracker);
  // Print for better diagnostics in case of failure.
  tracker->trace_tree()->Print(tracker);

  const char* names[] = {"", "start", "f_0_0"};
  AllocationTraceNode* node = FindNode(tracker, v8::base::ArrayVector(names));
  CHECK(node);
  // In lite mode, there is feedback and feedback metadata.
  unsigned int num_nodes = (i::v8_flags.lite_mode) ? 6 : 8;
  // Without forced source position collection, there is no source position
  // table.
  if (i::v8_flags.enable_lazy_source_positions) num_nodes -= 1;
  CHECK_GE(node->allocation_count(), num_nodes);
  CHECK_GE(node->allocation_size(), 4 * node->allocation_count());
  heap_profiler->StopTrackingHeapObjects();
}

TEST(TrackHeapAllocationsWithoutInlining) {
  i::v8_flags.turbo_inlining = false;
  // Disable inlining
  i::v8_flags.max_inlined_bytecode_size = 0;
  i::v8_flags.max_inlined_bytecode_size_small = 0;
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;

  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  heap_profiler->StartTrackingHeapObjects(true);

  CompileRun(record_trace_tree_source);

  AllocationTracker* tracker =
      reinterpret_cast<i::HeapProfiler*>(heap_profiler)->allocation_tracker();
  CHECK(tracker);
  // Print for better diagnostics in case of failure.
  tracker->trace_tree()->Print(tracker);

  const char* names[] = {"", "start", "f_0_0", "f_0_1", "f_0_2"};
  AllocationTraceNode* node = FindNode(tracker, v8::base::ArrayVector(names));
  CHECK(node);
  CHECK_GE(node->allocation_count(), 100u);
  CHECK_GE(node->allocation_size(), 4 * node->allocation_count());
  heap_profiler->StopTrackingHeapObjects();
}


static const char* inline_heap_allocation_source =
    "function f_0(x) {\n"
    "  return f_1(x+1);\n"
    "}\n"
    "%NeverOptimizeFunction(f_0);\n"
    "function f_1(x) {\n"
    "  return new f_2(x+1);\n"
    "}\n"
    "%NeverOptimizeFunction(f_1);\n"
    "function f_2(x) {\n"
    "  this.foo = x;\n"
    "}\n"
    "var instances = [];\n"
    "function start() {\n"
    "  instances.push(f_0(0));\n"
    "}\n"
    "\n"
    "for (var i = 0; i < 100; i++) start();\n";


TEST(TrackBumpPointerAllocations) {
  i::v8_flags.allow_natives_syntax = true;
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;

  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  const char* names[] = {"", "start", "f_0", "f_1"};
  // First check that normally all allocations are recorded.
  {
    heap_profiler->StartTrackingHeapObjects(true);

    CompileRun(inline_heap_allocation_source);

    AllocationTracker* tracker =
        reinterpret_cast<i::HeapProfiler*>(heap_profiler)->allocation_tracker();
    CHECK(tracker);
    // Print for better diagnostics in case of failure.
    tracker->trace_tree()->Print(tracker);

    AllocationTraceNode* node = FindNode(tracker, v8::base::ArrayVector(names));
    CHECK(node);
    CHECK_GE(node->allocation_count(), 100u);
    CHECK_GE(node->allocation_size(), 4 * node->allocation_count());
    heap_profiler->StopTrackingHeapObjects();
  }

  {
    heap_profiler->StartTrackingHeapObjects(true);

    // Now check that not all allocations are tracked if we manually reenable
    // inline allocations.
    CHECK(i::v8_flags.single_generation ||
          !CcTest::heap()->IsInlineAllocationEnabled());
    CcTest::heap()->EnableInlineAllocation();

    CompileRun(inline_heap_allocation_source);

    AllocationTracker* tracker =
        reinterpret_cast<i::HeapProfiler*>(heap_profiler)->allocation_tracker();
    CHECK(tracker);
    // Print for better diagnostics in case of failure.
    tracker->trace_tree()->Print(tracker);

    AllocationTraceNode* node = FindNode(tracker, v8::base::ArrayVector(names));
    CHECK(node);
    CHECK_LT(node->allocation_count(), 100u);

    CcTest::heap()->DisableInlineAllocation();
    heap_profiler->StopTrackingHeapObjects();
  }
}


TEST(TrackV8ApiAllocation) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;

  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  const char* names[] = { "(V8 API)" };
  heap_profiler->StartTrackingHeapObjects(true);

  v8::Local<v8::Object> o1 = v8::Object::New(env->GetIsolate());
  o1->Clone();

  AllocationTracker* tracker =
      reinterpret_cast<i::HeapProfiler*>(heap_profiler)->allocation_tracker();
  CHECK(tracker);
  // Print for better diagnostics in case of failure.
  tracker->trace_tree()->Print(tracker);

  AllocationTraceNode* node = FindNode(tracker, v8::base::ArrayVector(names));
  CHECK(node);
  CHECK_GE(node->allocation_count(), 2u);
  CHECK_GE(node->allocation_size(), 4 * node->allocation_count());
  heap_profiler->StopTrackingHeapObjects();
}


TEST(ArrayBufferAndArrayBufferView) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun("arr1 = new Uint32Array(100);\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* arr1_obj = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "arr1");
  CHECK(arr1_obj);
  const v8::HeapGraphNode* arr1_buffer = GetProperty(
      env->GetIsolate(), arr1_obj, v8::HeapGraphEdge::kInternal, "buffer");
  CHECK(arr1_buffer);
  const v8::HeapGraphNode* backing_store =
      GetProperty(env->GetIsolate(), arr1_buffer, v8::HeapGraphEdge::kInternal,
                  "backing_store");
  CHECK(backing_store);
  CHECK_EQ(400, static_cast<int>(backing_store->GetShallowSize()));
}


static int GetRetainersCount(const v8::HeapSnapshot* snapshot,
                             const v8::HeapGraphNode* node) {
  int count = 0;
  for (int i = 0, l = snapshot->GetNodesCount(); i < l; ++i) {
    const v8::HeapGraphNode* parent = snapshot->GetNode(i);
    for (int j = 0, l2 = parent->GetChildrenCount(); j < l2; ++j) {
      if (parent->GetChild(j)->GetToNode() == node) {
        ++count;
      }
    }
  }
  return count;
}


TEST(ArrayBufferSharedBackingStore) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, 1024);
  CHECK_EQ(1024, static_cast<int>(ab->ByteLength()));
  std::shared_ptr<v8::BackingStore> backing_store = ab->GetBackingStore();

  CHECK_EQ(1024, static_cast<int>(backing_store->ByteLength()));
  void* data = backing_store->Data();
  CHECK_NOT_NULL(data);
  v8::Local<v8::ArrayBuffer> ab2 = v8::ArrayBuffer::New(isolate, backing_store);
  env->Global()->Set(env.local(), v8_str("ab1"), ab).FromJust();
  env->Global()->Set(env.local(), v8_str("ab2"), ab2).FromJust();

  v8::Local<v8::Value> result = CompileRun("ab2.byteLength");
  CHECK_EQ(1024, result->Int32Value(env.local()).FromJust());

  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* ab1_node = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "ab1");
  CHECK(ab1_node);
  const v8::HeapGraphNode* ab1_data =
      GetProperty(env->GetIsolate(), ab1_node, v8::HeapGraphEdge::kInternal,
                  "backing_store");
  CHECK(ab1_data);
  const v8::HeapGraphNode* ab2_node = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "ab2");
  CHECK(ab2_node);
  const v8::HeapGraphNode* ab2_data =
      GetProperty(env->GetIsolate(), ab2_node, v8::HeapGraphEdge::kInternal,
                  "backing_store");
  CHECK(ab2_data);
  CHECK_EQ(ab1_data, ab2_data);
  CHECK_EQ(2, GetRetainersCount(snapshot, ab1_data));
}


TEST(WeakContainers) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  if (!CcTest::i_isolate()->use_optimizer()) return;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun(
      "function foo(a) { return a.x; }\n"
      "obj = {x : 123};\n"
      "%PrepareFunctionForOptimization(foo);"
      "foo(obj);\n"
      "foo(obj);\n"
      "%OptimizeFunctionOnNextCall(foo);\n"
      "foo(obj);\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* obj = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "obj");
  CHECK(obj);
  const v8::HeapGraphNode* map =
      GetProperty(env->GetIsolate(), obj, v8::HeapGraphEdge::kInternal, "map");
  CHECK(map);
  const v8::HeapGraphNode* dependent_code = GetProperty(
      env->GetIsolate(), map, v8::HeapGraphEdge::kInternal, "dependent_code");
  if (!dependent_code) return;
  int count = dependent_code->GetChildrenCount();
  CHECK_NE(0, count);
  for (int i = 0; i < count; ++i) {
    const v8::HeapGraphEdge* prop = dependent_code->GetChild(i);
    CHECK(prop->GetType() == v8::HeapGraphEdge::kInternal ||
          prop->GetType() == v8::HeapGraphEdge::kWeak);
  }
}

TEST(JSPromise) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun(
      "function A() {}\n"
      "function B() {}\n"
      "resolved = Promise.resolve(new A());\n"
      "rejected = Promise.reject(new B());\n"
      "pending = new Promise(() => 0);\n"
      "chained = pending.then(A, B);\n");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);

  const v8::HeapGraphNode* resolved = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "resolved");
  CHECK(GetProperty(env->GetIsolate(), resolved, v8::HeapGraphEdge::kInternal,
                    "reactions_or_result"));

  const v8::HeapGraphNode* rejected = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "rejected");
  CHECK(GetProperty(env->GetIsolate(), rejected, v8::HeapGraphEdge::kInternal,
                    "reactions_or_result"));

  const v8::HeapGraphNode* pending = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, "pending");
  CHECK(GetProperty(env->GetIsolate(), pending, v8::HeapGraphEdge::kInternal,
                    "reactions_or_result"));

  const char* objectNames[] = {"resolved", "rejected", "pending", "chained"};
  for (auto objectName : objectNames) {
    const v8::HeapGraphNode* promise = GetProperty(
        env->GetIsolate(), global, v8::HeapGraphEdge::kProperty, objectName);
    EnsureNoUninstrumentedInternals(env->GetIsolate(), promise);
  }
}

TEST(HeapSnapshotScriptContext) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  CompileRun("class Foo{}; const foo = new Foo();");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* global_map = GetProperty(
      env->GetIsolate(), global, v8::HeapGraphEdge::kInternal, "map");
  const v8::HeapGraphNode* map_map = GetProperty(
      env->GetIsolate(), global_map, v8::HeapGraphEdge::kInternal, "map");
  const v8::HeapGraphNode* native_context =
      GetProperty(env->GetIsolate(), map_map, v8::HeapGraphEdge::kInternal,
                  "native_context");
  const v8::HeapGraphNode* script_context_table =
      GetProperty(env->GetIsolate(), native_context,
                  v8::HeapGraphEdge::kInternal, "script_context_table");

  CHECK(script_context_table);
  bool found_foo = false;
  for (int i = 0, count = script_context_table->GetChildrenCount(); i < count;
       ++i) {
    const v8::HeapGraphNode* context =
        script_context_table->GetChild(i)->GetToNode();
    const v8::HeapGraphNode* foo = GetProperty(
        env->GetIsolate(), context, v8::HeapGraphEdge::kContextVariable, "foo");
    if (foo) {
      found_foo = true;
    }
  }
  CHECK(found_foo);
}

class EmbedderNode : public v8::EmbedderGraph::Node {
 public:
  EmbedderNode(const char* name, size_t size,
               v8::EmbedderGraph::Node* wrapper_node = nullptr)
      : name_(name), size_(size), wrapper_node_(wrapper_node) {}

  // Graph::Node overrides.
  const char* Name() override { return name_; }
  size_t SizeInBytes() override { return size_; }
  Node* WrapperNode() override { return wrapper_node_; }

 private:
  const char* name_;
  size_t size_;
  Node* wrapper_node_;
};

class EmbedderRootNode : public EmbedderNode {
 public:
  explicit EmbedderRootNode(const char* name) : EmbedderNode(name, 0) {}
  // Graph::Node override.
  bool IsRootNode() override { return true; }
};

// Used to pass the global object to the BuildEmbedderGraph callback.
// Otherwise, the callback has to iterate the global handles to find the
// global object.
v8::Local<v8::Value>* global_object_pointer;

void BuildEmbedderGraph(v8::Isolate* v8_isolate, v8::EmbedderGraph* graph,
                        void* data) {
  using Node = v8::EmbedderGraph::Node;
  Node* global_node = graph->V8Node(*global_object_pointer);
  Node* embedder_node_A = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderNode("EmbedderNodeA", 10)));
  Node* embedder_node_B = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderNode("EmbedderNodeB", 20)));
  Node* embedder_node_C = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderNode("EmbedderNodeC", 30)));
  Node* embedder_root = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderRootNode("EmbedderRoot")));
  graph->AddEdge(global_node, embedder_node_A);
  graph->AddEdge(embedder_node_A, embedder_node_B);
  graph->AddEdge(embedder_root, embedder_node_C);
  graph->AddEdge(embedder_node_C, global_node);
}

void CheckEmbedderGraphSnapshot(v8::Isolate* isolate,
                                const v8::HeapSnapshot* snapshot) {
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* embedder_node_A =
      GetChildByName(global, "EmbedderNodeA");
  CHECK_EQ(10, GetSize(embedder_node_A));
  const v8::HeapGraphNode* embedder_node_B =
      GetChildByName(embedder_node_A, "EmbedderNodeB");
  CHECK_EQ(20, GetSize(embedder_node_B));
  const v8::HeapGraphNode* embedder_root =
      GetRootChild(snapshot, "EmbedderRoot");
  CHECK(embedder_root);
  const v8::HeapGraphNode* embedder_node_C =
      GetChildByName(embedder_root, "EmbedderNodeC");
  CHECK_EQ(30, GetSize(embedder_node_C));
  const v8::HeapGraphNode* global_reference =
      GetChildByName(embedder_node_C, "Object");
  CHECK(global_reference);
}

TEST(EmbedderGraph) {
  i::v8_flags.heap_profiler_use_embedder_graph = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());
  v8::Local<v8::Value> global_object =
      v8::Utils::ToLocal(i::Handle<i::JSObject>(
          (isolate->context()->native_context()->global_object()), isolate));
  global_object_pointer = &global_object;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  heap_profiler->AddBuildEmbedderGraphCallback(BuildEmbedderGraph, nullptr);
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  CheckEmbedderGraphSnapshot(env->GetIsolate(), snapshot);
}

void BuildEmbedderGraphWithNamedEdges(v8::Isolate* v8_isolate,
                                      v8::EmbedderGraph* graph, void* data) {
  using Node = v8::EmbedderGraph::Node;
  Node* global_node = graph->V8Node(*global_object_pointer);
  Node* embedder_node_A = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderNode("EmbedderNodeA", 10)));
  Node* embedder_node_B = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderNode("EmbedderNodeB", 20)));
  Node* embedder_node_C = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderNode("EmbedderNodeC", 30)));
  graph->AddEdge(global_node, embedder_node_A, "global_to_a");
  graph->AddEdge(embedder_node_A, embedder_node_B, "a_to_b");
  graph->AddEdge(embedder_node_B, embedder_node_C);
}

void CheckEmbedderGraphWithNamedEdges(v8::Isolate* isolate,
                                      const v8::HeapSnapshot* snapshot) {
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphEdge* global_to_a =
      GetEdgeByChildName(global, "EmbedderNodeA");
  CHECK(global_to_a);
  CHECK_EQ(v8::HeapGraphEdge::kInternal, global_to_a->GetType());
  CHECK(global_to_a->GetName()->IsString());
  CHECK_EQ(0, strcmp("global_to_a", GetName(global_to_a)));
  const v8::HeapGraphNode* embedder_node_A = global_to_a->GetToNode();
  CHECK_EQ(0, strcmp("EmbedderNodeA", GetName(embedder_node_A)));
  CHECK_EQ(10, GetSize(embedder_node_A));

  const v8::HeapGraphEdge* a_to_b =
      GetEdgeByChildName(embedder_node_A, "EmbedderNodeB");
  CHECK(a_to_b);
  CHECK(a_to_b->GetName()->IsString());
  CHECK_EQ(0, strcmp("a_to_b", GetName(a_to_b)));
  CHECK_EQ(v8::HeapGraphEdge::kInternal, a_to_b->GetType());
  const v8::HeapGraphNode* embedder_node_B = a_to_b->GetToNode();
  CHECK_EQ(0, strcmp("EmbedderNodeB", GetName(embedder_node_B)));
  CHECK_EQ(20, GetSize(embedder_node_B));

  const v8::HeapGraphEdge* b_to_c =
      GetEdgeByChildName(embedder_node_B, "EmbedderNodeC");
  CHECK(b_to_c);
  CHECK(b_to_c->GetName()->IsNumber());
  CHECK_EQ(v8::HeapGraphEdge::kElement, b_to_c->GetType());
  const v8::HeapGraphNode* embedder_node_C = b_to_c->GetToNode();
  CHECK_EQ(0, strcmp("EmbedderNodeC", GetName(embedder_node_C)));
  CHECK_EQ(30, GetSize(embedder_node_C));
}

TEST(EmbedderGraphWithNamedEdges) {
  i::v8_flags.heap_profiler_use_embedder_graph = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());
  v8::Local<v8::Value> global_object =
      v8::Utils::ToLocal(i::Handle<i::JSObject>(
          (isolate->context()->native_context()->global_object()), isolate));
  global_object_pointer = &global_object;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  heap_profiler->AddBuildEmbedderGraphCallback(BuildEmbedderGraphWithNamedEdges,
                                               nullptr);
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  CheckEmbedderGraphWithNamedEdges(env->GetIsolate(), snapshot);
}

struct GraphBuildingContext {
  int counter = 0;
};

void CheckEmbedderGraphSnapshotWithContext(
    v8::Isolate* isolate, const v8::HeapSnapshot* snapshot,
    const GraphBuildingContext* context) {
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  CHECK_GE(context->counter, 1);
  CHECK_LE(context->counter, 2);

  const v8::HeapGraphNode* embedder_node_A =
      GetChildByName(global, "EmbedderNodeA");
  CHECK_EQ(10, GetSize(embedder_node_A));

  const v8::HeapGraphNode* embedder_node_B =
      GetChildByName(global, "EmbedderNodeB");
  if (context->counter == 2) {
    CHECK_NOT_NULL(embedder_node_B);
    CHECK_EQ(20, GetSize(embedder_node_B));
  } else {
    CHECK_NULL(embedder_node_B);
  }
}

void BuildEmbedderGraphWithContext(v8::Isolate* v8_isolate,
                                   v8::EmbedderGraph* graph, void* data) {
  using Node = v8::EmbedderGraph::Node;
  GraphBuildingContext* context = static_cast<GraphBuildingContext*>(data);
  Node* global_node = graph->V8Node(*global_object_pointer);

  CHECK_GE(context->counter, 0);
  CHECK_LE(context->counter, 1);
  switch (context->counter++) {
    case 0: {
      Node* embedder_node_A = graph->AddNode(
          std::unique_ptr<Node>(new EmbedderNode("EmbedderNodeA", 10)));
      graph->AddEdge(global_node, embedder_node_A);
      break;
    }
    case 1: {
      Node* embedder_node_B = graph->AddNode(
          std::unique_ptr<Node>(new EmbedderNode("EmbedderNodeB", 20)));
      graph->AddEdge(global_node, embedder_node_B);
      break;
    }
  }
}

TEST(EmbedderGraphMultipleCallbacks) {
  i::v8_flags.heap_profiler_use_embedder_graph = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());
  v8::Local<v8::Value> global_object =
      v8::Utils::ToLocal(i::Handle<i::JSObject>(
          (isolate->context()->native_context()->global_object()), isolate));
  global_object_pointer = &global_object;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  GraphBuildingContext context;

  heap_profiler->AddBuildEmbedderGraphCallback(BuildEmbedderGraphWithContext,
                                               &context);
  heap_profiler->AddBuildEmbedderGraphCallback(BuildEmbedderGraphWithContext,
                                               &context);
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK_EQ(context.counter, 2);
  CHECK(ValidateSnapshot(snapshot));
  CheckEmbedderGraphSnapshotWithContext(env->GetIsolate(), snapshot, &context);

  heap_profiler->RemoveBuildEmbedderGraphCallback(BuildEmbedderGraphWithContext,
                                                  &context);
  context.counter = 0;

  snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK_EQ(context.counter, 1);
  CHECK(ValidateSnapshot(snapshot));
  CheckEmbedderGraphSnapshotWithContext(env->GetIsolate(), snapshot, &context);
}

TEST(StrongHandleAnnotation) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Persistent<v8::Object> handle1, handle2;
  handle1.Reset(env->GetIsolate(), v8::Object::New(env->GetIsolate()));
  handle2.Reset(env->GetIsolate(), v8::Object::New(env->GetIsolate()));
  handle1.AnnotateStrongRetainer("my_label");
  handle2.AnnotateStrongRetainer("my_label");
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  const v8::HeapGraphNode* gc_roots = GetRootChild(snapshot, "(GC roots)");
  CHECK(gc_roots);
  const v8::HeapGraphNode* global_handles =
      GetChildByName(gc_roots, "(Global handles)");
  CHECK(global_handles);
  int found = 0;
  for (int i = 0, count = global_handles->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* edge = global_handles->GetChild(i);
    v8::String::Utf8Value edge_name(CcTest::isolate(), edge->GetName());
    if (EndsWith(*edge_name, "my_label")) ++found;
  }
  CHECK_EQ(2, found);
}

void BuildEmbedderGraphWithWrapperNode(v8::Isolate* v8_isolate,
                                       v8::EmbedderGraph* graph, void* data) {
  using Node = v8::EmbedderGraph::Node;
  Node* global_node = graph->V8Node(*global_object_pointer);
  Node* wrapper_node = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderNode("WrapperNode / TAG", 10)));
  Node* embedder_node = graph->AddNode(std::unique_ptr<Node>(
      new EmbedderNode("EmbedderNode", 10, wrapper_node)));
  Node* other_node =
      graph->AddNode(std::unique_ptr<Node>(new EmbedderNode("OtherNode", 20)));
  graph->AddEdge(global_node, embedder_node);
  graph->AddEdge(wrapper_node, other_node);

  Node* wrapper_node2 = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderNode("WrapperNode2", 10)));
  Node* embedder_node2 = graph->AddNode(std::unique_ptr<Node>(
      new EmbedderNode("EmbedderNode2", 10, wrapper_node2)));
  graph->AddEdge(global_node, embedder_node2);
  graph->AddEdge(embedder_node2, wrapper_node2);
  graph->AddEdge(wrapper_node2, other_node);
}

TEST(EmbedderGraphWithWrapperNode) {
  i::v8_flags.heap_profiler_use_embedder_graph = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());
  v8::Local<v8::Value> global_object =
      v8::Utils::ToLocal(i::Handle<i::JSObject>(
          (isolate->context()->native_context()->global_object()), isolate));
  global_object_pointer = &global_object;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  heap_profiler->AddBuildEmbedderGraphCallback(
      BuildEmbedderGraphWithWrapperNode, nullptr);
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* embedder_node =
      GetChildByName(global, "EmbedderNode / TAG");
  const v8::HeapGraphNode* other_node =
      GetChildByName(embedder_node, "OtherNode");
  CHECK(other_node);
  const v8::HeapGraphNode* wrapper_node =
      GetChildByName(embedder_node, "WrapperNode / TAG");
  CHECK(!wrapper_node);

  const v8::HeapGraphNode* embedder_node2 =
      GetChildByName(global, "EmbedderNode2");
  other_node = GetChildByName(embedder_node2, "OtherNode");
  CHECK(other_node);
  const v8::HeapGraphNode* wrapper_node2 =
      GetChildByName(embedder_node, "WrapperNode2");
  CHECK(!wrapper_node2);
}

class EmbedderNodeWithPrefix : public v8::EmbedderGraph::Node {
 public:
  EmbedderNodeWithPrefix(const char* prefix, const char* name)
      : prefix_(prefix), name_(name) {}

  // Graph::Node overrides.
  const char* Name() override { return name_; }
  size_t SizeInBytes() override { return 0; }
  const char* NamePrefix() override { return prefix_; }

 private:
  const char* prefix_;
  const char* name_;
};

void BuildEmbedderGraphWithPrefix(v8::Isolate* v8_isolate,
                                  v8::EmbedderGraph* graph, void* data) {
  using Node = v8::EmbedderGraph::Node;
  Node* global_node = graph->V8Node(*global_object_pointer);
  Node* node = graph->AddNode(
      std::unique_ptr<Node>(new EmbedderNodeWithPrefix("Detached", "Node")));
  graph->AddEdge(global_node, node);
}

TEST(EmbedderGraphWithPrefix) {
  i::v8_flags.heap_profiler_use_embedder_graph = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());
  v8::Local<v8::Value> global_object =
      v8::Utils::ToLocal(i::Handle<i::JSObject>(
          (isolate->context()->native_context()->global_object()), isolate));
  global_object_pointer = &global_object;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  heap_profiler->AddBuildEmbedderGraphCallback(BuildEmbedderGraphWithPrefix,
                                               nullptr);
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);
  const v8::HeapGraphNode* node = GetChildByName(global, "Detached Node");
  CHECK(node);
}

static inline i::Address ToAddress(int n) { return static_cast<i::Address>(n); }

TEST(AddressToTraceMap) {
  i::AddressToTraceMap map;

  CHECK_EQ(0u, map.GetTraceNodeId(ToAddress(150)));

  // [0x100, 0x200) -> 1
  map.AddRange(ToAddress(0x100), 0x100, 1U);
  CHECK_EQ(0u, map.GetTraceNodeId(ToAddress(0x50)));
  CHECK_EQ(1u, map.GetTraceNodeId(ToAddress(0x100)));
  CHECK_EQ(1u, map.GetTraceNodeId(ToAddress(0x150)));
  CHECK_EQ(0u, map.GetTraceNodeId(ToAddress(0x100 + 0x100)));
  CHECK_EQ(1u, map.size());

  // [0x100, 0x200) -> 1, [0x200, 0x300) -> 2
  map.AddRange(ToAddress(0x200), 0x100, 2U);
  CHECK_EQ(2u, map.GetTraceNodeId(ToAddress(0x2A0)));
  CHECK_EQ(2u, map.size());

  // [0x100, 0x180) -> 1, [0x180, 0x280) -> 3, [0x280, 0x300) -> 2
  map.AddRange(ToAddress(0x180), 0x100, 3U);
  CHECK_EQ(1u, map.GetTraceNodeId(ToAddress(0x17F)));
  CHECK_EQ(2u, map.GetTraceNodeId(ToAddress(0x280)));
  CHECK_EQ(3u, map.GetTraceNodeId(ToAddress(0x180)));
  CHECK_EQ(3u, map.size());

  // [0x100, 0x180) -> 1, [0x180, 0x280) -> 3, [0x280, 0x300) -> 2,
  // [0x400, 0x500) -> 4
  map.AddRange(ToAddress(0x400), 0x100, 4U);
  CHECK_EQ(1u, map.GetTraceNodeId(ToAddress(0x17F)));
  CHECK_EQ(2u, map.GetTraceNodeId(ToAddress(0x280)));
  CHECK_EQ(3u, map.GetTraceNodeId(ToAddress(0x180)));
  CHECK_EQ(4u, map.GetTraceNodeId(ToAddress(0x450)));
  CHECK_EQ(0u, map.GetTraceNodeId(ToAddress(0x500)));
  CHECK_EQ(0u, map.GetTraceNodeId(ToAddress(0x350)));
  CHECK_EQ(4u, map.size());

  // [0x100, 0x180) -> 1, [0x180, 0x200) -> 3, [0x200, 0x600) -> 5
  map.AddRange(ToAddress(0x200), 0x400, 5U);
  CHECK_EQ(5u, map.GetTraceNodeId(ToAddress(0x200)));
  CHECK_EQ(5u, map.GetTraceNodeId(ToAddress(0x400)));
  CHECK_EQ(3u, map.size());

  // [0x100, 0x180) -> 1, [0x180, 0x200) -> 7, [0x200, 0x600) ->5
  map.AddRange(ToAddress(0x180), 0x80, 6U);
  map.AddRange(ToAddress(0x180), 0x80, 7U);
  CHECK_EQ(7u, map.GetTraceNodeId(ToAddress(0x180)));
  CHECK_EQ(5u, map.GetTraceNodeId(ToAddress(0x200)));
  CHECK_EQ(3u, map.size());

  map.Clear();
  CHECK_EQ(0u, map.size());
  CHECK_EQ(0u, map.GetTraceNodeId(ToAddress(0x400)));
}

static const v8::AllocationProfile::Node* FindAllocationProfileNode(
    v8::Isolate* isolate, v8::AllocationProfile* profile,
    v8::base::Vector<const char*> names) {
  v8::AllocationProfile::Node* node = profile->GetRootNode();
  for (int i = 0; node != nullptr && i < names.length(); ++i) {
    const char* name = names[i];
    auto children = node->children;
    node = nullptr;
    for (v8::AllocationProfile::Node* child : children) {
      v8::String::Utf8Value child_name(isolate, child->name);
      if (strcmp(*child_name, name) == 0) {
        node = child;
        break;
      }
    }
  }
  return node;
}

static void CheckNoZeroCountNodes(v8::AllocationProfile::Node* node) {
  for (auto alloc : node->allocations) {
    CHECK_GT(alloc.count, 0u);
  }
  for (auto child : node->children) {
    CheckNoZeroCountNodes(child);
  }
}

static int NumberOfAllocations(const v8::AllocationProfile::Node* node) {
  int count = 0;
  for (auto allocation : node->allocations) {
    count += allocation.count;
  }
  return count;
}

static const char* simple_sampling_heap_profiler_script =
    "var A = [];\n"
    "function bar(size) { return new Array(size); }\n"
    "%NeverOptimizeFunction(bar);\n"
    "var foo = function() {\n"
    "  for (var i = 0; i < 2048; ++i) {\n"
    "    A[i] = bar(1024);\n"
    "  }\n"
    "}\n"
    "foo();";

TEST(SamplingHeapProfiler) {
  i::v8_flags.allow_natives_syntax = true;
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  // Turn off always_turbofan. Inlining can cause stack traces to be shorter
  // than what we expect in this test.
  i::v8_flags.always_turbofan = false;

  // Suppress randomness to avoid flakiness in tests.
  i::v8_flags.sampling_heap_profiler_suppress_randomness = true;

  // Sample should be empty if requested before sampling has started.
  {
    v8::AllocationProfile* profile = heap_profiler->GetAllocationProfile();
    CHECK_NULL(profile);
  }

  {
    heap_profiler->StartSamplingHeapProfiler(1024);
    CompileRun(simple_sampling_heap_profiler_script);

    std::unique_ptr<v8::AllocationProfile> profile(
        heap_profiler->GetAllocationProfile());
    CHECK(profile);

    const char* names[] = {"", "foo", "bar"};
    auto node_bar = FindAllocationProfileNode(env->GetIsolate(), profile.get(),
                                              v8::base::ArrayVector(names));
    CHECK(node_bar);

    heap_profiler->StopSamplingHeapProfiler();
  }

  // Samples should get cleared once sampling is stopped.
  {
    v8::AllocationProfile* profile = heap_profiler->GetAllocationProfile();
    CHECK_NULL(profile);
  }

  // A more complicated test cases with deeper call graph and dynamically
  // generated function names.
  {
    heap_profiler->StartSamplingHeapProfiler(64);
    CompileRun(record_trace_tree_source);

    std::unique_ptr<v8::AllocationProfile> profile(
        heap_profiler->GetAllocationProfile());
    CHECK(profile);

    const char* names1[] = {"", "start", "f_0_0", "f_0_1", "f_0_2"};
    auto node1 = FindAllocationProfileNode(env->GetIsolate(), profile.get(),
                                           v8::base::ArrayVector(names1));
    CHECK(node1);

    const char* names2[] = {"", "generateFunctions"};
    auto node2 = FindAllocationProfileNode(env->GetIsolate(), profile.get(),
                                           v8::base::ArrayVector(names2));
    CHECK(node2);

    heap_profiler->StopSamplingHeapProfiler();
  }

  // A test case with scripts unloaded before profile gathered
  {
    heap_profiler->StartSamplingHeapProfiler(64);
    CompileRun(
        "for (var i = 0; i < 1024; i++) {\n"
        "  eval(\"new Array(100)\");\n"
        "}\n");

    i::heap::InvokeMajorGC(CcTest::heap());

    std::unique_ptr<v8::AllocationProfile> profile(
        heap_profiler->GetAllocationProfile());
    CHECK(profile);

    CheckNoZeroCountNodes(profile->GetRootNode());

    heap_profiler->StopSamplingHeapProfiler();
  }
}

TEST(SamplingHeapProfilerRateAgnosticEstimates) {
  i::v8_flags.allow_natives_syntax = true;
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  // Turn off always_turbofan. Inlining can cause stack traces to be shorter
  // than what we expect in this test.
  i::v8_flags.always_turbofan = false;

  // Disable compilation cache to force compilation in both cases
  i::v8_flags.compilation_cache = false;

  // Suppress randomness to avoid flakiness in tests.
  i::v8_flags.sampling_heap_profiler_suppress_randomness = true;

  // stress_incremental_marking adds randomness to the test.
  i::v8_flags.stress_incremental_marking = false;

  // warmup compilation
  CompileRun(simple_sampling_heap_profiler_script);

  int count_1024 = 0;
  {
    heap_profiler->StartSamplingHeapProfiler(1024);
    CompileRun(simple_sampling_heap_profiler_script);

    std::unique_ptr<v8::AllocationProfile> profile(
        heap_profiler->GetAllocationProfile());
    CHECK(profile);

    const char* path_to_foo[] = {"", "foo"};
    auto node_foo = FindAllocationProfileNode(
        env->GetIsolate(), profile.get(), v8::base::ArrayVector(path_to_foo));
    CHECK(node_foo);
    const char* path_to_bar[] = {"", "foo", "bar"};
    auto node_bar = FindAllocationProfileNode(
        env->GetIsolate(), profile.get(), v8::base::ArrayVector(path_to_bar));
    CHECK(node_bar);

    // Function bar can be inlined in foo.
    count_1024 = NumberOfAllocations(node_foo) + NumberOfAllocations(node_bar);

    heap_profiler->StopSamplingHeapProfiler();
  }

  // Sampling at a higher rate should give us similar numbers of objects.
  {
    heap_profiler->StartSamplingHeapProfiler(128);
    CompileRun(simple_sampling_heap_profiler_script);

    std::unique_ptr<v8::AllocationProfile> profile(
        heap_profiler->GetAllocationProfile());
    CHECK(profile);

    const char* path_to_foo[] = {"", "foo"};
    auto node_foo = FindAllocationProfileNode(
        env->GetIsolate(), profile.get(), v8::base::ArrayVector(path_to_foo));
    CHECK(node_foo);
    const char* path_to_bar[] = {"", "foo", "bar"};
    auto node_bar = FindAllocationProfileNode(
        env->GetIsolate(), profile.get(), v8::base::ArrayVector(path_to_bar));
    CHECK(node_bar);

    // Function bar can be inlined in foo.
    int count_128 =
        NumberOfAllocations(node_foo) + NumberOfAllocations(node_bar);

    // We should have similar unsampled counts of allocations. Though
    // we will sample different numbers of objects at different rates,
    // the unsampling process should produce similar final estimates
    // at the true number of allocations. However, the process to
    // determine these unsampled counts is probabilisitic so we need to
    // account for error.
    double max_count = std::max(count_128, count_1024);
    double min_count = std::min(count_128, count_1024);
    double percent_difference = (max_count - min_count) / min_count;
    CHECK_LT(percent_difference, 0.1);

    heap_profiler->StopSamplingHeapProfiler();
  }
}

TEST(SamplingHeapProfilerApiAllocation) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  // Suppress randomness to avoid flakiness in tests.
  i::v8_flags.sampling_heap_profiler_suppress_randomness = true;

  heap_profiler->StartSamplingHeapProfiler(256);

  for (int i = 0; i < 8 * 1024; ++i) v8::Object::New(env->GetIsolate());

  std::unique_ptr<v8::AllocationProfile> profile(
      heap_profiler->GetAllocationProfile());
  CHECK(profile);
  const char* names[] = {"(V8 API)"};
  auto node = FindAllocationProfileNode(env->GetIsolate(), profile.get(),
                                        v8::base::ArrayVector(names));
  CHECK(node);

  heap_profiler->StopSamplingHeapProfiler();
}

TEST(SamplingHeapProfilerApiSamples) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  // Suppress randomness to avoid flakiness in tests.
  i::v8_flags.sampling_heap_profiler_suppress_randomness = true;

  heap_profiler->StartSamplingHeapProfiler(1024);

  size_t count = 8 * 1024;
  for (size_t i = 0; i < count; ++i) v8::Object::New(env->GetIsolate());

  std::unique_ptr<v8::AllocationProfile> profile(
      heap_profiler->GetAllocationProfile());
  CHECK(profile);

  std::vector<v8::AllocationProfile::Node*> nodes_to_visit;
  std::unordered_set<uint32_t> node_ids;
  nodes_to_visit.push_back(profile->GetRootNode());
  while (!nodes_to_visit.empty()) {
    v8::AllocationProfile::Node* node = nodes_to_visit.back();
    nodes_to_visit.pop_back();
    CHECK_LT(0, node->node_id);
    CHECK_EQ(0, node_ids.count(node->node_id));
    node_ids.insert(node->node_id);
    nodes_to_visit.insert(nodes_to_visit.end(), node->children.begin(),
                          node->children.end());
  }

  size_t total_size = 0;
  std::unordered_set<uint64_t> samples_set;
  for (auto& sample : profile->GetSamples()) {
    total_size += sample.size * sample.count;
    CHECK_EQ(0, samples_set.count(sample.sample_id));
    CHECK_EQ(1, node_ids.count(sample.node_id));
    CHECK_GT(sample.node_id, 0);
    CHECK_GT(sample.sample_id, 0);
    samples_set.insert(sample.sample_id);
  }
  size_t object_size = total_size / count;
  CHECK_GE(object_size, sizeof(void*) * 2);
  heap_profiler->StopSamplingHeapProfiler();
}

TEST(SamplingHeapProfilerLeftTrimming) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  // Suppress randomness to avoid flakiness in tests.
  i::v8_flags.sampling_heap_profiler_suppress_randomness = true;

  heap_profiler->StartSamplingHeapProfiler(64);

  CompileRun(
      "for (var j = 0; j < 500; ++j) {\n"
      "  var a = [];\n"
      "  for (var i = 0; i < 5; ++i)\n"
      "      a[i] = i;\n"
      "  for (var i = 0; i < 3; ++i)\n"
      "      a.shift();\n"
      "}\n");

  i::heap::InvokeMinorGC(CcTest::heap());
  // Should not crash.

  heap_profiler->StopSamplingHeapProfiler();
}

TEST(SamplingHeapProfilerPretenuredInlineAllocations) {
  i::v8_flags.allow_natives_syntax = true;
  i::v8_flags.expose_gc = true;

  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || i::v8_flags.always_turbofan)
    return;
  if (i::v8_flags.gc_global || i::v8_flags.stress_compaction ||
      i::v8_flags.stress_incremental_marking ||
      i::v8_flags.stress_concurrent_allocation ||
      i::v8_flags.single_generation) {
    return;
  }

  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  // Suppress randomness to avoid flakiness in tests.
  i::v8_flags.sampling_heap_profiler_suppress_randomness = true;

  // Disable loop unrolling to have a more predictable number of allocations
  // (loop unrolling could cause allocation folding).
  i::v8_flags.turboshaft_loop_unrolling = false;

  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  v8::base::ScopedVector<char> source(1024);
  v8::base::SNPrintF(source,
                     "var number_elements = %d;"
                     "var elements = new Array(number_elements);"
                     "function f() {"
                     "  for (var i = 0; i < number_elements; i++) {"
                     "    elements[i] = [{}, {}, {}];"
                     "  }"
                     "  return elements[number_elements - 1];"
                     "};"
                     "%%PrepareFunctionForOptimization(f);"
                     "f(); gc();"
                     "f(); f();"
                     "%%OptimizeFunctionOnNextCall(f);"
                     "f();"
                     "f;",
                     i::PretenuringHandler::GetMinMementoCountForTesting() + 1);

  v8::Local<v8::Function> f =
      v8::Local<v8::Function>::Cast(CompileRun(source.begin()));

  // Make sure the function is producing pre-tenured objects.
  auto res = f->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  i::DirectHandle<i::JSObject> o = i::Cast<i::JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));
  CHECK(CcTest::heap()->InOldSpace(o->elements()));
  CHECK(CcTest::heap()->InOldSpace(*o));

  // Call the function and profile it.
  heap_profiler->StartSamplingHeapProfiler(64);
  for (int i = 0; i < 80; ++i) {
    f->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  }

  std::unique_ptr<v8::AllocationProfile> profile(
      heap_profiler->GetAllocationProfile());
  CHECK(profile);
  heap_profiler->StopSamplingHeapProfiler();

  const char* names[] = {"f"};
  auto node_f = FindAllocationProfileNode(env->GetIsolate(), profile.get(),
                                          v8::base::ArrayVector(names));
  CHECK(node_f);

  int count = 0;
  for (auto allocation : node_f->allocations) {
    count += allocation.count;
  }

  CHECK_GE(count, 8000);
}

TEST(SamplingHeapProfilerLargeInterval) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  // Suppress randomness to avoid flakiness in tests.
  i::v8_flags.sampling_heap_profiler_suppress_randomness = true;

  heap_profiler->StartSamplingHeapProfiler(512 * 1024);

  for (int i = 0; i < 8 * 1024; ++i) {
    CcTest::i_isolate()->factory()->NewFixedArray(1024);
  }

  std::unique_ptr<v8::AllocationProfile> profile(
      heap_profiler->GetAllocationProfile());
  CHECK(profile);
  const char* names[] = {"(EXTERNAL)"};
  auto node = FindAllocationProfileNode(env->GetIsolate(), profile.get(),
                                        v8::base::ArrayVector(names));
  CHECK(node);

  heap_profiler->StopSamplingHeapProfiler();
}

TEST(HeapSnapshotPrototypeNotJSReceiver) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  CompileRun(
      "function object() {}"
      "object.prototype = 42;");
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
}

TEST(SamplingHeapProfilerSampleDuringDeopt) {
  i::v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  // Suppress randomness to avoid flakiness in tests.
  i::v8_flags.sampling_heap_profiler_suppress_randomness = true;

  // Small sample interval to force each object to be sampled.
  heap_profiler->StartSamplingHeapProfiler(i::kTaggedSize);

  // Lazy deopt from runtime call from inlined callback function.
  const char* source =
      "var b = "
      "  [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25];"
      "(function f() {"
      "  var result = 0;"
      "  var lazyDeopt = function(deopt) {"
      "    var callback = function(v,i,o) {"
      "      result += i;"
      "      if (i == 13 && deopt) {"
      "          %DeoptimizeNow();"
      "      }"
      "      return v;"
      "    };"
      "    b.map(callback);"
      "  };"
      "  %PrepareFunctionForOptimization(lazyDeopt);"
      "  lazyDeopt();"
      "  lazyDeopt();"
      "  %OptimizeFunctionOnNextCall(lazyDeopt);"
      "  lazyDeopt();"
      "  lazyDeopt(true);"
      "  lazyDeopt();"
      "})();";

  CompileRun(source);
  // Should not crash.

  std::unique_ptr<v8::AllocationProfile> profile(
      heap_profiler->GetAllocationProfile());
  CHECK(profile);
  heap_profiler->StopSamplingHeapProfiler();
}

namespace {
class TestQueryObjectPredicate : public v8::QueryObjectPredicate {
 public:
  TestQueryObjectPredicate(v8::Local<v8::Context> context,
                           v8::Local<v8::Symbol> symbol)
      : context_(context), symbol_(symbol) {}

  bool Filter(v8::Local<v8::Object> object) override {
    return object->HasOwnProperty(context_, symbol_).FromMaybe(false);
  }

 private:
  v8::Local<v8::Context> context_;
  v8::Local<v8::Symbol> symbol_;
};

class IncludeAllQueryObjectPredicate : public v8::QueryObjectPredicate {
 public:
  IncludeAllQueryObjectPredicate() {}
  bool Filter(v8::Local<v8::Object> object) override { return true; }
};
}  // anonymous namespace

TEST(QueryObjects) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context = env.local();

  v8::Local<v8::Symbol> sym =
      v8::Symbol::New(isolate, v8_str("query_object_test"));
  context->Global()->Set(context, v8_str("test_symbol"), sym).Check();
  v8::Local<v8::Value> arr = CompileRun(R"(
      const arr = [];
      for (let i = 0; i < 10; ++i) {
        arr.push({[test_symbol]: true});
      }
      arr;
    )");
  context->Global()->Set(context, v8_str("arr"), arr).Check();
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  {
    TestQueryObjectPredicate predicate(context, sym);
    std::vector<v8::Global<v8::Object>> out;
    heap_profiler->QueryObjects(context, &predicate, &out);

    CHECK_EQ(out.size(), 10);
    for (size_t i = 0; i < out.size(); ++i) {
      CHECK(out[i].Get(isolate)->HasOwnProperty(context, sym).FromMaybe(false));
    }
  }

  {
    IncludeAllQueryObjectPredicate predicate;
    std::vector<v8::Global<v8::Object>> out;
    heap_profiler->QueryObjects(context, &predicate, &out);
    CHECK_GE(out.size(), 10);
  }
}

TEST(WeakReference) {
  v8::Isolate* isolate = CcTest::isolate();
  i::Isolate* i_isolate = CcTest::i_isolate();
  i::Factory* factory = i_isolate->factory();
  i::HandleScope scope(i_isolate);
  LocalContext env;

  // Create a FeedbackVector.
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          isolate->GetCurrentContext(),
          v8::String::NewFromUtf8Literal(isolate, "function foo() {}"))
          .ToLocalChecked();
  v8::MaybeLocal<v8::Value> value = script->Run(isolate->GetCurrentContext());
  CHECK(!value.IsEmpty());

  i::DirectHandle<i::Object> obj = v8::Utils::OpenDirectHandle(*script);
  i::DirectHandle<i::SharedFunctionInfo> shared_function(
      i::Cast<i::JSFunction>(*obj)->shared(), i_isolate);
  i::DirectHandle<i::ClosureFeedbackCellArray> feedback_cell_array =
      i::ClosureFeedbackCellArray::New(i_isolate, shared_function);
  i::DirectHandle<i::FeedbackVector> fv = factory->NewFeedbackVector(
      shared_function, feedback_cell_array,
      handle(i::Cast<i::JSFunction>(*obj)->raw_feedback_cell(), i_isolate));

  // Create a Code object.
  i::Assembler assm(i::AssemblerOptions{});
  assm.nop();  // supported on all architectures
  i::CodeDesc desc;
  assm.GetCode(i_isolate, &desc);
  i::DirectHandle<i::Code> code =
      i::Factory::CodeBuilder(i_isolate, desc, i::CodeKind::FOR_TESTING)
          .Build();
  CHECK(IsCode(*code));

  // Manually inlined version of FeedbackVector::SetOptimizedCode (needed due
  // to the FOR_TESTING code kind).
  fv->set_maybe_optimized_code(i::MakeWeak(code->wrapper()));
  fv->set_flags(
      i::FeedbackVector::MaybeHasTurbofanCodeBit::encode(true) |
      i::FeedbackVector::TieringStateBits::encode(i::TieringState::kNone));

  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
}

TEST(Bug8373_1) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  heap_profiler->StartSamplingHeapProfiler(100);

  heap_profiler->TakeHeapSnapshot();
  // Causes the StringsStorage to be deleted.
  heap_profiler->DeleteAllHeapSnapshots();

  // Triggers an allocation sample that tries to use the StringsStorage.
  for (int i = 0; i < 2 * 1024; ++i) {
    CompileRun(
        "new Array(64);"
        "new Uint8Array(16);");
  }

  heap_profiler->StopSamplingHeapProfiler();
}

TEST(Bug8373_2) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();

  heap_profiler->StartTrackingHeapObjects(true);

  heap_profiler->TakeHeapSnapshot();
  // Causes the StringsStorage to be deleted.
  heap_profiler->DeleteAllHeapSnapshots();

  // Triggers an allocations that try to use the StringsStorage.
  for (int i = 0; i < 2 * 1024; ++i) {
    CompileRun(
        "new Array(64);"
        "new Uint8Array(16);");
  }

  heap_profiler->StopTrackingHeapObjects();
}

TEST(HeapSnapshotDeleteDuringTakeSnapshot) {
  // Check that a heap snapshot can be deleted during GC while another one
  // is being taken.

  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::HeapProfiler* heap_profiler = env->GetIsolate()->GetHeapProfiler();
  int gc_calls = 0;
  v8::Global<v8::Object> handle;
  v8::Isolate* isolate = env->GetIsolate();
  i::Heap* heap = reinterpret_cast<i::Isolate*>(isolate)->heap();

  {
    struct WeakData {
      const v8::HeapSnapshot* snapshot;
      int* gc_calls;
      v8::Global<v8::Object>* handle;
    };
    WeakData* data =
        new WeakData{heap_profiler->TakeHeapSnapshot(), &gc_calls, &handle};

    v8::HandleScope inner_scope(isolate);
    handle.Reset(isolate, v8::Object::New(isolate));
    handle.SetWeak(
        data,
        [](const v8::WeakCallbackInfo<WeakData>& data) {
          std::unique_ptr<WeakData> weakdata{data.GetParameter()};
          const_cast<v8::HeapSnapshot*>(weakdata->snapshot)->Delete();
          ++*weakdata->gc_calls;
          weakdata->handle->Reset();
        },
        v8::WeakCallbackType::kParameter);
  }
  CHECK_EQ(gc_calls, 0);

  // We need to invoke GC without stack, otherwise some objects may survive.
  i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
  // For the same reason, we need to take the snapshot without scanning the
  // stack.
  v8::HeapProfiler::HeapSnapshotOptions options;
  options.stack_state = cppgc::EmbedderStackState::kNoHeapPointers;

  CHECK(ValidateSnapshot(heap_profiler->TakeHeapSnapshot(options)));
  CHECK_EQ(gc_calls, 1);
}

TEST(ObjectRetainedInHandle) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  // Allocate an array and keep a handle to it.
  i::Handle<i::FixedArray> handle = i_isolate->factory()->NewFixedArray(1024);

  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  // Make sure to keep the handle alive.
  CHECK(!handle.is_null());
}

TEST(ObjectRetainedInDirectHandle) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::HandleScope scope(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  // Allocate an array and keep a direct handle to it.
  i::DirectHandle<i::FixedArray> direct;
  {
    // Make sure the temporary indirect handle goes away.
    v8::HandleScope inner_scope(isolate);
    direct = i_isolate->factory()->NewFixedArray(1024);
  }

  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));

  // Make sure to keep the handle alive.
  CHECK(!direct.is_null());
}

#if V8_ENABLE_WEBASSEMBLY
TEST(HeapSnapshotWithWasmInstance) {
  LocalContext env2;
  v8::Isolate* isolate = env2->GetIsolate();
  v8::HandleScope scope(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::HeapProfiler* heap_profiler = isolate->GetHeapProfiler();

  i::Zone zone(i_isolate->allocator(), ZONE_NAME);
  i::wasm::ZoneBuffer buffer(&zone);
  i::wasm::WasmModuleBuilder module_builder{&zone};
  module_builder.WriteTo(&buffer);

  v8::Local<v8::Context> context = isolate->GetCurrentContext();

  // Get the "WebAssembly.Module" and "WebAssembly.Instance" functions.
  auto get_property = [context, isolate](
                          v8::Local<v8::Object> obj,
                          const char* property_name) -> v8::Local<v8::Object> {
    auto name = v8::String::NewFromUtf8(isolate, property_name,
                                        v8::NewStringType::kInternalized)
                    .ToLocalChecked();
    return obj->Get(context, name).ToLocalChecked().As<v8::Object>();
  };
  auto wasm_class = get_property(context->Global(), "WebAssembly");
  auto module_class = get_property(wasm_class, "Module");
  auto instance_class = get_property(wasm_class, "Instance");

  // Create an arraybuffer with the wire bytes.
  v8::Local<v8::ArrayBuffer> buf = v8::ArrayBuffer::New(isolate, buffer.size());
  memcpy(static_cast<uint8_t*>(buf->GetBackingStore()->Data()), buffer.data(),
         buffer.size());

  // Now call the "WebAssembly.Module" function with the array buffer.
  v8::Local<v8::Value> module_args[] = {buf};
  v8::Local<v8::Value> module_object =
      module_class
          ->CallAsConstructor(context, arraysize(module_args), module_args)
          .ToLocalChecked();
  auto set_property = [context, isolate](v8::Local<v8::Object> obj,
                                         const char* property_name,
                                         v8::Local<v8::Value> value) {
    auto name = v8::String::NewFromUtf8(isolate, property_name,
                                        v8::NewStringType::kInternalized)
                    .ToLocalChecked();
    CHECK(obj->Set(context, name, value).FromMaybe(false));
  };
  // Store the module as global "module".
  set_property(context->Global(), "module", module_object);

  // Create a Wasm instance by calling "WebAssembly.Instance" with the module.
  v8::Local<v8::Value> instance_args[] = {module_object};
  v8::Local<v8::Value> instance_object =
      instance_class
          ->CallAsConstructor(context, arraysize(instance_args), instance_args)
          .ToLocalChecked();
  // Store the instance object as global "instance".
  set_property(context->Global(), "instance", instance_object);

  // Now take a snapshot and check the representation of the Wasm objects.
  const v8::HeapSnapshot* snapshot = heap_profiler->TakeHeapSnapshot();
  CHECK(ValidateSnapshot(snapshot));
  const v8::HeapGraphNode* global = GetGlobalObject(snapshot);

  // Check the properties of the global "instance" (adapt this when fields are
  // added or removed).
  const v8::HeapGraphNode* instance_node =
      GetProperty(isolate, global, v8::HeapGraphEdge::kProperty, "instance");
  CHECK_NOT_NULL(instance_node);
  CheckProperties(
      isolate, instance_node,
      {"__proto__", "exports", "map", "module_object", "trusted_data"});

  // Check the properties of the WasmTrustedInstanceData.
  const v8::HeapGraphNode* trusted_instance_data_node = GetProperty(
      isolate, instance_node, v8::HeapGraphEdge::kInternal, "trusted_data");
  CHECK_NOT_NULL(trusted_instance_data_node);
  CheckProperties(
      isolate, trusted_instance_data_node,
      {"dispatch_table0", "dispatch_table_for_imports", "dispatch_tables",
       "instance_object", "managed_native_module", "map",
       "memory_bases_and_sizes", "native_context", "shared_part"});

  // "module_object" should be the same as the global "module".
  const v8::HeapGraphNode* module_node =
      GetProperty(isolate, global, v8::HeapGraphEdge::kProperty, "module");
  CHECK_NOT_NULL(module_node);
  CHECK_EQ(module_node,
           GetProperty(isolate, instance_node, v8::HeapGraphEdge::kInternal,
                       "module_object"));
  // Check that all properties of the WasmModuleObject are there (adapt this
  // when fields are added or removed).
  CheckProperties(isolate, module_node,
                  {"__proto__", "managed_native_module", "map", "script"});
  // Check the "managed_native_module" specifically. It should say
  // "Managed<wasm::NativeModule>" and should have a reasonable size.
  const v8::HeapGraphNode* managed_node =
      GetProperty(isolate, module_node, v8::HeapGraphEdge::kInternal,
                  "managed_native_module");
  CHECK_NOT_NULL(managed_node);
  v8::String::Utf8Value managed_name{isolate, managed_node->GetName()};
#if V8_ENABLE_SANDBOX
  CHECK_EQ(std::string_view{"system / Managed<wasm::NativeModule>"},
           std::string_view{*managed_name});
  // The size of the Managed is computed from the size of the NativeModule. This
  // is multiple kB, just conservatively assume >= 500b here.
  CHECK_LE(500, managed_node->GetShallowSize());
#else
  CHECK_EQ(std::string_view{"system / Foreign"},
           std::string_view{*managed_name});
#endif  // V8_ENABLE_SANDBOX
}
#endif  // V8_ENABLE_WEBASSEMBLY
