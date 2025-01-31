/* Copyright The libuv project and contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "task.h"
#define uv__malloc malloc
#include "../src/idna.c"
#include <string.h>

TEST_IMPL(utf8_decode1) {
  const char* p;
  char b[32];
  int i;

  /* ASCII. */
  p = b;
  snprintf(b, sizeof(b), "%c\x7F", 0x00);
  ASSERT_OK(uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 1);
  ASSERT_EQ(127, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 2);

  /* Two-byte sequences. */
  p = b;
  snprintf(b, sizeof(b), "%s", "\xC2\x80\xDF\xBF");
  ASSERT_EQ(128, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 2);
  ASSERT_EQ(0x7FF, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 4);

  /* Three-byte sequences. */
  p = b;
  snprintf(b, sizeof(b), "%s", "\xE0\xA0\x80\xEF\xBF\xBF");
  ASSERT_EQ(0x800, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 3);
  ASSERT_EQ(0xFFFF, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 6);

  /* Four-byte sequences. */
  p = b;
  snprintf(b, sizeof(b), "%s", "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF");
  ASSERT_EQ(0x10000, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 4);
  ASSERT_EQ(0x10FFFF, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 8);

  /* Four-byte sequences > U+10FFFF; disallowed. */
  p = b;
  snprintf(b, sizeof(b), "%s", "\xF4\x90\xC0\xC0\xF7\xBF\xBF\xBF");
  ASSERT_EQ((unsigned) -1, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 4);
  ASSERT_EQ((unsigned) -1, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 8);

  /* Overlong; disallowed. */
  p = b;
  snprintf(b, sizeof(b), "%s", "\xC0\x80\xC1\x80");
  ASSERT_EQ((unsigned) -1, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 2);
  ASSERT_EQ((unsigned) -1, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 4);

  /* Surrogate pairs; disallowed. */
  p = b;
  snprintf(b, sizeof(b), "%s", "\xED\xA0\x80\xED\xA3\xBF");
  ASSERT_EQ((unsigned) -1, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 3);
  ASSERT_EQ((unsigned) -1, uv__utf8_decode1(&p, b + sizeof(b)));
  ASSERT_PTR_EQ(p, b + 6);

  /* Simply illegal. */
  p = b;
  snprintf(b, sizeof(b), "%s", "\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF");

  for (i = 1; i <= 8; i++) {
    ASSERT_EQ((unsigned) -1, uv__utf8_decode1(&p, b + sizeof(b)));
    ASSERT_PTR_EQ(p, b + i);
  }

  return 0;
}

TEST_IMPL(utf8_decode1_overrun) {
  const char* p;
  char b[1];
  char c[1];

  /* Single byte. */
  p = b;
  b[0] = 0x7F;
  ASSERT_EQ(0x7F, uv__utf8_decode1(&p, b + 1));
  ASSERT_PTR_EQ(p, b + 1);

  /* Multi-byte. */
  p = b;
  b[0] = 0xC0;
  ASSERT_EQ((unsigned) -1, uv__utf8_decode1(&p, b + 1));
  ASSERT_PTR_EQ(p, b + 1);

  b[0] = 0x7F;
  ASSERT_EQ(UV_EINVAL, uv__idna_toascii(b, b + 0, c, c + 1));
  ASSERT_EQ(UV_EINVAL, uv__idna_toascii(b, b + 1, c, c + 1));

  return 0;
}

/* Doesn't work on z/OS because that platform uses EBCDIC, not ASCII. */
#ifndef __MVS__

#define F(input, err)                                                         \
  do {                                                                        \
    char d[256] = {0};                                                        \
    static const char s[] = "" input "";                                      \
    ASSERT_EQ(err, uv__idna_toascii(s, s + sizeof(s) - 1, d, d + sizeof(d))); \
  } while (0)

#define T(input, expected)                                                    \
  do {                                                                        \
    long n;                                                                   \
    char d1[256] = {0};                                                       \
    char d2[256] = {0};                                                       \
    static const char s[] = "" input "";                                      \
    n = uv__idna_toascii(s, s + sizeof(s) - 1, d1, d1 + sizeof(d1));          \
    ASSERT_EQ(n, sizeof(expected));                                           \
    ASSERT_OK(memcmp(d1, expected, n));                                       \
    /* Sanity check: encoding twice should not change the output. */          \
    n = uv__idna_toascii(d1, d1 + strlen(d1), d2, d2 + sizeof(d2));           \
    ASSERT_EQ(n, sizeof(expected));                                           \
    ASSERT_OK(memcmp(d2, expected, n));                                       \
    ASSERT_OK(memcmp(d1, d2, sizeof(d2)));                                    \
  } while (0)

TEST_IMPL(idna_toascii) {
  /* Illegal inputs. */
  F("\xC0\x80\xC1\x80", UV_EINVAL);  /* Overlong UTF-8 sequence. */
  F("\xC0\x80\xC1\x80.com", UV_EINVAL);  /* Overlong UTF-8 sequence. */
  F("", UV_EINVAL);
  /* No conversion. */
  T(".", ".");
  T(".com", ".com");
  T("example", "example");
  T("example-", "example-");
  T("straße.de", "xn--strae-oqa.de");
  /* Test cases adapted from punycode.js. Most are from RFC 3492. */
  T("foo.bar", "foo.bar");
  T("mañana.com", "xn--maana-pta.com");
  T("example.com.", "example.com.");
  T("bücher.com", "xn--bcher-kva.com");
  T("café.com", "xn--caf-dma.com");
  T("café.café.com", "xn--caf-dma.xn--caf-dma.com");
  T("☃-⌘.com", "xn----dqo34k.com");
  T("퐀☃-⌘.com", "xn----dqo34kn65z.com");
  T("💩.la", "xn--ls8h.la");
  T("mañana.com", "xn--maana-pta.com");
  T("mañana。com", "xn--maana-pta.com");
  T("mañana．com", "xn--maana-pta.com");
  T("mañana｡com", "xn--maana-pta.com");
  T("ü", "xn--tda");
  T(".ü", ".xn--tda");
  T("ü.ü", "xn--tda.xn--tda");
  T("ü.ü.", "xn--tda.xn--tda.");
  T("üëäö♥", "xn--4can8av2009b");
  T("Willst du die Blüthe des frühen, die Früchte des späteren Jahres",
    "xn--Willst du die Blthe des frhen, "
    "die Frchte des spteren Jahres-x9e96lkal");
  T("ليهمابتكلموشعربي؟", "xn--egbpdaj6bu4bxfgehfvwxn");
  T("他们为什么不说中文", "xn--ihqwcrb4cv8a8dqg056pqjye");
  T("他們爲什麽不說中文", "xn--ihqwctvzc91f659drss3x8bo0yb");
  T("Pročprostěnemluvíčesky", "xn--Proprostnemluvesky-uyb24dma41a");
  T("למההםפשוטלאמדבריםעברית", "xn--4dbcagdahymbxekheh6e0a7fei0b");
  T("यहलोगहिन्दीक्योंनहींबोलसकतेहैं",
    "xn--i1baa7eci9glrd9b2ae1bj0hfcgg6iyaf8o0a1dig0cd");
  T("なぜみんな日本語を話してくれないのか",
    "xn--n8jok5ay5dzabd5bym9f0cm5685rrjetr6pdxa");
  T("세계의모든사람들이한국어를이해한다면얼마나좋을까",
    "xn--989aomsvi5e83db1d2a355cv1e0vak1d"
    "wrv93d5xbh15a0dt30a5jpsd879ccm6fea98c");
  T("почемужеонинеговорятпорусски", "xn--b1abfaaepdrnnbgefbadotcwatmq2g4l");
  T("PorquénopuedensimplementehablarenEspañol",
    "xn--PorqunopuedensimplementehablarenEspaol-fmd56a");
  T("TạisaohọkhôngthểchỉnóitiếngViệt",
    "xn--TisaohkhngthchnitingVit-kjcr8268qyxafd2f1b9g");
  T("3年B組金八先生", "xn--3B-ww4c5e180e575a65lsy2b");
  T("安室奈美恵-with-SUPER-MONKEYS",
    "xn---with-SUPER-MONKEYS-pc58ag80a8qai00g7n9n");
  T("Hello-Another-Way-それぞれの場所",
    "xn--Hello-Another-Way--fc4qua05auwb3674vfr0b");
  T("ひとつ屋根の下2", "xn--2-u9tlzr9756bt3uc0v");
  T("MajiでKoiする5秒前", "xn--MajiKoi5-783gue6qz075azm5e");
  T("パフィーdeルンバ", "xn--de-jg4avhby1noc0d");
  T("そのスピードで", "xn--d9juau41awczczp");
  T("-> $1.00 <-", "-> $1.00 <-");
  /* Test cases from https://unicode.org/reports/tr46/ */
  T("faß.de", "xn--fa-hia.de");
  T("βόλος.com", "xn--nxasmm1c.com");
  T("ශ්‍රී.com", "xn--10cl1a0b660p.com");
  T("نامه‌ای.com", "xn--mgba3gch31f060k.com");
  return 0;
}

#undef T

#endif  /* __MVS__ */

TEST_IMPL(wtf8) {
  static const char input[] = "ᜄȺy𐞲:𞢢𘴇𐀀'¥3̞[<i$";
  uint16_t buf[32];
  ssize_t len;

  len = uv_wtf8_length_as_utf16(input);
  ASSERT_GT(len, 0);
  ASSERT_LT(len, ARRAY_SIZE(buf));
  uv_wtf8_to_utf16(input, buf, len);
  return 0;
}
