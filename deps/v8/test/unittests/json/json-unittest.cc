// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <json/json.h>

#include "include/v8-json.h"
#include "test/unittests/fuzztest.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace {

class JSONTest : public fuzztest::PerFuzzTestFixtureAdapter<TestWithContext> {
 public:
  JSONTest() : context_(context()), isolate_(isolate()) {
    internal::v8_flags.expose_gc = true;
    Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    v8::Context::Scope context_scope(context_);
    v8::TryCatch try_catch(isolate_);
  }
  ~JSONTest() override = default;

  void ParseValidJsonP(const std::string&);

 private:
  Local<Context> context_;
  Isolate* isolate_;
};

// Utilities to construct and transform json values.

static Json::Value ToJsonArray(const std::vector<Json::Value>& vec) {
  Json::Value result(Json::arrayValue);
  for (auto elem : vec) {
    result.append(elem);
  }
  return result;
}

static Json::Value ToJsonObject(const std::map<std::string, Json::Value>& map) {
  Json::Value result(Json::objectValue);
  for (auto const& [key, val] : map) {
    result[key] = val;
  }
  return result;
}

static std::string ToJsonString(const Json::Value& val) {
  Json::StreamWriterBuilder wbuilder;
  return Json::writeString(wbuilder, val);
}

// FuzzTest domain construction.

static fuzztest::Domain<Json::Value> JustJsonNullPtr() {
  return fuzztest::Just(Json::Value());
}

template <typename T>
static fuzztest::Domain<Json::Value> ArbitraryJsonPrimitive() {
  return fuzztest::Map([](const T& val) { return Json::Value(val); },
                       fuzztest::Arbitrary<T>());
}

static fuzztest::Domain<Json::Value> LeafJson() {
  return fuzztest::OneOf(JustJsonNullPtr(), ArbitraryJsonPrimitive<bool>(),
                         ArbitraryJsonPrimitive<double>(),
                         ArbitraryJsonPrimitive<std::string>());
}

static fuzztest::Domain<Json::Value> ArbitraryJson() {
  fuzztest::DomainBuilder builder;
  auto leaf_domain = LeafJson();

  auto json_array = fuzztest::ContainerOf<std::vector<Json::Value>>(
      builder.Get<Json::Value>("json"));
  auto array_domain = fuzztest::Map(&ToJsonArray, json_array);

  auto json_object = fuzztest::MapOf(fuzztest::Arbitrary<std::string>(),
                                     builder.Get<Json::Value>("json"));
  auto object_domain = fuzztest::Map(&ToJsonObject, json_object);

  builder.Set<Json::Value>(
      "json", fuzztest::OneOf(leaf_domain, array_domain, object_domain));
  return std::move(builder).Finalize<Json::Value>("json");
}

// Fuzz tests.

void JSONTest::ParseValidJsonP(const std::string& input) {
  v8::Local<v8::String> source;

  if (!v8::String::NewFromUtf8(isolate_, input.c_str(),
                               v8::NewStringType::kNormal, input.size())
           .ToLocal(&source)) {
    return;
  }
  v8::JSON::Parse(context_, source).IsEmpty();
  isolate_->RequestGarbageCollectionForTesting(
      v8::Isolate::kFullGarbageCollection);
}

V8_FUZZ_TEST_F(JSONTest, ParseValidJsonP)
    .WithDomains(fuzztest::Map(&ToJsonString, ArbitraryJson()));

}  // namespace
}  // namespace v8
