#include "nbytes.h"
#include "simdutf.h"
#include "util-inl.h"

#include <cstddef>
#include <cstring>

#include "gtest/gtest.h"

TEST(Base64Test, Encode) {
  auto test = [](const char* string, const char* base64_string) {
    const size_t len = strlen(base64_string);
    const size_t slen = strlen(string);
    char* const buffer = new char[len + 1];
    buffer[len] = 0;
    CHECK(len >= simdutf::base64_length_from_binary(slen) &&
          "not enough space provided for base64 encode");
    simdutf::binary_to_base64(string, slen, buffer);
    EXPECT_STREQ(base64_string, buffer);
    delete[] buffer;
  };

  test("a", "YQ==");
  test("ab", "YWI=");
  test("abc", "YWJj");
  test("abcd", "YWJjZA==");
  test("abcde", "YWJjZGU=");
  test("abcdef", "YWJjZGVm");

  test("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
       "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut "
       "enim ad minim veniam, quis nostrud exercitation ullamco laboris "
       "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
       "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
       "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
       "culpa qui officia deserunt mollit anim id est laborum.",

       "TG9yZW0gaXBzdW0gZG9sb3Igc2l0IGFtZXQsIGNvbnNlY3RldHVyIGFkaXBpc2Npbmcg"
       "ZWxpdCwgc2VkIGRvIGVpdXNtb2QgdGVtcG9yIGluY2lkaWR1bnQgdXQgbGFib3JlIGV0"
       "IGRvbG9yZSBtYWduYSBhbGlxdWEuIFV0IGVuaW0gYWQgbWluaW0gdmVuaWFtLCBxdWlz"
       "IG5vc3RydWQgZXhlcmNpdGF0aW9uIHVsbGFtY28gbGFib3JpcyBuaXNpIHV0IGFsaXF1"
       "aXAgZXggZWEgY29tbW9kbyBjb25zZXF1YXQuIER1aXMgYXV0ZSBpcnVyZSBkb2xvciBp"
       "biByZXByZWhlbmRlcml0IGluIHZvbHVwdGF0ZSB2ZWxpdCBlc3NlIGNpbGx1bSBkb2xv"
       "cmUgZXUgZnVnaWF0IG51bGxhIHBhcmlhdHVyLiBFeGNlcHRldXIgc2ludCBvY2NhZWNh"
       "dCBjdXBpZGF0YXQgbm9uIHByb2lkZW50LCBzdW50IGluIGN1bHBhIHF1aSBvZmZpY2lh"
       "IGRlc2VydW50IG1vbGxpdCBhbmltIGlkIGVzdCBsYWJvcnVtLg==");
}

TEST(Base64Test, EncodeURL) {
  auto test = [](const char* string, const char* base64_string) {
    const size_t len = strlen(base64_string);
    const size_t slen = strlen(string);
    char* const buffer = new char[len + 1];
    buffer[len] = 0;
    CHECK(len >=
              simdutf::base64_length_from_binary(slen, simdutf::base64_url) &&
          "not enough space provided for base64 encode");
    simdutf::binary_to_base64(string, slen, buffer, simdutf::base64_url);
    EXPECT_STREQ(base64_string, buffer);
    delete[] buffer;
  };

  test("\x68\xd9\x16\x25\x5c\x1e\x40\x92\x2d\xfb", "aNkWJVweQJIt-w");
  test("\xac\xc7\x93\xaa\x83\x6f\xc3\xe3\x3f\x75", "rMeTqoNvw-M_dQ");
}

TEST(Base64Test, Decode) {
  auto test = [](const char* base64_string, const char* string) {
    const size_t len = strlen(string);
    char* const buffer = new char[len + 1];
    buffer[len] = 0;
    nbytes::Base64Decode(buffer, len, base64_string, strlen(base64_string));
    EXPECT_STREQ(string, buffer);
    delete[] buffer;
  };

  test("YQ", "a");
  test("Y Q", "a");
  test("Y Q ", "a");
  test(" Y Q", "a");
  test("Y Q==", "a");
  test("YQ ==", "a");
  test("YQ == junk", "a");
  test("YWI", "ab");
  test("YWI=", "ab");
  test("YWJj", "abc");
  test("YWJjZA", "abcd");
  test("YWJjZA==", "abcd");
  test("YW Jj ZA ==", "abcd");
  test("YWJjZGU=", "abcde");
  test("YWJjZGVm", "abcdef");
  test("Y WJjZGVm", "abcdef");
  test("YW JjZGVm", "abcdef");
  test("YWJ jZGVm", "abcdef");
  test("YWJj ZGVm", "abcdef");
  test("Y W J j Z G V m", "abcdef");
  test("Y   W\n JjZ \nG Vm", "abcdef");
  test("rMeTqoNvw-M_dQ", "\xac\xc7\x93\xaa\x83\x6f\xc3\xe3\x3f\x75");

  const char* text =
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
      "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut "
      "enim ad minim veniam, quis nostrud exercitation ullamco laboris "
      "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
      "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
      "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
      "culpa qui officia deserunt mollit anim id est laborum.";

  test("TG9yZW0gaXBzdW0gZG9sb3Igc2l0IGFtZXQsIGNvbnNlY3RldHVyIGFkaXBpc2Npbmcg"
       "ZWxpdCwgc2VkIGRvIGVpdXNtb2QgdGVtcG9yIGluY2lkaWR1bnQgdXQgbGFib3JlIGV0"
       "IGRvbG9yZSBtYWduYSBhbGlxdWEuIFV0IGVuaW0gYWQgbWluaW0gdmVuaWFtLCBxdWlz"
       "IG5vc3RydWQgZXhlcmNpdGF0aW9uIHVsbGFtY28gbGFib3JpcyBuaXNpIHV0IGFsaXF1"
       "aXAgZXggZWEgY29tbW9kbyBjb25zZXF1YXQuIER1aXMgYXV0ZSBpcnVyZSBkb2xvciBp"
       "biByZXByZWhlbmRlcml0IGluIHZvbHVwdGF0ZSB2ZWxpdCBlc3NlIGNpbGx1bSBkb2xv"
       "cmUgZXUgZnVnaWF0IG51bGxhIHBhcmlhdHVyLiBFeGNlcHRldXIgc2ludCBvY2NhZWNh"
       "dCBjdXBpZGF0YXQgbm9uIHByb2lkZW50LCBzdW50IGluIGN1bHBhIHF1aSBvZmZpY2lh"
       "IGRlc2VydW50IG1vbGxpdCBhbmltIGlkIGVzdCBsYWJvcnVtLg==", text);

  test("TG9yZW0gaXBzdW0gZG9sb3Igc2l0IGFtZXQsIGNvbnNlY3RldHVyIGFkaXBpc2Npbmcg"
       "ZWxpdCwgc2VkIGRvIGVpdXNtb2QgdGVtcG9yIGluY2lkaWR1bnQgdXQgbGFib3JlIGV0"
       "IGRvbG9yZSBtYWduYSBhbGlxdWEuIFV0IGVuaW0gYWQgbWluaW0gdmVuaWFtLCBxdWlz"
       "IG5vc3RydWQgZXhlcmNpdGF0aW9uIHVsbGFtY28gbGFib3JpcyBuaXNpIHV0IGFsaXF1"
       "aXAgZXggZWEgY29tbW9kbyBjb25zZXF1YXQuIER1aXMgYXV0ZSBpcnVyZSBkb2xvciBp"
       "biByZXByZWhlbmRlcml0IGluIHZvbHVwdGF0ZSB2ZWxpdCBlc3NlIGNpbGx1bSBkb2xv"
       "cmUgZXUgZnVnaWF0IG51bGxhIHBhcmlhdHVyLiBFeGNlcHRldXIgc2ludCBvY2NhZWNh"
       "dCBjdXBpZGF0YXQgbm9uIHByb2lkZW50LCBzdW50IGluIGN1bHBhIHF1aSBvZmZpY2lh"
       "IGRlc2VydW50IG1vbGxpdCBhbmltIGlkIGVzdCBsYWJvcnVtLg", text);

  test("TG9yZW0gaXBzdW0gZG9sb3Igc2l0IGFtZXQsIGNvbnNlY3RldHVyIGFkaXBpc2Npbmcg\n"
       "ZWxpdCwgc2VkIGRvIGVpdXNtb2QgdGVtcG9yIGluY2lkaWR1bnQgdXQgbGFib3JlIGV0\n"
       "IGRvbG9yZSBtYWduYSBhbGlxdWEuIFV0IGVuaW0gYWQgbWluaW0gdmVuaWFtLCBxdWlz\n"
       "IG5vc3RydWQgZXhlcmNpdGF0aW9uIHVsbGFtY28gbGFib3JpcyBuaXNpIHV0IGFsaXF1\n"
       "aXAgZXggZWEgY29tbW9kbyBjb25zZXF1YXQuIER1aXMgYXV0ZSBpcnVyZSBkb2xvciBp\n"
       "biByZXByZWhlbmRlcml0IGluIHZvbHVwdGF0ZSB2ZWxpdCBlc3NlIGNpbGx1bSBkb2xv\n"
       "cmUgZXUgZnVnaWF0IG51bGxhIHBhcmlhdHVyLiBFeGNlcHRldXIgc2ludCBvY2NhZWNh\n"
       "dCBjdXBpZGF0YXQgbm9uIHByb2lkZW50LCBzdW50IGluIGN1bHBhIHF1aSBvZmZpY2lh\n"
       "IGRlc2VydW50IG1vbGxpdCBhbmltIGlkIGVzdCBsYWJvcnVtLg==", text);

  test("TG9yZW0gaXBzdW0gZG9sb3Igc2l0IGFtZXQsIGNvbnNlY3RldHVyIGFkaXBpc2Npbmcg\n"
       "ZWxpdCwgc2VkIGRvIGVpdXNtb2QgdGVtcG9yIGluY2lkaWR1bnQgdXQgbGFib3JlIGV0\n"
       "IGRvbG9yZSBtYWduYSBhbGlxdWEuIFV0IGVuaW0gYWQgbWluaW0gdmVuaWFtLCBxdWlz\n"
       "IG5vc3RydWQgZXhlcmNpdGF0aW9uIHVsbGFtY28gbGFib3JpcyBuaXNpIHV0IGFsaXF1\n"
       "aXAgZXggZWEgY29tbW9kbyBjb25zZXF1YXQuIER1aXMgYXV0ZSBpcnVyZSBkb2xvciBp\n"
       "biByZXByZWhlbmRlcml0IGluIHZvbHVwdGF0ZSB2ZWxpdCBlc3NlIGNpbGx1bSBkb2xv\n"
       "cmUgZXUgZnVnaWF0IG51bGxhIHBhcmlhdHVyLiBFeGNlcHRldXIgc2ludCBvY2NhZWNh\n"
       "dCBjdXBpZGF0YXQgbm9uIHByb2lkZW50LCBzdW50IGluIGN1bHBhIHF1aSBvZmZpY2lh\n"
       "IGRlc2VydW50IG1vbGxpdCBhbmltIGlkIGVzdCBsYWJvcnVtLg", text);
}
