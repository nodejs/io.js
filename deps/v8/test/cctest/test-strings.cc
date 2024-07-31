// Copyright 2012 the V8 project authors. All rights reserved.
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

// Check that we can traverse very deep stacks of ConsStrings using
// StringCharacterStram.  Check that Get(int) works on very deep stacks
// of ConsStrings.  These operations may not be very fast, but they
// should be possible without getting errors due to too deep recursion.

#include <stdlib.h>

#include "include/v8-json.h"
#include "include/v8-template.h"
#include "src/api/api-inl.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/strings.h"
#include "src/execution/messages.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

// Adapted from http://en.wikipedia.org/wiki/Multiply-with-carry
class MyRandomNumberGenerator {
 public:
  MyRandomNumberGenerator() { init(); }

  void init(uint32_t seed = 0x5688C73E) {
    static const uint32_t phi = 0x9E3779B9;
    c = 362436;
    i = kQSize - 1;
    Q[0] = seed;
    Q[1] = seed + phi;
    Q[2] = seed + phi + phi;
    for (unsigned j = 3; j < kQSize; j++) {
      Q[j] = Q[j - 3] ^ Q[j - 2] ^ phi ^ j;
    }
  }

  uint32_t next() {
    uint64_t a = 18782;
    uint32_t r = 0xFFFFFFFE;
    i = (i + 1) & (kQSize - 1);
    uint64_t t = a * Q[i] + c;
    c = (t >> 32);
    uint32_t x = static_cast<uint32_t>(t + c);
    if (x < c) {
      x++;
      c++;
    }
    return (Q[i] = r - x);
  }

  uint32_t next(int max) { return next() % max; }

  bool next(double threshold) {
    CHECK(threshold >= 0.0 && threshold <= 1.0);
    if (threshold == 1.0) return true;
    if (threshold == 0.0) return false;
    uint32_t value = next() % 100000;
    return threshold > static_cast<double>(value) / 100000.0;
  }

 private:
  static const uint32_t kQSize = 4096;
  uint32_t Q[kQSize];
  uint32_t c;
  uint32_t i;
};

namespace v8 {
namespace internal {
namespace test_strings {

static const int DEEP_DEPTH = 8 * 1024;
static const int SUPER_DEEP_DEPTH = 80 * 1024;

class Resource : public v8::String::ExternalStringResource {
 public:
  Resource(const base::uc16* data, size_t length)
      : data_(data), length_(length) {}
  ~Resource() override { i::DeleteArray(data_); }
  const uint16_t* data() const override { return data_; }
  size_t length() const override { return length_; }

 private:
  const base::uc16* data_;
  size_t length_;
};

class OneByteResource : public v8::String::ExternalOneByteStringResource {
 public:
  OneByteResource(const char* data, size_t length)
      : data_(data), length_(length) {}
  ~OneByteResource() override { i::DeleteArray(data_); }
  const char* data() const override { return data_; }
  size_t length() const override { return length_; }

 private:
  const char* data_;
  size_t length_;
};

static void InitializeBuildingBlocks(Handle<String>* building_blocks,
                                     int bb_length, bool long_blocks,
                                     MyRandomNumberGenerator* rng) {
  // A list of pointers that we don't have any interest in cleaning up.
  // If they are reachable from a root then leak detection won't complain.
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  for (int i = 0; i < bb_length; i++) {
    int len = rng->next(16);
    int slice_head_chars = 0;
    int slice_tail_chars = 0;
    int slice_depth = 0;
    for (int j = 0; j < 3; j++) {
      if (rng->next(0.35)) slice_depth++;
    }
    // Must truncate something for a slice string. Loop until
    // at least one end will be sliced.
    while (slice_head_chars == 0 && slice_tail_chars == 0) {
      slice_head_chars = rng->next(15);
      slice_tail_chars = rng->next(12);
    }
    if (long_blocks) {
      // Generate building blocks which will never be merged
      len += ConsString::kMinLength + 1;
    } else if (len > 14) {
      len += 1234;
    }
    // Don't slice 0 length strings.
    if (len == 0) slice_depth = 0;
    int slice_length = slice_depth * (slice_head_chars + slice_tail_chars);
    len += slice_length;
    switch (rng->next(4)) {
      case 0: {
        base::uc16 buf[2000];
        for (int j = 0; j < len; j++) {
          buf[j] = rng->next(0x10000);
        }
        building_blocks[i] =
            factory
                ->NewStringFromTwoByte(
                    v8::base::Vector<const base::uc16>(buf, len))
                .ToHandleChecked();
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
      case 1: {
        char buf[2000];
        for (int j = 0; j < len; j++) {
          buf[j] = rng->next(0x80);
        }
        building_blocks[i] =
            factory->NewStringFromOneByte(v8::base::OneByteVector(buf, len))
                .ToHandleChecked();
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
      case 2: {
        base::uc16* buf = NewArray<base::uc16>(len);
        for (int j = 0; j < len; j++) {
          buf[j] = rng->next(0x10000);
        }
        Resource* resource = new Resource(buf, len);
        building_blocks[i] = v8::Utils::OpenHandle(
            *v8::String::NewExternalTwoByte(CcTest::isolate(), resource)
                 .ToLocalChecked());
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
      case 3: {
        char* buf = NewArray<char>(len);
        for (int j = 0; j < len; j++) {
          buf[j] = rng->next(0x80);
        }
        OneByteResource* resource = new OneByteResource(buf, len);
        building_blocks[i] = v8::Utils::OpenHandle(
            *v8::String::NewExternalOneByte(CcTest::isolate(), resource)
                 .ToLocalChecked());
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
    }
    for (int j = slice_depth; j > 0; j--) {
      building_blocks[i] = factory->NewSubString(
          building_blocks[i], slice_head_chars,
          building_blocks[i]->length() - slice_tail_chars);
    }
    CHECK(len == building_blocks[i]->length() + slice_length);
  }
}

class ConsStringStats {
 public:
  ConsStringStats() { Reset(); }
  ConsStringStats(const ConsStringStats&) = delete;
  ConsStringStats& operator=(const ConsStringStats&) = delete;
  void Reset();
  void VerifyEqual(const ConsStringStats& that) const;
  int leaves_;
  int empty_leaves_;
  int chars_;
  int left_traversals_;
  int right_traversals_;

 private:
};

void ConsStringStats::Reset() {
  leaves_ = 0;
  empty_leaves_ = 0;
  chars_ = 0;
  left_traversals_ = 0;
  right_traversals_ = 0;
}

void ConsStringStats::VerifyEqual(const ConsStringStats& that) const {
  CHECK_EQ(this->leaves_, that.leaves_);
  CHECK_EQ(this->empty_leaves_, that.empty_leaves_);
  CHECK_EQ(this->chars_, that.chars_);
  CHECK_EQ(this->left_traversals_, that.left_traversals_);
  CHECK_EQ(this->right_traversals_, that.right_traversals_);
}

class ConsStringGenerationData {
 public:
  static const int kNumberOfBuildingBlocks = 256;
  explicit ConsStringGenerationData(bool long_blocks);
  ConsStringGenerationData(const ConsStringGenerationData&) = delete;
  ConsStringGenerationData& operator=(const ConsStringGenerationData&) = delete;
  void Reset();
  inline Handle<String> block(int offset);
  inline Handle<String> block(uint32_t offset);
  // Input variables.
  double early_termination_threshold_;
  double leftness_;
  double rightness_;
  double empty_leaf_threshold_;
  int max_leaves_;
  // Cached data.
  Handle<String> building_blocks_[kNumberOfBuildingBlocks];
  Tagged<String> empty_string_;
  MyRandomNumberGenerator rng_;
  // Stats.
  ConsStringStats stats_;
  int early_terminations_;
};

ConsStringGenerationData::ConsStringGenerationData(bool long_blocks) {
  rng_.init();
  InitializeBuildingBlocks(building_blocks_, kNumberOfBuildingBlocks,
                           long_blocks, &rng_);
  empty_string_ = ReadOnlyRoots(CcTest::heap()).empty_string();
  Reset();
}

Handle<String> ConsStringGenerationData::block(uint32_t offset) {
  return building_blocks_[offset % kNumberOfBuildingBlocks];
}

Handle<String> ConsStringGenerationData::block(int offset) {
  CHECK_GE(offset, 0);
  return building_blocks_[offset % kNumberOfBuildingBlocks];
}

void ConsStringGenerationData::Reset() {
  early_termination_threshold_ = 0.01;
  leftness_ = 0.75;
  rightness_ = 0.75;
  empty_leaf_threshold_ = 0.02;
  max_leaves_ = 1000;
  stats_.Reset();
  early_terminations_ = 0;
  rng_.init();
}

void AccumulateStats(Tagged<ConsString> cons_string, ConsStringStats* stats) {
  int left_length = cons_string->first()->length();
  int right_length = cons_string->second()->length();
  CHECK(cons_string->length() == left_length + right_length);
  // Check left side.
  bool left_is_cons = IsConsString(cons_string->first());
  if (left_is_cons) {
    stats->left_traversals_++;
    AccumulateStats(ConsString::cast(cons_string->first()), stats);
  } else {
    CHECK_NE(left_length, 0);
    stats->leaves_++;
    stats->chars_ += left_length;
  }
  // Check right side.
  if (IsConsString(cons_string->second())) {
    stats->right_traversals_++;
    AccumulateStats(ConsString::cast(cons_string->second()), stats);
  } else {
    if (right_length == 0) {
      stats->empty_leaves_++;
      CHECK(!left_is_cons);
    }
    stats->leaves_++;
    stats->chars_ += right_length;
  }
}

void AccumulateStats(Handle<String> cons_string, ConsStringStats* stats) {
  DisallowGarbageCollection no_gc;
  if (IsConsString(*cons_string)) {
    return AccumulateStats(ConsString::cast(*cons_string), stats);
  }
  // This string got flattened by gc.
  stats->chars_ += cons_string->length();
}

void AccumulateStatsWithOperator(Tagged<ConsString> cons_string,
                                 ConsStringStats* stats) {
  ConsStringIterator iter(cons_string);
  int offset;
  for (Tagged<String> string = iter.Next(&offset); !string.is_null();
       string = iter.Next(&offset)) {
    // Accumulate stats.
    CHECK_EQ(0, offset);
    stats->leaves_++;
    stats->chars_ += string->length();
  }
}

void VerifyConsString(Handle<String> root, ConsStringGenerationData* data) {
  // Verify basic data.
  CHECK(IsConsString(*root));
  CHECK_EQ(root->length(), data->stats_.chars_);
  // Recursive verify.
  ConsStringStats stats;
  AccumulateStats(ConsString::cast(*root), &stats);
  stats.VerifyEqual(data->stats_);
  // Iteratively verify.
  stats.Reset();
  AccumulateStatsWithOperator(ConsString::cast(*root), &stats);
  // Don't see these. Must copy over.
  stats.empty_leaves_ = data->stats_.empty_leaves_;
  stats.left_traversals_ = data->stats_.left_traversals_;
  stats.right_traversals_ = data->stats_.right_traversals_;
  // Adjust total leaves to compensate.
  stats.leaves_ += stats.empty_leaves_;
  stats.VerifyEqual(data->stats_);
}

static Handle<String> ConstructRandomString(ConsStringGenerationData* data,
                                            unsigned max_recursion) {
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  // Compute termination characteristics.
  bool terminate = false;
  bool flat = data->rng_.next(data->empty_leaf_threshold_);
  bool terminate_early = data->rng_.next(data->early_termination_threshold_);
  if (terminate_early) data->early_terminations_++;
  // The obvious condition.
  terminate |= max_recursion == 0;
  // Flat cons string terminate by definition.
  terminate |= flat;
  // Cap for max leaves.
  terminate |= data->stats_.leaves_ >= data->max_leaves_;
  // Roll the dice.
  terminate |= terminate_early;
  // Compute termination characteristics for each side.
  bool terminate_left = terminate || !data->rng_.next(data->leftness_);
  bool terminate_right = terminate || !data->rng_.next(data->rightness_);
  // Generate left string.
  Handle<String> left;
  if (terminate_left) {
    left = data->block(data->rng_.next());
    data->stats_.leaves_++;
    data->stats_.chars_ += left->length();
  } else {
    data->stats_.left_traversals_++;
  }
  // Generate right string.
  Handle<String> right;
  if (terminate_right) {
    right = data->block(data->rng_.next());
    data->stats_.leaves_++;
    data->stats_.chars_ += right->length();
  } else {
    data->stats_.right_traversals_++;
  }
  // Generate the necessary sub-nodes recursively.
  if (!terminate_right) {
    // Need to balance generation fairly.
    if (!terminate_left && data->rng_.next(0.5)) {
      left = ConstructRandomString(data, max_recursion - 1);
    }
    right = ConstructRandomString(data, max_recursion - 1);
  }
  if (!terminate_left && left.is_null()) {
    left = ConstructRandomString(data, max_recursion - 1);
  }
  // Build the cons string.
  Handle<String> root = factory->NewConsString(left, right).ToHandleChecked();
  CHECK(IsConsString(*root) && !root->IsFlat());
  // Special work needed for flat string.
  if (flat) {
    data->stats_.empty_leaves_++;
    String::Flatten(isolate, root);
    CHECK(IsConsString(*root) && root->IsFlat());
  }
  return root;
}

static Handle<String> ConstructLeft(ConsStringGenerationData* data, int depth) {
  Factory* factory = CcTest::i_isolate()->factory();
  Handle<String> answer = factory->NewStringFromStaticChars("");
  data->stats_.leaves_++;
  for (int i = 0; i < depth; i++) {
    Handle<String> block = data->block(i);
    Handle<String> next =
        factory->NewConsString(answer, block).ToHandleChecked();
    if (IsConsString(*next)) data->stats_.leaves_++;
    data->stats_.chars_ += block->length();
    answer = next;
  }
  data->stats_.left_traversals_ = data->stats_.leaves_ - 2;
  return answer;
}

static Handle<String> ConstructRight(ConsStringGenerationData* data,
                                     int depth) {
  Factory* factory = CcTest::i_isolate()->factory();
  Handle<String> answer = factory->NewStringFromStaticChars("");
  data->stats_.leaves_++;
  for (int i = depth - 1; i >= 0; i--) {
    Handle<String> block = data->block(i);
    Handle<String> next =
        factory->NewConsString(block, answer).ToHandleChecked();
    if (IsConsString(*next)) data->stats_.leaves_++;
    data->stats_.chars_ += block->length();
    answer = next;
  }
  data->stats_.right_traversals_ = data->stats_.leaves_ - 2;
  return answer;
}

static Handle<String> ConstructBalancedHelper(ConsStringGenerationData* data,
                                              int from, int to) {
  Factory* factory = CcTest::i_isolate()->factory();
  CHECK(to > from);
  if (to - from == 1) {
    data->stats_.chars_ += data->block(from)->length();
    return data->block(from);
  }
  if (to - from == 2) {
    data->stats_.chars_ += data->block(from)->length();
    data->stats_.chars_ += data->block(from + 1)->length();
    return factory->NewConsString(data->block(from), data->block(from + 1))
        .ToHandleChecked();
  }
  Handle<String> part1 =
      ConstructBalancedHelper(data, from, from + ((to - from) / 2));
  Handle<String> part2 =
      ConstructBalancedHelper(data, from + ((to - from) / 2), to);
  if (IsConsString(*part1)) data->stats_.left_traversals_++;
  if (IsConsString(*part2)) data->stats_.right_traversals_++;
  return factory->NewConsString(part1, part2).ToHandleChecked();
}

static Handle<String> ConstructBalanced(ConsStringGenerationData* data,
                                        int depth = DEEP_DEPTH) {
  Handle<String> string = ConstructBalancedHelper(data, 0, depth);
  data->stats_.leaves_ =
      data->stats_.left_traversals_ + data->stats_.right_traversals_ + 2;
  return string;
}

static void Traverse(Handle<String> s1, Handle<String> s2) {
  int i = 0;
  StringCharacterStream character_stream_1(*s1);
  StringCharacterStream character_stream_2(*s2);
  while (character_stream_1.HasMore()) {
    CHECK(character_stream_2.HasMore());
    uint16_t c = character_stream_1.GetNext();
    CHECK_EQ(c, character_stream_2.GetNext());
    i++;
  }
  CHECK(!character_stream_1.HasMore());
  CHECK(!character_stream_2.HasMore());
  CHECK_EQ(s1->length(), i);
  CHECK_EQ(s2->length(), i);
}

static void TraverseFirst(Handle<String> s1, Handle<String> s2, int chars) {
  int i = 0;
  StringCharacterStream character_stream_1(*s1);
  StringCharacterStream character_stream_2(*s2);
  while (character_stream_1.HasMore() && i < chars) {
    CHECK(character_stream_2.HasMore());
    uint16_t c = character_stream_1.GetNext();
    CHECK_EQ(c, character_stream_2.GetNext());
    i++;
  }
  s1->Get(s1->length() - 1);
  s2->Get(s2->length() - 1);
}

TEST(Traverse) {
  printf("TestTraverse\n");
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());
  ConsStringGenerationData data(false);
  Handle<String> flat = ConstructBalanced(&data);
  String::Flatten(isolate, flat);
  Handle<String> left_asymmetric = ConstructLeft(&data, DEEP_DEPTH);
  Handle<String> right_asymmetric = ConstructRight(&data, DEEP_DEPTH);
  Handle<String> symmetric = ConstructBalanced(&data);
  printf("1\n");
  Traverse(flat, symmetric);
  printf("2\n");
  Traverse(flat, left_asymmetric);
  printf("3\n");
  Traverse(flat, right_asymmetric);
  printf("4\n");
  Handle<String> left_deep_asymmetric = ConstructLeft(&data, SUPER_DEEP_DEPTH);
  Handle<String> right_deep_asymmetric =
      ConstructRight(&data, SUPER_DEEP_DEPTH);
  printf("5\n");
  TraverseFirst(left_asymmetric, left_deep_asymmetric, 1050);
  printf("6\n");
  TraverseFirst(left_asymmetric, right_deep_asymmetric, 65536);
  printf("7\n");
  String::Flatten(isolate, left_asymmetric);
  printf("10\n");
  Traverse(flat, left_asymmetric);
  printf("11\n");
  String::Flatten(isolate, right_asymmetric);
  printf("12\n");
  Traverse(flat, right_asymmetric);
  printf("14\n");
  String::Flatten(isolate, symmetric);
  printf("15\n");
  Traverse(flat, symmetric);
  printf("16\n");
  String::Flatten(isolate, left_deep_asymmetric);
  printf("18\n");
}

TEST(ConsStringWithEmptyFirstFlatten) {
  printf("ConsStringWithEmptyFirstFlatten\n");
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  i::Handle<i::String> initial_fst =
      isolate->factory()->NewStringFromAsciiChecked("fst012345");
  i::Handle<i::String> initial_snd =
      isolate->factory()->NewStringFromAsciiChecked("snd012345");
  i::Handle<i::String> str = isolate->factory()
                                 ->NewConsString(initial_fst, initial_snd)
                                 .ToHandleChecked();
  CHECK(IsConsString(*str));
  auto cons = i::Handle<i::ConsString>::cast(str);

  const int initial_length = cons->length();

  // set_first / set_second does not update the length (which the heap verifier
  // checks), so we need to ensure the length stays the same.

  i::Handle<i::String> new_fst = isolate->factory()->empty_string();
  i::Handle<i::String> new_snd =
      isolate->factory()->NewStringFromAsciiChecked("snd012345012345678");
  cons->set_first(*new_fst);
  cons->set_second(*new_snd);
  CHECK(!cons->IsFlat());
  CHECK_EQ(initial_length, new_fst->length() + new_snd->length());
  CHECK_EQ(initial_length, cons->length());

  // Make sure Flatten doesn't alloc a new string.
  DisallowGarbageCollection no_alloc;
  i::Handle<i::String> flat = i::String::Flatten(isolate, cons);
  CHECK(flat->IsFlat());
  CHECK_EQ(initial_length, flat->length());
}

static void VerifyCharacterStream(Tagged<String> flat_string,
                                  Tagged<String> cons_string) {
  // Do not want to test ConString traversal on flat string.
  CHECK(flat_string->IsFlat() && !IsConsString(flat_string));
  CHECK(IsConsString(cons_string));
  // TODO(dcarney) Test stream reset as well.
  int length = flat_string->length();
  // Iterate start search in multiple places in the string.
  int outer_iterations = length > 20 ? 20 : length;
  for (int j = 0; j <= outer_iterations; j++) {
    int offset = length * j / outer_iterations;
    if (offset < 0) offset = 0;
    // Want to test the offset == length case.
    if (offset > length) offset = length;
    StringCharacterStream flat_stream(flat_string, offset);
    StringCharacterStream cons_stream(cons_string, offset);
    for (int i = offset; i < length; i++) {
      uint16_t c = flat_string->Get(i);
      CHECK(flat_stream.HasMore());
      CHECK(cons_stream.HasMore());
      CHECK_EQ(c, flat_stream.GetNext());
      CHECK_EQ(c, cons_stream.GetNext());
    }
    CHECK(!flat_stream.HasMore());
    CHECK(!cons_stream.HasMore());
  }
}

static inline void PrintStats(const ConsStringGenerationData& data) {
#ifdef DEBUG
  printf("%s: [%u], %s: [%u], %s: [%u], %s: [%u], %s: [%u], %s: [%u]\n",
         "leaves", data.stats_.leaves_, "empty", data.stats_.empty_leaves_,
         "chars", data.stats_.chars_, "lefts", data.stats_.left_traversals_,
         "rights", data.stats_.right_traversals_, "early_terminations",
         data.early_terminations_);
#endif
}

template <typename BuildString>
void TestStringCharacterStream(BuildString build, int test_cases) {
  v8_flags.gc_global = true;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope outer_scope(isolate);
  ConsStringGenerationData data(true);
  for (int i = 0; i < test_cases; i++) {
    printf("%d\n", i);
    HandleScope inner_scope(isolate);
    AlwaysAllocateScopeForTesting always_allocate(isolate->heap());
    // Build flat version of cons string.
    Handle<String> flat_string = build(i, &data);
    ConsStringStats flat_string_stats;
    AccumulateStats(flat_string, &flat_string_stats);
    // Flatten string.
    String::Flatten(isolate, flat_string);
    // Build unflattened version of cons string to test.
    Handle<String> cons_string = build(i, &data);
    ConsStringStats cons_string_stats;
    AccumulateStats(cons_string, &cons_string_stats);
    DisallowGarbageCollection no_gc;
    PrintStats(data);
    // Full verify of cons string.
    cons_string_stats.VerifyEqual(flat_string_stats);
    cons_string_stats.VerifyEqual(data.stats_);
    VerifyConsString(cons_string, &data);
    // TODO(leszeks): Remove Tagged cast when .first() returns a Tagged.
    static_assert(kTaggedCanConvertToRawObjects);
    Tagged<String> flat_string_ptr =
        IsConsString(*flat_string)
            ? Tagged(Tagged<ConsString>::cast(*flat_string)->first())
            : *flat_string;
    VerifyCharacterStream(flat_string_ptr, *cons_string);
  }
}

static const int kCharacterStreamNonRandomCases = 8;

static Handle<String> BuildEdgeCaseConsString(int test_case,
                                              ConsStringGenerationData* data) {
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  data->Reset();
  switch (test_case) {
    case 0:
      return ConstructBalanced(data, 71);
    case 1:
      return ConstructLeft(data, 71);
    case 2:
      return ConstructRight(data, 71);
    case 3:
      return ConstructLeft(data, 10);
    case 4:
      return ConstructRight(data, 10);
    case 5:
      // 2 element balanced tree.
      data->stats_.chars_ += data->block(0)->length();
      data->stats_.chars_ += data->block(1)->length();
      data->stats_.leaves_ += 2;
      return factory->NewConsString(data->block(0), data->block(1))
          .ToHandleChecked();
    case 6:
      // Simple flattened tree.
      data->stats_.chars_ += data->block(0)->length();
      data->stats_.chars_ += data->block(1)->length();
      data->stats_.leaves_ += 2;
      data->stats_.empty_leaves_ += 1;
      {
        Handle<String> string =
            factory->NewConsString(data->block(0), data->block(1))
                .ToHandleChecked();
        String::Flatten(isolate, string);
        return string;
      }
    case 7:
      // Left node flattened.
      data->stats_.chars_ += data->block(0)->length();
      data->stats_.chars_ += data->block(1)->length();
      data->stats_.chars_ += data->block(2)->length();
      data->stats_.leaves_ += 3;
      data->stats_.empty_leaves_ += 1;
      data->stats_.left_traversals_ += 1;
      {
        Handle<String> left =
            factory->NewConsString(data->block(0), data->block(1))
                .ToHandleChecked();
        String::Flatten(isolate, left);
        return factory->NewConsString(left, data->block(2)).ToHandleChecked();
      }
    case 8:
      // Left node and right node flattened.
      data->stats_.chars_ += data->block(0)->length();
      data->stats_.chars_ += data->block(1)->length();
      data->stats_.chars_ += data->block(2)->length();
      data->stats_.chars_ += data->block(3)->length();
      data->stats_.leaves_ += 4;
      data->stats_.empty_leaves_ += 2;
      data->stats_.left_traversals_ += 1;
      data->stats_.right_traversals_ += 1;
      {
        Handle<String> left =
            factory->NewConsString(data->block(0), data->block(1))
                .ToHandleChecked();
        String::Flatten(isolate, left);
        Handle<String> right =
            factory->NewConsString(data->block(2), data->block(2))
                .ToHandleChecked();
        String::Flatten(isolate, right);
        return factory->NewConsString(left, right).ToHandleChecked();
      }
  }
  UNREACHABLE();
}

TEST(StringCharacterStreamEdgeCases) {
  printf("TestStringCharacterStreamEdgeCases\n");
  TestStringCharacterStream(BuildEdgeCaseConsString,
                            kCharacterStreamNonRandomCases);
}

static const int kBalances = 3;
static const int kTreeLengths = 4;
static const int kEmptyLeaves = 4;
static const int kUniqueRandomParameters =
    kBalances * kTreeLengths * kEmptyLeaves;

static void InitializeGenerationData(int test_case,
                                     ConsStringGenerationData* data) {
  // Clear the settings and reinit the rng.
  data->Reset();
  // Spin up the rng to a known location that is unique per test.
  static const int kPerTestJump = 501;
  for (int j = 0; j < test_case * kPerTestJump; j++) {
    data->rng_.next();
  }
  // Choose balanced, left or right heavy trees.
  switch (test_case % kBalances) {
    case 0:
      // Nothing to do.  Already balanced.
      break;
    case 1:
      // Left balanced.
      data->leftness_ = 0.90;
      data->rightness_ = 0.15;
      break;
    case 2:
      // Right balanced.
      data->leftness_ = 0.15;
      data->rightness_ = 0.90;
      break;
    default:
      UNREACHABLE();
  }
  // Must remove the influence of the above decision.
  test_case /= kBalances;
  // Choose tree length.
  switch (test_case % kTreeLengths) {
    case 0:
      data->max_leaves_ = 16;
      data->early_termination_threshold_ = 0.2;
      break;
    case 1:
      data->max_leaves_ = 50;
      data->early_termination_threshold_ = 0.05;
      break;
    case 2:
      data->max_leaves_ = 500;
      data->early_termination_threshold_ = 0.03;
      break;
    case 3:
      data->max_leaves_ = 5000;
      data->early_termination_threshold_ = 0.001;
      break;
    default:
      UNREACHABLE();
  }
  // Must remove the influence of the above decision.
  test_case /= kTreeLengths;
  // Choose how much we allow empty nodes, including not at all.
  data->empty_leaf_threshold_ =
      0.03 * static_cast<double>(test_case % kEmptyLeaves);
}

static Handle<String> BuildRandomConsString(int test_case,
                                            ConsStringGenerationData* data) {
  InitializeGenerationData(test_case, data);
  return ConstructRandomString(data, 200);
}

TEST(StringCharacterStreamRandom) {
  printf("StringCharacterStreamRandom\n");
  TestStringCharacterStream(BuildRandomConsString, kUniqueRandomParameters * 7);
}

static const int kDeepOneByteDepth = 100000;

TEST(DeepOneByte) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  v8::HandleScope scope(CcTest::isolate());

  char* foo = NewArray<char>(kDeepOneByteDepth);
  for (int i = 0; i < kDeepOneByteDepth; i++) {
    foo[i] = "foo "[i % 4];
  }
  Handle<String> string = factory
                              ->NewStringFromOneByte(v8::base::OneByteVector(
                                  foo, kDeepOneByteDepth))
                              .ToHandleChecked();
  Handle<String> foo_string = factory->NewStringFromStaticChars("foo");
  for (int i = 0; i < kDeepOneByteDepth; i += 10) {
    string = factory->NewConsString(string, foo_string).ToHandleChecked();
  }
  Handle<String> flat_string =
      factory->NewConsString(string, foo_string).ToHandleChecked();
  String::Flatten(isolate, flat_string);

  for (int i = 0; i < 500; i++) {
    TraverseFirst(flat_string, string, kDeepOneByteDepth);
  }
  DeleteArray<char>(foo);
}

TEST(Utf8Conversion) {
  // Smoke test for converting strings to utf-8.
  CcTest::InitializeVM();
  v8::HandleScope handle_scope(CcTest::isolate());
  // A simple one-byte string
  const char* one_byte_string = "abcdef12345";
  int len = v8::String::NewFromUtf8(CcTest::isolate(), one_byte_string,
                                    v8::NewStringType::kNormal,
                                    static_cast<int>(strlen(one_byte_string)))
                .ToLocalChecked()
                ->Utf8Length(CcTest::isolate());
  CHECK_EQ(strlen(one_byte_string), len);
  // A mixed one-byte and two-byte string
  // U+02E4 -> CB A4
  // U+0064 -> 64
  // U+12E4 -> E1 8B A4
  // U+0030 -> 30
  // U+3045 -> E3 81 85
  const uint16_t mixed_string[] = {0x02E4, 0x0064, 0x12E4, 0x0030, 0x3045};
  // The characters we expect to be output
  const unsigned char as_utf8[11] = {0xCB, 0xA4, 0x64, 0xE1, 0x8B, 0xA4,
                                     0x30, 0xE3, 0x81, 0x85, 0x00};
  // The number of bytes expected to be written for each length
  const int lengths[12] = {0, 0, 2, 3, 3, 3, 6, 7, 7, 7, 10, 11};
  const int char_lengths[12] = {0, 0, 1, 2, 2, 2, 3, 4, 4, 4, 5, 5};
  v8::Local<v8::String> mixed =
      v8::String::NewFromTwoByte(CcTest::isolate(), mixed_string,
                                 v8::NewStringType::kNormal, 5)
          .ToLocalChecked();
  CHECK_EQ(10, mixed->Utf8Length(CcTest::isolate()));
  // Try encoding the string with all capacities
  char buffer[11];
  const char kNoChar = static_cast<char>(-1);
  for (int i = 0; i <= 11; i++) {
    // Clear the buffer before reusing it
    for (int j = 0; j < 11; j++) buffer[j] = kNoChar;
    int chars_written;
    int written =
        mixed->WriteUtf8(CcTest::isolate(), buffer, i, &chars_written);
    CHECK_EQ(lengths[i], written);
    CHECK_EQ(char_lengths[i], chars_written);
    // Check that the contents are correct
    for (int j = 0; j < lengths[i]; j++)
      CHECK_EQ(as_utf8[j], static_cast<unsigned char>(buffer[j]));
    // Check that the rest of the buffer hasn't been touched
    for (int j = lengths[i]; j < 11; j++) CHECK_EQ(kNoChar, buffer[j]);
  }
}

TEST(Utf8ConversionPerf) {
  // Smoke test for converting strings to utf-8.
  LocalContext context;
  v8::HandleScope handle_scope(CcTest::isolate());
  v8::Local<v8::String> ascii_string =
      CompileRun("'abc'.repeat(1E6)").As<v8::String>();
  v8::Local<v8::String> one_byte_string =
      CompileRun("'\\u0255\\u0254\\u0253'.repeat(1E6)").As<v8::String>();
  v8::Local<v8::String> two_byte_string =
      CompileRun("'\\u2255\\u2254\\u2253'.repeat(1E6)").As<v8::String>();
  v8::Local<v8::String> surrogate_string =
      CompileRun("'\\u{12345}\\u2244'.repeat(1E6)").As<v8::String>();
  int size = 1E7;
  char* buffer = new char[4 * size];
  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    ascii_string->WriteUtf8(CcTest::isolate(), buffer, size, nullptr);
    printf("ascii string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }
  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    ascii_string->WriteUtf8(CcTest::isolate(), buffer, size, nullptr);
    printf("ascii string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }
  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    ascii_string->WriteUtf8(CcTest::isolate(), buffer, 4 * size, nullptr);
    printf("ascii string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }

  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    one_byte_string->WriteUtf8(CcTest::isolate(), buffer, size, nullptr);
    printf("one byte string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }
  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    one_byte_string->WriteUtf8(CcTest::isolate(), buffer, size, nullptr);
    printf("one byte string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }
  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    one_byte_string->WriteUtf8(CcTest::isolate(), buffer, 4 * size, nullptr);
    printf("one byte string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }

  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    two_byte_string->WriteUtf8(CcTest::isolate(), buffer, size, nullptr);
    printf("two byte string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }
  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    two_byte_string->WriteUtf8(CcTest::isolate(), buffer, size, nullptr);
    printf("two byte string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }
  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    two_byte_string->WriteUtf8(CcTest::isolate(), buffer, 4 * size, nullptr);
    printf("two byte string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }

  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    surrogate_string->WriteUtf8(CcTest::isolate(), buffer, size, nullptr);
    printf("surrogate string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }
  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    surrogate_string->WriteUtf8(CcTest::isolate(), buffer, size, nullptr);
    printf("surrogate string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }
  {
    v8::base::ElapsedTimer timer;
    timer.Start();
    surrogate_string->WriteUtf8(CcTest::isolate(), buffer, 4 * size, nullptr);
    printf("surrogate string %0.3f\n", timer.Elapsed().InMillisecondsF());
    timer.Stop();
  }
  delete[] buffer;
}

TEST(ExternalShortStringAdd) {
  LocalContext context;
  v8::HandleScope handle_scope(CcTest::isolate());

  // Make sure we cover all always-flat lengths and at least one above.
  static const int kMaxLength = 20;
  CHECK_GT(kMaxLength, i::ConsString::kMinLength);

  // Allocate two JavaScript arrays for holding short strings.
  v8::Local<v8::Array> one_byte_external_strings =
      v8::Array::New(CcTest::isolate(), kMaxLength + 1);
  v8::Local<v8::Array> non_one_byte_external_strings =
      v8::Array::New(CcTest::isolate(), kMaxLength + 1);

  // Generate short one-byte and two-byte external strings.
  for (int i = 0; i <= kMaxLength; i++) {
    char* one_byte = NewArray<char>(i + 1);
    for (int j = 0; j < i; j++) {
      one_byte[j] = 'a';
    }
    // Terminating '\0' is left out on purpose. It is not required for external
    // string data.
    OneByteResource* one_byte_resource = new OneByteResource(one_byte, i);
    v8::Local<v8::String> one_byte_external_string =
        v8::String::NewExternalOneByte(CcTest::isolate(), one_byte_resource)
            .ToLocalChecked();

    one_byte_external_strings
        ->Set(context.local(), v8::Integer::New(CcTest::isolate(), i),
              one_byte_external_string)
        .FromJust();
    base::uc16* non_one_byte = NewArray<base::uc16>(i + 1);
    for (int j = 0; j < i; j++) {
      non_one_byte[j] = 0x1234;
    }
    // Terminating '\0' is left out on purpose. It is not required for external
    // string data.
    Resource* resource = new Resource(non_one_byte, i);
    v8::Local<v8::String> non_one_byte_external_string =
        v8::String::NewExternalTwoByte(CcTest::isolate(), resource)
            .ToLocalChecked();
    non_one_byte_external_strings
        ->Set(context.local(), v8::Integer::New(CcTest::isolate(), i),
              non_one_byte_external_string)
        .FromJust();
  }

  // Add the arrays with the short external strings in the global object.
  v8::Local<v8::Object> global = context->Global();
  global
      ->Set(context.local(), v8_str("external_one_byte"),
            one_byte_external_strings)
      .FromJust();
  global
      ->Set(context.local(), v8_str("external_non_one_byte"),
            non_one_byte_external_strings)
      .FromJust();
  global
      ->Set(context.local(), v8_str("max_length"),
            v8::Integer::New(CcTest::isolate(), kMaxLength))
      .FromJust();

  // Add short external one-byte and two-byte strings checking the result.
  static const char* source =
      "function test() {"
      "  var one_byte_chars = 'aaaaaaaaaaaaaaaaaaaa';"
      "  var non_one_byte_chars = "
      "'\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1"
      "234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\"
      "u1234';"
      "  if (one_byte_chars.length != max_length) return 1;"
      "  if (non_one_byte_chars.length != max_length) return 2;"
      "  var one_byte = Array(max_length + 1);"
      "  var non_one_byte = Array(max_length + 1);"
      "  for (var i = 0; i <= max_length; i++) {"
      "    one_byte[i] = one_byte_chars.substring(0, i);"
      "    non_one_byte[i] = non_one_byte_chars.substring(0, i);"
      "  };"
      "  for (var i = 0; i <= max_length; i++) {"
      "    if (one_byte[i] != external_one_byte[i]) return 3;"
      "    if (non_one_byte[i] != external_non_one_byte[i]) return 4;"
      "    for (var j = 0; j < i; j++) {"
      "      if (external_one_byte[i] !="
      "          (external_one_byte[j] + external_one_byte[i - j])) return "
      "5;"
      "      if (external_non_one_byte[i] !="
      "          (external_non_one_byte[j] + external_non_one_byte[i - "
      "j])) return 6;"
      "      if (non_one_byte[i] != (non_one_byte[j] + non_one_byte[i - "
      "j])) return 7;"
      "      if (one_byte[i] != (one_byte[j] + one_byte[i - j])) return 8;"
      "      if (one_byte[i] != (external_one_byte[j] + one_byte[i - j])) "
      "return 9;"
      "      if (one_byte[i] != (one_byte[j] + external_one_byte[i - j])) "
      "return 10;"
      "      if (non_one_byte[i] !="
      "          (external_non_one_byte[j] + non_one_byte[i - j])) return "
      "11;"
      "      if (non_one_byte[i] !="
      "          (non_one_byte[j] + external_non_one_byte[i - j])) return "
      "12;"
      "    }"
      "  }"
      "  return 0;"
      "};"
      "test()";
  CHECK_EQ(0, CompileRun(source)->Int32Value(context.local()).FromJust());
}

TEST(ReplaceInvalidUtf8) {
  LocalContext context;
  v8::HandleScope handle_scope(CcTest::isolate());
  v8::Local<v8::String> string = CompileRun("'ab\\ud800cd'").As<v8::String>();
  char buffer[7];
  memset(buffer, 0, 7);
  int chars_written = 0;
  int size = string->WriteUtf8(CcTest::isolate(), buffer, 7, &chars_written,
                               v8::String::REPLACE_INVALID_UTF8);
  CHECK_EQ(7, size);
  CHECK_EQ(5, chars_written);
  CHECK_EQ(0, memcmp("\x61\x62\xef\xbf\xbd\x63\x64", buffer, 7));

  memset(buffer, 0, 7);
  chars_written = 0;
  size = string->WriteUtf8(CcTest::isolate(), buffer, 6, &chars_written,
                           v8::String::REPLACE_INVALID_UTF8);
  CHECK_EQ(6, size);
  CHECK_EQ(4, chars_written);
  CHECK_EQ(0, memcmp("\x61\x62\xef\xbf\xbd\x63", buffer, 6));
}

TEST(JSONStringifyWellFormed) {
  CcTest::InitializeVM();
  v8::HandleScope handle_scope(CcTest::isolate());
  v8::Local<v8::Context> context = CcTest::isolate()->GetCurrentContext();

  // Test some leading surrogates (U+D800 to U+DBFF).
  {  // U+D800
    CHECK_EQ(
        0, strcmp("\"\\ud800\"", *v8::String::Utf8Value(
                                     CcTest::isolate(),
                                     CompileRun("JSON.stringify('\\uD800')"))));
    v8::Local<v8::String> json = v8_str("\"\\ud800\"");
    v8::Local<v8::Value> parsed =
        v8::JSON::Parse(context, json).ToLocalChecked();
    CHECK(v8::JSON::Stringify(context, parsed)
              .ToLocalChecked()
              ->Equals(context, json)
              .FromJust());
  }

  {  // U+DAAA
    CHECK_EQ(
        0, strcmp("\"\\udaaa\"", *v8::String::Utf8Value(
                                     CcTest::isolate(),
                                     CompileRun("JSON.stringify('\\uDAAA')"))));
    v8::Local<v8::String> json = v8_str("\"\\udaaa\"");
    v8::Local<v8::Value> parsed =
        v8::JSON::Parse(context, json).ToLocalChecked();
    CHECK(v8::JSON::Stringify(context, parsed)
              .ToLocalChecked()
              ->Equals(context, json)
              .FromJust());
  }

  {  // U+DBFF
    CHECK_EQ(
        0, strcmp("\"\\udbff\"", *v8::String::Utf8Value(
                                     CcTest::isolate(),
                                     CompileRun("JSON.stringify('\\uDBFF')"))));
    v8::Local<v8::String> json = v8_str("\"\\udbff\"");
    v8::Local<v8::Value> parsed =
        v8::JSON::Parse(context, json).ToLocalChecked();
    CHECK(v8::JSON::Stringify(context, parsed)
              .ToLocalChecked()
              ->Equals(context, json)
              .FromJust());
  }

  // Test some trailing surrogates (U+DC00 to U+DFFF).
  {  // U+DC00
    CHECK_EQ(
        0, strcmp("\"\\udc00\"", *v8::String::Utf8Value(
                                     CcTest::isolate(),
                                     CompileRun("JSON.stringify('\\uDC00')"))));
    v8::Local<v8::String> json = v8_str("\"\\udc00\"");
    v8::Local<v8::Value> parsed =
        v8::JSON::Parse(context, json).ToLocalChecked();
    CHECK(v8::JSON::Stringify(context, parsed)
              .ToLocalChecked()
              ->Equals(context, json)
              .FromJust());
  }

  {  // U+DDDD
    CHECK_EQ(
        0, strcmp("\"\\udddd\"", *v8::String::Utf8Value(
                                     CcTest::isolate(),
                                     CompileRun("JSON.stringify('\\uDDDD')"))));
    v8::Local<v8::String> json = v8_str("\"\\udddd\"");
    v8::Local<v8::Value> parsed =
        v8::JSON::Parse(context, json).ToLocalChecked();
    CHECK(v8::JSON::Stringify(context, parsed)
              .ToLocalChecked()
              ->Equals(context, json)
              .FromJust());
  }

  {  // U+DFFF
    CHECK_EQ(
        0, strcmp("\"\\udfff\"", *v8::String::Utf8Value(
                                     CcTest::isolate(),
                                     CompileRun("JSON.stringify('\\uDFFF')"))));
    v8::Local<v8::String> json = v8_str("\"\\udfff\"");
    v8::Local<v8::Value> parsed =
        v8::JSON::Parse(context, json).ToLocalChecked();
    CHECK(v8::JSON::Stringify(context, parsed)
              .ToLocalChecked()
              ->Equals(context, json)
              .FromJust());
  }
}

TEST(CachedHashOverflow) {
  CcTest::InitializeVM();
  // We incorrectly allowed strings to be tagged as array indices even if their
  // values didn't fit in the hash field.
  // See http://code.google.com/p/v8/issues/detail?id=728
  Isolate* isolate = CcTest::i_isolate();

  v8::HandleScope handle_scope(CcTest::isolate());
  // Lines must be executed sequentially. Combining them into one script
  // makes the bug go away.
  const char* lines[] = {"var x = [];", "x[4] = 42;", "var s = \"1073741828\";",
                         "x[s];",       "x[s] = 37;", "x[4];",
                         "x[s];"};

  Handle<Smi> fortytwo(Smi::FromInt(42), isolate);
  Handle<Smi> thirtyseven(Smi::FromInt(37), isolate);
  Handle<Object> results[] = {
      isolate->factory()->undefined_value(),
      fortytwo,
      isolate->factory()->undefined_value(),
      isolate->factory()->undefined_value(),
      thirtyseven,
      fortytwo,
      thirtyseven  // Bug yielded 42 here.
  };

  v8::Local<v8::Context> context = CcTest::isolate()->GetCurrentContext();
  for (size_t i = 0; i < arraysize(lines); i++) {
    const char* line = lines[i];
    printf("%s\n", line);
    v8::Local<v8::Value> result =
        v8::Script::Compile(
            context,
            v8::String::NewFromUtf8(CcTest::isolate(), line).ToLocalChecked())
            .ToLocalChecked()
            ->Run(context)
            .ToLocalChecked();
    CHECK_EQ(IsUndefined(*results[i], CcTest::i_isolate()),
             result->IsUndefined());
    CHECK_EQ(IsNumber(*results[i]), result->IsNumber());
    if (result->IsNumber()) {
      int32_t value = 0;
      CHECK(Object::ToInt32(*results[i], &value));
      CHECK_EQ(value, result->ToInt32(context).ToLocalChecked()->Value());
    }
  }
}

TEST(SliceFromCons) {
  if (!v8_flags.string_slices) return;
  CcTest::InitializeVM();
  Factory* factory = CcTest::i_isolate()->factory();
  v8::HandleScope scope(CcTest::isolate());
  Handle<String> string =
      factory->NewStringFromStaticChars("parentparentparent");
  Handle<String> parent =
      factory->NewConsString(string, string).ToHandleChecked();
  CHECK(IsConsString(*parent));
  CHECK(!parent->IsFlat());
  Handle<String> slice = factory->NewSubString(parent, 1, 25);
  // After slicing, the original string becomes a flat cons.
  CHECK(parent->IsFlat());
  CHECK(IsSlicedString(*slice));
  // TODO(leszeks): Remove Tagged cast when .first() returns a Tagged.
  static_assert(kTaggedCanConvertToRawObjects);
  CHECK_EQ(SlicedString::cast(*slice)->parent(),
           // Parent could have been short-circuited.
           IsConsString(*parent) ? Tagged(ConsString::cast(*parent)->first())
                                 : *parent);
  CHECK(IsSeqString(SlicedString::cast(*slice)->parent()));
  CHECK(slice->IsFlat());
}

class OneByteVectorResource : public v8::String::ExternalOneByteStringResource {
 public:
  explicit OneByteVectorResource(v8::base::Vector<const char> vector)
      : data_(vector) {}
  ~OneByteVectorResource() override = default;
  size_t length() const override { return data_.length(); }
  const char* data() const override { return data_.begin(); }

 private:
  v8::base::Vector<const char> data_;
};

TEST(InternalizeExternal) {
  v8_flags.stress_incremental_marking = false;
  CcTest::InitializeVM();
  i::Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  // This won't leak; the external string mechanism will call Dispose() on it.
  OneByteVectorResource* resource =
      new OneByteVectorResource(v8::base::Vector<const char>("prop-1234", 9));
  {
    v8::HandleScope scope(CcTest::isolate());
    v8::Local<v8::String> ext_string =
        v8::String::NewExternalOneByte(CcTest::isolate(), resource)
            .ToLocalChecked();
    Handle<String> string = v8::Utils::OpenHandle(*ext_string);
    CHECK(IsExternalString(*string));
    CHECK(!IsInternalizedString(*string));
    CHECK(!i::Heap::InYoungGeneration(*string));
    CHECK_EQ(isolate->string_table()->TryStringToIndexOrLookupExisting(
                 isolate, string->ptr()),
             Smi::FromInt(ResultSentinel::kNotFound).ptr());
    factory->InternalizeName(string);
    CHECK(IsExternalString(*string));
    CHECK(IsInternalizedString(*string));
    CHECK(!i::Heap::InYoungGeneration(*string));
  }
  i::heap::InvokeMajorGC(CcTest::heap());
  i::heap::InvokeMajorGC(CcTest::heap());
}

TEST(Regress1402187) {
  CcTest::InitializeVM();
  i::Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  // This won't leak; the external string mechanism will call Dispose() on it.
  const char ext_string_content[] = "prop-1234567890asdf";
  OneByteVectorResource* resource =
      new OneByteVectorResource(v8::base::Vector<const char>(
          ext_string_content, strlen(ext_string_content)));
  const uint32_t fake_hash =
      String::CreateHashFieldValue(4711, String::HashFieldType::kHash);
  {
    v8::HandleScope scope(CcTest::isolate());
    // Internalize a string with the same hash to ensure collision.
    Handle<String> intern = factory->NewStringFromAsciiChecked(
        "internalized1234567", AllocationType::kOld);
    intern->set_raw_hash_field(fake_hash);
    factory->InternalizeName(intern);
    CHECK(IsInternalizedString(*intern));

    v8::Local<v8::String> ext_string =
        Utils::ToLocal(factory->NewStringFromAsciiChecked(
            ext_string_content, AllocationType::kOld));
    CHECK(ext_string->MakeExternal(resource));
    Handle<String> string = v8::Utils::OpenHandle(*ext_string);
    string->set_raw_hash_field(fake_hash);
    CHECK(IsExternalString(*string));
    CHECK(!StringShape(*string).IsUncachedExternal());
    CHECK(!IsInternalizedString(*string));
    CHECK(!String::Equals(isolate, string, intern));
    CHECK_EQ(string->hash(), intern->hash());
    CHECK_EQ(string->length(), intern->length());

    CHECK_EQ(isolate->string_table()->TryStringToIndexOrLookupExisting(
                 isolate, string->ptr()),
             Smi::FromInt(ResultSentinel::kNotFound).ptr());
    string = factory->InternalizeString(string);
    CHECK(IsExternalString(*string));
    CHECK(IsInternalizedString(*string));
  }
  i::heap::InvokeMajorGC(CcTest::heap());
  i::heap::InvokeMajorGC(CcTest::heap());
}

TEST(SliceFromExternal) {
  if (!v8_flags.string_slices) return;
  CcTest::InitializeVM();
  Factory* factory = CcTest::i_isolate()->factory();
  v8::HandleScope scope(CcTest::isolate());
  OneByteVectorResource resource(
      v8::base::Vector<const char>("abcdefghijklmnopqrstuvwxyz", 26));
  Handle<String> string =
      factory->NewExternalStringFromOneByte(&resource).ToHandleChecked();
  CHECK(IsExternalString(*string));
  Handle<String> slice = factory->NewSubString(string, 1, 25);
  CHECK(IsSlicedString(*slice));
  CHECK(IsExternalString(*string));
  CHECK_EQ(SlicedString::cast(*slice)->parent(), *string);
  CHECK(IsExternalString(SlicedString::cast(*slice)->parent()));
  CHECK(slice->IsFlat());
  // This avoids the GC from trying to free stack allocated resources.
  i::Handle<i::ExternalOneByteString>::cast(string)->SetResource(
      CcTest::i_isolate(), nullptr);
}

static void ExternalizeDuringJsonStringifyCallback(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Local<v8::Value> key = v8_compile("p")
                                 ->Run(CcTest::isolate()->GetCurrentContext())
                                 .ToLocalChecked();
  const static char ext_string_content[] = "prop-1234567890asdf";
  OneByteVectorResource* resource =
      new OneByteVectorResource(v8::base::Vector<const char>(
          ext_string_content, strlen(ext_string_content)));
  CHECK(v8::String::Cast(*key)->MakeExternal(resource));
}

TEST(ExternalizeDuringJsonStringify) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  const char script[] = R"(
    var p = "prop-1234567890asdf";
    JSON.stringify([
      { [p]: 3 },
      { toJSON: callExternal },
      { [p]: 4 },
    ]);
  )";
  v8::Local<v8::ObjectTemplate> global = ObjectTemplate::New(isolate);
  global->Set(isolate, "callExternal",
              v8::FunctionTemplate::New(
                  isolate, ExternalizeDuringJsonStringifyCallback));
  LocalContext context(nullptr, global);
  v8::Local<v8::Value> stringified =
      v8_compile(script)->Run(context.local()).ToLocalChecked();
  CHECK(v8::String::NewFromUtf8Literal(
            isolate,
            R"([{"prop-1234567890asdf":3},null,{"prop-1234567890asdf":4}])")
            ->Equals(context.local(), stringified)
            .FromJust());
}

TEST(TrivialSlice) {
  // This tests whether a slice that contains the entire parent string
  // actually creates a new string (it should not).
  if (!v8_flags.string_slices) return;
  CcTest::InitializeVM();
  Factory* factory = CcTest::i_isolate()->factory();
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Value> result;
  Handle<String> string;
  const char* init = "var str = 'abcdefghijklmnopqrstuvwxyz';";
  const char* check = "str.slice(0,26)";
  const char* crosscheck = "str.slice(1,25)";

  CompileRun(init);

  result = CompileRun(check);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(!IsSlicedString(*string));

  string = factory->NewSubString(string, 0, 26);
  CHECK(!IsSlicedString(*string));
  result = CompileRun(crosscheck);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(IsSlicedString(*string));
  CHECK_EQ(0, strcmp("bcdefghijklmnopqrstuvwxy", string->ToCString().get()));
}

TEST(SliceFromSlice) {
  // This tests whether a slice that contains the entire parent string
  // actually creates a new string (it should not).
  if (!v8_flags.string_slices) return;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Value> result;
  Handle<String> string;
  const char* init = "var str = 'abcdefghijklmnopqrstuvwxyz';";
  const char* slice = "var slice = ''; slice = str.slice(1,-1); slice";
  const char* slice_from_slice = "slice.slice(1,-1);";

  CompileRun(init);
  result = CompileRun(slice);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(IsSlicedString(*string));
  CHECK(IsSeqString(SlicedString::cast(*string)->parent()));
  CHECK_EQ(0, strcmp("bcdefghijklmnopqrstuvwxy", string->ToCString().get()));

  result = CompileRun(slice_from_slice);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(IsSlicedString(*string));
  CHECK(IsSeqString(SlicedString::cast(*string)->parent()));
  CHECK_EQ(0, strcmp("cdefghijklmnopqrstuvwx", string->ToCString().get()));
}

UNINITIALIZED_TEST(OneByteArrayJoin) {
  v8::Isolate::CreateParams create_params;
  // Set heap limits.
  create_params.constraints.set_max_young_generation_size_in_bytes(3 * MB);
#ifdef DEBUG
  create_params.constraints.set_max_old_generation_size_in_bytes(20 * MB);
#else
  create_params.constraints.set_max_old_generation_size_in_bytes(7 * MB);
#endif
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  isolate->Enter();

  {
    // String s is made of 2^17 = 131072 'c' characters and a is an array
    // starting with 'bad', followed by 2^14 times the string s. That means the
    // total length of the concatenated strings is 2^31 + 3. So on 32bit systems
    // summing the lengths of the strings (as Smis) overflows and wraps.
    LocalContext context(isolate);
    v8::HandleScope scope(isolate);
    v8::TryCatch try_catch(isolate);
    CHECK(CompileRun("var two_14 = Math.pow(2, 14);"
                     "var two_17 = Math.pow(2, 17);"
                     "var s = Array(two_17 + 1).join('c');"
                     "var a = ['bad'];"
                     "for (var i = 1; i <= two_14; i++) a.push(s);"
                     "a.join("
                     ");")
              .IsEmpty());
    CHECK(try_catch.HasCaught());
  }
  isolate->Exit();
  isolate->Dispose();
}  // namespace
namespace {

int* global_use_counts = nullptr;

void MockUseCounterCallback(v8::Isolate* isolate,
                            v8::Isolate::UseCounterFeature feature) {
  ++global_use_counts[feature];
}
}  // namespace

TEST(CountBreakIterator) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  LocalContext context;
  int use_counts[v8::Isolate::kUseCounterFeatureCount] = {};
  global_use_counts = use_counts;
  CcTest::isolate()->SetUseCounterCallback(MockUseCounterCallback);
  CHECK_EQ(0, use_counts[v8::Isolate::kBreakIterator]);
  v8::Local<v8::Value> result = CompileRun(
      "(function() {"
      "  if (!this.Intl) return 0;"
      "  var iterator = Intl.v8BreakIterator(['en']);"
      "  iterator.adoptText('Now is the time');"
      "  iterator.next();"
      "  return iterator.next();"
      "})();");
  CHECK(result->IsNumber());
  int uses =
      result->ToInt32(context.local()).ToLocalChecked()->Value() == 0 ? 0 : 1;
  CHECK_EQ(uses, use_counts[v8::Isolate::kBreakIterator]);
  // Make sure GC cleans up the break iterator, so we don't get a memory leak
  // reported by ASAN.
  CcTest::isolate()->LowMemoryNotification();
}

TEST(StringReplaceAtomTwoByteResult) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  LocalContext context;
  v8::Local<v8::Value> result = CompileRun(
      "var subject = 'one_byte~only~string~'; "
      "var replace = '\x80';            "
      "subject.replace(/~/g, replace);  ");
  CHECK(result->IsString());
  Handle<String> string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(string->IsTwoByteRepresentation());

  v8::Local<v8::String> expected = v8_str("one_byte\x80only\x80string\x80");
  CHECK(expected->Equals(context.local(), result).FromJust());
}

TEST(IsAscii) {
  CHECK(String::IsAscii(static_cast<char*>(nullptr), 0));
  CHECK(String::IsOneByte(static_cast<base::uc16*>(nullptr), 0));
}

template <typename Op, bool return_first>
static uint16_t ConvertLatin1(uint16_t c) {
  uint32_t result[Op::kMaxWidth];
  int chars;
  chars = Op::Convert(c, 0, result, nullptr);
  if (chars == 0) return 0;
  CHECK_LE(chars, static_cast<int>(sizeof(result)));
  if (!return_first && chars > 1) {
    return 0;
  }
  return result[0];
}

#ifndef V8_INTL_SUPPORT
static void CheckCanonicalEquivalence(uint16_t c, uint16_t test) {
  uint16_t expect = ConvertLatin1<unibrow::Ecma262UnCanonicalize, true>(c);
  if (expect > unibrow::Latin1::kMaxChar || expect == 0) expect = c;
  CHECK_EQ(expect, test);
}

TEST(Latin1IgnoreCase) {
  for (uint16_t c = unibrow::Latin1::kMaxChar + 1; c != 0; c++) {
    uint16_t lower = ConvertLatin1<unibrow::ToLowercase, false>(c);
    uint16_t upper = ConvertLatin1<unibrow::ToUppercase, false>(c);
    uint16_t test = unibrow::Latin1::TryConvertToLatin1(c);
    // Filter out all character whose upper is not their lower or vice versa.
    if (lower == 0 && upper == 0) {
      CheckCanonicalEquivalence(c, test);
      continue;
    }
    if (lower > unibrow::Latin1::kMaxChar &&
        upper > unibrow::Latin1::kMaxChar) {
      CheckCanonicalEquivalence(c, test);
      continue;
    }
    if (lower == 0 && upper != 0) {
      lower = ConvertLatin1<unibrow::ToLowercase, false>(upper);
    }
    if (upper == 0 && lower != c) {
      upper = ConvertLatin1<unibrow::ToUppercase, false>(lower);
    }
    if (lower > unibrow::Latin1::kMaxChar &&
        upper > unibrow::Latin1::kMaxChar) {
      CheckCanonicalEquivalence(c, test);
      continue;
    }
    if (upper != c && lower != c) {
      CheckCanonicalEquivalence(c, test);
      continue;
    }
    CHECK_EQ(std::min(upper, lower), test);
  }
}
#endif

class DummyResource : public v8::String::ExternalStringResource {
 public:
  const uint16_t* data() const override { return nullptr; }
  size_t length() const override { return 1 << 30; }
};

class DummyOneByteResource : public v8::String::ExternalOneByteStringResource {
 public:
  const char* data() const override { return nullptr; }
  size_t length() const override { return 1 << 30; }
};

TEST(InvalidExternalString) {
  CcTest::InitializeVM();
  LocalContext context;
  Isolate* isolate = CcTest::i_isolate();
  {
    HandleScope scope(isolate);
    DummyOneByteResource r;
    CHECK(isolate->factory()->NewExternalStringFromOneByte(&r).is_null());
    CHECK(isolate->has_exception());
    isolate->clear_exception();
  }

  {
    HandleScope scope(isolate);
    DummyResource r;
    CHECK(isolate->factory()->NewExternalStringFromTwoByte(&r).is_null());
    CHECK(isolate->has_exception());
    isolate->clear_exception();
  }
}

#define INVALID_STRING_TEST(FUN, TYPE)                                   \
  TEST(StringOOM##FUN) {                                                 \
    CcTest::InitializeVM();                                              \
    LocalContext context;                                                \
    Isolate* isolate = CcTest::i_isolate();                              \
    static_assert(String::kMaxLength < kMaxInt);                         \
    static const int invalid = String::kMaxLength + 1;                   \
    HandleScope scope(isolate);                                          \
    v8::base::Vector<TYPE> dummy = v8::base::Vector<TYPE>::New(invalid); \
    memset(dummy.begin(), 0x0, dummy.length() * sizeof(TYPE));           \
    CHECK(isolate->factory()                                             \
              ->FUN(v8::base::Vector<const TYPE>::cast(dummy))           \
              .is_null());                                               \
    memset(dummy.begin(), 0x20, dummy.length() * sizeof(TYPE));          \
    CHECK(isolate->has_exception());                                     \
    isolate->clear_exception();                                          \
    dummy.Dispose();                                                     \
  }

INVALID_STRING_TEST(NewStringFromUtf8, char)
INVALID_STRING_TEST(NewStringFromOneByte, uint8_t)

#undef INVALID_STRING_TEST

TEST(FormatMessage) {
  CcTest::InitializeVM();
  LocalContext context;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Handle<String> arg0 = isolate->factory()->NewStringFromAsciiChecked("arg0");
  Handle<String> arg1 = isolate->factory()->NewStringFromAsciiChecked("arg1");
  Handle<String> arg2 = isolate->factory()->NewStringFromAsciiChecked("arg2");
  Handle<String> result = MessageFormatter::TryFormat(
                              isolate, MessageTemplate::kPropertyNotFunction,
                              base::VectorOf({arg0, arg1, arg2}))
                              .ToHandleChecked();
  Handle<String> expected = isolate->factory()->NewStringFromAsciiChecked(
      "'arg0' returned for property 'arg1' of object 'arg2' is not a function");
  CHECK(String::Equals(isolate, result, expected));
}

TEST(Regress609831) {
  CcTest::InitializeVM();
  LocalContext context;
  Isolate* isolate = CcTest::i_isolate();
  {
    HandleScope scope(isolate);
    v8::Local<v8::Value> result = CompileRun(
        "String.fromCharCode(32, 32, 32, 32, 32, "
        "32, 32, 32, 32, 32, 32, 32, 32, 32, 32, "
        "32, 32, 32, 32, 32, 32, 32, 32, 32, 32)");
    CHECK(IsSeqOneByteString(*v8::Utils::OpenDirectHandle(*result)));
  }
  {
    HandleScope scope(isolate);
    v8::Local<v8::Value> result = CompileRun(
        "String.fromCharCode(432, 432, 432, 432, 432, "
        "432, 432, 432, 432, 432, 432, 432, 432, 432, "
        "432, 432, 432, 432, 432, 432, 432, 432, 432)");
    CHECK(IsSeqTwoByteString(*v8::Utils::OpenDirectHandle(*result)));
  }
}

TEST(ExternalStringIndexOf) {
  CcTest::InitializeVM();
  LocalContext context;
  v8::HandleScope scope(CcTest::isolate());

  const char* raw_string = "abcdefghijklmnopqrstuvwxyz";
  v8::Local<v8::String> string =
      v8::String::NewExternalOneByte(CcTest::isolate(),
                                     new StaticOneByteResource(raw_string))
          .ToLocalChecked();
  v8::Local<v8::Object> global = context->Global();
  global->Set(context.local(), v8_str("external"), string).FromJust();

  char source[] = "external.indexOf('%')";
  for (size_t i = 0; i < strlen(raw_string); i++) {
    source[18] = raw_string[i];
    int result_position = static_cast<int>(i);
    CHECK_EQ(result_position,
             CompileRun(source)->Int32Value(context.local()).FromJust());
  }
  CHECK_EQ(-1,
           CompileRun("external.indexOf('abcdefghijklmnopqrstuvwxyz%%%%%%')")
               ->Int32Value(context.local())
               .FromJust());
  CHECK_EQ(1, CompileRun("external.indexOf('', 1)")
                  ->Int32Value(context.local())
                  .FromJust());
  CHECK_EQ(-1, CompileRun("external.indexOf('a', 1)")
                   ->Int32Value(context.local())
                   .FromJust());
  CHECK_EQ(-1, CompileRun("external.indexOf('$')")
                   ->Int32Value(context.local())
                   .FromJust());
}

namespace {

struct IndexData {
  const char* string;
  bool is_array_index;
  uint32_t array_index;
  bool is_integer_index;
  size_t integer_index;
};

void TestString(i::Isolate* isolate, const IndexData& data) {
  Handle<String> s = isolate->factory()->NewStringFromAsciiChecked(data.string);
  if (data.is_array_index) {
    uint32_t index;
    CHECK(s->AsArrayIndex(&index));
    CHECK_EQ(data.array_index, index);
  }
  if (data.is_integer_index) {
    size_t index;
    CHECK(s->AsIntegerIndex(&index));
    CHECK_EQ(data.integer_index, index);
    CHECK(String::IsIntegerIndex(s->EnsureRawHash()));
    CHECK(s->HasHashCode());
  }
  if (!s->HasHashCode()) s->EnsureHash();
  CHECK(s->HasHashCode());
  if (!data.is_integer_index) {
    CHECK(String::IsHash(s->raw_hash_field()));
  }
}

}  // namespace

TEST(HashArrayIndexStrings) {
  CcTest::InitializeVM();
  LocalContext context;
  v8::HandleScope scope(CcTest::isolate());
  i::Isolate* isolate = CcTest::i_isolate();

  CHECK_EQ(Name::HashBits::decode(
               StringHasher::MakeArrayIndexHash(0 /* value */, 1 /* length */)),
           isolate->factory()->zero_string()->hash());

  CHECK_EQ(Name::HashBits::decode(
               StringHasher::MakeArrayIndexHash(1 /* value */, 1 /* length */)),
           isolate->factory()->one_string()->hash());

  IndexData tests[] = {
    {"", false, 0, false, 0},
    {"123no", false, 0, false, 0},
    {"12345", true, 12345, true, 12345},
    {"12345678", true, 12345678, true, 12345678},
    {"4294967294", true, 4294967294u, true, 4294967294u},
#if V8_TARGET_ARCH_32_BIT
    {"4294967295", false, 0, false, 0},  // Valid length but not index.
    {"4294967296", false, 0, false, 0},
    {"9007199254740991", false, 0, false, 0},
#else
    {"4294967295", false, 0, true, 4294967295u},
    {"4294967296", false, 0, true, 4294967296ull},
    {"9007199254740991", false, 0, true, 9007199254740991ull},
#endif
    {"9007199254740992", false, 0, false, 0},
    {"18446744073709551615", false, 0, false, 0},
    {"18446744073709551616", false, 0, false, 0}
  };
  for (int i = 0, n = arraysize(tests); i < n; i++) {
    TestString(isolate, tests[i]);
  }
}

TEST(StringEquals) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);

  auto foo_str = v8::String::NewFromUtf8Literal(isolate, "foo");
  auto bar_str = v8::String::NewFromUtf8Literal(isolate, "bar");
  auto foo_str2 = v8::String::NewFromUtf8Literal(isolate, "foo");

  uint16_t* two_byte_source = AsciiToTwoByteString("foo");
  auto foo_two_byte_str =
      v8::String::NewFromTwoByte(isolate, two_byte_source).ToLocalChecked();
  i::DeleteArray(two_byte_source);

  CHECK(foo_str->StringEquals(foo_str));
  CHECK(!foo_str->StringEquals(bar_str));
  CHECK(foo_str->StringEquals(foo_str2));
  CHECK(foo_str->StringEquals(foo_two_byte_str));
  CHECK(!bar_str->StringEquals(foo_str2));
}

class OneByteStringResource : public v8::String::ExternalOneByteStringResource {
 public:
  // Takes ownership of |data|.
  OneByteStringResource(char* data, size_t length)
      : data_(data), length_(length) {}
  ~OneByteStringResource() override { delete[] data_; }
  const char* data() const override { return data_; }
  size_t length() const override { return length_; }

 private:
  char* data_;
  size_t length_;
};

// Show that it is possible to internalize an external string without a copy, as
// long as it is not uncached.
TEST(InternalizeExternalString) {
  CcTest::InitializeVM();
  Factory* factory = CcTest::i_isolate()->factory();
  v8::HandleScope scope(CcTest::isolate());

  // Create the string.
  const char* raw_string = "external";
  OneByteResource* resource =
      new OneByteResource(i::StrDup(raw_string), strlen(raw_string));
  Handle<String> string =
      factory->NewExternalStringFromOneByte(resource).ToHandleChecked();
  CHECK(IsExternalString(*string));

  // Check it is not uncached.
  Handle<ExternalString> external = Handle<ExternalString>::cast(string);
  CHECK(!external->is_uncached());

  // Internalize succesfully, without a copy.
  Handle<String> internal = factory->InternalizeString(external);
  CHECK(IsInternalizedString(*string));
  CHECK(string.equals(internal));
}

// Show that it is possible to internalize an external string without a copy, as
// long as it is not uncached. Two byte version.
TEST(InternalizeExternalStringTwoByte) {
  CcTest::InitializeVM();
  Factory* factory = CcTest::i_isolate()->factory();
  v8::HandleScope scope(CcTest::isolate());

  // Create the string.
  const char* raw_string = "external";
  Resource* resource =
      new Resource(AsciiToTwoByteString(raw_string), strlen(raw_string));
  Handle<String> string =
      factory->NewExternalStringFromTwoByte(resource).ToHandleChecked();
  CHECK(IsExternalString(*string));

  // Check it is not uncached.
  Handle<ExternalString> external = Handle<ExternalString>::cast(string);
  CHECK(!external->is_uncached());

  // Internalize succesfully, without a copy.
  Handle<String> internal = factory->InternalizeString(external);
  CHECK(IsInternalizedString(*string));
  CHECK(string.equals(internal));
}

class UncachedExternalOneByteResource
    : public v8::String::ExternalOneByteStringResource {
 public:
  explicit UncachedExternalOneByteResource(const char* data)
      : data_(data), length_(strlen(data)) {}

  ~UncachedExternalOneByteResource() override { i::DeleteArray(data_); }

  const char* data() const override { return data_; }
  size_t length() const override { return length_; }
  bool IsCacheable() const override { return false; }

 private:
  const char* data_;
  size_t length_;
};

// Show that we can internalize an external uncached string, by creating a copy.
TEST(InternalizeExternalStringUncachedWithCopy) {
  CcTest::InitializeVM();
  Factory* factory = CcTest::i_isolate()->factory();
  v8::HandleScope scope(CcTest::isolate());

  // Create the string.
  const char* raw_string = "external";
  UncachedExternalOneByteResource* resource =
      new UncachedExternalOneByteResource(i::StrDup(raw_string));
  Handle<String> string =
      factory->NewExternalStringFromOneByte(resource).ToHandleChecked();
  CHECK(IsExternalString(*string));

  // Check it is uncached.
  Handle<ExternalString> external = Handle<ExternalString>::cast(string);
  CHECK(external->is_uncached());

  // Internalize succesfully, with a copy.
  Handle<String> internal = factory->InternalizeString(external);
  CHECK(!IsInternalizedString(*external));
  CHECK(IsInternalizedString(*internal));
}

class UncachedExternalResource : public v8::String::ExternalStringResource {
 public:
  explicit UncachedExternalResource(const uint16_t* data)
      : data_(data), length_(0) {
    while (data[length_]) ++length_;
  }

  ~UncachedExternalResource() override { i::DeleteArray(data_); }

  const uint16_t* data() const override { return data_; }
  size_t length() const override { return length_; }
  bool IsCacheable() const override { return false; }

 private:
  const uint16_t* data_;
  size_t length_;
};

// Show that we can internalize an external uncached string, by creating a copy.
// Two byte version.
TEST(InternalizeExternalStringUncachedWithCopyTwoByte) {
  CcTest::InitializeVM();
  Factory* factory = CcTest::i_isolate()->factory();
  v8::HandleScope scope(CcTest::isolate());

  // Create the string.
  const char* raw_string = "external";
  UncachedExternalResource* resource =
      new UncachedExternalResource(AsciiToTwoByteString(raw_string));
  Handle<String> string =
      factory->NewExternalStringFromTwoByte(resource).ToHandleChecked();
  CHECK(IsExternalString(*string));

  // Check it is uncached.
  Handle<ExternalString> external = Handle<ExternalString>::cast(string);
  CHECK(external->is_uncached());

  // Internalize succesfully, with a copy.
  CHECK(!IsInternalizedString(*external));
  Handle<String> internal = factory->InternalizeString(external);
  CHECK(!IsInternalizedString(*external));
  CHECK(IsInternalizedString(*internal));
}

// Show that we cache the data pointer for internal, external and uncached
// strings with cacheable resources through MakeExternal. One byte version.
TEST(CheckCachedDataInternalExternalUncachedString) {
  CcTest::InitializeVM();
  Factory* factory = CcTest::i_isolate()->factory();
  v8::HandleScope scope(CcTest::isolate());

  // Due to different size restrictions the string needs to be small but not too
  // small. One of these restrictions is whether pointer compression is enabled.
#ifdef V8_COMPRESS_POINTERS
  const char* raw_small = "small string";
#elif V8_TARGET_ARCH_32_BIT
  const char* raw_small = "smol";
#else
  const char* raw_small = "smalls";
#endif  // V8_COMPRESS_POINTERS

  Handle<String> string =
      factory->InternalizeString(factory->NewStringFromAsciiChecked(raw_small));
  OneByteResource* resource =
      new OneByteResource(i::StrDup(raw_small), strlen(raw_small));

  // Check it is external, internalized, and uncached with a cacheable resource.
  string->MakeExternal(resource);
  CHECK(string->IsOneByteRepresentation());
  CHECK(IsExternalString(*string));
  CHECK(IsInternalizedString(*string));

  // Check that the external string is uncached, its resource is cacheable, and
  // that we indeed cached it.
  Handle<ExternalOneByteString> external_string =
      Handle<ExternalOneByteString>::cast(string);
  // If the sandbox is enabled, string objects will always be cacheable because
  // they are smaller.
  CHECK(V8_ENABLE_SANDBOX_BOOL || external_string->is_uncached());
  CHECK(external_string->resource()->IsCacheable());
  if (!V8_ENABLE_SANDBOX_BOOL) {
    CHECK_NOT_NULL(external_string->resource()->cached_data());
    CHECK_EQ(external_string->resource()->cached_data(),
             external_string->resource()->data());
  }
}

// Show that we cache the data pointer for internal, external and uncached
// strings with cacheable resources through MakeExternal. Two byte version.
TEST(CheckCachedDataInternalExternalUncachedStringTwoByte) {
  CcTest::InitializeVM();
  Factory* factory = CcTest::i_isolate()->factory();
  v8::HandleScope scope(CcTest::isolate());

  // Due to different size restrictions the string needs to be small but not too
  // small. One of these restrictions is whether pointer compression is enabled.
#ifdef V8_COMPRESS_POINTERS
  const char16_t* raw_small = u"smøl🤓";
#elif V8_TARGET_ARCH_32_BIT
  const char16_t* raw_small = u"🤓";
#else
  const char16_t* raw_small = u"s🤓";
#endif  // V8_COMPRESS_POINTERS

  size_t len;
  const uint16_t* two_byte = AsciiToTwoByteString(raw_small, &len);
  Handle<String> string = factory->InternalizeString(
      factory->NewStringFromTwoByte(base::VectorOf(two_byte, len))
          .ToHandleChecked());
  Resource* resource = new Resource(two_byte, len);

  // Check it is external, internalized, and uncached with a cacheable resource.
  string->MakeExternal(resource);
  CHECK(string->IsTwoByteRepresentation());
  CHECK(IsExternalString(*string));
  CHECK(IsInternalizedString(*string));

  // Check that the external string is uncached, its resource is cacheable, and
  // that we indeed cached it.
  Handle<ExternalTwoByteString> external_string =
      Handle<ExternalTwoByteString>::cast(string);
  // If the sandbox is enabled, string objects will always be cacheable because
  // they are smaller.
  CHECK(V8_ENABLE_SANDBOX_BOOL || external_string->is_uncached());
  CHECK(external_string->resource()->IsCacheable());
  if (!V8_ENABLE_SANDBOX_BOOL) {
    CHECK_NOT_NULL(external_string->resource()->cached_data());
    CHECK_EQ(external_string->resource()->cached_data(),
             external_string->resource()->data());
  }
}

}  // namespace test_strings
}  // namespace internal
}  // namespace v8
