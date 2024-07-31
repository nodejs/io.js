//
// Copyright 2022 The Abseil Authors.
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
//
// Tests for stripping of literal strings.
// ---------------------------------------
//
// When a `LOG` statement can be trivially proved at compile time to never fire,
// e.g. due to `ABSL_MIN_LOG_LEVEL`, `NDEBUG`, or some explicit condition, data
// streamed in can be dropped from the compiled program completely if they are
// not used elsewhere.  This most commonly affects string literals, which users
// often want to strip to reduce binary size and/or redact information about
// their program's internals (e.g. in a release build).
//
// These tests log strings and then validate whether they appear in the compiled
// binary.  This is done by opening the file corresponding to the running test
// and running a simple string search on its contents.  The strings to be logged
// and searched for must be unique, and we must take care not to emit them into
// the binary in any other place, e.g. when searching for them.  The latter is
// accomplished by computing them using base64; the source string appears in the
// binary but the target string is computed at runtime.

#include <stdio.h>

#if defined(__MACH__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <Windows.h>
#include <tchar.h>
#endif

#include <algorithm>
#include <functional>
#include <memory>
#include <ostream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/internal/strerror.h"
#include "absl/base/log_severity.h"
#include "absl/flags/internal/program_name.h"
#include "absl/log/check.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

// Set a flag that controls whether we actually execute fatal statements, but
// prevent the compiler from optimizing it out.
static volatile bool kReallyDie = false;

namespace {
using ::testing::_;
using ::testing::Eq;
using ::testing::NotNull;

using absl::log_internal::kAbslMinLogLevel;

std::string Base64UnescapeOrDie(absl::string_view data) {
  std::string decoded;
  CHECK(absl::Base64Unescape(data, &decoded));
  return decoded;
}

// -----------------------------------------------------------------------------
// A Googletest matcher which searches the running binary for a given string
// -----------------------------------------------------------------------------

// This matcher is used to validate that literal strings streamed into
// `LOG` statements that ought to be compiled out (e.g. `LOG_IF(INFO, false)`)
// do not appear in the binary.
//
// Note that passing the string to be sought directly to `FileHasSubstr()` all
// but forces its inclusion in the binary regardless of the logging library's
// behavior. For example:
//
//   LOG_IF(INFO, false) << "you're the man now dog";
//   // This will always pass:
//   // EXPECT_THAT(fp, FileHasSubstr("you're the man now dog"));
//   // So use this instead:
//   EXPECT_THAT(fp, FileHasSubstr(
//       Base64UnescapeOrDie("eW91J3JlIHRoZSBtYW4gbm93IGRvZw==")));

class FileHasSubstrMatcher final : public ::testing::MatcherInterface<FILE*> {
 public:
  explicit FileHasSubstrMatcher(absl::string_view needle) : needle_(needle) {}

  bool MatchAndExplain(
      FILE* fp, ::testing::MatchResultListener* listener) const override {
    std::string buf(
        std::max<std::string::size_type>(needle_.size() * 2, 163840000), '\0');
    size_t buf_start_offset = 0;  // The file offset of the byte at `buf[0]`.
    size_t buf_data_size = 0;     // The number of bytes of `buf` which contain
                                  // data.

    ::fseek(fp, 0, SEEK_SET);
    while (true) {
      // Fill the buffer to capacity or EOF:
      while (buf_data_size < buf.size()) {
        const size_t ret = fread(&buf[buf_data_size], sizeof(char),
                                 buf.size() - buf_data_size, fp);
        if (ret == 0) break;
        buf_data_size += ret;
      }
      if (ferror(fp)) {
        *listener << "error reading file";
        return false;
      }
      const absl::string_view haystack(&buf[0], buf_data_size);
      const auto off = haystack.find(needle_);
      if (off != haystack.npos) {
        *listener << "string found at offset " << buf_start_offset + off;
        return true;
      }
      if (feof(fp)) {
        *listener << "string not found";
        return false;
      }
      // Copy the end of `buf` to the beginning so we catch matches that span
      // buffer boundaries.  `buf` and `buf_data_size` are always large enough
      // that these ranges don't overlap.
      memcpy(&buf[0], &buf[buf_data_size - needle_.size()], needle_.size());
      buf_start_offset += buf_data_size - needle_.size();
      buf_data_size = needle_.size();
    }
  }
  void DescribeTo(std::ostream* os) const override {
    *os << "contains the string \"" << needle_ << "\" (base64(\""
        << Base64UnescapeOrDie(needle_) << "\"))";
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not ";
    DescribeTo(os);
  }

 private:
  std::string needle_;
};

class StrippingTest : public ::testing::Test {
 protected:
  void SetUp() override {
#ifndef NDEBUG
    // Non-optimized builds don't necessarily eliminate dead code at all, so we
    // don't attempt to validate stripping against such builds.
    GTEST_SKIP() << "StrippingTests skipped since this build is not optimized";
#elif defined(__EMSCRIPTEN__)
    // These tests require a way to examine the running binary and look for
    // strings; there's no portable way to do that.
    GTEST_SKIP()
        << "StrippingTests skipped since this platform is not optimized";
#endif
  }

  // Opens this program's executable file.  Returns `nullptr` and writes to
  // `stderr` on failure.
  std::unique_ptr<FILE, std::function<void(FILE*)>> OpenTestExecutable() {
#if defined(__linux__)
    std::unique_ptr<FILE, std::function<void(FILE*)>> fp(
        fopen("/proc/self/exe", "rb"), [](FILE* fp) { fclose(fp); });
    if (!fp) {
      const std::string err = absl::base_internal::StrError(errno);
      absl::FPrintF(stderr, "Failed to open /proc/self/exe: %s\n", err);
    }
    return fp;
#elif defined(__Fuchsia__)
    // TODO(b/242579714): We need to restore the test coverage on this platform.
    std::unique_ptr<FILE, std::function<void(FILE*)>> fp(
        fopen(absl::StrCat("/pkg/bin/",
                           absl::flags_internal::ShortProgramInvocationName())
                  .c_str(),
              "rb"),
        [](FILE* fp) { fclose(fp); });
    if (!fp) {
      const std::string err = absl::base_internal::StrError(errno);
      absl::FPrintF(stderr, "Failed to open /pkg/bin/<binary name>: %s\n", err);
    }
    return fp;
#elif defined(__MACH__)
    uint32_t size = 0;
    int ret = _NSGetExecutablePath(nullptr, &size);
    if (ret != -1) {
      absl::FPrintF(stderr,
                    "Failed to get executable path: "
                    "_NSGetExecutablePath(nullptr) returned %d\n",
                    ret);
      return nullptr;
    }
    std::string path(size, '\0');
    ret = _NSGetExecutablePath(&path[0], &size);
    if (ret != 0) {
      absl::FPrintF(
          stderr,
          "Failed to get executable path: _NSGetExecutablePath(buffer) "
          "returned %d\n",
          ret);
      return nullptr;
    }
    std::unique_ptr<FILE, std::function<void(FILE*)>> fp(
        fopen(path.c_str(), "rb"), [](FILE* fp) { fclose(fp); });
    if (!fp) {
      const std::string err = absl::base_internal::StrError(errno);
      absl::FPrintF(stderr, "Failed to open executable at %s: %s\n", path, err);
    }
    return fp;
#elif defined(_WIN32)
    std::basic_string<TCHAR> path(4096, _T('\0'));
    while (true) {
      const uint32_t ret = ::GetModuleFileName(nullptr, &path[0],
                                               static_cast<DWORD>(path.size()));
      if (ret == 0) {
        absl::FPrintF(
            stderr,
            "Failed to get executable path: GetModuleFileName(buffer) "
            "returned 0\n");
        return nullptr;
      }
      if (ret < path.size()) break;
      path.resize(path.size() * 2, _T('\0'));
    }
    std::unique_ptr<FILE, std::function<void(FILE*)>> fp(
        _tfopen(path.c_str(), _T("rb")), [](FILE* fp) { fclose(fp); });
    if (!fp) absl::FPrintF(stderr, "Failed to open executable\n");
    return fp;
#else
    absl::FPrintF(stderr,
                  "OpenTestExecutable() unimplemented on this platform\n");
    return nullptr;
#endif
  }

  ::testing::Matcher<FILE*> FileHasSubstr(absl::string_view needle) {
    return MakeMatcher(new FileHasSubstrMatcher(needle));
  }
};

// This tests whether out methodology for testing stripping works on this
// platform by looking for one string that definitely ought to be there and one
// that definitely ought not to.  If this fails, none of the `StrippingTest`s
// are going to produce meaningful results.
TEST_F(StrippingTest, Control) {
  constexpr char kEncodedPositiveControl[] =
      "U3RyaXBwaW5nVGVzdC5Qb3NpdGl2ZUNvbnRyb2w=";
  const std::string encoded_negative_control =
      absl::Base64Escape("StrippingTest.NegativeControl");

  // Verify this mainly so we can encode other strings and know definitely they
  // won't encode to `kEncodedPositiveControl`.
  EXPECT_THAT(Base64UnescapeOrDie("U3RyaXBwaW5nVGVzdC5Qb3NpdGl2ZUNvbnRyb2w="),
              Eq("StrippingTest.PositiveControl"));

  auto exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());
  EXPECT_THAT(exe.get(), FileHasSubstr(kEncodedPositiveControl));
  EXPECT_THAT(exe.get(), Not(FileHasSubstr(encoded_negative_control)));
}

TEST_F(StrippingTest, Literal) {
  // We need to load a copy of the needle string into memory (so we can search
  // for it) without leaving it lying around in plaintext in the executable file
  // as would happen if we used a literal.  We might (or might not) leave it
  // lying around later; that's what the tests are for!
  const std::string needle = absl::Base64Escape("StrippingTest.Literal");
  LOG(INFO) << "U3RyaXBwaW5nVGVzdC5MaXRlcmFs";
  auto exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());
  if (absl::LogSeverity::kInfo >= kAbslMinLogLevel) {
    EXPECT_THAT(exe.get(), FileHasSubstr(needle));
  } else {
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(needle)));
  }
}

TEST_F(StrippingTest, LiteralInExpression) {
  // We need to load a copy of the needle string into memory (so we can search
  // for it) without leaving it lying around in plaintext in the executable file
  // as would happen if we used a literal.  We might (or might not) leave it
  // lying around later; that's what the tests are for!
  const std::string needle =
      absl::Base64Escape("StrippingTest.LiteralInExpression");
  LOG(INFO) << absl::StrCat("secret: ",
                            "U3RyaXBwaW5nVGVzdC5MaXRlcmFsSW5FeHByZXNzaW9u");
  std::unique_ptr<FILE, std::function<void(FILE*)>> exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());
  if (absl::LogSeverity::kInfo >= kAbslMinLogLevel) {
    EXPECT_THAT(exe.get(), FileHasSubstr(needle));
  } else {
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(needle)));
  }
}

TEST_F(StrippingTest, Fatal) {
  // We need to load a copy of the needle string into memory (so we can search
  // for it) without leaving it lying around in plaintext in the executable file
  // as would happen if we used a literal.  We might (or might not) leave it
  // lying around later; that's what the tests are for!
  const std::string needle = absl::Base64Escape("StrippingTest.Fatal");
  // We don't care if the LOG statement is actually executed, we're just
  // checking that it's stripped.
  if (kReallyDie) LOG(FATAL) << "U3RyaXBwaW5nVGVzdC5GYXRhbA==";

  std::unique_ptr<FILE, std::function<void(FILE*)>> exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());
  if (absl::LogSeverity::kFatal >= kAbslMinLogLevel) {
    EXPECT_THAT(exe.get(), FileHasSubstr(needle));
  } else {
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(needle)));
  }
}

TEST_F(StrippingTest, DFatal) {
  // We need to load a copy of the needle string into memory (so we can search
  // for it) without leaving it lying around in plaintext in the executable file
  // as would happen if we used a literal.  We might (or might not) leave it
  // lying around later; that's what the tests are for!
  const std::string needle = absl::Base64Escape("StrippingTest.DFatal");
  // We don't care if the LOG statement is actually executed, we're just
  // checking that it's stripped.
  if (kReallyDie) LOG(DFATAL) << "U3RyaXBwaW5nVGVzdC5ERmF0YWw=";

  std::unique_ptr<FILE, std::function<void(FILE*)>> exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());
  // `DFATAL` can be `ERROR` or `FATAL`, and a compile-time optimizer doesn't
  // know which, because `absl::kLogDebugFatal` is declared `extern` and defined
  // in another TU.  Link-time optimization might do better.  We have six cases:
  // |         `AMLL` is-> | `<=ERROR` | `FATAL` | `>FATAL` |
  // | ------------------- | --------- | ------- | -------- |
  // | `DFATAL` is `ERROR` |   present |       ? | stripped |
  // | `DFATAL` is `FATAL` |   present | present | stripped |

  // These constexpr variables are used to suppress unreachable code warnings
  // in the if-else statements below.

  // "present" in the table above: `DFATAL` exceeds `ABSL_MIN_LOG_LEVEL`, so
  // `DFATAL` statements should not be stripped (and they should be logged
  // when executed, but that's a different testsuite).
  constexpr bool kExpectPresent = absl::kLogDebugFatal >= kAbslMinLogLevel;

  // "stripped" in the table above: even though the compiler may not know
  // which value `DFATAL` has, it should be able to strip it since both
  // possible values ought to be stripped.
  constexpr bool kExpectStripped = kAbslMinLogLevel > absl::LogSeverity::kFatal;

  if (kExpectPresent) {
    EXPECT_THAT(exe.get(), FileHasSubstr(needle));
  } else if (kExpectStripped) {
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(needle)));
  } else {
    // "?" in the table above; may or may not be stripped depending on whether
    // any link-time optimization is done.  Either outcome is ok.
  }
}

TEST_F(StrippingTest, Level) {
  const std::string needle = absl::Base64Escape("StrippingTest.Level");
  volatile auto severity = absl::LogSeverity::kWarning;
  // Ensure that `severity` is not a compile-time constant to prove that
  // stripping works regardless:
  LOG(LEVEL(severity)) << "U3RyaXBwaW5nVGVzdC5MZXZlbA==";
  std::unique_ptr<FILE, std::function<void(FILE*)>> exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());
  if (absl::LogSeverity::kFatal >= kAbslMinLogLevel) {
    // This can't be stripped at compile-time because it might evaluate to a
    // level that shouldn't be stripped.
    EXPECT_THAT(exe.get(), FileHasSubstr(needle));
  } else {
#if (defined(_MSC_VER) && !defined(__clang__)) || defined(__APPLE__)
    // Dead code elimination misses this case.
#else
    // All levels should be stripped, so it doesn't matter what the severity
    // winds up being.
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(needle)));
#endif
  }
}

TEST_F(StrippingTest, Check) {
  // Here we also need a variable name with enough entropy that it's unlikely to
  // appear in the binary by chance.  `volatile` keeps the tautological
  // comparison (and the rest of the `CHECK`) from being optimized away.
  const std::string var_needle = absl::Base64Escape("StrippingTestCheckVar");
  const std::string msg_needle = absl::Base64Escape("StrippingTest.Check");
  volatile int U3RyaXBwaW5nVGVzdENoZWNrVmFy = 0xCAFE;
  // We don't care if the CHECK is actually executed, just that stripping works.
  // Hiding it behind `kReallyDie` works around some overly aggressive
  // optimizations in older versions of MSVC.
  if (kReallyDie) {
    CHECK(U3RyaXBwaW5nVGVzdENoZWNrVmFy != U3RyaXBwaW5nVGVzdENoZWNrVmFy)
        << "U3RyaXBwaW5nVGVzdC5DaGVjaw==";
  }

  std::unique_ptr<FILE, std::function<void(FILE*)>> exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());
  if (absl::LogSeverity::kFatal >= kAbslMinLogLevel) {
    EXPECT_THAT(exe.get(), FileHasSubstr(var_needle));
    EXPECT_THAT(exe.get(), FileHasSubstr(msg_needle));
  } else {
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(var_needle)));
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(msg_needle)));
  }
}

TEST_F(StrippingTest, CheckOp) {
  // See `StrippingTest.Check` for some hairy implementation notes.
  const std::string var_needle1 =
      absl::Base64Escape("StrippingTestCheckOpVar1");
  const std::string var_needle2 =
      absl::Base64Escape("StrippingTestCheckOpVar2");
  const std::string msg_needle = absl::Base64Escape("StrippingTest.CheckOp");
  volatile int U3RyaXBwaW5nVGVzdENoZWNrT3BWYXIx = 0xFEED;
  volatile int U3RyaXBwaW5nVGVzdENoZWNrT3BWYXIy = 0xCAFE;
  if (kReallyDie) {
    CHECK_EQ(U3RyaXBwaW5nVGVzdENoZWNrT3BWYXIx, U3RyaXBwaW5nVGVzdENoZWNrT3BWYXIy)
        << "U3RyaXBwaW5nVGVzdC5DaGVja09w";
  }

  std::unique_ptr<FILE, std::function<void(FILE*)>> exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());

  if (absl::LogSeverity::kFatal >= kAbslMinLogLevel) {
    EXPECT_THAT(exe.get(), FileHasSubstr(var_needle1));
    EXPECT_THAT(exe.get(), FileHasSubstr(var_needle2));
    EXPECT_THAT(exe.get(), FileHasSubstr(msg_needle));
  } else {
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(var_needle1)));
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(var_needle2)));
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(msg_needle)));
  }
}

TEST_F(StrippingTest, CheckStrOp) {
  // See `StrippingTest.Check` for some hairy implementation notes.
  const std::string var_needle1 =
      absl::Base64Escape("StrippingTestCheckStrOpVar1");
  const std::string var_needle2 =
      absl::Base64Escape("StrippingTestCheckStrOpVar2");
  const std::string msg_needle = absl::Base64Escape("StrippingTest.CheckStrOp");
  const char *volatile U3RyaXBwaW5nVGVzdENoZWNrU3RyT3BWYXIx = "FEED";
  const char *volatile U3RyaXBwaW5nVGVzdENoZWNrU3RyT3BWYXIy = "CAFE";
  if (kReallyDie) {
    CHECK_STREQ(U3RyaXBwaW5nVGVzdENoZWNrU3RyT3BWYXIx,
                U3RyaXBwaW5nVGVzdENoZWNrU3RyT3BWYXIy)
        << "U3RyaXBwaW5nVGVzdC5DaGVja1N0ck9w";
  }

  std::unique_ptr<FILE, std::function<void(FILE*)>> exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());

  if (absl::LogSeverity::kFatal >= kAbslMinLogLevel) {
    EXPECT_THAT(exe.get(), FileHasSubstr(var_needle1));
    EXPECT_THAT(exe.get(), FileHasSubstr(var_needle2));
    EXPECT_THAT(exe.get(), FileHasSubstr(msg_needle));
  } else {
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(var_needle1)));
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(var_needle2)));
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(msg_needle)));
  }
}

TEST_F(StrippingTest, CheckOk) {
  // See `StrippingTest.Check` for some hairy implementation notes.
  const std::string var_needle = absl::Base64Escape("StrippingTestCheckOkVar1");
  const std::string msg_needle = absl::Base64Escape("StrippingTest.CheckOk");
  volatile bool x = false;
  auto U3RyaXBwaW5nVGVzdENoZWNrT2tWYXIx = absl::OkStatus();
  if (x) {
    U3RyaXBwaW5nVGVzdENoZWNrT2tWYXIx =
        absl::InvalidArgumentError("Stripping this is not my job!");
  }
  if (kReallyDie) {
    CHECK_OK(U3RyaXBwaW5nVGVzdENoZWNrT2tWYXIx)
        << "U3RyaXBwaW5nVGVzdC5DaGVja09r";
  }

  std::unique_ptr<FILE, std::function<void(FILE*)>> exe = OpenTestExecutable();
  ASSERT_THAT(exe, NotNull());

  if (absl::LogSeverity::kFatal >= kAbslMinLogLevel) {
    EXPECT_THAT(exe.get(), FileHasSubstr(var_needle));
    EXPECT_THAT(exe.get(), FileHasSubstr(msg_needle));
  } else {
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(var_needle)));
    EXPECT_THAT(exe.get(), Not(FileHasSubstr(msg_needle)));
  }
}

}  // namespace
