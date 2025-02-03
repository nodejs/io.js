#!/usr/bin/env python3
# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import sys
import subprocess
import re
import math
from pathlib import Path
from typing import List, Union

INPUT_PATH = Path("src/parsing/keywords.txt")
OUTPUT_PATH = Path("src/parsing/keywords-gen.h")

# TODO(leszeks): Trimming seems to regress performance, investigate.
TRIM_CHAR_TABLE: bool = False


def next_power_of_2(x):
  return 1 if x == 0 else 2**int(math.ceil(math.log(x, 2)))


def call_with_input(cmd: List[Union[str, Path]], input_string: str = "") -> str:
  p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
  stdout, _ = p.communicate(input_string.encode())
  retcode = p.wait()
  if retcode != 0:
    raise subprocess.CalledProcessError(retcode, cmd)
  return stdout.decode()


def checked_sub(pattern: Union[str, re.Pattern[str]],
                sub: str,
                out: str,
                count: int = 1,
                flags: int = 0) -> str:
  out, n = re.subn(pattern, sub, out, flags=flags)
  if n != count:
    raise Exception("Expected %d and got %d replacement(s) for pattern: %s" %
                    (count, n, pattern))
  return out


def change_sizet_to_int(out: str) -> str:
  # Literal buffer lengths are given as ints, not size_t
  return checked_sub(r'\bsize_t\b', 'int', out, count=4)


def drop_line_directives(out: str) -> str:
  # #line causes gcov issue, so drop it
  return re.sub(r'^#\s*line .*$\n', '', out, flags=re.MULTILINE)


def trim_and_dcheck_char_table(out: str) -> str:
  # Potential keyword strings are known to be lowercase ascii, so chop off the
  # rest of the table and mask out the char

  reads_re = re.compile(
      r'asso_values\[static_cast<unsigned char>\((str\[\w+\](\+\d)?)\)\]')

  dchecks = []
  for str_read in reads_re.finditer(out):
    dchecks.append("DCHECK_LT(%s, 129);" % str_read.group(1))

  if TRIM_CHAR_TABLE:
    out = checked_sub(
        r'static const unsigned char asso_values\[\]\s*=\s*\{(\s*\d+\s*,){96}',
        "".join(dchecks) + r'static const unsigned char asso_values[32] = {',
        out,
        flags=re.MULTILINE)
    out = checked_sub(
        reads_re.pattern,
        r'asso_values[static_cast<unsigned char>(str[(\1)]&31)]',
        out,
        count=len(dchecks),
        flags=re.MULTILINE)
  else:
    out = checked_sub(
        r'static const unsigned char asso_values\[\]\s*=\s*\{',
        "".join(dchecks) + r'static const unsigned char asso_values[129] = {',
        out,
        flags=re.MULTILINE)

  return out


def use_isinrange(out: str) -> str:
  # Our IsInRange method is more efficient than checking for min/max length
  return checked_sub(r'if \(len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH\)',
                     r'if (base::IsInRange(len, MIN_WORD_LENGTH, '
                     + r'MAX_WORD_LENGTH))',
                     out)


def pad_tables(out: str) -> str:
  # We don't want to compare against the max hash value, so pad the tables up
  # to a power of two and mask the hash.

  # First get the new size
  max_hash_value = int(re.search(r'MAX_HASH_VALUE\s*=\s*(\d+)', out).group(1))
  old_table_length = max_hash_value + 1
  new_table_length = next_power_of_2(old_table_length)
  table_padding_len = new_table_length - old_table_length

  # Pad the length table.
  single_lengthtable_entry = r'\d+'
  out = checked_sub(
      r"""
      static\ const\ unsigned\ char\ kPerfectKeywordLengthTable\[\]\s*=\s*\{
        (
          \s*%(single_lengthtable_entry)s\s*
          (?:,\s*%(single_lengthtable_entry)s\s*)*
        )
      \}
    """ % {'single_lengthtable_entry': single_lengthtable_entry},
      r'static const unsigned char kPerfectKeywordLengthTable[%d] = { \1 %s }'
      % (new_table_length, "".join([',0'] * table_padding_len)),
      out,
      flags=re.MULTILINE | re.VERBOSE)

  # Pad the word list.
  single_wordlist_entry = r"""
      (?:\#line\ \d+\ ".*"$\s*)?
      \{\s*"[a-z]*"\s*,\s*Token::[a-zA-Z_]+\}
    """
  out = checked_sub(
      r"""
      static\ const\ struct\ PerfectKeywordHashTableEntry\ kPerfectKeywordHashTable\[\]\s*=\s*\{
        (
          \s*%(single_wordlist_entry)s\s*
          (?:,\s*%(single_wordlist_entry)s\s*)*
        )
      \}
    """ % {'single_wordlist_entry': single_wordlist_entry},
      r'static const struct PerfectKeywordHashTableEntry kPerfectKeywordHashTable[%d] = {\1 %s }'
      % (new_table_length, "".join(
          [',{"",Token::kIdentifier}'] * table_padding_len)),
      out,
      flags=re.MULTILINE | re.VERBOSE)

  # Mask the hash and replace the range check with DCHECKs.
  out = checked_sub(r'Hash\s*\(\s*str,\s*len\s*\)',
                    r'Hash(str, len)&0x%x' % (new_table_length - 1), out)
  out = checked_sub(
      r'if \(key <= MAX_HASH_VALUE\)',
      r'DCHECK_LT(key, arraysize(kPerfectKeywordLengthTable));DCHECK_LT(key, arraysize(kPerfectKeywordHashTable));',
      out)

  return out


def return_token(out: str) -> str:
  # We want to return the actual token rather than the table entry.

  # Change the return type of the function. Make it inline too.
  out = checked_sub(
      r'const\s*struct\s*PerfectKeywordHashTableEntry\s*\*\s*((?:PerfectKeywordHash::)?GetToken)',
      r'inline Token::Value \1',
      out,
      count=2)

  # Change the return value when the keyword is found
  out = checked_sub(r'return &kPerfectKeywordHashTable\[key\];',
                    r'return kPerfectKeywordHashTable[key].value;', out)

  # Change the return value when the keyword is not found
  out = checked_sub(r'return 0;', r'return Token::kIdentifier;', out)

  return out


def memcmp_to_while(out: str) -> str:
  # It's faster to loop over the keyword with a while loop than calling memcmp.
  # Careful, this replacement is quite flaky, because otherwise the regex is
  # unreadable.
  return checked_sub(
      re.escape("if (*str == *s && !memcmp (str + 1, s + 1, len - 1))") +
      r"\s*" + re.escape("return kPerfectKeywordHashTable[key].value;"),
      """
      while(*s!=0) {
        if (*s++ != *str++) return Token::kIdentifier;
      }
      return kPerfectKeywordHashTable[key].value;
      """,
      out,
      flags=re.MULTILINE)


def wrap_namespace(out):
  return """// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is automatically generated by gen-keywords-gen-h.py and should not
// be modified manually.

#ifndef V8_PARSING_KEYWORDS_GEN_H_
#define V8_PARSING_KEYWORDS_GEN_H_

#include "src/parsing/token.h"

namespace v8 {
namespace internal {

%s

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_KEYWORDS_GEN_H_
""" % (out)


def trim_character_set_warning(out: str) -> str:
  # gperf generates an error message that is too large, trim it

  return out.replace(
      '"gperf generated tables don\'t work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."',
      '"gperf generated tables don\'t work with this execution character set."\\\n// If you see this error, please report a bug to <bug-gperf@gnu.org>.'
  )


def main():
  try:
    script_dir = Path(sys.argv[0]).parent
    root_dir = script_dir.parent

    out: str = subprocess.check_output(["gperf", "-m100", INPUT_PATH],
                                       cwd=root_dir,
                                       encoding="UTF-8")

    # And now some munging of the generated file.
    out = change_sizet_to_int(out)
    out = drop_line_directives(out)
    out = trim_and_dcheck_char_table(out)
    out = use_isinrange(out)
    out = pad_tables(out)
    out = return_token(out)
    out = memcmp_to_while(out)
    out = wrap_namespace(out)
    out = trim_character_set_warning(out)

    # Final formatting.
    clang_format_path = root_dir / 'third_party/depot_tools/clang-format'
    out = call_with_input([clang_format_path], out)

    with (root_dir / OUTPUT_PATH).open('w') as f:
      f.write(out)

    return 0

  except subprocess.CalledProcessError as e:
    sys.stderr.write("Error calling '{}'\n".format(" ".join(e.cmd)))
    return e.returncode


if __name__ == '__main__':
  sys.exit(main())
