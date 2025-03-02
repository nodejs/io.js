// Copyright 2010 the V8 project authors. All rights reserved.
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
// Tests of profiles generator and utilities.

#include "include/v8-function.h"
#include "include/v8-profiler.h"
#include "src/api/api-inl.h"
#include "src/base/strings.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "src/profiler/cpu-profiler.h"
#include "src/profiler/profile-generator-inl.h"
#include "src/profiler/symbolizer.h"
#include "test/cctest/cctest.h"
#include "test/cctest/profiler-extension.h"

namespace v8 {
namespace internal {
namespace test_profile_generator {

TEST(ProfileNodeFindOrAddChild) {
  CcTest::InitializeVM();
  ProfileTree tree(CcTest::i_isolate());
  ProfileNode* node = tree.root();
  CodeEntry entry1(i::LogEventListener::CodeTag::kFunction, "aaa");
  ProfileNode* childNode1 = node->FindOrAddChild(&entry1);
  CHECK(childNode1);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry1));
  CodeEntry entry2(i::LogEventListener::CodeTag::kFunction, "bbb");
  ProfileNode* childNode2 = node->FindOrAddChild(&entry2);
  CHECK(childNode2);
  CHECK_NE(childNode1, childNode2);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry1));
  CHECK_EQ(childNode2, node->FindOrAddChild(&entry2));
  CodeEntry entry3(i::LogEventListener::CodeTag::kFunction, "ccc");
  ProfileNode* childNode3 = node->FindOrAddChild(&entry3);
  CHECK(childNode3);
  CHECK_NE(childNode1, childNode3);
  CHECK_NE(childNode2, childNode3);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry1));
  CHECK_EQ(childNode2, node->FindOrAddChild(&entry2));
  CHECK_EQ(childNode3, node->FindOrAddChild(&entry3));
}

TEST(ProfileNodeFindOrAddChildWithLineNumber) {
  CcTest::InitializeVM();
  ProfileTree tree(CcTest::i_isolate());
  ProfileNode* root = tree.root();
  CodeEntry a(i::LogEventListener::CodeTag::kFunction, "a");
  ProfileNode* a_node = root->FindOrAddChild(&a, -1);

  // a --(22)--> child1
  //   --(23)--> child1

  CodeEntry child1(i::LogEventListener::CodeTag::kFunction, "child1");
  ProfileNode* child1_node = a_node->FindOrAddChild(&child1, 22);
  CHECK(child1_node);
  CHECK_EQ(child1_node, a_node->FindOrAddChild(&child1, 22));

  ProfileNode* child2_node = a_node->FindOrAddChild(&child1, 23);
  CHECK(child2_node);
  CHECK_NE(child1_node, child2_node);
}

TEST(ProfileNodeFindOrAddChildForSameFunction) {
  CcTest::InitializeVM();
  const char* aaa = "aaa";
  ProfileTree tree(CcTest::i_isolate());
  ProfileNode* node = tree.root();
  CodeEntry entry1(i::LogEventListener::CodeTag::kFunction, aaa);
  ProfileNode* childNode1 = node->FindOrAddChild(&entry1);
  CHECK(childNode1);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry1));
  // The same function again.
  CodeEntry entry2(i::LogEventListener::CodeTag::kFunction, aaa);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry2));
  // Now with a different security token.
  CodeEntry entry3(i::LogEventListener::CodeTag::kFunction, aaa);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry3));
}


namespace {

class ProfileTreeTestHelper {
 public:
  explicit ProfileTreeTestHelper(const ProfileTree* tree)
      : tree_(tree) { }

  ProfileNode* Walk(CodeEntry* entry1, CodeEntry* entry2 = nullptr,
                    CodeEntry* entry3 = nullptr) {
    ProfileNode* node = tree_->root();
    node = node->FindChild(entry1);
    if (node == nullptr) return nullptr;
    if (entry2 != nullptr) {
      node = node->FindChild(entry2);
      if (node == nullptr) return nullptr;
    }
    if (entry3 != nullptr) {
      node = node->FindChild(entry3);
    }
    return node;
  }

 private:
  const ProfileTree* tree_;
};

}  // namespace


TEST(ProfileTreeAddPathFromEnd) {
  CcTest::InitializeVM();
  CodeEntry entry1(i::LogEventListener::CodeTag::kFunction, "aaa");
  CodeEntry entry2(i::LogEventListener::CodeTag::kFunction, "bbb");
  CodeEntry entry3(i::LogEventListener::CodeTag::kFunction, "ccc");
  ProfileTree tree(CcTest::i_isolate());
  ProfileTreeTestHelper helper(&tree);
  CHECK(!helper.Walk(&entry1));
  CHECK(!helper.Walk(&entry2));
  CHECK(!helper.Walk(&entry3));

  CodeEntry* path[] = {nullptr, &entry3, nullptr, &entry2,
                       nullptr, nullptr, &entry1, nullptr};
  std::vector<CodeEntry*> path_vec(path, path + arraysize(path));
  tree.AddPathFromEnd(path_vec);
  CHECK(!helper.Walk(&entry2));
  CHECK(!helper.Walk(&entry3));
  ProfileNode* node1 = helper.Walk(&entry1);
  CHECK(node1);
  CHECK_EQ(0u, node1->self_ticks());
  CHECK(!helper.Walk(&entry1, &entry1));
  CHECK(!helper.Walk(&entry1, &entry3));
  ProfileNode* node2 = helper.Walk(&entry1, &entry2);
  CHECK(node2);
  CHECK_NE(node1, node2);
  CHECK_EQ(0u, node2->self_ticks());
  CHECK(!helper.Walk(&entry1, &entry2, &entry1));
  CHECK(!helper.Walk(&entry1, &entry2, &entry2));
  ProfileNode* node3 = helper.Walk(&entry1, &entry2, &entry3);
  CHECK(node3);
  CHECK_NE(node1, node3);
  CHECK_NE(node2, node3);
  CHECK_EQ(1u, node3->self_ticks());

  tree.AddPathFromEnd(path_vec);
  CHECK_EQ(node1, helper.Walk(&entry1));
  CHECK_EQ(node2, helper.Walk(&entry1, &entry2));
  CHECK_EQ(node3, helper.Walk(&entry1, &entry2, &entry3));
  CHECK_EQ(0u, node1->self_ticks());
  CHECK_EQ(0u, node2->self_ticks());
  CHECK_EQ(2u, node3->self_ticks());

  CodeEntry* path2[] = {&entry2, &entry2, &entry1};
  std::vector<CodeEntry*> path2_vec(path2, path2 + arraysize(path2));
  tree.AddPathFromEnd(path2_vec);
  CHECK(!helper.Walk(&entry2));
  CHECK(!helper.Walk(&entry3));
  CHECK_EQ(node1, helper.Walk(&entry1));
  CHECK(!helper.Walk(&entry1, &entry1));
  CHECK(!helper.Walk(&entry1, &entry3));
  CHECK_EQ(node2, helper.Walk(&entry1, &entry2));
  CHECK(!helper.Walk(&entry1, &entry2, &entry1));
  CHECK_EQ(node3, helper.Walk(&entry1, &entry2, &entry3));
  CHECK_EQ(2u, node3->self_ticks());
  ProfileNode* node4 = helper.Walk(&entry1, &entry2, &entry2);
  CHECK(node4);
  CHECK_NE(node3, node4);
  CHECK_EQ(1u, node4->self_ticks());
}

TEST(ProfileTreeAddPathFromEndWithLineNumbers) {
  CcTest::InitializeVM();
  CodeEntry a(i::LogEventListener::CodeTag::kFunction, "a");
  CodeEntry b(i::LogEventListener::CodeTag::kFunction, "b");
  CodeEntry c(i::LogEventListener::CodeTag::kFunction, "c");
  ProfileTree tree(CcTest::i_isolate());
  ProfileTreeTestHelper helper(&tree);

  ProfileStackTrace path = {{&c, 5}, {&b, 3}, {&a, 1}};
  tree.AddPathFromEnd(path, v8::CpuProfileNode::kNoLineNumberInfo, true,
                      v8::CpuProfilingMode::kCallerLineNumbers);

  ProfileNode* a_node =
      tree.root()->FindChild(&a, v8::CpuProfileNode::kNoLineNumberInfo);
  tree.Print();
  CHECK(a_node);

  ProfileNode* b_node = a_node->FindChild(&b, 1);
  CHECK(b_node);

  ProfileNode* c_node = b_node->FindChild(&c, 3);
  CHECK(c_node);
}

TEST(ProfileTreeCalculateTotalTicks) {
  CcTest::InitializeVM();
  ProfileTree empty_tree(CcTest::i_isolate());
  CHECK_EQ(0u, empty_tree.root()->self_ticks());
  empty_tree.root()->IncrementSelfTicks();
  CHECK_EQ(1u, empty_tree.root()->self_ticks());

  CodeEntry entry1(i::LogEventListener::CodeTag::kFunction, "aaa");
  CodeEntry* e1_path[] = {&entry1};
  std::vector<CodeEntry*> e1_path_vec(e1_path, e1_path + arraysize(e1_path));

  ProfileTree single_child_tree(CcTest::i_isolate());
  single_child_tree.AddPathFromEnd(e1_path_vec);
  single_child_tree.root()->IncrementSelfTicks();
  CHECK_EQ(1u, single_child_tree.root()->self_ticks());
  ProfileTreeTestHelper single_child_helper(&single_child_tree);
  ProfileNode* node1 = single_child_helper.Walk(&entry1);
  CHECK(node1);
  CHECK_EQ(1u, single_child_tree.root()->self_ticks());
  CHECK_EQ(1u, node1->self_ticks());

  CodeEntry entry2(i::LogEventListener::CodeTag::kFunction, "bbb");
  CodeEntry* e2_e1_path[] = {&entry2, &entry1};
  std::vector<CodeEntry*> e2_e1_path_vec(e2_e1_path,
                                         e2_e1_path + arraysize(e2_e1_path));

  ProfileTree flat_tree(CcTest::i_isolate());
  ProfileTreeTestHelper flat_helper(&flat_tree);
  flat_tree.AddPathFromEnd(e1_path_vec);
  flat_tree.AddPathFromEnd(e1_path_vec);
  flat_tree.AddPathFromEnd(e2_e1_path_vec);
  flat_tree.AddPathFromEnd(e2_e1_path_vec);
  flat_tree.AddPathFromEnd(e2_e1_path_vec);
  // Results in {root,0,0} -> {entry1,0,2} -> {entry2,0,3}
  CHECK_EQ(0u, flat_tree.root()->self_ticks());
  node1 = flat_helper.Walk(&entry1);
  CHECK(node1);
  CHECK_EQ(2u, node1->self_ticks());
  ProfileNode* node2 = flat_helper.Walk(&entry1, &entry2);
  CHECK(node2);
  CHECK_EQ(3u, node2->self_ticks());
  // Must calculate {root,5,0} -> {entry1,5,2} -> {entry2,3,3}
  CHECK_EQ(0u, flat_tree.root()->self_ticks());
  CHECK_EQ(2u, node1->self_ticks());

  CodeEntry* e2_path[] = {&entry2};
  std::vector<CodeEntry*> e2_path_vec(e2_path, e2_path + arraysize(e2_path));
  CodeEntry entry3(i::LogEventListener::CodeTag::kFunction, "ccc");
  CodeEntry* e3_path[] = {&entry3};
  std::vector<CodeEntry*> e3_path_vec(e3_path, e3_path + arraysize(e3_path));

  ProfileTree wide_tree(CcTest::i_isolate());
  ProfileTreeTestHelper wide_helper(&wide_tree);
  wide_tree.AddPathFromEnd(e1_path_vec);
  wide_tree.AddPathFromEnd(e1_path_vec);
  wide_tree.AddPathFromEnd(e2_e1_path_vec);
  wide_tree.AddPathFromEnd(e2_path_vec);
  wide_tree.AddPathFromEnd(e2_path_vec);
  wide_tree.AddPathFromEnd(e2_path_vec);
  wide_tree.AddPathFromEnd(e3_path_vec);
  wide_tree.AddPathFromEnd(e3_path_vec);
  wide_tree.AddPathFromEnd(e3_path_vec);
  wide_tree.AddPathFromEnd(e3_path_vec);
  // Results in            -> {entry1,0,2} -> {entry2,0,1}
  //            {root,0,0} -> {entry2,0,3}
  //                       -> {entry3,0,4}
  CHECK_EQ(0u, wide_tree.root()->self_ticks());
  node1 = wide_helper.Walk(&entry1);
  CHECK(node1);
  CHECK_EQ(2u, node1->self_ticks());
  ProfileNode* node1_2 = wide_helper.Walk(&entry1, &entry2);
  CHECK(node1_2);
  CHECK_EQ(1u, node1_2->self_ticks());
  node2 = wide_helper.Walk(&entry2);
  CHECK(node2);
  CHECK_EQ(3u, node2->self_ticks());
  ProfileNode* node3 = wide_helper.Walk(&entry3);
  CHECK(node3);
  CHECK_EQ(4u, node3->self_ticks());
  // Calculates             -> {entry1,3,2} -> {entry2,1,1}
  //            {root,10,0} -> {entry2,3,3}
  //                        -> {entry3,4,4}
  CHECK_EQ(0u, wide_tree.root()->self_ticks());
  CHECK_EQ(2u, node1->self_ticks());
  CHECK_EQ(1u, node1_2->self_ticks());
  CHECK_EQ(3u, node2->self_ticks());
  CHECK_EQ(4u, node3->self_ticks());
}

static inline i::Address ToAddress(int n) { return static_cast<i::Address>(n); }

static inline void* ToPointer(int n) { return reinterpret_cast<void*>(n); }

TEST(CodeMapAddCode) {
  CodeEntryStorage storage;
  InstructionStreamMap instruction_stream_map(storage);
  CodeEntry* entry1 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  CodeEntry* entry2 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "bbb");
  CodeEntry* entry3 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "ccc");
  CodeEntry* entry4 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "ddd");
  instruction_stream_map.AddCode(ToAddress(0x1500), entry1, 0x200);
  instruction_stream_map.AddCode(ToAddress(0x1700), entry2, 0x100);
  instruction_stream_map.AddCode(ToAddress(0x1900), entry3, 0x50);
  instruction_stream_map.AddCode(ToAddress(0x1950), entry4, 0x10);
  CHECK(!instruction_stream_map.FindEntry(0));
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1500 - 1)));
  CHECK_EQ(entry1, instruction_stream_map.FindEntry(ToAddress(0x1500)));
  CHECK_EQ(entry1, instruction_stream_map.FindEntry(ToAddress(0x1500 + 0x100)));
  CHECK_EQ(entry1,
           instruction_stream_map.FindEntry(ToAddress(0x1500 + 0x200 - 1)));
  CHECK_EQ(entry2, instruction_stream_map.FindEntry(ToAddress(0x1700)));
  CHECK_EQ(entry2, instruction_stream_map.FindEntry(ToAddress(0x1700 + 0x50)));
  CHECK_EQ(entry2,
           instruction_stream_map.FindEntry(ToAddress(0x1700 + 0x100 - 1)));
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1700 + 0x100)));
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1900 - 1)));
  CHECK_EQ(entry3, instruction_stream_map.FindEntry(ToAddress(0x1900)));
  CHECK_EQ(entry3, instruction_stream_map.FindEntry(ToAddress(0x1900 + 0x28)));
  CHECK_EQ(entry4, instruction_stream_map.FindEntry(ToAddress(0x1950)));
  CHECK_EQ(entry4, instruction_stream_map.FindEntry(ToAddress(0x1950 + 0x7)));
  CHECK_EQ(entry4,
           instruction_stream_map.FindEntry(ToAddress(0x1950 + 0x10 - 1)));
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1950 + 0x10)));
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0xFFFFFFFF)));
}

TEST(CodeMapMoveAndDeleteCode) {
  CodeEntryStorage storage;
  InstructionStreamMap instruction_stream_map(storage);
  CodeEntry* entry1 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  CodeEntry* entry2 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "bbb");
  instruction_stream_map.AddCode(ToAddress(0x1500), entry1, 0x200);
  instruction_stream_map.AddCode(ToAddress(0x1700), entry2, 0x100);
  CHECK_EQ(entry1, instruction_stream_map.FindEntry(ToAddress(0x1500)));
  CHECK_EQ(entry2, instruction_stream_map.FindEntry(ToAddress(0x1700)));
  instruction_stream_map.MoveCode(ToAddress(0x1500),
                                  ToAddress(0x1700));  // Deprecate bbb.
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1500)));
  CHECK_EQ(entry1, instruction_stream_map.FindEntry(ToAddress(0x1700)));
}

TEST(CodeMapClear) {
  CodeEntryStorage storage;
  InstructionStreamMap instruction_stream_map(storage);
  CodeEntry* entry1 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  CodeEntry* entry2 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "bbb");
  instruction_stream_map.AddCode(ToAddress(0x1500), entry1, 0x200);
  instruction_stream_map.AddCode(ToAddress(0x1700), entry2, 0x100);

  instruction_stream_map.Clear();
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1500)));
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1700)));

  // Check that Clear() doesn't cause issues if called twice.
  instruction_stream_map.Clear();
}

namespace {

class TestSetup {
 public:
  TestSetup() : old_flag_prof_browser_mode_(i::v8_flags.prof_browser_mode) {
    i::v8_flags.prof_browser_mode = false;
  }

  ~TestSetup() { i::v8_flags.prof_browser_mode = old_flag_prof_browser_mode_; }

 private:
  bool old_flag_prof_browser_mode_;
};

}  // namespace

TEST(SymbolizeTickSample) {
  TestSetup test_setup;
  CodeEntryStorage storage;
  InstructionStreamMap instruction_stream_map(storage);
  Symbolizer symbolizer(&instruction_stream_map);
  CodeEntry* entry1 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  CodeEntry* entry2 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "bbb");
  CodeEntry* entry3 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "ccc");
  symbolizer.instruction_stream_map()->AddCode(ToAddress(0x1500), entry1,
                                               0x200);
  symbolizer.instruction_stream_map()->AddCode(ToAddress(0x1700), entry2,
                                               0x100);
  symbolizer.instruction_stream_map()->AddCode(ToAddress(0x1900), entry3, 0x50);

  // We are building the following calls tree:
  //      -> aaa         - sample1
  //  aaa -> bbb -> ccc  - sample2
  //      -> ccc -> aaa  - sample3
  TickSample sample1;
  sample1.pc = ToPointer(0x1600);
  sample1.tos = ToPointer(0x1500);
  sample1.stack[0] = ToPointer(0x1510);
  sample1.frames_count = 1;
  Symbolizer::SymbolizedSample symbolized =
      symbolizer.SymbolizeTickSample(sample1);
  ProfileStackTrace& stack_trace = symbolized.stack_trace;
  CHECK_EQ(2, stack_trace.size());
  CHECK_EQ(entry1, stack_trace[0].code_entry);
  CHECK_EQ(entry1, stack_trace[1].code_entry);

  TickSample sample2;
  sample2.pc = ToPointer(0x1925);
  sample2.tos = ToPointer(0x1900);
  sample2.stack[0] = ToPointer(0x1780);
  sample2.stack[1] = ToPointer(0x10000);  // non-existent.
  sample2.stack[2] = ToPointer(0x1620);
  sample2.frames_count = 3;
  symbolized = symbolizer.SymbolizeTickSample(sample2);
  stack_trace = symbolized.stack_trace;
  CHECK_EQ(4, stack_trace.size());
  CHECK_EQ(entry3, stack_trace[0].code_entry);
  CHECK_EQ(entry2, stack_trace[1].code_entry);
  CHECK_EQ(nullptr, stack_trace[2].code_entry);
  CHECK_EQ(entry1, stack_trace[3].code_entry);

  TickSample sample3;
  sample3.pc = ToPointer(0x1510);
  sample3.tos = ToPointer(0x1500);
  sample3.stack[0] = ToPointer(0x1910);
  sample3.stack[1] = ToPointer(0x1610);
  sample3.frames_count = 2;
  symbolized = symbolizer.SymbolizeTickSample(sample3);
  stack_trace = symbolized.stack_trace;
  CHECK_EQ(3, stack_trace.size());
  CHECK_EQ(entry1, stack_trace[0].code_entry);
  CHECK_EQ(entry3, stack_trace[1].code_entry);
  CHECK_EQ(entry1, stack_trace[2].code_entry);
}

static void CheckNodeIds(const ProfileNode* node, unsigned* expectedId) {
  CHECK_EQ((*expectedId)++, node->id());
  for (const ProfileNode* child : *node->children()) {
    CheckNodeIds(child, expectedId);
  }
}

TEST(SampleIds) {
  TestSetup test_setup;
  i::Isolate* isolate = CcTest::i_isolate();
  CpuProfiler profiler(isolate);
  CpuProfilesCollection profiles(isolate);
  profiles.set_cpu_profiler(&profiler);
  ProfilerId id =
      profiles.StartProfiling("", {CpuProfilingMode::kLeafNodeLineNumbers}).id;
  CodeEntryStorage storage;
  InstructionStreamMap instruction_stream_map(storage);
  Symbolizer symbolizer(&instruction_stream_map);
  CodeEntry* entry1 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  CodeEntry* entry2 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "bbb");
  CodeEntry* entry3 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "ccc");
  symbolizer.instruction_stream_map()->AddCode(ToAddress(0x1500), entry1,
                                               0x200);
  symbolizer.instruction_stream_map()->AddCode(ToAddress(0x1700), entry2,
                                               0x100);
  symbolizer.instruction_stream_map()->AddCode(ToAddress(0x1900), entry3, 0x50);

  // We are building the following calls tree:
  //                    -> aaa #3           - sample1
  // (root)#1 -> aaa #2 -> bbb #4 -> ccc #5 - sample2
  //                    -> ccc #6 -> aaa #7 - sample3
  TickSample sample1;
  sample1.timestamp = v8::base::TimeTicks::Now();
  sample1.pc = ToPointer(0x1600);
  sample1.stack[0] = ToPointer(0x1510);
  sample1.frames_count = 1;
  auto symbolized = symbolizer.SymbolizeTickSample(sample1);
  profiles.AddPathToCurrentProfiles(
      sample1.timestamp, symbolized.stack_trace, symbolized.src_line, true,
      base::TimeDelta(), StateTag::JS, EmbedderStateTag::EMPTY, kNullAddress,
      kNullAddress, 1);

  TickSample sample2;
  sample2.timestamp = v8::base::TimeTicks::Now();
  sample2.pc = ToPointer(0x1925);
  sample2.stack[0] = ToPointer(0x1780);
  sample2.stack[1] = ToPointer(0x10000);  // non-existent.
  sample2.stack[2] = ToPointer(0x1620);
  sample2.frames_count = 3;
  symbolized = symbolizer.SymbolizeTickSample(sample2);
  profiles.AddPathToCurrentProfiles(
      sample2.timestamp, symbolized.stack_trace, symbolized.src_line, true,
      base::TimeDelta(), StateTag::JS, EmbedderStateTag::EMPTY, kNullAddress,
      kNullAddress, 2);

  TickSample sample3;
  sample3.timestamp = v8::base::TimeTicks::Now();
  sample3.pc = ToPointer(0x1510);
  sample3.stack[0] = ToPointer(0x1910);
  sample3.stack[1] = ToPointer(0x1610);
  sample3.frames_count = 2;
  symbolized = symbolizer.SymbolizeTickSample(sample3);
  profiles.AddPathToCurrentProfiles(
      sample3.timestamp, symbolized.stack_trace, symbolized.src_line, true,
      base::TimeDelta(), StateTag::JS, EmbedderStateTag::EMPTY, kNullAddress,
      kNullAddress, 3);

  CpuProfile* profile = profiles.StopProfiling(id);
  unsigned nodeId = 1;
  CheckNodeIds(profile->top_down()->root(), &nodeId);
  CHECK_EQ(7u, nodeId - 1);

  CHECK_EQ(3, profile->samples_count());
  unsigned expected_id[] = {3, 5, 7};
  const uint64_t expected_trace_id[] = {1, 2, 3};
  for (int i = 0; i < 3; i++) {
    CHECK_EQ(expected_id[i], profile->sample(i).node->id());
    CHECK_EQ(expected_trace_id[i], profile->sample(i).trace_id);
  }
}

TEST(SampleIds_StopProfilingByProfilerId) {
  TestSetup test_setup;
  i::Isolate* isolate = CcTest::i_isolate();
  CpuProfiler profiler(isolate);
  CpuProfilesCollection profiles(isolate);
  profiles.set_cpu_profiler(&profiler);
  CpuProfilingResult result =
      profiles.StartProfiling("", {CpuProfilingMode::kLeafNodeLineNumbers});
  CHECK_EQ(result.status, CpuProfilingStatus::kStarted);

  CpuProfile* profile = profiles.StopProfiling(result.id);
  CHECK_NE(profile, nullptr);
}

TEST(CpuProfilesCollectionDuplicateId) {
  CpuProfilesCollection collection(CcTest::i_isolate());
  CpuProfiler profiler(CcTest::i_isolate());
  collection.set_cpu_profiler(&profiler);

  auto profile_result = collection.StartProfiling();
  CHECK_EQ(CpuProfilingStatus::kStarted, profile_result.status);
  CHECK_EQ(CpuProfilingStatus::kAlreadyStarted,
           collection.StartProfilingForTesting(profile_result.id).status);

  collection.StopProfiling(profile_result.id);
}

TEST(CpuProfilesCollectionDuplicateTitle) {
  CpuProfilesCollection collection(CcTest::i_isolate());
  CpuProfiler profiler(CcTest::i_isolate());
  collection.set_cpu_profiler(&profiler);

  auto profile_result = collection.StartProfiling("duplicate");
  CHECK_EQ(CpuProfilingStatus::kStarted, profile_result.status);
  CHECK_EQ(CpuProfilingStatus::kAlreadyStarted,
           collection.StartProfiling("duplicate").status);

  collection.StopProfiling(profile_result.id);
}

namespace {
class DiscardedSamplesDelegateImpl : public v8::DiscardedSamplesDelegate {
 public:
  DiscardedSamplesDelegateImpl() : DiscardedSamplesDelegate() {}
  void Notify() override { CHECK_GT(GetId(), 0); }
};

class MockPlatform final : public TestPlatform {
 public:
  MockPlatform() : mock_task_runner_(new MockTaskRunner()) {}

  std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(
      v8::Isolate*, v8::TaskPriority priority) override {
    return mock_task_runner_;
  }

  int posted_count() { return mock_task_runner_->posted_count(); }

 private:
  class MockTaskRunner : public v8::TaskRunner {
   public:
    void PostTaskImpl(std::unique_ptr<v8::Task> task,
                      const SourceLocation&) override {
      task->Run();
      posted_count_++;
    }

    void PostDelayedTaskImpl(std::unique_ptr<Task> task,
                             double delay_in_seconds,
                             const SourceLocation&) override {
      task_ = std::move(task);
      delay_ = delay_in_seconds;
    }

    void PostIdleTaskImpl(std::unique_ptr<IdleTask> task,
                          const SourceLocation&) override {
      UNREACHABLE();
    }

    bool IdleTasksEnabled() override { return false; }
    bool NonNestableTasksEnabled() const override { return true; }
    bool NonNestableDelayedTasksEnabled() const override { return true; }

    int posted_count() { return posted_count_; }

   private:
    int posted_count_ = 0;
    double delay_ = -1;
    std::unique_ptr<Task> task_;
  };

  std::shared_ptr<MockTaskRunner> mock_task_runner_;
};
}  // namespace

TEST_WITH_PLATFORM(MaxSamplesCallback, MockPlatform) {
  i::Isolate* isolate = CcTest::i_isolate();
  CpuProfilesCollection profiles(isolate);
  CpuProfiler profiler(isolate);
  profiles.set_cpu_profiler(&profiler);
  std::unique_ptr<DiscardedSamplesDelegateImpl> impl =
      std::make_unique<DiscardedSamplesDelegateImpl>(
          DiscardedSamplesDelegateImpl());
  ProfilerId id =
      profiles
          .StartProfiling("",
                          {v8::CpuProfilingMode::kLeafNodeLineNumbers, 1, 1,
                           MaybeLocal<v8::Context>()},
                          std::move(impl))
          .id;

  CodeEntryStorage storage;
  InstructionStreamMap instruction_stream_map(storage);
  Symbolizer symbolizer(&instruction_stream_map);
  TickSample sample1;
  sample1.timestamp = v8::base::TimeTicks::Now();
  sample1.pc = ToPointer(0x1600);
  sample1.stack[0] = ToPointer(0x1510);
  sample1.frames_count = 1;
  auto symbolized = symbolizer.SymbolizeTickSample(sample1);
  profiles.AddPathToCurrentProfiles(
      sample1.timestamp, symbolized.stack_trace, symbolized.src_line, true,
      base::TimeDelta(), StateTag::JS, EmbedderStateTag::EMPTY);
  CHECK_EQ(0, platform.posted_count());
  TickSample sample2;
  sample2.timestamp = v8::base::TimeTicks::Now();
  sample2.pc = ToPointer(0x1925);
  sample2.stack[0] = ToPointer(0x1780);
  sample2.stack[1] = ToPointer(0x1760);
  sample2.frames_count = 2;
  symbolized = symbolizer.SymbolizeTickSample(sample2);
  profiles.AddPathToCurrentProfiles(
      sample2.timestamp, symbolized.stack_trace, symbolized.src_line, true,
      base::TimeDelta(), StateTag::JS, EmbedderStateTag::EMPTY);
  CHECK_EQ(1, platform.posted_count());
  TickSample sample3;
  sample3.timestamp = v8::base::TimeTicks::Now();
  sample3.pc = ToPointer(0x1510);
  sample3.stack[0] = ToPointer(0x1780);
  sample3.stack[1] = ToPointer(0x1760);
  sample3.stack[2] = ToPointer(0x1740);
  sample3.frames_count = 3;
  symbolized = symbolizer.SymbolizeTickSample(sample3);
  profiles.AddPathToCurrentProfiles(
      sample3.timestamp, symbolized.stack_trace, symbolized.src_line, true,
      base::TimeDelta(), StateTag::JS, EmbedderStateTag::EMPTY);
  CHECK_EQ(1, platform.posted_count());

  // Teardown
  profiles.StopProfiling(id);
}

TEST(NoSamples) {
  TestSetup test_setup;
  i::Isolate* isolate = CcTest::i_isolate();
  CpuProfiler profiler(isolate);
  CpuProfilesCollection profiles(isolate);
  profiles.set_cpu_profiler(&profiler);
  ProfilerId id = profiles.StartProfiling().id;
  CodeEntryStorage storage;
  InstructionStreamMap instruction_stream_map(storage);
  Symbolizer symbolizer(&instruction_stream_map);
  CodeEntry* entry1 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  symbolizer.instruction_stream_map()->AddCode(ToAddress(0x1500), entry1,
                                               0x200);

  // We are building the following calls tree:
  // (root)#1 -> aaa #2 -> aaa #3 - sample1
  TickSample sample1;
  sample1.pc = ToPointer(0x1600);
  sample1.stack[0] = ToPointer(0x1510);
  sample1.frames_count = 1;
  auto symbolized = symbolizer.SymbolizeTickSample(sample1);
  profiles.AddPathToCurrentProfiles(
      v8::base::TimeTicks::Now(), symbolized.stack_trace, symbolized.src_line,
      true, base::TimeDelta(), StateTag::JS, EmbedderStateTag::EMPTY);

  CpuProfile* profile = profiles.StopProfiling(id);
  unsigned nodeId = 1;
  CheckNodeIds(profile->top_down()->root(), &nodeId);
  CHECK_EQ(3u, nodeId - 1);

  CHECK_EQ(1, profile->samples_count());
}

static const ProfileNode* PickChild(const ProfileNode* parent,
                                    const char* name) {
  for (const ProfileNode* child : *parent->children()) {
    if (strcmp(child->entry()->name(), name) == 0) return child;
  }
  return nullptr;
}


TEST(RecordStackTraceAtStartProfiling) {
  // This test does not pass with inlining enabled since inlined functions
  // don't appear in the stack trace.
  i::v8_flags.turbo_inlining = false;

  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> env = CcTest::NewContext({PROFILER_EXTENSION_ID});
  v8::Context::Scope context_scope(env);
  std::unique_ptr<i::CpuProfiler> iprofiler(
      new i::CpuProfiler(CcTest::i_isolate()));
  i::ProfilerExtension::set_profiler(iprofiler.get());

  CompileRun(
      "function c() { startProfiling(); }\n"
      "function b() { c(); }\n"
      "function a() { b(); }\n"
      "a();\n"
      "stopProfiling();");
  CHECK_EQ(1, iprofiler->GetProfilesCount());
  CpuProfile* profile = iprofiler->GetProfile(0);
  const ProfileTree* topDown = profile->top_down();
  const ProfileNode* current = topDown->root();
  const_cast<ProfileNode*>(current)->Print(0);
  // The tree should look like this:
  //  (root)
  //   ""
  //     a
  //       b
  //         c
  // There can also be:
  //           startProfiling
  // if the sampler managed to get a tick.
  current = PickChild(current, "");
  CHECK(const_cast<ProfileNode*>(current));
  current = PickChild(current, "a");
  CHECK(const_cast<ProfileNode*>(current));
  current = PickChild(current, "b");
  CHECK(const_cast<ProfileNode*>(current));
  current = PickChild(current, "c");
  CHECK(const_cast<ProfileNode*>(current));
  CHECK(current->children()->empty() || current->children()->size() == 1);
  if (current->children()->size() == 1) {
    current = PickChild(current, "startProfiling");
    CHECK(current->children()->empty());
  }
}


TEST(Issue51919) {
  CpuProfilesCollection collection(CcTest::i_isolate());
  CpuProfiler profiler(CcTest::i_isolate());
  collection.set_cpu_profiler(&profiler);
  base::EmbeddedVector<char*, CpuProfilesCollection::kMaxSimultaneousProfiles>
      titles;
  for (int i = 0; i < CpuProfilesCollection::kMaxSimultaneousProfiles; ++i) {
    base::Vector<char> title = v8::base::Vector<char>::New(16);
    base::SNPrintF(title, "%d", i);
    CHECK_EQ(CpuProfilingStatus::kStarted,
             collection.StartProfiling(title.begin()).status);
    titles[i] = title.begin();
  }
  CHECK_EQ(CpuProfilingStatus::kErrorTooManyProfilers,
           collection.StartProfiling("maximum").status);
  for (int i = 0; i < CpuProfilesCollection::kMaxSimultaneousProfiles; ++i)
    i::DeleteArray(titles[i]);
}

static const v8::CpuProfileNode* PickChild(const v8::CpuProfileNode* parent,
                                           const char* name) {
  for (int i = 0; i < parent->GetChildrenCount(); ++i) {
    const v8::CpuProfileNode* child = parent->GetChild(i);
    v8::String::Utf8Value function_name(CcTest::isolate(),
                                        child->GetFunctionName());
    if (strcmp(*function_name, name) == 0) return child;
  }
  return nullptr;
}


TEST(ProfileNodeScriptId) {
  // This test does not pass with inlining enabled since inlined functions
  // don't appear in the stack trace.
  i::v8_flags.turbo_inlining = false;

  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> env = CcTest::NewContext({PROFILER_EXTENSION_ID});
  v8::Context::Scope context_scope(env);
  std::unique_ptr<CpuProfiler> iprofiler(new CpuProfiler(CcTest::i_isolate()));
  i::ProfilerExtension::set_profiler(iprofiler.get());

  v8::Local<v8::Script> script_a =
      v8_compile(v8_str("function a() { startProfiling(); }\n"));
  script_a->Run(env).ToLocalChecked();
  v8::Local<v8::Script> script_b =
      v8_compile(v8_str("function b() { a(); }\n"
                        "b();\n"
                        "stopProfiling();\n"));
  script_b->Run(env).ToLocalChecked();
  CHECK_EQ(1, iprofiler->GetProfilesCount());
  const v8::CpuProfile* profile = i::ProfilerExtension::last_profile;
  const v8::CpuProfileNode* current = profile->GetTopDownRoot();
  reinterpret_cast<ProfileNode*>(
      const_cast<v8::CpuProfileNode*>(current))->Print(0);
  // The tree should look like this:
  //  (root)
  //   ""
  //     b
  //       a
  // There can also be:
  //         startProfiling
  // if the sampler managed to get a tick.
  current = PickChild(current, "");
  CHECK(const_cast<v8::CpuProfileNode*>(current));

  current = PickChild(current, "b");
  CHECK(const_cast<v8::CpuProfileNode*>(current));
  CHECK_EQ(script_b->GetUnboundScript()->GetId(), current->GetScriptId());

  current = PickChild(current, "a");
  CHECK(const_cast<v8::CpuProfileNode*>(current));
  CHECK_EQ(script_a->GetUnboundScript()->GetId(), current->GetScriptId());
}

static const char* line_number_test_source_existing_functions =
"function foo_at_the_first_line() {\n"
"}\n"
"foo_at_the_first_line();\n"
"function lazy_func_at_forth_line() {}\n";

static const char* line_number_test_source_profile_time_functions =
"// Empty first line\n"
"function bar_at_the_second_line() {\n"
"  foo_at_the_first_line();\n"
"}\n"
"bar_at_the_second_line();\n"
"function lazy_func_at_6th_line() {}";

int GetFunctionLineNumber(CpuProfiler* profiler, LocalContext* env,
                          i::Isolate* isolate, const char* name) {
  InstructionStreamMap* instruction_stream_map =
      profiler->symbolizer()->instruction_stream_map();
  i::DirectHandle<i::JSFunction> func = i::Cast<i::JSFunction>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Function>::Cast(
          (*env)->Global()->Get(env->local(), v8_str(name)).ToLocalChecked())));
  PtrComprCageBase cage_base(isolate);
  CodeEntry* func_entry = instruction_stream_map->FindEntry(
      func->abstract_code(isolate)->InstructionStart(cage_base));
  if (!func_entry) FATAL("%s", name);
  return func_entry->line_number();
}

TEST(LineNumber) {
  CcTest::InitializeVM();
  LocalContext env;
  i::Isolate* isolate = CcTest::i_isolate();
  TestSetup test_setup;

  i::HandleScope scope(isolate);

  CompileRun(line_number_test_source_existing_functions);

  CpuProfiler profiler(isolate);
  profiler.StartProfiling("LineNumber");

  CompileRun(line_number_test_source_profile_time_functions);

  profiler.processor()->StopSynchronously();

  bool is_lazy = i::v8_flags.lazy;
  CHECK_EQ(1, GetFunctionLineNumber(&profiler, &env, isolate,
                                    "foo_at_the_first_line"));
  CHECK_EQ(is_lazy ? 0 : 4, GetFunctionLineNumber(&profiler, &env, isolate,
                                                  "lazy_func_at_forth_line"));
  CHECK_EQ(2, GetFunctionLineNumber(&profiler, &env, isolate,
                                    "bar_at_the_second_line"));
  CHECK_EQ(is_lazy ? 0 : 6, GetFunctionLineNumber(&profiler, &env, isolate,
                                                  "lazy_func_at_6th_line"));

  profiler.StopProfiling("LineNumber");
}

TEST(BailoutReason) {
#if !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
  i::v8_flags.allow_natives_syntax = true;
  i::v8_flags.always_turbofan = false;
  i::v8_flags.turbofan = true;
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> env = CcTest::NewContext({PROFILER_EXTENSION_ID});
  v8::Context::Scope context_scope(env);
  std::unique_ptr<CpuProfiler> iprofiler(new CpuProfiler(CcTest::i_isolate()));
  i::ProfilerExtension::set_profiler(iprofiler.get());

  CHECK_EQ(0, iprofiler->GetProfilesCount());
  v8::Local<v8::Function> function = CompileRun(
                                         "function Debugger() {\n"
                                         "  startProfiling();\n"
                                         "}"
                                         "Debugger")
                                         .As<v8::Function>();
  i::Handle<i::JSFunction> i_function =
      i::Cast<i::JSFunction>(v8::Utils::OpenHandle(*function));
  USE(i_function);

  CompileRun(
      "%PrepareFunctionForOptimization(Debugger);"
      "%OptimizeFunctionOnNextCall(Debugger);"
      "%NeverOptimizeFunction(Debugger);"
      "Debugger();"
      "stopProfiling()");
  CHECK_EQ(1, iprofiler->GetProfilesCount());
  const v8::CpuProfile* profile = i::ProfilerExtension::last_profile;
  CHECK(profile);
  const v8::CpuProfileNode* current = profile->GetTopDownRoot();
  reinterpret_cast<ProfileNode*>(
      const_cast<v8::CpuProfileNode*>(current))->Print(0);
  // The tree should look like this:
  //  (root)
  //   ""
  //     kOptimizationDisabledForTest
  current = PickChild(current, "");
  CHECK(const_cast<v8::CpuProfileNode*>(current));

  current = PickChild(current, "Debugger");
  CHECK(const_cast<v8::CpuProfileNode*>(current));
  CHECK(
      !strcmp("Optimization is always disabled", current->GetBailoutReason()));
#endif  // !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
}

TEST(NodeSourceTypes) {
  ProfileTree tree(CcTest::i_isolate());
  CodeEntry function_entry(LogEventListener::CodeTag::kFunction, "function");
  tree.AddPathFromEnd({&function_entry});
  CodeEntry builtin_entry(LogEventListener::CodeTag::kBuiltin, "builtin");
  tree.AddPathFromEnd({&builtin_entry});
  CodeEntry callback_entry(LogEventListener::CodeTag::kCallback, "callback");
  tree.AddPathFromEnd({&callback_entry});
  CodeEntry regex_entry(LogEventListener::CodeTag::kRegExp, "regex");
  tree.AddPathFromEnd({&regex_entry});
  CodeEntry stub_entry(LogEventListener::CodeTag::kStub, "stub");
  tree.AddPathFromEnd({&stub_entry});

  tree.AddPathFromEnd({CodeEntry::gc_entry()});
  tree.AddPathFromEnd({CodeEntry::idle_entry()});
  tree.AddPathFromEnd({CodeEntry::program_entry()});
  tree.AddPathFromEnd({CodeEntry::unresolved_entry()});

  auto* root = tree.root();
  CHECK(root);
  CHECK_EQ(root->source_type(), v8::CpuProfileNode::kInternal);

  auto* function_node = PickChild(root, "function");
  CHECK(function_node);
  CHECK_EQ(function_node->source_type(), v8::CpuProfileNode::kScript);

  auto* builtin_node = PickChild(root, "builtin");
  CHECK(builtin_node);
  CHECK_EQ(builtin_node->source_type(), v8::CpuProfileNode::kBuiltin);

  auto* callback_node = PickChild(root, "callback");
  CHECK(callback_node);
  CHECK_EQ(callback_node->source_type(), v8::CpuProfileNode::kCallback);

  auto* regex_node = PickChild(root, "regex");
  CHECK(regex_node);
  CHECK_EQ(regex_node->source_type(), v8::CpuProfileNode::kInternal);

  auto* stub_node = PickChild(root, "stub");
  CHECK(stub_node);
  CHECK_EQ(stub_node->source_type(), v8::CpuProfileNode::kInternal);

  auto* gc_node = PickChild(root, "(garbage collector)");
  CHECK(gc_node);
  CHECK_EQ(gc_node->source_type(), v8::CpuProfileNode::kInternal);

  auto* idle_node = PickChild(root, "(idle)");
  CHECK(idle_node);
  CHECK_EQ(idle_node->source_type(), v8::CpuProfileNode::kInternal);

  auto* program_node = PickChild(root, "(program)");
  CHECK(program_node);
  CHECK_EQ(program_node->source_type(), v8::CpuProfileNode::kInternal);

  auto* unresolved_node = PickChild(root, "(unresolved function)");
  CHECK(unresolved_node);
  CHECK_EQ(unresolved_node->source_type(), v8::CpuProfileNode::kUnresolved);
}

TEST(CodeMapRemoveCode) {
  CodeEntryStorage storage;
  InstructionStreamMap instruction_stream_map(storage);

  CodeEntry* entry =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  instruction_stream_map.AddCode(ToAddress(0x1000), entry, 0x100);
  CHECK(instruction_stream_map.RemoveCode(entry));
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1000)));

  // Test that when two entries share the same address, we remove only the
  // entry that we desired to.
  CodeEntry* colliding_entry1 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  CodeEntry* colliding_entry2 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  instruction_stream_map.AddCode(ToAddress(0x1000), colliding_entry1, 0x100);
  instruction_stream_map.AddCode(ToAddress(0x1000), colliding_entry2, 0x100);

  CHECK(instruction_stream_map.RemoveCode(colliding_entry1));
  CHECK_EQ(instruction_stream_map.FindEntry(ToAddress(0x1000)),
           colliding_entry2);

  CHECK(instruction_stream_map.RemoveCode(colliding_entry2));
  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1000)));
}

TEST(CodeMapMoveOverlappingCode) {
  CodeEntryStorage storage;
  InstructionStreamMap instruction_stream_map(storage);
  CodeEntry* colliding_entry1 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "aaa");
  CodeEntry* colliding_entry2 =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "bbb");
  CodeEntry* after_entry =
      storage.Create(i::LogEventListener::CodeTag::kFunction, "ccc");

  instruction_stream_map.AddCode(ToAddress(0x1400), colliding_entry1, 0x200);
  instruction_stream_map.AddCode(ToAddress(0x1400), colliding_entry2, 0x200);
  instruction_stream_map.AddCode(ToAddress(0x1800), after_entry, 0x200);

  CHECK_EQ(colliding_entry1->instruction_start(), ToAddress(0x1400));
  CHECK_EQ(colliding_entry2->instruction_start(), ToAddress(0x1400));
  CHECK_EQ(after_entry->instruction_start(), ToAddress(0x1800));

  CHECK(instruction_stream_map.FindEntry(ToAddress(0x1400)));
  CHECK_EQ(instruction_stream_map.FindEntry(ToAddress(0x1800)), after_entry);

  instruction_stream_map.MoveCode(ToAddress(0x1400), ToAddress(0x1600));

  CHECK(!instruction_stream_map.FindEntry(ToAddress(0x1400)));
  CHECK(instruction_stream_map.FindEntry(ToAddress(0x1600)));
  CHECK_EQ(instruction_stream_map.FindEntry(ToAddress(0x1800)), after_entry);

  CHECK_EQ(colliding_entry1->instruction_start(), ToAddress(0x1600));
  CHECK_EQ(colliding_entry2->instruction_start(), ToAddress(0x1600));
  CHECK_EQ(after_entry->instruction_start(), ToAddress(0x1800));
}

}  // namespace test_profile_generator
}  // namespace internal
}  // namespace v8
