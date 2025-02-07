// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "protocol_core.h"

#include <memory>

#include "cbor.h"
#include "maybe.h"
#include "status_test_support.h"
#include "test_platform.h"
#include "test_string_traits.h"

namespace crdtp {

namespace {
using ::testing::Eq;

template <typename TResult, typename TArg>
std::unique_ptr<TResult> RoundtripToType(const TArg& obj) {
  std::vector<uint8_t> bytes;
  obj.AppendSerialized(&bytes);

  StatusOr<std::unique_ptr<TResult>> result =
      TResult::ReadFrom(std::move(bytes));
  return std::move(result).value();
}

template <typename T>
std::unique_ptr<T> Roundtrip(const T& obj) {
  return RoundtripToType<T, T>(obj);
}

// These TestTypeFOO classes below would normally be generated
// by the protocol generator.

class TestTypeBasic : public ProtocolObject<TestTypeBasic> {
 public:
  TestTypeBasic() = default;

  const std::string& GetValue() const { return value_; }
  void SetValue(std::string value) { value_ = std::move(value); }

 private:
  DECLARE_SERIALIZATION_SUPPORT();

  std::string value_;
};

// clang-format off
CRDTP_BEGIN_DESERIALIZER(TestTypeBasic)
  CRDTP_DESERIALIZE_FIELD("value", value_)
CRDTP_END_DESERIALIZER()

CRDTP_BEGIN_SERIALIZER(TestTypeBasic)
  CRDTP_SERIALIZE_FIELD("value", value_);
CRDTP_END_SERIALIZER();
// clang-format on

TEST(ProtocolCoreTest, Basic) {
  TestTypeBasic obj1;
  obj1.SetValue("foo");

  auto obj2 = Roundtrip(obj1);
  ASSERT_THAT(obj2, Not(testing::IsNull()));
  EXPECT_THAT(obj2->GetValue(), Eq("foo"));
}

TEST(ProtocolCoreTest, FailedToDeserializeTestTypeBasic) {
  std::vector<uint8_t> garbage = {'g', 'a', 'r', 'b', 'a', 'g', 'e'};
  StatusOr<std::unique_ptr<TestTypeBasic>> result =
      TestTypeBasic::ReadFrom(std::move(garbage));
  EXPECT_THAT(result.status(), StatusIs(Error::CBOR_INVALID_STRING8, 0));
}

class TestTypeBasicDouble : public ProtocolObject<TestTypeBasicDouble> {
 public:
  TestTypeBasicDouble() = default;

  double GetValue() const { return value_; }
  void SetValue(double value) { value_ = value; }

 private:
  DECLARE_SERIALIZATION_SUPPORT();

  double value_;
};

// clang-format off
CRDTP_BEGIN_DESERIALIZER(TestTypeBasicDouble)
  CRDTP_DESERIALIZE_FIELD("value", value_)
CRDTP_END_DESERIALIZER()

CRDTP_BEGIN_SERIALIZER(TestTypeBasicDouble)
  CRDTP_SERIALIZE_FIELD("value", value_);
CRDTP_END_SERIALIZER();
// clang-format on

TEST(TestBasicDouble, ParserAllowsAllowsDoubleEncodedAsInt) {
  // We allow double's encoded as INT32, because this is what a roundtrip via
  // JSON would produce.
  std::vector<uint8_t> encoded;
  crdtp::cbor::EnvelopeEncoder envelope;
  envelope.EncodeStart(&encoded);
  encoded.push_back(crdtp::cbor::EncodeIndefiniteLengthMapStart());
  crdtp::cbor::EncodeString8(crdtp::SpanFrom("value"), &encoded);
  crdtp::cbor::EncodeInt32(
      42, &encoded);  // It's a double field, but we encode an int.
  encoded.push_back(crdtp::cbor::EncodeStop());
  envelope.EncodeStop(&encoded);
  auto obj = TestTypeBasicDouble::ReadFrom(encoded).value();
  ASSERT_THAT(obj, Not(testing::IsNull()));
  EXPECT_THAT(obj->GetValue(), Eq(42));
}

class TestTypeComposite : public ProtocolObject<TestTypeComposite> {
 public:
  bool GetBoolField() const { return bool_field_; }
  void SetBoolField(bool value) { bool_field_ = value; }

  int GetIntField() const { return int_field_; }
  void SetIntField(int value) { int_field_ = value; }

  double GetDoubleField() const { return double_field_; }
  void SetDoubleField(double value) { double_field_ = value; }

  const std::string& GetStrField() const { return str_field_; }
  void SetStrField(std::string value) { str_field_ = std::move(value); }

  const TestTypeBasic* GetTestTypeBasicField() {
    return test_type1_field_.get();
  }
  void SetTestTypeBasicField(std::unique_ptr<TestTypeBasic> value) {
    test_type1_field_ = std::move(value);
  }

 private:
  DECLARE_SERIALIZATION_SUPPORT();

  bool bool_field_ = false;
  int int_field_ = 0;
  double double_field_ = 0.0;
  std::string str_field_;
  std::unique_ptr<TestTypeBasic> test_type1_field_;
};

// clang-format off
CRDTP_BEGIN_DESERIALIZER(TestTypeComposite)
  CRDTP_DESERIALIZE_FIELD("bool_field", bool_field_),
  CRDTP_DESERIALIZE_FIELD("double_field", double_field_),
  CRDTP_DESERIALIZE_FIELD("int_field", int_field_),
  CRDTP_DESERIALIZE_FIELD("str_field", str_field_),
  CRDTP_DESERIALIZE_FIELD("test_type1_field", test_type1_field_),
CRDTP_END_DESERIALIZER()

CRDTP_BEGIN_SERIALIZER(TestTypeComposite)
  CRDTP_SERIALIZE_FIELD("bool_field", bool_field_),
  CRDTP_SERIALIZE_FIELD("double_field", double_field_),
  CRDTP_SERIALIZE_FIELD("int_field", int_field_),
  CRDTP_SERIALIZE_FIELD("str_field", str_field_),
  CRDTP_SERIALIZE_FIELD("test_type1_field", test_type1_field_),
CRDTP_END_SERIALIZER();
// clang-format on

TEST(ProtocolCoreTest, Composite) {
  TestTypeComposite obj1;
  obj1.SetBoolField(true);
  obj1.SetIntField(42);
  obj1.SetDoubleField(2.718281828);
  obj1.SetStrField("bar");
  auto val1 = std::make_unique<TestTypeBasic>();
  val1->SetValue("bazzzz");
  obj1.SetTestTypeBasicField(std::move(val1));

  auto obj2 = Roundtrip(obj1);
  ASSERT_THAT(obj2, Not(testing::IsNull()));
  EXPECT_THAT(obj2->GetBoolField(), Eq(true));
  EXPECT_THAT(obj2->GetIntField(), Eq(42));
  EXPECT_THAT(obj2->GetDoubleField(), Eq(2.718281828));
  EXPECT_THAT(obj2->GetStrField(), Eq("bar"));
  EXPECT_THAT(obj2->GetTestTypeBasicField()->GetValue(), Eq("bazzzz"));
}

class CompositeParsingTest : public testing::Test {
 public:
  CompositeParsingTest() {
    TestTypeComposite top;
    top.SetIntField(42);
    top.SetBoolField(true);
    top.SetIntField(42);
    top.SetDoubleField(2.718281828);
    top.SetStrField("junk");
    auto child = std::make_unique<TestTypeBasic>();
    child->SetValue("child_value");
    top.SetTestTypeBasicField(std::move(child));

    // Let's establish that |serialized_| is a properly serialized
    // representation of |top|, by checking that it deserializes ok.
    top.AppendSerialized(&serialized_);
    TestTypeComposite::ReadFrom(serialized_).value();
  }

 protected:
  std::vector<uint8_t> serialized_;
};

TEST_F(CompositeParsingTest, DecodingFailure_CBORTokenizer) {
  // Mutates |serialized_| so that it won't parse correctly. In this case,
  // we're changing a string value so that it's invalid, making CBORTokenizer
  // unhappy.
  size_t position =
      std::string(reinterpret_cast<const char*>(serialized_.data()),
                  serialized_.size())
          .find("child_value");
  EXPECT_GT(position, 0ul);
  // We override the byte just before so that it's still a string
  // (3 << 5), but the length is encoded in the bytes that follows.
  // So, we override that with 0xff (255), which exceeds the length
  // of the message and thereby makes the string8 invalid.
  --position;
  serialized_[position] = 3 << 5 |   // major type: STRING
                          25;        // length in encoded in byte that follows.
  serialized_[position + 1] = 0xff;  // length
  auto result = TestTypeComposite::ReadFrom(serialized_);

  EXPECT_THAT(result.status(), StatusIs(Error::CBOR_INVALID_STRING8, position));
}

TEST_F(CompositeParsingTest, DecodingFailure_MandatoryFieldMissingShallow) {
  // We're changing the string key "int_field" to something else ("lnt_field"),
  // so that the mandatory field value won't be found. Unknown fields are
  // ignored for compatibility, so that's why this simple technique works here.
  size_t position =
      std::string(reinterpret_cast<const char*>(serialized_.data()),
                  serialized_.size())
          .find("int_field");
  serialized_[position] = 'l';  // Change 'i' to 'l'.
  // serialized_.size() - 1 is the STOP character for the entire message,
  size_t expected_error_pos = serialized_.size() - 1;
  auto result = TestTypeComposite::ReadFrom(serialized_);
  EXPECT_THAT(result.status(), StatusIs(Error::BINDINGS_MANDATORY_FIELD_MISSING,
                                        expected_error_pos));
}

TEST_F(CompositeParsingTest, DecodingFailure_MandatoryFieldMissingNested) {
  // We're changing the string key "value" to something else ("falue"), so that
  // the mandatory field value in TestTypeBasic in the child won't be found.
  size_t position =
      std::string(reinterpret_cast<const char*>(serialized_.data()),
                  serialized_.size())
          .find("value");
  serialized_[position] = 'f';  // Change 'v' to 'f'.
  // serialized_.size() - 1 is the STOP character for the enclosing message,
  // and serialized_.size() - 2 is the STOP character for TestTypeBasic.
  size_t expected_error_pos = serialized_.size() - 2;
  auto result = TestTypeComposite::ReadFrom(serialized_);
  EXPECT_THAT(result.status(), StatusIs(Error::BINDINGS_MANDATORY_FIELD_MISSING,
                                        expected_error_pos));
}

TEST_F(CompositeParsingTest, DecodingFailure_BoolValueExpected) {
  // We're changing the bool value (true) to null; we do this by looking
  // for bool_field, and searching from there for TRUE; both TRUE and null
  // are just one byte in the serialized buffer, so this swap is convenient.
  std::string serialized_view(reinterpret_cast<const char*>(serialized_.data()),
                              serialized_.size());
  size_t position = serialized_view.find("bool_field");
  for (; position < serialized_.size(); ++position) {
    if (serialized_[position] == crdtp::cbor::EncodeTrue()) {
      serialized_[position] = crdtp::cbor::EncodeNull();
      break;
    }
  }
  auto result = TestTypeComposite::ReadFrom(serialized_);
  EXPECT_THAT(result.status(),
              StatusIs(Error::BINDINGS_BOOL_VALUE_EXPECTED, position));
}

class TestTypeArrays : public ProtocolObject<TestTypeArrays> {
 public:
  const std::vector<int>* GetIntArray() const { return &int_array_; }
  void SetIntArray(std::vector<int> value) { int_array_ = std::move(value); }

  const std::vector<double>* GetDoubleArray() const { return &double_array_; }
  void SetDoubleArray(std::vector<double> value) {
    double_array_ = std::move(value);
  }

  const std::vector<std::string>* GetStrArray() const { return &str_array_; }
  void SetStrArray(std::vector<std::string> value) {
    str_array_ = std::move(value);
  }

  const std::vector<std::unique_ptr<TestTypeBasic>>* GetTestTypeBasicArray()
      const {
    return &test_type_basic_array_;
  }

  void SetTestTypeBasicArray(
      std::vector<std::unique_ptr<TestTypeBasic>> value) {
    test_type_basic_array_ = std::move(value);
  }

 private:
  DECLARE_SERIALIZATION_SUPPORT();

  std::vector<int> int_array_;
  std::vector<double> double_array_;
  std::vector<std::string> str_array_;
  std::vector<std::unique_ptr<TestTypeBasic>> test_type_basic_array_;
};

// clang-format off
CRDTP_BEGIN_DESERIALIZER(TestTypeArrays)
  CRDTP_DESERIALIZE_FIELD("int_array", int_array_),
  CRDTP_DESERIALIZE_FIELD("str_array", str_array_),
  CRDTP_DESERIALIZE_FIELD("test_type_basic_array", test_type_basic_array_),
CRDTP_END_DESERIALIZER()

CRDTP_BEGIN_SERIALIZER(TestTypeArrays)
  CRDTP_SERIALIZE_FIELD("int_array", int_array_),
  CRDTP_SERIALIZE_FIELD("str_array", str_array_),
  CRDTP_SERIALIZE_FIELD("test_type_basic_array", test_type_basic_array_),
CRDTP_END_SERIALIZER();
// clang-format on

TEST_F(CompositeParsingTest, Arrays) {
  TestTypeArrays obj1;
  obj1.SetIntArray(std::vector<int>{1, 3, 5, 7});
  std::vector<std::string> strs;
  strs.emplace_back("foo");
  strs.emplace_back(std::string("bar"));
  obj1.SetStrArray(std::move(strs));
  auto val1 = std::make_unique<TestTypeBasic>();
  val1->SetValue("bazzzz");
  std::vector<std::unique_ptr<TestTypeBasic>> vec1;
  vec1.emplace_back(std::move(val1));
  obj1.SetTestTypeBasicArray(std::move(vec1));

  auto obj2 = Roundtrip(obj1);
  ASSERT_THAT(obj2, Not(testing::IsNull()));
  EXPECT_THAT(*obj2->GetIntArray(), testing::ElementsAre(1, 3, 5, 7));
  EXPECT_THAT(*obj2->GetStrArray(), testing::ElementsAre("foo", "bar"));
  EXPECT_THAT(obj2->GetDoubleArray()->size(), Eq(0ul));
  EXPECT_THAT(obj2->GetTestTypeBasicArray()->size(), Eq(1ul));
  EXPECT_THAT(obj2->GetTestTypeBasicArray()->front()->GetValue(), Eq("bazzzz"));
}

class TestTypeOptional : public ProtocolObject<TestTypeOptional> {
 public:
  TestTypeOptional() = default;

  bool HasIntField() const { return int_field_.has_value(); }
  int GetIntField() const { return int_field_.value(); }
  void SetIntField(int value) { int_field_ = value; }

  bool HasStrField() { return str_field_.has_value(); }
  const std::string& GetStrField() const { return str_field_.value(); }
  void SetStrField(std::string value) { str_field_ = std::move(value); }

  bool HasTestTypeBasicField() { return test_type_basic_field_.has_value(); }
  const TestTypeBasic* GetTestTypeBasicField() const {
    return test_type_basic_field_.has_value() ? &test_type_basic_field_.value()
                                              : nullptr;
  }
  void SetTestTypeBasicField(std::unique_ptr<TestTypeBasic> value) {
    test_type_basic_field_ = std::move(value);
  }

 private:
  DECLARE_SERIALIZATION_SUPPORT();

  Maybe<int> int_field_;
  Maybe<std::string> str_field_;
  Maybe<TestTypeBasic> test_type_basic_field_;
};

// clang-format off
CRDTP_BEGIN_DESERIALIZER(TestTypeOptional)
  CRDTP_DESERIALIZE_FIELD_OPT("int_field", int_field_),
  CRDTP_DESERIALIZE_FIELD_OPT("str_field", str_field_),
  CRDTP_DESERIALIZE_FIELD_OPT("test_type_basic_field", test_type_basic_field_),
CRDTP_END_DESERIALIZER()

CRDTP_BEGIN_SERIALIZER(TestTypeOptional)
  CRDTP_SERIALIZE_FIELD("int_field", int_field_),
  CRDTP_SERIALIZE_FIELD("str_field", str_field_),
  CRDTP_SERIALIZE_FIELD("test_type_basic_field", test_type_basic_field_),
CRDTP_END_SERIALIZER();
// clang-format on

TEST(ProtocolCoreTest, OptionalAbsent) {
  TestTypeOptional obj1;
  auto obj2 = Roundtrip(obj1);
  ASSERT_THAT(obj2, Not(testing::IsNull()));

  EXPECT_THAT(obj2->HasIntField(), Eq(false));
  EXPECT_THAT(obj2->HasStrField(), Eq(false));
  EXPECT_THAT(obj2->HasTestTypeBasicField(), Eq(false));
}

TEST(ProtocolCoreTest, OptionalPresent) {
  TestTypeOptional obj1;
  obj1.SetIntField(42);
  obj1.SetStrField("foo");

  auto val1 = std::make_unique<TestTypeBasic>();
  val1->SetValue("bar");
  obj1.SetTestTypeBasicField(std::move(val1));

  auto obj2 = Roundtrip(obj1);
  ASSERT_THAT(obj2, Not(testing::IsNull()));

  EXPECT_THAT(obj2->HasIntField(), Eq(true));
  EXPECT_THAT(obj2->GetIntField(), Eq(42));
  EXPECT_THAT(obj2->HasStrField(), Eq(true));
  EXPECT_THAT(obj2->GetStrField(), Eq("foo"));
  EXPECT_THAT(obj2->HasTestTypeBasicField(), Eq(true));
  EXPECT_THAT(obj2->GetTestTypeBasicField()->GetValue(), Eq("bar"));
}

class TestTypeLazy : public ProtocolObject<TestTypeLazy> {
 public:
  TestTypeLazy() = default;

  const std::string& GetStrField() const { return str_field_; }
  void SetStrField(std::string value) { str_field_ = std::move(value); }

  const DeferredMessage* deferred_test_type1_field() const {
    return test_type1_field_.get();
  }

 private:
  DECLARE_SERIALIZATION_SUPPORT();

  std::string str_field_;
  std::unique_ptr<DeferredMessage> test_type1_field_;
};

// clang-format off
CRDTP_BEGIN_DESERIALIZER(TestTypeLazy)
  CRDTP_DESERIALIZE_FIELD("str_field", str_field_),
  CRDTP_DESERIALIZE_FIELD_OPT("test_type1_field", test_type1_field_),
CRDTP_END_DESERIALIZER()

CRDTP_BEGIN_SERIALIZER(TestTypeLazy)
  CRDTP_SERIALIZE_FIELD("str_field", str_field_),
  CRDTP_SERIALIZE_FIELD("test_type1_field", test_type1_field_),
CRDTP_END_SERIALIZER();
// clang-format on

TEST(ProtocolCoreTest, TestDeferredMessage) {
  TestTypeComposite obj1;
  obj1.SetStrField("bar");
  auto val1 = std::make_unique<TestTypeBasic>();
  val1->SetValue("bazzzz");
  obj1.SetTestTypeBasicField(std::move(val1));

  auto obj2 = RoundtripToType<TestTypeLazy>(obj1);
  EXPECT_THAT(obj2->GetStrField(), Eq("bar"));

  TestTypeBasic basic_val;
  auto deserializer = obj2->deferred_test_type1_field()->MakeDeserializer();
  EXPECT_THAT(TestTypeBasic::Deserialize(&deserializer, &basic_val), Eq(true));
  EXPECT_THAT(basic_val.GetValue(), Eq("bazzzz"));

  StatusOr<std::unique_ptr<TestTypeBasic>> maybe_parsed =
      TestTypeBasic::ReadFrom(*obj2->deferred_test_type1_field());
  ASSERT_THAT(maybe_parsed.status(), StatusIsOk());
  ASSERT_THAT((*maybe_parsed), Not(testing::IsNull()));
  ASSERT_EQ((*maybe_parsed)->GetValue(), "bazzzz");
}

}  // namespace
}  // namespace crdtp
