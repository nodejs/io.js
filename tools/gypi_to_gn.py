#!/usr/bin/env python3
# Copyright 2014 The Chromium Authors. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#    * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#    * Neither the name of Google LLC nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Deleted from Chromium in https://crrev.com/097f64c631.

"""Converts a given gypi file to a python scope and writes the result to stdout.
USING THIS SCRIPT IN CHROMIUM
Forking Python to run this script in the middle of GN is slow, especially on
Windows, and it makes both the GYP and GN files harder to follow. You can't
use "git grep" to find files in the GN build any more, and tracking everything
in GYP down requires a level of indirection. Any calls will have to be removed
and cleaned up once the GYP-to-GN transition is complete.
As a result, we only use this script when the list of files is large and
frequently-changing. In these cases, having one canonical list outweighs the
downsides.
As of this writing, the GN build is basically complete. It's likely that all
large and frequently changing targets where this is appropriate use this
mechanism already. And since we hope to turn down the GYP build soon, the time
horizon is also relatively short. As a result, it is likely that no additional
uses of this script should every be added to the build. During this later part
of the transition period, we should be focusing more and more on the absolute
readability of the GN build.
HOW TO USE
It is assumed that the file contains a toplevel dictionary, and this script
will return that dictionary as a GN "scope" (see example below). This script
does not know anything about GYP and it will not expand variables or execute
conditions.
It will strip conditions blocks.
A variables block at the top level will be flattened so that the variables
appear in the root dictionary. This way they can be returned to the GN code.
Say your_file.gypi looked like this:
  {
     'sources': [ 'a.cc', 'b.cc' ],
     'defines': [ 'ENABLE_DOOM_MELON' ],
  }
You would call it like this:
  gypi_values = exec_script("//build/gypi_to_gn.py",
                            [ rebase_path("your_file.gypi") ],
                            "scope",
                            [ "your_file.gypi" ])
Notes:
 - The rebase_path call converts the gypi file from being relative to the
   current build file to being system absolute for calling the script, which
   will have a different current directory than this file.
 - The "scope" parameter tells GN to interpret the result as a series of GN
   variable assignments.
 - The last file argument to exec_script tells GN that the given file is a
   dependency of the build so Ninja can automatically re-run GN if the file
   changes.
Read the values into a target like this:
  component("mycomponent") {
    sources = gypi_values.sources
    defines = gypi_values.defines
  }
Sometimes your .gypi file will include paths relative to a different
directory than the current .gn file. In this case, you can rebase them to
be relative to the current directory.
  sources = rebase_path(gypi_values.sources, ".",
                        "//path/gypi/input/values/are/relative/to")
This script will tolerate a 'variables' in the toplevel dictionary or not. If
the toplevel dictionary just contains one item called 'variables', it will be
collapsed away and the result will be the contents of that dictinoary. Some
.gypi files are written with or without this, depending on how they expect to
be embedded into a .gyp file.
This script also has the ability to replace certain substrings in the input.
Generally this is used to emulate GYP variable expansion. If you passed the
argument "--replace=<(foo)=bar" then all instances of "<(foo)" in strings in
the input will be replaced with "bar":
  gypi_values = exec_script("//build/gypi_to_gn.py",
                            [ rebase_path("your_file.gypi"),
                              "--replace=<(foo)=bar"],
                            "scope",
                            [ "your_file.gypi" ])
"""

from __future__ import absolute_import
from __future__ import print_function
from optparse import OptionParser
import sys


# This function is copied from build/gn_helpers.py in Chromium.
def ToGNString(value, pretty=False):
  """Returns a stringified GN equivalent of a Python value.

  Args:
    value: The Python value to convert.
    pretty: Whether to pretty print. If true, then non-empty lists are rendered
        recursively with one item per line, with indents. Otherwise lists are
        rendered without new line.
  Returns:
    The stringified GN equivalent to |value|.

  Raises:
    ValueError: |value| cannot be printed to GN.
  """

  # Emits all output tokens without intervening whitespaces.
  def GenerateTokens(v, level):
    if isinstance(v, str):
      yield '"' + ''.join(TranslateToGnChars(v)) + '"'

    elif isinstance(v, bool):
      yield 'true' if v else 'false'

    elif isinstance(v, int):
      yield str(v)

    elif isinstance(v, list):
      yield '['
      for i, item in enumerate(v):
        if i > 0:
          yield ','
        for tok in GenerateTokens(item, level + 1):
          yield tok
      yield ']'

    elif isinstance(v, dict):
      if level > 0:
        yield '{'
      for key in sorted(v):
        if not isinstance(key, str):
          raise ValueError('Dictionary key is not a string.')
        if not key or key[0].isdigit() or not key.replace('_', '').isalnum():
          raise ValueError('Dictionary key is not a valid GN identifier.')
        yield key  # No quotations.
        yield '='
        for tok in GenerateTokens(v[key], level + 1):
          yield tok
      if level > 0:
        yield '}'

    else:  # Not supporting float: Add only when needed.
      raise ValueError('Unsupported type when printing to GN.')

  can_start = lambda tok: tok and tok not in ',}]='
  can_end = lambda tok: tok and tok not in ',{[='

  # Adds whitespaces, trying to keep everything (except dicts) in 1 line.
  def PlainGlue(gen):
    prev_tok = None
    for i, tok in enumerate(gen):
      if i > 0:
        if can_end(prev_tok) and can_start(tok):
          yield '\n'  # New dict item.
        elif prev_tok == '[' and tok == ']':
          yield '  '  # Special case for [].
        elif tok != ',':
          yield ' '
      yield tok
      prev_tok = tok

  # Adds whitespaces so non-empty lists can span multiple lines, with indent.
  def PrettyGlue(gen):
    prev_tok = None
    level = 0
    for i, tok in enumerate(gen):
      if i > 0:
        if can_end(prev_tok) and can_start(tok):
          yield '\n' + '  ' * level  # New dict item.
        elif tok == '=' or prev_tok in '=':
          yield ' '  # Separator before and after '=', on same line.
      if tok in ']}':
        level -= 1
      # Exclude '[]' and '{}' cases.
      if int(prev_tok == '[') + int(tok == ']') == 1 or \
         int(prev_tok == '{') + int(tok == '}') == 1:
        yield '\n' + '  ' * level
      yield tok
      if tok in '[{':
        level += 1
      if tok == ',':
        yield '\n' + '  ' * level
      prev_tok = tok

  token_gen = GenerateTokens(value, 0)
  ret = ''.join((PrettyGlue if pretty else PlainGlue)(token_gen))
  # Add terminating '\n' for dict |value| or multi-line output.
  if isinstance(value, dict) or '\n' in ret:
    return ret + '\n'
  return ret


def TranslateToGnChars(s):
  for code in s.encode('utf-8'):
    if code in (34, 36, 92):  # For '"', '$', or '\\'.
      yield '\\' + chr(code)
    elif 32 <= code < 127:
      yield chr(code)
    else:
      yield '$0x%02X' % code


def LoadPythonDictionary(path):
  file_string = open(path).read()
  try:
    file_data = eval(file_string, {'__builtins__': None}, None)
  except SyntaxError as e:
    e.filename = path
    raise
  except Exception as e:
    raise Exception("Unexpected error while reading %s: %s" % (path, str(e)))

  assert isinstance(file_data, dict), "%s does not eval to a dictionary" % path

  # Flatten any variables to the top level.
  if 'variables' in file_data:
    file_data.update(file_data['variables'])
    del file_data['variables']

  # Strip all elements that this script can't process.
  elements_to_strip = [
    'conditions',
    'direct_dependent_settings',
    'target_conditions',
    'target_defaults',
    'targets',
    'includes',
    'actions',
  ]
  for element in elements_to_strip:
    if element in file_data:
      del file_data[element]

  return file_data


def ReplaceSubstrings(values, search_for, replace_with):
  """Recursively replaces substrings in a value.
  Replaces all substrings of the "search_for" with "replace_with" for all
  strings occurring in "values". This is done by recursively iterating into
  lists as well as the keys and values of dictionaries."""
  if isinstance(values, str):
    return values.replace(search_for, replace_with)

  if isinstance(values, list):
    result = []
    for v in values:
      # Remove the item from list for complete match.
      if v == search_for and replace_with == '':
        continue
      result.append(ReplaceSubstrings(v, search_for, replace_with))
    return result

  if isinstance(values, dict):
    # For dictionaries, do the search for both the key and values.
    result = {}
    for key, value in values.items():
      new_key = ReplaceSubstrings(key, search_for, replace_with)
      new_value = ReplaceSubstrings(value, search_for, replace_with)
      result[new_key] = new_value
    return result

  # Assume everything else is unchanged.
  return values


def DeduplicateLists(values):
  """Recursively remove duplicate values in lists."""
  if isinstance(values, list):
    return sorted(list(set(values)))

  if isinstance(values, dict):
    for key in values:
      values[key] = DeduplicateLists(values[key])
  return values


def main():
  parser = OptionParser()
  parser.add_option("-r", "--replace", action="append",
    help="Replaces substrings. If passed a=b, replaces all substrs a with b.")
  (options, args) = parser.parse_args()

  if len(args) != 1:
    raise Exception("Need one argument which is the .gypi file to read.")

  data = LoadPythonDictionary(args[0])
  if options.replace:
    # Do replacements for all specified patterns.
    for replace in options.replace:
      split = replace.split('=')
      # Allow "foo=" to replace with nothing.
      if len(split) == 1:
        split.append('')
      assert len(split) == 2, "Replacement must be of the form 'key=value'."
      data = ReplaceSubstrings(data, split[0], split[1])

  gn_dict = {}
  for key in data:
    gn_key = key.replace('-', '_')
    # Sometimes .gypi files use the GYP syntax with percents at the end of the
    # variable name (to indicate not to overwrite a previously-defined value):
    #   'foo%': 'bar',
    # Convert these to regular variables.
    if len(key) > 1 and key[len(key) - 1] == '%':
      gn_dict[gn_key[:-1]] = data[key]
    else:
      gn_dict[gn_key] = data[key]

  print(ToGNString(DeduplicateLists(gn_dict)))

if __name__ == '__main__':
  try:
    main()
  except Exception as e:
    print(str(e))
    sys.exit(1)
