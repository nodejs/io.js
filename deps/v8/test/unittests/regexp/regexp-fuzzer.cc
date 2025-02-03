// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp.h"
#include "test/unittests/fuzztest.h"
#include "test/unittests/regexp/regexp-grammar.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace {

using RegExpFlag = internal::RegExpFlag;

template <class T>
class RegExpTest : public fuzztest::PerFuzzTestFixtureAdapter<TestWithContext> {
 public:
  RegExpTest()
      : context_(context()),
        isolate_(isolate()),
        i_isolate_(reinterpret_cast<i::Isolate*>(isolate_)),
        factory_(i_isolate_->factory()) {
    internal::v8_flags.expose_gc = true;
  }
  ~RegExpTest() override = default;

  void RunRegExp(const std::string&, const i::RegExpFlags&,
                 const std::vector<T>&);

 protected:
  virtual i::Handle<i::String> CreateString(v8::base::Vector<const T>) = 0;
  void Test(i::DirectHandle<i::JSRegExp>, i::Handle<i::String>);

  Local<Context> context_;
  Isolate* isolate_;
  i::Isolate* i_isolate_;
  i::Factory* factory_;
};

// Domain over all combinations of regexp flags.
static fuzztest::Domain<i::RegExpFlags> ArbitraryFlags() {
  // The unicode and unicode_sets bits are incompatible.
  auto bits_supporting_unicode = fuzztest::BitFlagCombinationOf(
      {RegExpFlag::kHasIndices, RegExpFlag::kGlobal, RegExpFlag::kIgnoreCase,
       RegExpFlag::kMultiline, RegExpFlag::kSticky, RegExpFlag::kUnicode,
       RegExpFlag::kDotAll});
  auto bits_supporting_unicode_sets = fuzztest::BitFlagCombinationOf(
      {RegExpFlag::kHasIndices, RegExpFlag::kGlobal, RegExpFlag::kIgnoreCase,
       RegExpFlag::kMultiline, RegExpFlag::kSticky, RegExpFlag::kUnicodeSets,
       RegExpFlag::kDotAll});
  auto bits =
      fuzztest::OneOf(bits_supporting_unicode, bits_supporting_unicode_sets);
  auto flags = fuzztest::Map(
      [](auto bits) { return static_cast<i::RegExpFlags>(bits); }, bits);

  // Filter out any other incompatibilities.
  return fuzztest::Filter(
      [](i::RegExpFlags f) { return i::RegExp::VerifyFlags(f); }, flags);
}

// Domain over bytes for a test string to test regular expressions on.
// The resulting strings will consist of a fixed example, simple strings
// of just a, b and space, strings with printable ascii characters and
// strings with arbitrary characters.
template <typename T>
static fuzztest::Domain<std::vector<T>> ArbitraryBytes(
    const std::vector<T>& example) {
  auto signed_to_unsigned = [](const char& cr) { return static_cast<T>(cr); };

  auto just_example = fuzztest::Just(example);

  auto simple_char = fuzztest::Map(
      signed_to_unsigned,
      fuzztest::OneOf(fuzztest::InRange('a', 'b'), fuzztest::Just(' ')));
  auto simple_chars =
      fuzztest::ContainerOf<std::vector<T>>(simple_char).WithMaxSize(10);

  auto printable_char =
      fuzztest::Map(signed_to_unsigned, fuzztest::PrintableAsciiChar());
  auto printable_chars =
      fuzztest::ContainerOf<std::vector<T>>(printable_char).WithMaxSize(10);

  auto arbitrary_chars =
      fuzztest::ContainerOf<std::vector<T>>(fuzztest::Arbitrary<T>())
          .WithMaxSize(10);

  return fuzztest::OneOf(just_example, simple_chars, printable_chars,
                         arbitrary_chars);
}

static fuzztest::Domain<std::vector<uint8_t>> ArbitraryOneBytes() {
  return ArbitraryBytes<uint8_t>(
      std::vector<uint8_t>{'f', 'o', 'o', 'b', 'a', 'r'});
}

static fuzztest::Domain<std::vector<v8::base::uc16>> ArbitraryTwoBytes() {
  return ArbitraryBytes<v8::base::uc16>(
      std::vector<v8::base::uc16>{'f', 0xD83D, 0xDCA9, 'b', 'a', 0x2603});
}

template <class T>
void RegExpTest<T>::Test(i::DirectHandle<i::JSRegExp> regexp,
                         i::Handle<i::String> subject) {
  v8::TryCatch try_catch(isolate_);
  // Exceptions will be swallowed by the try/catch above.
  USE(i::RegExp::Exec(i_isolate_, regexp, subject, 0,
                      i::RegExpMatchInfo::New(i_isolate_, 2)));
}

template <class T>
void RegExpTest<T>::RunRegExp(const std::string& regexp_input,
                              const i::RegExpFlags& flags,
                              const std::vector<T>& test_input) {
  CHECK(!i_isolate_->has_exception());
  if (regexp_input.size() > INT_MAX) return;

  // Convert input string.
  i::MaybeHandle<i::String> maybe_source =
      factory_->NewStringFromUtf8(v8::base::CStrVector(regexp_input.c_str()));
  i::Handle<i::String> source;
  if (!maybe_source.ToHandle(&source)) {
    i_isolate_->clear_exception();
    return;
  }

  // Create regexp.
  i::Handle<i::JSRegExp> regexp;
  {
    CHECK(!i_isolate_->has_exception());
    v8::TryCatch try_catch_inner(isolate_);
    i::MaybeHandle<i::JSRegExp> maybe_regexp = i::JSRegExp::New(
        i_isolate_, source, i::JSRegExp::AsJSRegExpFlags(flags),
        /*backtrack_limit*/ 1000000);
    if (!maybe_regexp.ToHandle(&regexp)) {
      i_isolate_->clear_exception();
      return;
    }
  }

  // Convert input bytes for the subject string.
  auto subject = CreateString(
      v8::base::Vector<const T>(test_input.data(), test_input.size()));

  // Test the regexp on the subject, itself and an empty string.
  Test(regexp, subject);
  Test(regexp, source);
  Test(regexp, factory_->empty_string());

  isolate_->RequestGarbageCollectionForTesting(
      v8::Isolate::kFullGarbageCollection);
  CHECK(!i_isolate_->has_exception());
}

class RegExpOneByteTest : public RegExpTest<uint8_t> {
 protected:
  i::Handle<i::String> CreateString(
      v8::base::Vector<const uint8_t> test_input) {
    return factory_->NewStringFromOneByte(test_input).ToHandleChecked();
  }
};

V8_FUZZ_TEST_F(RegExpOneByteTest, RunRegExp)
    .WithDomains(fuzztest::internal_no_adl::InPatternGrammar(),
                 ArbitraryFlags(), ArbitraryOneBytes());

class RegExpTwoByteTest : public RegExpTest<v8::base::uc16> {
 protected:
  i::Handle<i::String> CreateString(
      v8::base::Vector<const v8::base::uc16> test_input) {
    return factory_->NewStringFromTwoByte(test_input).ToHandleChecked();
  }
};

V8_FUZZ_TEST_F(RegExpTwoByteTest, RunRegExp)
    .WithDomains(fuzztest::internal_no_adl::InPatternGrammar(),
                 ArbitraryFlags(), ArbitraryTwoBytes());

}  // namespace
}  // namespace v8
