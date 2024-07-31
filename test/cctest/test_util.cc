#include "debug_utils-inl.h"
#include "env-inl.h"
#include "gtest/gtest.h"
#include "node_options-inl.h"
#include "node_test_fixture.h"
#include "simdutf.h"
#include "util-inl.h"
#include "v8-function-callback.h"
#include "v8-primitive.h"
#include "v8.h"

using node::Calloc;
using node::Malloc;
using node::MaybeStackBuffer;
using node::SPrintF;
using node::StringEqualNoCase;
using node::StringEqualNoCaseN;
using node::ToLower;
using node::UncheckedCalloc;
using node::UncheckedMalloc;

class UtilTest : public EnvironmentTestFixture {};

TEST_F(UtilTest, ListHead) {
  struct Item { node::ListNode<Item> node_; };
  typedef node::ListHead<Item, &Item::node_> List;

  List list;
  EXPECT_TRUE(list.IsEmpty());

  Item one;
  EXPECT_TRUE(one.node_.IsEmpty());

  list.PushBack(&one);
  EXPECT_FALSE(list.IsEmpty());
  EXPECT_FALSE(one.node_.IsEmpty());

  {
    List::Iterator it = list.begin();
    EXPECT_NE(list.end(), it);
    EXPECT_EQ(&one, *it);
    ++it;
    EXPECT_FALSE(it != list.end());  // Iterator only implements != operator.
  }

  Item two;
  list.PushBack(&two);

  {
    List::Iterator it = list.begin();
    EXPECT_NE(list.end(), it);
    EXPECT_EQ(&one, *it);
    ++it;
    EXPECT_NE(list.end(), it);
    EXPECT_EQ(&two, *it);
    ++it;
    EXPECT_FALSE(it != list.end());  // Iterator only implements != operator.
  }

  EXPECT_EQ(&one, list.PopFront());
  EXPECT_TRUE(one.node_.IsEmpty());
  EXPECT_FALSE(list.IsEmpty());

  {
    List::Iterator it = list.begin();
    EXPECT_NE(list.end(), it);
    EXPECT_EQ(&two, *it);
    ++it;
    EXPECT_FALSE(it != list.end());  // Iterator only implements != operator.
  }

  EXPECT_EQ(&two, list.PopFront());
  EXPECT_TRUE(two.node_.IsEmpty());
  EXPECT_TRUE(list.IsEmpty());
  EXPECT_FALSE(list.begin() != list.end());
}

TEST_F(UtilTest, StringEqualNoCase) {
  EXPECT_FALSE(StringEqualNoCase("a", "b"));
  EXPECT_TRUE(StringEqualNoCase("", ""));
  EXPECT_TRUE(StringEqualNoCase("equal", "equal"));
  EXPECT_TRUE(StringEqualNoCase("equal", "EQUAL"));
  EXPECT_TRUE(StringEqualNoCase("EQUAL", "EQUAL"));
  EXPECT_FALSE(StringEqualNoCase("equal", "equals"));
  EXPECT_FALSE(StringEqualNoCase("equals", "equal"));
}

TEST_F(UtilTest, StringEqualNoCaseN) {
  EXPECT_FALSE(StringEqualNoCaseN("a", "b", strlen("a")));
  EXPECT_TRUE(StringEqualNoCaseN("", "", strlen("")));
  EXPECT_TRUE(StringEqualNoCaseN("equal", "equal", strlen("equal")));
  EXPECT_TRUE(StringEqualNoCaseN("equal", "EQUAL", strlen("equal")));
  EXPECT_TRUE(StringEqualNoCaseN("EQUAL", "EQUAL", strlen("equal")));
  EXPECT_TRUE(StringEqualNoCaseN("equal", "equals", strlen("equal")));
  EXPECT_FALSE(StringEqualNoCaseN("equal", "equals", strlen("equals")));
  EXPECT_TRUE(StringEqualNoCaseN("equals", "equal", strlen("equal")));
  EXPECT_FALSE(StringEqualNoCaseN("equals", "equal", strlen("equals")));
  EXPECT_TRUE(StringEqualNoCaseN("abc\0abc", "abc\0efg", strlen("abcdefgh")));
  EXPECT_FALSE(StringEqualNoCaseN("abc\0abc", "abcd\0efg", strlen("abcdefgh")));
}

TEST_F(UtilTest, ToLower) {
  EXPECT_EQ('0', ToLower('0'));
  EXPECT_EQ('a', ToLower('a'));
  EXPECT_EQ('a', ToLower('A'));
}

#define TEST_AND_FREE(expression, size)                                        \
  do {                                                                         \
    auto pointer = expression(size);                                           \
    EXPECT_EQ(pointer == nullptr, size == 0);                                  \
    free(pointer);                                                             \
  } while (0)

TEST_F(UtilTest, Malloc) {
  TEST_AND_FREE(Malloc<char>, 0);
  TEST_AND_FREE(Malloc<char>, 1);
  TEST_AND_FREE(Malloc, 0);
  TEST_AND_FREE(Malloc, 1);
}

TEST_F(UtilTest, Calloc) {
  TEST_AND_FREE(Calloc<char>, 0);
  TEST_AND_FREE(Calloc<char>, 1);
  TEST_AND_FREE(Calloc, 0);
  TEST_AND_FREE(Calloc, 1);
}

TEST_F(UtilTest, UncheckedMalloc) {
  TEST_AND_FREE(UncheckedMalloc<char>, 0);
  TEST_AND_FREE(UncheckedMalloc<char>, 1);
  TEST_AND_FREE(UncheckedMalloc, 0);
  TEST_AND_FREE(UncheckedMalloc, 1);
}

TEST_F(UtilTest, UncheckedCalloc) {
  TEST_AND_FREE(UncheckedCalloc<char>, 0);
  TEST_AND_FREE(UncheckedCalloc<char>, 1);
  TEST_AND_FREE(UncheckedCalloc, 0);
  TEST_AND_FREE(UncheckedCalloc, 1);
}

template <typename T>
static void MaybeStackBufferBasic() {
  MaybeStackBuffer<T> buf;
  size_t old_length;
  size_t old_capacity;

  // Default constructor.
  EXPECT_EQ(0U, buf.length());
  EXPECT_FALSE(buf.IsAllocated());
  EXPECT_GT(buf.capacity(), buf.length());

  // SetLength() expansion.
  buf.SetLength(buf.capacity());
  EXPECT_EQ(buf.capacity(), buf.length());
  EXPECT_FALSE(buf.IsAllocated());

  // Means of accessing raw buffer.
  EXPECT_EQ(buf.out(), *buf);
  EXPECT_EQ(&buf[0], *buf);

  // Basic I/O.
  for (size_t i = 0; i < buf.length(); i++)
    buf[i] = static_cast<T>(i);
  for (size_t i = 0; i < buf.length(); i++)
    EXPECT_EQ(static_cast<T>(i), buf[i]);

  // SetLengthAndZeroTerminate().
  buf.SetLengthAndZeroTerminate(buf.capacity() - 1);
  EXPECT_EQ(buf.capacity() - 1, buf.length());
  for (size_t i = 0; i < buf.length(); i++)
    EXPECT_EQ(static_cast<T>(i), buf[i]);
  buf.SetLength(buf.capacity());
  EXPECT_EQ(0, buf[buf.length() - 1]);

  // Initial Realloc.
  old_length = buf.length() - 1;
  old_capacity = buf.capacity();
  buf.AllocateSufficientStorage(buf.capacity() * 2);
  EXPECT_EQ(buf.capacity(), buf.length());
  EXPECT_TRUE(buf.IsAllocated());
  for (size_t i = 0; i < old_length; i++)
    EXPECT_EQ(static_cast<T>(i), buf[i]);
  EXPECT_EQ(0, buf[old_length]);

  // SetLength() reduction and expansion.
  for (size_t i = 0; i < buf.length(); i++)
    buf[i] = static_cast<T>(i);
  buf.SetLength(10);
  for (size_t i = 0; i < buf.length(); i++)
    EXPECT_EQ(static_cast<T>(i), buf[i]);
  buf.SetLength(buf.capacity());
  for (size_t i = 0; i < buf.length(); i++)
    EXPECT_EQ(static_cast<T>(i), buf[i]);

  // Subsequent Realloc.
  old_length = buf.length();
  old_capacity = buf.capacity();
  buf.AllocateSufficientStorage(old_capacity * 1.5);
  EXPECT_EQ(buf.capacity(), buf.length());
  EXPECT_EQ(static_cast<size_t>(old_capacity * 1.5), buf.length());
  EXPECT_TRUE(buf.IsAllocated());
  for (size_t i = 0; i < old_length; i++)
    EXPECT_EQ(static_cast<T>(i), buf[i]);

  // Basic I/O on Realloc'd buffer.
  for (size_t i = 0; i < buf.length(); i++)
    buf[i] = static_cast<T>(i);
  for (size_t i = 0; i < buf.length(); i++)
    EXPECT_EQ(static_cast<T>(i), buf[i]);

  // Release().
  T* rawbuf = buf.out();
  buf.Release();
  EXPECT_EQ(0U, buf.length());
  EXPECT_FALSE(buf.IsAllocated());
  EXPECT_GT(buf.capacity(), buf.length());
  free(rawbuf);
}

TEST_F(UtilTest, MaybeStackBuffer) {
  MaybeStackBufferBasic<uint8_t>();
  MaybeStackBufferBasic<uint16_t>();

  // Constructor with size parameter.
  {
    MaybeStackBuffer<unsigned char> buf(100);
    EXPECT_EQ(100U, buf.length());
    EXPECT_FALSE(buf.IsAllocated());
    EXPECT_GT(buf.capacity(), buf.length());
    buf.SetLength(buf.capacity());
    EXPECT_EQ(buf.capacity(), buf.length());
    EXPECT_FALSE(buf.IsAllocated());
    for (size_t i = 0; i < buf.length(); i++)
      buf[i] = static_cast<unsigned char>(i);
    for (size_t i = 0; i < buf.length(); i++)
      EXPECT_EQ(static_cast<unsigned char>(i), buf[i]);

    MaybeStackBuffer<unsigned char> bigbuf(10000);
    EXPECT_EQ(10000U, bigbuf.length());
    EXPECT_TRUE(bigbuf.IsAllocated());
    EXPECT_EQ(bigbuf.length(), bigbuf.capacity());
    for (size_t i = 0; i < bigbuf.length(); i++)
      bigbuf[i] = static_cast<unsigned char>(i);
    for (size_t i = 0; i < bigbuf.length(); i++)
      EXPECT_EQ(static_cast<unsigned char>(i), bigbuf[i]);
  }

  // Invalidated buffer.
  {
    MaybeStackBuffer<char> buf;
    buf.Invalidate();
    EXPECT_TRUE(buf.IsInvalidated());
    EXPECT_FALSE(buf.IsAllocated());
    EXPECT_EQ(0U, buf.length());
    EXPECT_EQ(0U, buf.capacity());
    buf.Invalidate();
    EXPECT_TRUE(buf.IsInvalidated());
  }
}

TEST_F(UtilTest, SPrintF) {
  // %d, %u and %s all do the same thing. The actual C++ type is used to infer
  // the right representation.
  EXPECT_EQ(SPrintF("%s", false), "false");
  EXPECT_EQ(SPrintF("%s", true), "true");
  EXPECT_EQ(SPrintF("%d", true), "true");
  EXPECT_EQ(SPrintF("%u", true), "true");
  EXPECT_EQ(SPrintF("%d", 10000000000LL), "10000000000");
  EXPECT_EQ(SPrintF("%d", -10000000000LL), "-10000000000");
  EXPECT_EQ(SPrintF("%u", 10000000000LL), "10000000000");
  EXPECT_EQ(SPrintF("%u", -10000000000LL), "-10000000000");
  EXPECT_EQ(SPrintF("%i", 10), "10");
  EXPECT_EQ(SPrintF("%d", 10), "10");
  EXPECT_EQ(SPrintF("%x", 15), "f");
  EXPECT_EQ(SPrintF("%x", 16), "10");
  EXPECT_EQ(SPrintF("%X", 15), "F");
  EXPECT_EQ(SPrintF("%X", 16), "10");
  EXPECT_EQ(SPrintF("%o", 7), "7");
  EXPECT_EQ(SPrintF("%o", 8), "10");

  EXPECT_EQ(atof(SPrintF("%s", 0.5).c_str()), 0.5);
  EXPECT_EQ(atof(SPrintF("%s", -0.5).c_str()), -0.5);

  void (*fn)() = []() {};
  void* p = reinterpret_cast<void*>(&fn);
  EXPECT_GE(SPrintF("%p", fn).size(), 4u);
  EXPECT_GE(SPrintF("%p", p).size(), 4u);

  const std::string foo = "foo";
  const char* bar = "bar";
  EXPECT_EQ(SPrintF("%s %s", foo, "bar"), "foo bar");
  EXPECT_EQ(SPrintF("%s %s", foo, bar), "foo bar");
  EXPECT_EQ(SPrintF("%s", nullptr), "(null)");

  EXPECT_EQ(SPrintF("[%% %s %%]", foo), "[% foo %]");

  struct HasToString {
    std::string ToString() const {
      return "meow";
    }
  };
  EXPECT_EQ(SPrintF("%s", HasToString{}), "meow");

  const std::string with_zero = std::string("a") + '\0' + 'b';
  EXPECT_EQ(SPrintF("%s", with_zero), with_zero);
}

TEST_F(UtilTest, DumpJavaScriptStackWithNoIsolate) {
  node::DumpJavaScriptBacktrace(stderr);
}

TEST_F(UtilTest, DetermineSpecificErrorType) {
  const v8::HandleScope handle_scope(isolate_);
  Argv argv;
  Env env{handle_scope, argv, node::EnvironmentFlags::kNoBrowserGlobals};

  // Boolean
  EXPECT_EQ(
      node::DetermineSpecificErrorType(*env, v8::Boolean::New(isolate_, true)),
      "type boolean (true)");

  // BigInt
  EXPECT_EQ(
      node::DetermineSpecificErrorType(*env, v8::BigInt::New(isolate_, 255)),
      "type bigint (255)");

  // String
  EXPECT_EQ(
      node::DetermineSpecificErrorType(
          *env, v8::String::NewFromUtf8(isolate_, "input").ToLocalChecked()),
      "type string ('input')");
  // String that calls JSONStringify
  EXPECT_EQ(
      node::DetermineSpecificErrorType(
          *env, v8::String::NewFromUtf8(isolate_, "'input'").ToLocalChecked()),
      "type string (\"'input'\")");
  EXPECT_EQ(node::DetermineSpecificErrorType(
                *env,
                v8::String::NewFromUtf8(isolate_,
                                        "string with more than 26 characters")
                    .ToLocalChecked()),
            "type string ('string with more than 26 ...')");

  // Number, Int32, Uint32
  EXPECT_EQ(
      node::DetermineSpecificErrorType(*env, v8::Number::New(isolate_, 10)),
      "type number (10)");
  EXPECT_EQ(
      node::DetermineSpecificErrorType(*env, v8::Int32::New(isolate_, -255)),
      "type number (-255)");
  EXPECT_EQ(
      node::DetermineSpecificErrorType(*env, v8::Uint32::New(isolate_, 255)),
      "type number (255)");
}
