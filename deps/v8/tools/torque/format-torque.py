#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This program either generates the parser files for Torque, generating
the source and header files directly in V8's src directory."""

import sys
import re
import json
from subprocess import Popen, PIPE

def decode(arg, encoding="utf-8"):
  return arg.decode(encoding)

def encode(arg, encoding="utf-8"):
  return arg.encode(encoding)

kPercentEscape = r'α';  # Unicode alpha
kDerefEscape = r'☆'; # Unicode star
kAddressofEscape = r'⌂'; # Unicode house

def preprocess(input):
  # Special handing of '%' for intrinsics, turn the percent
  # into a unicode character so that it gets treated as part of the
  # intrinsic's name if it's already adjacent to it.
  input = re.sub(r'%([A-Za-z])', kPercentEscape + r'\1', input)
  # Similarly, avoid treating * and & as binary operators when they're
  # probably used as address operators.
  input = re.sub(r'([^/])\*([a-zA-Z(])', r'\1' + kDerefEscape + r'\2', input)
  input = re.sub(r'&([a-zA-Z(])', kAddressofEscape + r'\1', input)

  input = re.sub(r'(if\s+)constexpr(\s*\()', r'\1/*COxp*/\2', input)
  input = re.sub(r'\btypeswitch\s*(\([^{]*\))\s{', r' if /*tPsW*/ \1 {', input)
  input = re.sub(r'\bcase\s*(\([^{]*\))\s*:\s*deferred\s*{', r' if /*cAsEdEfF*/ \1 {', input)
  input = re.sub(r'\bcase\s*(\([^{]*\))\s*:\s*{', r' if /*cA*/ \1 {', input)

  input = re.sub(r'\bgenerates\s+\'([^\']+)\'\s*',
      r'_GeNeRaTeS00_/*\1@*/', input)
  input = re.sub(r'\bconstexpr\s+\'([^\']+)\'\s*', r'_CoNsExP_/*\1@*/', input)

  def createFunctionReplacement(m):
    torque_def = m.group(1)
    torque_def = re.sub('\s+', ' ', torque_def)
    function_len = len("function")
    function_and_comment_len = len("function /**/")
    if len(torque_def) < function_len:
      return f'// !!torquefunc {torque_def}\nfunction '
    else:
      return f'// !!torquefunc {torque_def}\nfunction /*{"-" * (len(torque_def) - function_and_comment_len)}*/ '

  input = re.sub(
      r'^[ \t]*((?:extern\s+)?(?:transitioning\s+)?(?:javascript\s+)?(?:operator\s*\'[^\']+\'\s*)?(?:macro|builtin|runtime))\s+',
      createFunctionReplacement,
      input,
      flags=re.MULTILINE)

  def createClassReplacement(m):
    torque_def = m.group(1)
    class_len = len("class")
    class_and_comment_len = len("class /**/")
    if len(torque_def) < class_len:
      return f'// !!torqueclass {torque_def}\nclass '
    else:
      return f'// !!torqueclass {torque_def}\nclass /*{"-" * (len(torque_def) - class_and_comment_len)}*/ '

  input = re.sub(
      r'^[ \t]*((?:extern\s+)?(?:bitfield\s+)?(?:struct|class|shape))\s+',
      createClassReplacement,
      input,
      flags=re.MULTILINE)

  input = re.sub(r'\notherwise',
      r'\n otherwise', input)
  input = re.sub(r'(\n\s*\S[^\n]*\s)otherwise',
      r'\1_OtheSaLi', input)
  input = re.sub(r'@if(not)?\(', r'if /*!if\1*/ (', input)
  input = re.sub(
      r'(js-)?implicit\s+([^)]*?)\s*\)\(\s*\)',
      r'/*\1ImPl*/\2Ǝ)',
      input,
      flags=re.DOTALL)
  input = re.sub(
      r'(js-)?implicit\s+([^)]*?)\s*\)\(\s*',
      r'/*\1ImPl*/\2Ǝ,',
      input,
      flags=re.DOTALL)

  # clang-format doesn't like decorators, so change them into line comments.
  input = re.sub(
      r'^(\s*)@([a-zA-Z]+)(\([^)]*\))?\n',
      r'\1//@\2\3\n',
      input,
      flags=re.MULTILINE)
  input = re.sub(r'^(\s*)@export\b', r'\1//@eXpOrT', input, flags=re.MULTILINE)

  # includes are not recognized, change them into comments so that the
  # formatter ignores them first, until we can figure out a way to format cpp
  # includes within a JS file.
  input = re.sub(r'^#include', r'// InClUdE', input, flags=re.MULTILINE)

  return input

def postprocess(output):
  output = re.sub(
      r'^(\s*)//@eXpOrT\b', r'\1@export', output, flags=re.MULTILINE)
  output = re.sub(
      r'^(\s*)//@([a-zA-Z]+)(\([^)]*\))?\n',
      r"\1@\2\3\n",
      output,
      flags=re.MULTILINE)

  output = re.sub(r'\/\*COxp\*\/', r'constexpr', output)
  output = re.sub(r'(\S+)\s*: type([,>])', r'\1: type\2', output)
  output = re.sub(r'(\n\s*)labels( [A-Z])', r'\1    labels\2', output)
  output = re.sub(r'\bif\s*\/\*tPsW\*\/', r'typeswitch', output)
  output = re.sub(r'\bif\s*\/\*cA\*\/\s*(\([^{]*\))\s*{', r'case \1: {', output)
  output = re.sub(r'\bif\s*\/\*cAsEdEfF\*\/\s*(\([^{]*\))\s*{', r'case \1: deferred {', output)
  output = re.sub(r'\n_GeNeRaTeS00_\s*\/\*([^@]+)@\*\/',
      r"\n    generates '\1'", output)
  output = re.sub(r'_GeNeRaTeS00_\s*\/\*([^@]+)@\*\/',
      r"generates '\1'", output)
  output = re.sub(r'\n_CoNsExP_\s*\/\*([^@]+)@\*\/', r"\n    constexpr '\1'",
                  output)
  output = re.sub(r'_CoNsExP_\s*\/\*([^@]+)@\*\/',
      r"constexpr '\1'", output)
  output = re.sub(r'// !!torqueclass (.*)\n\s*class(?:\s*/\*-*\*/)?', r"\1",
                  output)
  output = re.sub(r'// !!torquefunc (.*)\n\s*function(?:\s*/\*-*\*/)?', r"\1",
                  output)
  output = re.sub(r'\n(\s+)otherwise', r"\n\1    otherwise", output)
  output = re.sub(r'\n(\s+)_OtheSaLi', r"\n\1otherwise", output)
  output = re.sub(r'_OtheSaLi', r"otherwise", output)
  output = re.sub(r'if\s*/\*!if(not)?\*/\s*\(', r'@if\1(', output)
  output = re.sub(
      r'/\*\s*(js-)?ImPl\s*\*/\s*([^Ǝ]*?)Ǝ\s*\)',
      r'\1implicit \2)()',
      output,
      flags=re.DOTALL)
  output = re.sub(
      r'/\*\s*(js-)?ImPl\s*\*/\s*([^Ǝ]*?)Ǝ\s*, *',
      r'\1implicit \2)(',
      output,
      flags=re.DOTALL)
  output = re.sub(r'}\n *label ', r'} label ', output);

  output = re.sub(kPercentEscape, r'%', output)
  output = re.sub(kDerefEscape, r'*', output)
  output = re.sub(kAddressofEscape, r'&', output)

  output = re.sub( r'^// InClUdE',r'#include', output, flags=re.MULTILINE)

  return output

def process(filename, lint, should_format):
  with open(filename, 'r') as content_file:
    content = content_file.read()

  original_input = content

  style = {"BasedOnStyle": "Google", "NamespaceIndentation": "None"}

  if sys.platform.startswith('win'):
    p = Popen(
        ['clang-format', '-assume-filename=.ts', '-style=' + json.dumps(style)],
        stdin=PIPE,
        stdout=PIPE,
        stderr=PIPE,
        shell=True)
  else:
    p = Popen(
        ['clang-format', '-assume-filename=.ts', '-style=' + json.dumps(style)],
        stdin=PIPE,
        stdout=PIPE,
        stderr=PIPE)
  output, err = p.communicate(encode(preprocess(content)))
  output = postprocess(decode(output))
  rc = p.returncode
  if (rc != 0):
    print("error code " + str(rc) + " running clang-format. Exiting...")
    sys.exit(rc);

  if (output != original_input):
    if lint:
      print(filename + ' requires formatting', file=sys.stderr)

    if should_format:
      output_file = open(filename, 'wb')
      output_file.write(encode(output))
      output_file.close()

def print_usage():
  print('format-torque -i file1[, file2[, ...]]')
  print('    format and overwrite input files')
  print('format-torque -l file1[, file2[, ...]]')
  print('    merely indicate which files need formatting')

def Main():
  if len(sys.argv) < 3:
    print("error: at least 2 arguments required")
    print_usage();
    sys.exit(-1)

  def is_option(arg):
    return arg in ['-i', '-l', '-il']

  should_format = lint = False

  flag, files = sys.argv[1], sys.argv[2:]
  if is_option(flag):
    if '-i' == flag:
      should_format = True
    elif '-l' == flag:
      lint = True
    else:
      lint = True
      should_format = True
  else:
    print("error: -i and/or -l flags must be specified")
    print_usage();
    sys.exit(-1);

  for filename in files:
    process(filename, lint, should_format)

  return 0

if __name__ == '__main__':
  sys.exit(Main());
