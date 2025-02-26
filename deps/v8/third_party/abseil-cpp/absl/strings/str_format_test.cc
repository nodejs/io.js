// Copyright 2020 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/str_format.h"

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {
using str_format_internal::FormatArgImpl;

using FormatEntryPointTest = ::testing::Test;

TEST_F(FormatEntryPointTest, Format) {
  std::string sink;
  EXPECT_TRUE(Format(&sink, "A format %d", 123));
  EXPECT_EQ("A format 123", sink);
  sink.clear();

  ParsedFormat<'d'> pc("A format %d");
  EXPECT_TRUE(Format(&sink, pc, 123));
  EXPECT_EQ("A format 123", sink);
}

TEST_F(FormatEntryPointTest, FormatWithV) {
  std::string sink;
  EXPECT_TRUE(Format(&sink, "A format %v", 123));
  EXPECT_EQ("A format 123", sink);
  sink.clear();

  ParsedFormat<'v'> pc("A format %v");
  EXPECT_TRUE(Format(&sink, pc, 123));
  EXPECT_EQ("A format 123", sink);
}

TEST_F(FormatEntryPointTest, UntypedFormat) {
  constexpr const char* formats[] = {
    "",
    "a",
    "%80d",
#if !defined(_MSC_VER) && !defined(__ANDROID__) && !defined(__native_client__)
    // MSVC, NaCL and Android don't support positional syntax.
    "complicated multipart %% %1$d format %1$0999d",
#endif  // _MSC_VER
  };
  for (const char* fmt : formats) {
    std::string actual;
    int i = 123;
    FormatArgImpl arg_123(i);
    absl::Span<const FormatArgImpl> args(&arg_123, 1);
    UntypedFormatSpec format(fmt);

    EXPECT_TRUE(FormatUntyped(&actual, format, args));
    char buf[4096]{};
    snprintf(buf, sizeof(buf), fmt, 123);
    EXPECT_EQ(
        str_format_internal::FormatPack(
            str_format_internal::UntypedFormatSpecImpl::Extract(format), args),
        buf);
    EXPECT_EQ(actual, buf);
  }
  // The internal version works with a preparsed format.
  ParsedFormat<'d'> pc("A format %d");
  int i = 345;
  FormatArg arg(i);
  std::string out;
  EXPECT_TRUE(str_format_internal::FormatUntyped(
      &out, str_format_internal::UntypedFormatSpecImpl(&pc), {&arg, 1}));
  EXPECT_EQ("A format 345", out);
}

TEST_F(FormatEntryPointTest, StringFormat) {
  EXPECT_EQ("123", StrFormat("%d", 123));
  constexpr absl::string_view view("=%d=", 4);
  EXPECT_EQ("=123=", StrFormat(view, 123));
}

TEST_F(FormatEntryPointTest, StringFormatV) {
  std::string hello = "hello";
  EXPECT_EQ("hello", StrFormat("%v", hello));
  EXPECT_EQ("123", StrFormat("%v", 123));
  constexpr absl::string_view view("=%v=", 4);
  EXPECT_EQ("=123=", StrFormat(view, 123));
}

TEST_F(FormatEntryPointTest, AppendFormat) {
  std::string s;
  std::string& r = StrAppendFormat(&s, "%d", 123);
  EXPECT_EQ(&s, &r);  // should be same object
  EXPECT_EQ("123", r);
}

TEST_F(FormatEntryPointTest, AppendFormatWithV) {
  std::string s;
  std::string& r = StrAppendFormat(&s, "%v", 123);
  EXPECT_EQ(&s, &r);  // should be same object
  EXPECT_EQ("123", r);
}

TEST_F(FormatEntryPointTest, AppendFormatFail) {
  std::string s = "orig";

  UntypedFormatSpec format(" more %d");
  FormatArgImpl arg("not an int");

  EXPECT_EQ("orig",
            str_format_internal::AppendPack(
                &s, str_format_internal::UntypedFormatSpecImpl::Extract(format),
                {&arg, 1}));
}

TEST_F(FormatEntryPointTest, AppendFormatFailWithV) {
  std::string s = "orig";

  UntypedFormatSpec format(" more %v");
  FormatArgImpl arg("not an int");

  EXPECT_EQ("orig",
            str_format_internal::AppendPack(
                &s, str_format_internal::UntypedFormatSpecImpl::Extract(format),
                {&arg, 1}));
}

TEST_F(FormatEntryPointTest, ManyArgs) {
  EXPECT_EQ(
      "60 59 58 57 56 55 54 53 52 51 50 49 48 47 46 45 44 43 42 41 40 39 38 37 "
      "36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 "
      "12 11 10 9 8 7 6 5 4 3 2 1",
      StrFormat("%60$d %59$d %58$d %57$d %56$d %55$d %54$d %53$d %52$d %51$d "
                "%50$d %49$d %48$d %47$d %46$d %45$d %44$d %43$d %42$d %41$d "
                "%40$d %39$d %38$d %37$d %36$d %35$d %34$d %33$d %32$d %31$d "
                "%30$d %29$d %28$d %27$d %26$d %25$d %24$d %23$d %22$d %21$d "
                "%20$d %19$d %18$d %17$d %16$d %15$d %14$d %13$d %12$d %11$d "
                "%10$d %9$d %8$d %7$d %6$d %5$d %4$d %3$d %2$d %1$d",
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
                35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
                51, 52, 53, 54, 55, 56, 57, 58, 59, 60));
}

TEST_F(FormatEntryPointTest, Preparsed) {
  ParsedFormat<'d'> pc("%d");
  EXPECT_EQ("123", StrFormat(pc, 123));
  // rvalue ok?
  EXPECT_EQ("123", StrFormat(ParsedFormat<'d'>("%d"), 123));
  constexpr absl::string_view view("=%d=", 4);
  EXPECT_EQ("=123=", StrFormat(ParsedFormat<'d'>(view), 123));
}

TEST_F(FormatEntryPointTest, PreparsedWithV) {
  ParsedFormat<'v'> pc("%v");
  EXPECT_EQ("123", StrFormat(pc, 123));
  // rvalue ok?
  EXPECT_EQ("123", StrFormat(ParsedFormat<'v'>("%v"), 123));
  constexpr absl::string_view view("=%v=", 4);
  EXPECT_EQ("=123=", StrFormat(ParsedFormat<'v'>(view), 123));
}

TEST_F(FormatEntryPointTest, FormatCountCapture) {
  int n = 0;
  EXPECT_EQ("", StrFormat("%n", FormatCountCapture(&n)));
  EXPECT_EQ(0, n);
  EXPECT_EQ("123", StrFormat("%d%n", 123, FormatCountCapture(&n)));
  EXPECT_EQ(3, n);
}

TEST_F(FormatEntryPointTest, FormatCountCaptureWithV) {
  int n = 0;
  EXPECT_EQ("", StrFormat("%n", FormatCountCapture(&n)));
  EXPECT_EQ(0, n);
  EXPECT_EQ("123", StrFormat("%v%n", 123, FormatCountCapture(&n)));
  EXPECT_EQ(3, n);
}

TEST_F(FormatEntryPointTest, FormatCountCaptureWrongType) {
  // Should reject int*.
  int n = 0;
  UntypedFormatSpec format("%d%n");
  int i = 123, *ip = &n;
  FormatArgImpl args[2] = {FormatArgImpl(i), FormatArgImpl(ip)};

  EXPECT_EQ("", str_format_internal::FormatPack(
                    str_format_internal::UntypedFormatSpecImpl::Extract(format),
                    absl::MakeSpan(args)));
}

TEST_F(FormatEntryPointTest, FormatCountCaptureWrongTypeWithV) {
  // Should reject int*.
  int n = 0;
  UntypedFormatSpec format("%v%n");
  int i = 123, *ip = &n;
  FormatArgImpl args[2] = {FormatArgImpl(i), FormatArgImpl(ip)};

  EXPECT_EQ("", str_format_internal::FormatPack(
                    str_format_internal::UntypedFormatSpecImpl::Extract(format),
                    absl::MakeSpan(args)));
}

TEST_F(FormatEntryPointTest, FormatCountCaptureMultiple) {
  int n1 = 0;
  int n2 = 0;
  EXPECT_EQ("    1         2",
            StrFormat("%5d%n%10d%n", 1, FormatCountCapture(&n1), 2,
                      FormatCountCapture(&n2)));
  EXPECT_EQ(5, n1);
  EXPECT_EQ(15, n2);
}

TEST_F(FormatEntryPointTest, FormatCountCaptureExample) {
  int n;
  std::string s;
  StrAppendFormat(&s, "%s: %n%s\n", "(1,1)", FormatCountCapture(&n), "(1,2)");
  StrAppendFormat(&s, "%*s%s\n", n, "", "(2,2)");
  EXPECT_EQ(7, n);
  EXPECT_EQ(
      "(1,1): (1,2)\n"
      "       (2,2)\n",
      s);
}

TEST_F(FormatEntryPointTest, FormatCountCaptureExampleWithV) {
  int n;
  std::string s;
  std::string a1 = "(1,1)";
  std::string a2 = "(1,2)";
  std::string a3 = "(2,2)";
  StrAppendFormat(&s, "%v: %n%v\n", a1, FormatCountCapture(&n), a2);
  StrAppendFormat(&s, "%*s%v\n", n, "", a3);
  EXPECT_EQ(7, n);
  EXPECT_EQ(
      "(1,1): (1,2)\n"
      "       (2,2)\n",
      s);
}

TEST_F(FormatEntryPointTest, Stream) {
  const std::string formats[] = {
    "",
    "a",
    "%80d",
    "%d %u %c %s %f %g",
#if !defined(_MSC_VER) && !defined(__ANDROID__) && !defined(__native_client__)
    // MSVC, NaCL and Android don't support positional syntax.
    "complicated multipart %% %1$d format %1$080d",
#endif  // _MSC_VER
  };
  std::string buf(4096, '\0');
  for (const auto& fmt : formats) {
    const auto parsed =
        ParsedFormat<'d', 'u', 'c', 's', 'f', 'g'>::NewAllowIgnored(fmt);
    std::ostringstream oss;
    oss << StreamFormat(*parsed, 123, 3, 49, "multistreaming!!!", 1.01, 1.01);
    int fmt_result = snprintf(&*buf.begin(), buf.size(), fmt.c_str(),  //
                              123, 3, 49, "multistreaming!!!", 1.01, 1.01);
    ASSERT_TRUE(oss) << fmt;
    ASSERT_TRUE(fmt_result >= 0 && static_cast<size_t>(fmt_result) < buf.size())
        << fmt_result;
    EXPECT_EQ(buf.c_str(), oss.str());
  }
}

TEST_F(FormatEntryPointTest, StreamWithV) {
  const std::string formats[] = {
      "",
      "a",
      "%v %u %c %v %f %v",
  };

  const std::string formats_for_buf[] = {
      "",
      "a",
      "%d %u %c %s %f %g",
  };

  std::string buf(4096, '\0');
  for (auto i = 0; i < ABSL_ARRAYSIZE(formats); ++i) {
    const auto parsed =
        ParsedFormat<'v', 'u', 'c', 'v', 'f', 'v'>::NewAllowIgnored(formats[i]);
    std::ostringstream oss;
    oss << StreamFormat(*parsed, 123, 3, 49,
                        absl::string_view("multistreaming!!!"), 1.01, 1.01);
    int fmt_result =
        snprintf(&*buf.begin(), buf.size(), formats_for_buf[i].c_str(),  //
                 123, 3, 49, "multistreaming!!!", 1.01, 1.01);
    ASSERT_TRUE(oss) << formats[i];
    ASSERT_TRUE(fmt_result >= 0 && static_cast<size_t>(fmt_result) < buf.size())
        << fmt_result;
    EXPECT_EQ(buf.c_str(), oss.str());
  }
}

TEST_F(FormatEntryPointTest, StreamOk) {
  std::ostringstream oss;
  oss << StreamFormat("hello %d", 123);
  EXPECT_EQ("hello 123", oss.str());
  EXPECT_TRUE(oss.good());
}

TEST_F(FormatEntryPointTest, StreamOkWithV) {
  std::ostringstream oss;
  oss << StreamFormat("hello %v", 123);
  EXPECT_EQ("hello 123", oss.str());
  EXPECT_TRUE(oss.good());
}

TEST_F(FormatEntryPointTest, StreamFail) {
  std::ostringstream oss;
  UntypedFormatSpec format("hello %d");
  FormatArgImpl arg("non-numeric");
  oss << str_format_internal::Streamable(
      str_format_internal::UntypedFormatSpecImpl::Extract(format), {&arg, 1});
  EXPECT_EQ("hello ", oss.str());  // partial write
  EXPECT_TRUE(oss.fail());
}

TEST_F(FormatEntryPointTest, StreamFailWithV) {
  std::ostringstream oss;
  UntypedFormatSpec format("hello %v");
  FormatArgImpl arg("non-numeric");
  oss << str_format_internal::Streamable(
      str_format_internal::UntypedFormatSpecImpl::Extract(format), {&arg, 1});
  EXPECT_EQ("hello ", oss.str());  // partial write
  EXPECT_TRUE(oss.fail());
}

std::string WithSnprintf(const char* fmt, ...) {
  std::string buf;
  buf.resize(128);
  va_list va;
  va_start(va, fmt);
  int r = vsnprintf(&*buf.begin(), buf.size(), fmt, va);
  va_end(va);
  EXPECT_GE(r, 0);
  EXPECT_LT(r, buf.size());
  buf.resize(r);
  return buf;
}

TEST_F(FormatEntryPointTest, FloatPrecisionArg) {
  // Test that positional parameters for width and precision
  // are indexed to precede the value.
  // Also sanity check the same formats against snprintf.
  EXPECT_EQ("0.1", StrFormat("%.1f", 0.1));
  EXPECT_EQ("0.1", WithSnprintf("%.1f", 0.1));
  EXPECT_EQ("  0.1", StrFormat("%*.1f", 5, 0.1));
  EXPECT_EQ("  0.1", WithSnprintf("%*.1f", 5, 0.1));
  EXPECT_EQ("0.1", StrFormat("%.*f", 1, 0.1));
  EXPECT_EQ("0.1", WithSnprintf("%.*f", 1, 0.1));
  EXPECT_EQ("  0.1", StrFormat("%*.*f", 5, 1, 0.1));
  EXPECT_EQ("  0.1", WithSnprintf("%*.*f", 5, 1, 0.1));
}
namespace streamed_test {
struct X {};
std::ostream& operator<<(std::ostream& os, const X&) {
  return os << "X";
}
}  // streamed_test

TEST_F(FormatEntryPointTest, FormatStreamed) {
  EXPECT_EQ("123", StrFormat("%s", FormatStreamed(123)));
  EXPECT_EQ("  123", StrFormat("%5s", FormatStreamed(123)));
  EXPECT_EQ("123  ", StrFormat("%-5s", FormatStreamed(123)));
  EXPECT_EQ("X", StrFormat("%s", FormatStreamed(streamed_test::X())));
  EXPECT_EQ("123", StrFormat("%s", FormatStreamed(StreamFormat("%d", 123))));
}

TEST_F(FormatEntryPointTest, FormatStreamedWithV) {
  EXPECT_EQ("123", StrFormat("%v", FormatStreamed(123)));
  EXPECT_EQ("X", StrFormat("%v", FormatStreamed(streamed_test::X())));
  EXPECT_EQ("123", StrFormat("%v", FormatStreamed(StreamFormat("%d", 123))));
}

// Helper class that creates a temporary file and exposes a FILE* to it.
// It will close the file on destruction.
class TempFile {
 public:
  TempFile() : file_(std::tmpfile()) {}
  ~TempFile() { std::fclose(file_); }

  std::FILE* file() const { return file_; }

  // Read the file into a string.
  std::string ReadFile() {
    std::fseek(file_, 0, SEEK_END);
    int size = std::ftell(file_);
    EXPECT_GT(size, 0);
    std::rewind(file_);
    std::string str(2 * size, ' ');
    int read_bytes = std::fread(&str[0], 1, str.size(), file_);
    EXPECT_EQ(read_bytes, size);
    str.resize(read_bytes);
    EXPECT_TRUE(std::feof(file_));
    return str;
  }

 private:
  std::FILE* file_;
};

TEST_F(FormatEntryPointTest, FPrintF) {
  TempFile tmp;
  int result =
      FPrintF(tmp.file(), "STRING: %s NUMBER: %010d", std::string("ABC"), -19);
  EXPECT_EQ(result, 30);
  EXPECT_EQ(tmp.ReadFile(), "STRING: ABC NUMBER: -000000019");
}

TEST_F(FormatEntryPointTest, FPrintFWithV) {
  TempFile tmp;
  int result =
      FPrintF(tmp.file(), "STRING: %v NUMBER: %010d", std::string("ABC"), -19);
  EXPECT_EQ(result, 30);
  EXPECT_EQ(tmp.ReadFile(), "STRING: ABC NUMBER: -000000019");
}

TEST_F(FormatEntryPointTest, FPrintFError) {
  errno = 0;
  int result = FPrintF(stdin, "ABC");
  EXPECT_LT(result, 0);
  EXPECT_EQ(errno, EBADF);
}

#ifdef __GLIBC__
TEST_F(FormatEntryPointTest, FprintfTooLarge) {
  std::FILE* f = std::fopen("/dev/null", "w");
  int width = 2000000000;
  errno = 0;
  int result = FPrintF(f, "%*d %*d", width, 0, width, 0);
  EXPECT_LT(result, 0);
  EXPECT_EQ(errno, EFBIG);
  std::fclose(f);
}

TEST_F(FormatEntryPointTest, PrintF) {
  int stdout_tmp = dup(STDOUT_FILENO);

  TempFile tmp;
  std::fflush(stdout);
  dup2(fileno(tmp.file()), STDOUT_FILENO);

  int result = PrintF("STRING: %s NUMBER: %010d", std::string("ABC"), -19);

  std::fflush(stdout);
  dup2(stdout_tmp, STDOUT_FILENO);
  close(stdout_tmp);

  EXPECT_EQ(result, 30);
  EXPECT_EQ(tmp.ReadFile(), "STRING: ABC NUMBER: -000000019");
}

TEST_F(FormatEntryPointTest, PrintFWithV) {
  int stdout_tmp = dup(STDOUT_FILENO);

  TempFile tmp;
  std::fflush(stdout);
  dup2(fileno(tmp.file()), STDOUT_FILENO);

  int result = PrintF("STRING: %v NUMBER: %010d", std::string("ABC"), -19);

  std::fflush(stdout);
  dup2(stdout_tmp, STDOUT_FILENO);
  close(stdout_tmp);

  EXPECT_EQ(result, 30);
  EXPECT_EQ(tmp.ReadFile(), "STRING: ABC NUMBER: -000000019");
}
#endif  // __GLIBC__

TEST_F(FormatEntryPointTest, SNPrintF) {
  char buffer[16];
  int result =
      SNPrintF(buffer, sizeof(buffer), "STRING: %s", std::string("ABC"));
  EXPECT_EQ(result, 11);
  EXPECT_EQ(std::string(buffer), "STRING: ABC");

  result = SNPrintF(buffer, sizeof(buffer), "NUMBER: %d", 123456);
  EXPECT_EQ(result, 14);
  EXPECT_EQ(std::string(buffer), "NUMBER: 123456");

  result = SNPrintF(buffer, sizeof(buffer), "NUMBER: %d", 1234567);
  EXPECT_EQ(result, 15);
  EXPECT_EQ(std::string(buffer), "NUMBER: 1234567");

  result = SNPrintF(buffer, sizeof(buffer), "NUMBER: %d", 12345678);
  EXPECT_EQ(result, 16);
  EXPECT_EQ(std::string(buffer), "NUMBER: 1234567");

  result = SNPrintF(buffer, sizeof(buffer), "NUMBER: %d", 123456789);
  EXPECT_EQ(result, 17);
  EXPECT_EQ(std::string(buffer), "NUMBER: 1234567");

  // The `output` parameter is annotated nonnull, but we want to test that
  // it is never written to if the size is zero.
  // Use a variable instead of passing nullptr directly to avoid a `-Wnonnull`
  // warning.
  char* null_output = nullptr;
  result =
      SNPrintF(null_output, 0, "Just checking the %s of the output.", "size");
  EXPECT_EQ(result, 37);
}

TEST_F(FormatEntryPointTest, SNPrintFWithV) {
  char buffer[16];
  int result =
      SNPrintF(buffer, sizeof(buffer), "STRING: %v", std::string("ABC"));
  EXPECT_EQ(result, 11);
  EXPECT_EQ(std::string(buffer), "STRING: ABC");

  result = SNPrintF(buffer, sizeof(buffer), "NUMBER: %v", 123456);
  EXPECT_EQ(result, 14);
  EXPECT_EQ(std::string(buffer), "NUMBER: 123456");

  result = SNPrintF(buffer, sizeof(buffer), "NUMBER: %v", 1234567);
  EXPECT_EQ(result, 15);
  EXPECT_EQ(std::string(buffer), "NUMBER: 1234567");

  result = SNPrintF(buffer, sizeof(buffer), "NUMBER: %v", 12345678);
  EXPECT_EQ(result, 16);
  EXPECT_EQ(std::string(buffer), "NUMBER: 1234567");

  result = SNPrintF(buffer, sizeof(buffer), "NUMBER: %v", 123456789);
  EXPECT_EQ(result, 17);
  EXPECT_EQ(std::string(buffer), "NUMBER: 1234567");

  std::string size = "size";

  // The `output` parameter is annotated nonnull, but we want to test that
  // it is never written to if the size is zero.
  // Use a variable instead of passing nullptr directly to avoid a `-Wnonnull`
  // warning.
  char* null_output = nullptr;
  result =
      SNPrintF(null_output, 0, "Just checking the %v of the output.", size);
  EXPECT_EQ(result, 37);
}

TEST(StrFormat, BehavesAsDocumented) {
  std::string s = absl::StrFormat("%s, %d!", "Hello", 123);
  EXPECT_EQ("Hello, 123!", s);
  std::string hello = "Hello";
  std::string s2 = absl::StrFormat("%v, %v!", hello, 123);
  EXPECT_EQ("Hello, 123!", s2);
  // The format of a replacement is
  // '%'[position][flags][width['.'precision]][length_modifier][format]
  EXPECT_EQ(absl::StrFormat("%1$+3.2Lf", 1.1), "+1.10");
  // Text conversion:
  //     "c" - Character.              Eg: 'a' -> "A", 20 -> " "
  EXPECT_EQ(StrFormat("%c", 'a'), "a");
  EXPECT_EQ(StrFormat("%c", 0x20), " ");
  //           Formats char and integral types: int, long, uint64_t, etc.
  EXPECT_EQ(StrFormat("%c", int{'a'}), "a");
  EXPECT_EQ(StrFormat("%c", long{'a'}), "a");  // NOLINT
  EXPECT_EQ(StrFormat("%c", uint64_t{'a'}), "a");
  //     "s" - string       Eg: "C" -> "C", std::string("C++") -> "C++"
  //           Formats std::string, char*, string_view, and Cord.
  EXPECT_EQ(StrFormat("%s", "C"), "C");
  EXPECT_EQ(StrFormat("%v", std::string("C")), "C");
  EXPECT_EQ(StrFormat("%s", std::string("C++")), "C++");
  EXPECT_EQ(StrFormat("%v", std::string("C++")), "C++");
  EXPECT_EQ(StrFormat("%s", string_view("view")), "view");
  EXPECT_EQ(StrFormat("%v", string_view("view")), "view");
  EXPECT_EQ(StrFormat("%s", absl::Cord("cord")), "cord");
  EXPECT_EQ(StrFormat("%v", absl::Cord("cord")), "cord");
  // Integral Conversion
  //     These format integral types: char, int, long, uint64_t, etc.
  EXPECT_EQ(StrFormat("%d", char{10}), "10");
  EXPECT_EQ(StrFormat("%d", int{10}), "10");
  EXPECT_EQ(StrFormat("%d", long{10}), "10");  // NOLINT
  EXPECT_EQ(StrFormat("%d", uint64_t{10}), "10");
  EXPECT_EQ(StrFormat("%v", int{10}), "10");
  EXPECT_EQ(StrFormat("%v", long{10}), "10");  // NOLINT
  EXPECT_EQ(StrFormat("%v", uint64_t{10}), "10");
  //     d,i - signed decimal          Eg: -10 -> "-10"
  EXPECT_EQ(StrFormat("%d", -10), "-10");
  EXPECT_EQ(StrFormat("%i", -10), "-10");
  EXPECT_EQ(StrFormat("%v", -10), "-10");
  //      o  - octal                   Eg:  10 -> "12"
  EXPECT_EQ(StrFormat("%o", 10), "12");
  //      u  - unsigned decimal        Eg:  10 -> "10"
  EXPECT_EQ(StrFormat("%u", 10), "10");
  EXPECT_EQ(StrFormat("%v", 10), "10");
  //     x/X - lower,upper case hex    Eg:  10 -> "a"/"A"
  EXPECT_EQ(StrFormat("%x", 10), "a");
  EXPECT_EQ(StrFormat("%X", 10), "A");
  // Floating-point, with upper/lower-case output.
  //     These format floating points types: float, double, long double, etc.
  EXPECT_EQ(StrFormat("%.1f", float{1}), "1.0");
  EXPECT_EQ(StrFormat("%.1f", double{1}), "1.0");
  const long double long_double = 1.0;
  EXPECT_EQ(StrFormat("%.1f", long_double), "1.0");
  //     These also format integral types: char, int, long, uint64_t, etc.:
  EXPECT_EQ(StrFormat("%.1f", char{1}), "1.0");
  EXPECT_EQ(StrFormat("%.1f", int{1}), "1.0");
  EXPECT_EQ(StrFormat("%.1f", long{1}), "1.0");  // NOLINT
  EXPECT_EQ(StrFormat("%.1f", uint64_t{1}), "1.0");
  //     f/F - decimal.                Eg: 123456789 -> "123456789.000000"
  EXPECT_EQ(StrFormat("%f", 123456789), "123456789.000000");
  EXPECT_EQ(StrFormat("%F", 123456789), "123456789.000000");
  //     e/E - exponentiated           Eg: .01 -> "1.00000e-2"/"1.00000E-2"
  EXPECT_EQ(StrFormat("%e", .01), "1.000000e-02");
  EXPECT_EQ(StrFormat("%E", .01), "1.000000E-02");
  //     g/G - exponentiate to fit     Eg: .01 -> "0.01", 1e10 ->"1e+10"/"1E+10"
  EXPECT_EQ(StrFormat("%g", .01), "0.01");
  EXPECT_EQ(StrFormat("%g", 1e10), "1e+10");
  EXPECT_EQ(StrFormat("%G", 1e10), "1E+10");
  EXPECT_EQ(StrFormat("%v", .01), "0.01");
  EXPECT_EQ(StrFormat("%v", 1e10), "1e+10");
  //     a/A - lower,upper case hex    Eg: -3.0 -> "-0x1.8p+1"/"-0X1.8P+1"

// On Android platform <=21, there is a regression in hexfloat formatting.
#if !defined(__ANDROID_API__) || __ANDROID_API__ > 21
  EXPECT_EQ(StrFormat("%.1a", -3.0), "-0x1.8p+1");  // .1 to fix MSVC output
  EXPECT_EQ(StrFormat("%.1A", -3.0), "-0X1.8P+1");  // .1 to fix MSVC output
#endif

  // Other conversion
  int64_t value = 0x7ffdeb4;
  auto ptr_value = static_cast<uintptr_t>(value);
  const int& something = *reinterpret_cast<const int*>(ptr_value);
  EXPECT_EQ(StrFormat("%p", &something), StrFormat("0x%x", ptr_value));

  // The output of formatting a null pointer is not documented as being a
  // specific thing, but the attempt should at least compile.
  (void)StrFormat("%p", nullptr);

  // Output widths are supported, with optional flags.
  EXPECT_EQ(StrFormat("%3d", 1), "  1");
  EXPECT_EQ(StrFormat("%3d", 123456), "123456");
  EXPECT_EQ(StrFormat("%06.2f", 1.234), "001.23");
  EXPECT_EQ(StrFormat("%+d", 1), "+1");
  EXPECT_EQ(StrFormat("% d", 1), " 1");
  EXPECT_EQ(StrFormat("%-4d", -1), "-1  ");
  EXPECT_EQ(StrFormat("%#o", 10), "012");
  EXPECT_EQ(StrFormat("%#x", 15), "0xf");
  EXPECT_EQ(StrFormat("%04d", 8), "0008");
  EXPECT_EQ(StrFormat("%#04x", 0), "0000");
  EXPECT_EQ(StrFormat("%#04x", 1), "0x01");
  // Posix positional substitution.
  EXPECT_EQ(absl::StrFormat("%2$s, %3$s, %1$s!", "vici", "veni", "vidi"),
            "veni, vidi, vici!");
  // Length modifiers are ignored.
  EXPECT_EQ(StrFormat("%hhd", int{1}), "1");
  EXPECT_EQ(StrFormat("%hd", int{1}), "1");
  EXPECT_EQ(StrFormat("%ld", int{1}), "1");
  EXPECT_EQ(StrFormat("%lld", int{1}), "1");
  EXPECT_EQ(StrFormat("%Ld", int{1}), "1");
  EXPECT_EQ(StrFormat("%jd", int{1}), "1");
  EXPECT_EQ(StrFormat("%zd", int{1}), "1");
  EXPECT_EQ(StrFormat("%td", int{1}), "1");
  EXPECT_EQ(StrFormat("%qd", int{1}), "1");

  // Bool is handled correctly depending on whether %v is used
  EXPECT_EQ(StrFormat("%v", true), "true");
  EXPECT_EQ(StrFormat("%v", false), "false");
  EXPECT_EQ(StrFormat("%d", true), "1");
}

using str_format_internal::ExtendedParsedFormat;
using str_format_internal::ParsedFormatBase;

struct SummarizeConsumer {
  std::string* out;
  explicit SummarizeConsumer(std::string* out) : out(out) {}

  bool Append(string_view s) {
    *out += "[" + std::string(s) + "]";
    return true;
  }

  bool ConvertOne(const str_format_internal::UnboundConversion& conv,
                  string_view s) {
    *out += "{";
    *out += std::string(s);
    *out += ":";
    *out += std::to_string(conv.arg_position) + "$";
    if (conv.width.is_from_arg()) {
      *out += std::to_string(conv.width.get_from_arg()) + "$*";
    }
    if (conv.precision.is_from_arg()) {
      *out += "." + std::to_string(conv.precision.get_from_arg()) + "$*";
    }
    *out += str_format_internal::FormatConversionCharToChar(conv.conv);
    *out += "}";
    return true;
  }
};

std::string SummarizeParsedFormat(const ParsedFormatBase& pc) {
  std::string out;
  if (!pc.ProcessFormat(SummarizeConsumer(&out))) out += "!";
  return out;
}

using ParsedFormatTest = ::testing::Test;

TEST_F(ParsedFormatTest, SimpleChecked) {
  EXPECT_EQ("[ABC]{d:1$d}[DEF]",
            SummarizeParsedFormat(ParsedFormat<'d'>("ABC%dDEF")));
  EXPECT_EQ("{s:1$s}[FFF]{d:2$d}[ZZZ]{f:3$f}",
            SummarizeParsedFormat(ParsedFormat<'s', 'd', 'f'>("%sFFF%dZZZ%f")));
  EXPECT_EQ("{s:1$s}[ ]{.*d:3$.2$*d}",
            SummarizeParsedFormat(ParsedFormat<'s', '*', 'd'>("%s %.*d")));
}

TEST_F(ParsedFormatTest, SimpleCheckedWithV) {
  EXPECT_EQ("[ABC]{v:1$v}[DEF]",
            SummarizeParsedFormat(ParsedFormat<'v'>("ABC%vDEF")));
  EXPECT_EQ("{v:1$v}[FFF]{v:2$v}[ZZZ]{f:3$f}",
            SummarizeParsedFormat(ParsedFormat<'v', 'v', 'f'>("%vFFF%vZZZ%f")));
  EXPECT_EQ("{v:1$v}[ ]{.*d:3$.2$*d}",
            SummarizeParsedFormat(ParsedFormat<'v', '*', 'd'>("%v %.*d")));
}

TEST_F(ParsedFormatTest, SimpleUncheckedCorrect) {
  auto f = ParsedFormat<'d'>::New("ABC%dDEF");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]{d:1$d}[DEF]", SummarizeParsedFormat(*f));

  std::string format = "%sFFF%dZZZ%f";
  auto f2 = ParsedFormat<'s', 'd', 'f'>::New(format);

  ASSERT_TRUE(f2);
  EXPECT_EQ("{s:1$s}[FFF]{d:2$d}[ZZZ]{f:3$f}", SummarizeParsedFormat(*f2));

  f2 = ParsedFormat<'s', 'd', 'f'>::New("%s %d %f");

  ASSERT_TRUE(f2);
  EXPECT_EQ("{s:1$s}[ ]{d:2$d}[ ]{f:3$f}", SummarizeParsedFormat(*f2));

  auto star = ParsedFormat<'*', 'd'>::New("%*d");
  ASSERT_TRUE(star);
  EXPECT_EQ("{*d:2$1$*d}", SummarizeParsedFormat(*star));

  auto dollar = ParsedFormat<'d', 's'>::New("%2$s %1$d");
  ASSERT_TRUE(dollar);
  EXPECT_EQ("{2$s:2$s}[ ]{1$d:1$d}", SummarizeParsedFormat(*dollar));
  // with reuse
  dollar = ParsedFormat<'d', 's'>::New("%2$s %1$d %1$d");
  ASSERT_TRUE(dollar);
  EXPECT_EQ("{2$s:2$s}[ ]{1$d:1$d}[ ]{1$d:1$d}",
            SummarizeParsedFormat(*dollar));
}

TEST_F(ParsedFormatTest, SimpleUncheckedCorrectWithV) {
  auto f = ParsedFormat<'v'>::New("ABC%vDEF");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]{v:1$v}[DEF]", SummarizeParsedFormat(*f));

  std::string format = "%vFFF%vZZZ%f";
  auto f2 = ParsedFormat<'v', 'v', 'f'>::New(format);

  ASSERT_TRUE(f2);
  EXPECT_EQ("{v:1$v}[FFF]{v:2$v}[ZZZ]{f:3$f}", SummarizeParsedFormat(*f2));

  f2 = ParsedFormat<'v', 'v', 'f'>::New("%v %v %f");

  ASSERT_TRUE(f2);
  EXPECT_EQ("{v:1$v}[ ]{v:2$v}[ ]{f:3$f}", SummarizeParsedFormat(*f2));
}

TEST_F(ParsedFormatTest, SimpleUncheckedIgnoredArgs) {
  EXPECT_FALSE((ParsedFormat<'d', 's'>::New("ABC")));
  EXPECT_FALSE((ParsedFormat<'d', 's'>::New("%dABC")));
  EXPECT_FALSE((ParsedFormat<'d', 's'>::New("ABC%2$s")));
  auto f = ParsedFormat<'d', 's'>::NewAllowIgnored("ABC");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]", SummarizeParsedFormat(*f));
  f = ParsedFormat<'d', 's'>::NewAllowIgnored("%dABC");
  ASSERT_TRUE(f);
  EXPECT_EQ("{d:1$d}[ABC]", SummarizeParsedFormat(*f));
  f = ParsedFormat<'d', 's'>::NewAllowIgnored("ABC%2$s");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]{2$s:2$s}", SummarizeParsedFormat(*f));
}

TEST_F(ParsedFormatTest, SimpleUncheckedIgnoredArgsWithV) {
  EXPECT_FALSE((ParsedFormat<'v', 'v'>::New("ABC")));
  EXPECT_FALSE((ParsedFormat<'v', 'v'>::New("%vABC")));
  EXPECT_FALSE((ParsedFormat<'v', 's'>::New("ABC%2$s")));
  auto f = ParsedFormat<'v', 'v'>::NewAllowIgnored("ABC");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]", SummarizeParsedFormat(*f));
  f = ParsedFormat<'v', 'v'>::NewAllowIgnored("%vABC");
  ASSERT_TRUE(f);
  EXPECT_EQ("{v:1$v}[ABC]", SummarizeParsedFormat(*f));
}

TEST_F(ParsedFormatTest, SimpleUncheckedUnsupported) {
  EXPECT_FALSE(ParsedFormat<'d'>::New("%1$d %1$x"));
  EXPECT_FALSE(ParsedFormat<'x'>::New("%1$d %1$x"));
}

TEST_F(ParsedFormatTest, SimpleUncheckedIncorrect) {
  EXPECT_FALSE(ParsedFormat<'d'>::New(""));

  EXPECT_FALSE(ParsedFormat<'d'>::New("ABC%dDEF%d"));

  std::string format = "%sFFF%dZZZ%f";
  EXPECT_FALSE((ParsedFormat<'s', 'd', 'g'>::New(format)));
}

TEST_F(ParsedFormatTest, SimpleUncheckedIncorrectWithV) {
  EXPECT_FALSE(ParsedFormat<'v'>::New(""));

  EXPECT_FALSE(ParsedFormat<'v'>::New("ABC%vDEF%v"));

  std::string format = "%vFFF%vZZZ%f";
  EXPECT_FALSE((ParsedFormat<'v', 'v', 'g'>::New(format)));
}

#if defined(__cpp_nontype_template_parameter_auto)

template <auto T>
std::true_type IsValidParsedFormatArgTest(ParsedFormat<T>*);

template <auto T>
std::false_type IsValidParsedFormatArgTest(...);

template <auto T>
using IsValidParsedFormatArg = decltype(IsValidParsedFormatArgTest<T>(nullptr));

TEST_F(ParsedFormatTest, OnlyValidTypesAllowed) {
  ASSERT_TRUE(IsValidParsedFormatArg<'c'>::value);

  ASSERT_TRUE(IsValidParsedFormatArg<FormatConversionCharSet::d>::value);

  ASSERT_TRUE(IsValidParsedFormatArg<absl::FormatConversionCharSet::d |
                                     absl::FormatConversionCharSet::x>::value);
  ASSERT_TRUE(
      IsValidParsedFormatArg<absl::FormatConversionCharSet::kIntegral>::value);

  // This is an easy mistake to make, however, this will reduce to an integer
  // which has no meaning, so we need to ensure it doesn't compile.
  ASSERT_FALSE(IsValidParsedFormatArg<'x' | 'd'>::value);

  // For now, we disallow construction based on ConversionChar (rather than
  // CharSet)
  ASSERT_FALSE(IsValidParsedFormatArg<absl::FormatConversionChar::d>::value);
}

TEST_F(ParsedFormatTest, ExtendedTyping) {
  EXPECT_FALSE(ParsedFormat<FormatConversionCharSet::d>::New(""));
  ASSERT_TRUE(ParsedFormat<absl::FormatConversionCharSet::d>::New("%d"));
  auto v1 = ParsedFormat<'d', absl::FormatConversionCharSet::s>::New("%d%s");
  ASSERT_TRUE(v1);
  auto v2 = ParsedFormat<absl::FormatConversionCharSet::d, 's'>::New("%d%s");
  ASSERT_TRUE(v2);
  auto v3 = ParsedFormat<absl::FormatConversionCharSet::d |
                             absl::FormatConversionCharSet::s,
                         's'>::New("%d%s");
  ASSERT_TRUE(v3);
  auto v4 = ParsedFormat<absl::FormatConversionCharSet::d |
                             absl::FormatConversionCharSet::s,
                         's'>::New("%s%s");
  ASSERT_TRUE(v4);
}

TEST_F(ParsedFormatTest, ExtendedTypingWithV) {
  EXPECT_FALSE(ParsedFormat<FormatConversionCharSet::v>::New(""));
  ASSERT_TRUE(ParsedFormat<absl::FormatConversionCharSet::v>::New("%v"));
  auto v1 = ParsedFormat<'v', absl::FormatConversionCharSet::v>::New("%v%v");
  ASSERT_TRUE(v1);
  auto v2 = ParsedFormat<absl::FormatConversionCharSet::v, 'v'>::New("%v%v");
  ASSERT_TRUE(v2);
  auto v3 = ParsedFormat<absl::FormatConversionCharSet::v |
                             absl::FormatConversionCharSet::v,
                         'v'>::New("%v%v");
  ASSERT_TRUE(v3);
  auto v4 = ParsedFormat<absl::FormatConversionCharSet::v |
                             absl::FormatConversionCharSet::v,
                         'v'>::New("%v%v");
  ASSERT_TRUE(v4);
}
#endif

TEST_F(ParsedFormatTest, UncheckedCorrect) {
  auto f =
      ExtendedParsedFormat<absl::FormatConversionCharSet::d>::New("ABC%dDEF");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]{d:1$d}[DEF]", SummarizeParsedFormat(*f));

  std::string format = "%sFFF%dZZZ%f";
  auto f2 = ExtendedParsedFormat<
      absl::FormatConversionCharSet::kString, absl::FormatConversionCharSet::d,
      absl::FormatConversionCharSet::kFloating>::New(format);

  ASSERT_TRUE(f2);
  EXPECT_EQ("{s:1$s}[FFF]{d:2$d}[ZZZ]{f:3$f}", SummarizeParsedFormat(*f2));

  f2 = ExtendedParsedFormat<
      absl::FormatConversionCharSet::kString, absl::FormatConversionCharSet::d,
      absl::FormatConversionCharSet::kFloating>::New("%s %d %f");

  ASSERT_TRUE(f2);
  EXPECT_EQ("{s:1$s}[ ]{d:2$d}[ ]{f:3$f}", SummarizeParsedFormat(*f2));

  auto star =
      ExtendedParsedFormat<absl::FormatConversionCharSet::kStar,
                           absl::FormatConversionCharSet::d>::New("%*d");
  ASSERT_TRUE(star);
  EXPECT_EQ("{*d:2$1$*d}", SummarizeParsedFormat(*star));

  auto dollar =
      ExtendedParsedFormat<absl::FormatConversionCharSet::d,
                           absl::FormatConversionCharSet::s>::New("%2$s %1$d");
  ASSERT_TRUE(dollar);
  EXPECT_EQ("{2$s:2$s}[ ]{1$d:1$d}", SummarizeParsedFormat(*dollar));
  // with reuse
  dollar = ExtendedParsedFormat<
      absl::FormatConversionCharSet::d,
      absl::FormatConversionCharSet::s>::New("%2$s %1$d %1$d");
  ASSERT_TRUE(dollar);
  EXPECT_EQ("{2$s:2$s}[ ]{1$d:1$d}[ ]{1$d:1$d}",
            SummarizeParsedFormat(*dollar));
}

TEST_F(ParsedFormatTest, UncheckedCorrectWithV) {
  auto f =
      ExtendedParsedFormat<absl::FormatConversionCharSet::v>::New("ABC%vDEF");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]{v:1$v}[DEF]", SummarizeParsedFormat(*f));

  std::string format = "%vFFF%vZZZ%f";
  auto f2 = ExtendedParsedFormat<
      absl::FormatConversionCharSet::v, absl::FormatConversionCharSet::v,
      absl::FormatConversionCharSet::kFloating>::New(format);

  ASSERT_TRUE(f2);
  EXPECT_EQ("{v:1$v}[FFF]{v:2$v}[ZZZ]{f:3$f}", SummarizeParsedFormat(*f2));

  f2 = ExtendedParsedFormat<
      absl::FormatConversionCharSet::v, absl::FormatConversionCharSet::v,
      absl::FormatConversionCharSet::kFloating>::New("%v %v %f");

  ASSERT_TRUE(f2);
  EXPECT_EQ("{v:1$v}[ ]{v:2$v}[ ]{f:3$f}", SummarizeParsedFormat(*f2));
}

TEST_F(ParsedFormatTest, UncheckedIgnoredArgs) {
  EXPECT_FALSE(
      (ExtendedParsedFormat<absl::FormatConversionCharSet::d,
                            absl::FormatConversionCharSet::s>::New("ABC")));
  EXPECT_FALSE(
      (ExtendedParsedFormat<absl::FormatConversionCharSet::d,
                            absl::FormatConversionCharSet::s>::New("%dABC")));
  EXPECT_FALSE(
      (ExtendedParsedFormat<absl::FormatConversionCharSet::d,
                            absl::FormatConversionCharSet::s>::New("ABC%2$s")));
  auto f = ExtendedParsedFormat<
      absl::FormatConversionCharSet::d,
      absl::FormatConversionCharSet::s>::NewAllowIgnored("ABC");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]", SummarizeParsedFormat(*f));
  f = ExtendedParsedFormat<
      absl::FormatConversionCharSet::d,
      absl::FormatConversionCharSet::s>::NewAllowIgnored("%dABC");
  ASSERT_TRUE(f);
  EXPECT_EQ("{d:1$d}[ABC]", SummarizeParsedFormat(*f));
  f = ExtendedParsedFormat<
      absl::FormatConversionCharSet::d,
      absl::FormatConversionCharSet::s>::NewAllowIgnored("ABC%2$s");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]{2$s:2$s}", SummarizeParsedFormat(*f));
}

TEST_F(ParsedFormatTest, UncheckedIgnoredArgsWithV) {
  EXPECT_FALSE(
      (ExtendedParsedFormat<absl::FormatConversionCharSet::v,
                            absl::FormatConversionCharSet::v>::New("ABC")));
  EXPECT_FALSE(
      (ExtendedParsedFormat<absl::FormatConversionCharSet::v,
                            absl::FormatConversionCharSet::v>::New("%vABC")));
  EXPECT_FALSE((ExtendedParsedFormat<absl::FormatConversionCharSet::v,
                                     absl::FormatConversionCharSet::s>::
                    New("ABC%2$s")));
  auto f = ExtendedParsedFormat<
      absl::FormatConversionCharSet::v,
      absl::FormatConversionCharSet::v>::NewAllowIgnored("ABC");
  ASSERT_TRUE(f);
  EXPECT_EQ("[ABC]", SummarizeParsedFormat(*f));
  f = ExtendedParsedFormat<
      absl::FormatConversionCharSet::v,
      absl::FormatConversionCharSet::v>::NewAllowIgnored("%vABC");
  ASSERT_TRUE(f);
  EXPECT_EQ("{v:1$v}[ABC]", SummarizeParsedFormat(*f));
}

TEST_F(ParsedFormatTest, UncheckedMultipleTypes) {
  auto dx =
      ExtendedParsedFormat<absl::FormatConversionCharSet::d |
                           absl::FormatConversionCharSet::x>::New("%1$d %1$x");
  EXPECT_TRUE(dx);
  EXPECT_EQ("{1$d:1$d}[ ]{1$x:1$x}", SummarizeParsedFormat(*dx));

  dx = ExtendedParsedFormat<absl::FormatConversionCharSet::d |
                            absl::FormatConversionCharSet::x>::New("%1$d");
  EXPECT_TRUE(dx);
  EXPECT_EQ("{1$d:1$d}", SummarizeParsedFormat(*dx));
}

TEST_F(ParsedFormatTest, UncheckedIncorrect) {
  EXPECT_FALSE(ExtendedParsedFormat<absl::FormatConversionCharSet::d>::New(""));

  EXPECT_FALSE(ExtendedParsedFormat<absl::FormatConversionCharSet::d>::New(
      "ABC%dDEF%d"));

  std::string format = "%sFFF%dZZZ%f";
  EXPECT_FALSE(
      (ExtendedParsedFormat<absl::FormatConversionCharSet::s,
                            absl::FormatConversionCharSet::d,
                            absl::FormatConversionCharSet::g>::New(format)));
}

TEST_F(ParsedFormatTest, UncheckedIncorrectWithV) {
  EXPECT_FALSE(ExtendedParsedFormat<absl::FormatConversionCharSet::v>::New(""));

  EXPECT_FALSE(ExtendedParsedFormat<absl::FormatConversionCharSet::v>::New(
      "ABC%vDEF%v"));

  std::string format = "%vFFF%vZZZ%f";
  EXPECT_FALSE(
      (ExtendedParsedFormat<absl::FormatConversionCharSet::v,
                            absl::FormatConversionCharSet::g>::New(format)));
}

TEST_F(ParsedFormatTest, RegressionMixPositional) {
  EXPECT_FALSE(
      (ExtendedParsedFormat<absl::FormatConversionCharSet::d,
                            absl::FormatConversionCharSet::o>::New("%1$d %o")));
}

TEST_F(ParsedFormatTest, DisallowModifiersWithV) {
  auto f = ParsedFormat<'v'>::New("ABC%80vDEF");
  EXPECT_EQ(f, nullptr);

  f = ParsedFormat<'v'>::New("ABC%0vDEF");
  EXPECT_EQ(f, nullptr);

  f = ParsedFormat<'v'>::New("ABC%.1vDEF");
  EXPECT_EQ(f, nullptr);
}

using FormatWrapperTest = ::testing::Test;

// Plain wrapper for StrFormat.
template <typename... Args>
std::string WrappedFormat(const absl::FormatSpec<Args...>& format,
                          const Args&... args) {
  return StrFormat(format, args...);
}

TEST_F(FormatWrapperTest, ConstexprStringFormat) {
  EXPECT_EQ(WrappedFormat("%s there", "hello"), "hello there");
}

TEST_F(FormatWrapperTest, ConstexprStringFormatWithV) {
  std::string hello = "hello";
  EXPECT_EQ(WrappedFormat("%v there", hello), "hello there");
}

TEST_F(FormatWrapperTest, ParsedFormat) {
  ParsedFormat<'s'> format("%s there");
  EXPECT_EQ(WrappedFormat(format, "hello"), "hello there");
}

TEST_F(FormatWrapperTest, ParsedFormatWithV) {
  std::string hello = "hello";
  ParsedFormat<'v'> format("%v there");
  EXPECT_EQ(WrappedFormat(format, hello), "hello there");
}

}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl

namespace {
using FormatExtensionTest = ::testing::Test;

struct Point {
  friend absl::FormatConvertResult<absl::FormatConversionCharSet::kString |
                                   absl::FormatConversionCharSet::kIntegral |
                                   absl::FormatConversionCharSet::v>
  AbslFormatConvert(const Point& p, const absl::FormatConversionSpec& spec,
                    absl::FormatSink* s) {
    if (spec.conversion_char() == absl::FormatConversionChar::s) {
      s->Append(absl::StrCat("x=", p.x, " y=", p.y));
    } else {
      s->Append(absl::StrCat(p.x, ",", p.y));
    }
    return {true};
  }

  int x = 10;
  int y = 20;
};

TEST_F(FormatExtensionTest, AbslFormatConvertExample) {
  Point p;
  EXPECT_EQ(absl::StrFormat("a %s z", p), "a x=10 y=20 z");
  EXPECT_EQ(absl::StrFormat("a %d z", p), "a 10,20 z");
  EXPECT_EQ(absl::StrFormat("a %v z", p), "a 10,20 z");

  // Typed formatting will fail to compile an invalid format.
  // StrFormat("%f", p);  // Does not compile.
  std::string actual;
  absl::UntypedFormatSpec f1("%f");
  // FormatUntyped will return false for bad character.
  EXPECT_FALSE(absl::FormatUntyped(&actual, f1, {absl::FormatArg(p)}));
}

struct PointStringify {
  template <typename FormatSink>
  friend void AbslStringify(FormatSink& sink, const PointStringify& p) {
    sink.Append(absl::StrCat("(", p.x, ", ", p.y, ")"));
  }

  double x = 10.0;
  double y = 20.0;
};

TEST_F(FormatExtensionTest, AbslStringifyExample) {
  PointStringify p;
  EXPECT_EQ(absl::StrFormat("a %v z", p), "a (10, 20) z");
}

struct PointStringifyUsingFormat {
  template <typename FormatSink>
  friend void AbslStringify(FormatSink& sink,
                            const PointStringifyUsingFormat& p) {
    absl::Format(&sink, "(%g, %g)", p.x, p.y);
  }

  double x = 10.0;
  double y = 20.0;
};

TEST_F(FormatExtensionTest, AbslStringifyExampleUsingFormat) {
  PointStringifyUsingFormat p;
  EXPECT_EQ(absl::StrFormat("a %v z", p), "a (10, 20) z");
}

enum class EnumClassWithStringify { Many = 0, Choices = 1 };

template <typename Sink>
void AbslStringify(Sink& sink, EnumClassWithStringify e) {
  absl::Format(&sink, "%s",
               e == EnumClassWithStringify::Many ? "Many" : "Choices");
}

enum EnumWithStringify { Many, Choices };

template <typename Sink>
void AbslStringify(Sink& sink, EnumWithStringify e) {
  absl::Format(&sink, "%s", e == EnumWithStringify::Many ? "Many" : "Choices");
}

TEST_F(FormatExtensionTest, AbslStringifyWithEnumWithV) {
  const auto e_class = EnumClassWithStringify::Choices;
  EXPECT_EQ(absl::StrFormat("My choice is %v", e_class),
            "My choice is Choices");

  const auto e = EnumWithStringify::Choices;
  EXPECT_EQ(absl::StrFormat("My choice is %v", e), "My choice is Choices");
}

TEST_F(FormatExtensionTest, AbslStringifyEnumWithD) {
  const auto e_class = EnumClassWithStringify::Many;
  EXPECT_EQ(absl::StrFormat("My choice is %d", e_class), "My choice is 0");

  const auto e = EnumWithStringify::Choices;
  EXPECT_EQ(absl::StrFormat("My choice is %d", e), "My choice is 1");
}

enum class EnumWithLargerValue { x = 32 };

template <typename Sink>
void AbslStringify(Sink& sink, EnumWithLargerValue e) {
  absl::Format(&sink, "%s", "Many");
}

TEST_F(FormatExtensionTest, AbslStringifyEnumOtherSpecifiers) {
  const auto e = EnumWithLargerValue::x;
  EXPECT_EQ(absl::StrFormat("My choice is %g", e), "My choice is 32");
  EXPECT_EQ(absl::StrFormat("My choice is %x", e), "My choice is 20");
}

}  // namespace

// Some codegen thunks that we can use to easily dump the generated assembly for
// different StrFormat calls.

std::string CodegenAbslStrFormatInt(int i) {  // NOLINT
  return absl::StrFormat("%d", i);
}

std::string CodegenAbslStrFormatIntStringInt64(int i, const std::string& s,
                                               int64_t i64) {  // NOLINT
  return absl::StrFormat("%d %s %d", i, s, i64);
}

void CodegenAbslStrAppendFormatInt(std::string* out, int i) {  // NOLINT
  absl::StrAppendFormat(out, "%d", i);
}

void CodegenAbslStrAppendFormatIntStringInt64(std::string* out, int i,
                                              const std::string& s,
                                              int64_t i64) {  // NOLINT
  absl::StrAppendFormat(out, "%d %s %d", i, s, i64);
}
