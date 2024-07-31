#!/usr/bin/env python3
#
# Copyright 2012 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
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

import hashlib
md5er = hashlib.md5

from contextlib import AbstractContextManager
import io
import json
import multiprocessing
import optparse
import os
from os.path import abspath, join, dirname, basename, exists, isfile, isdir
import pickle
import re
import subprocess
from subprocess import PIPE
import sys
import time

from testrunner.local import statusfile

def decode(arg, encoding="utf-8"):
  return arg.decode(encoding)

# Special LINT rules diverging from default and reason.
# build/header_guard: Our guards have the form "V8_FOO_H_", not "SRC_FOO_H_".
#   We now run our own header guard check in PRESUBMIT.py.
# build/include_what_you_use: Started giving false positives for variables
#   named "string" and "map" assuming that you needed to include STL headers.
# runtime/references: As of May 2020 the C++ style guide suggests using
#   references for out parameters, see
#   https://google.github.io/styleguide/cppguide.html#Inputs_and_Outputs.
# whitespace/braces: Doesn't handle {}-initialization for custom types
#   well; also should be subsumed by clang-format.
# whitespace/parens: Conflicts with clang-format rule to treat Turboshaft's
#   IF, IF_NOT, ... as IfMacros and insert a whitespace before the parenthesis.

LINT_RULES = """
-build/header_guard
-build/include_what_you_use
-readability/fn_size
-readability/multiline_comment
-runtime/references
-whitespace/braces
-whitespace/comments
-whitespace/parens
""".split()

LINT_OUTPUT_PATTERN = re.compile(r'^.+[:(]\d+[:)]')
FLAGS_LINE = re.compile("//\s*Flags:.*--([A-z0-9-])+_[A-z0-9].*\n")
ASSERT_OPTIMIZED_PATTERN = re.compile("assertOptimized")
FLAGS_ENABLE_MAGLEV = re.compile("//\s*Flags:.*--maglev[^-].*\n")
FLAGS_ENABLE_TURBOFAN = re.compile("//\s*Flags:.*--turbofan[^-].*\n")
ASSERT_UNOPTIMIZED_PATTERN = re.compile("assertUnoptimized")
FLAGS_NO_ALWAYS_OPT = re.compile("//\s*Flags:.*--no-?always-turbofan.*\n")

TOOLS_PATH = dirname(abspath(__file__))
DEPS_DEPOT_TOOLS_PATH = abspath(
    join(TOOLS_PATH, '..', 'third_party', 'depot_tools'))

sys.path.append(DEPS_DEPOT_TOOLS_PATH)
import rdb_wrapper


def CppLintWorker(command):
  try:
    process = subprocess.Popen(command, stderr=subprocess.PIPE)
    process.wait()
    out_lines = ""
    error_count = -1
    while True:
      out_line = decode(process.stderr.readline())
      if out_line == '' and process.poll() is not None:
        if error_count == -1:
          print("Failed to process %s" % command.pop())
          return 1
        break
      if out_line.strip() == 'Total errors found: 0':
        out_lines += "Done processing %s\n" % command.pop()
        error_count += 1
      else:
        m = LINT_OUTPUT_PATTERN.match(out_line)
        if m:
          out_lines += out_line
          error_count += 1
    sys.stdout.write(out_lines)
    return error_count
  except KeyboardInterrupt:
    process.kill()
  except:
    print('Error running cpplint.py. Please make sure you have depot_tools' +
          ' in your third_party directory. Lint check skipped.')
    process.kill()

def TorqueLintWorker(command):
  try:
    process = subprocess.Popen(command, stderr=subprocess.PIPE)
    process.wait()
    out_lines = ""
    error_count = 0
    while True:
      out_line = decode(process.stderr.readline())
      if out_line == '' and process.poll() is not None:
        break
      out_lines += out_line
      error_count += 1
    sys.stdout.write(out_lines)
    if error_count != 0:
      sys.stdout.write(
          "warning: formatting and overwriting unformatted Torque files\n")
    return error_count
  except KeyboardInterrupt:
    process.kill()
  except:
    print('Error running format-torque.py')
    process.kill()

def JSLintWorker(command):
  def format_file(command):
    try:
      file_name = command[-1]
      with open(file_name, "r") as file_handle:
        contents = file_handle.read()

      process = subprocess.Popen(command, stdout=PIPE, stderr=subprocess.PIPE)
      output, err = process.communicate()
      rc = process.returncode
      if rc != 0:
        sys.stdout.write("error code " + str(rc) + " running clang-format.\n")
        return rc

      if decode(output) != contents:
        return 1

      return 0
    except KeyboardInterrupt:
      process.kill()
    except Exception:
      print(
          'Error running clang-format. Please make sure you have depot_tools' +
          ' in your third_party directory. Lint check skipped.')
      process.kill()

  rc = format_file(command)
  if rc == 1:
    # There are files that need to be formatted, let's format them in place.
    file_name = command[-1]
    sys.stdout.write("Formatting %s.\n" % (file_name))
    rc = format_file(command[:-1] + ["-i", file_name])
  return rc

class FileContentsCache(object):

  def __init__(self, sums_file_name):
    self.sums = {}
    self.sums_file_name = sums_file_name

  def Load(self):
    try:
      sums_file = None
      try:
        sums_file = open(self.sums_file_name, 'rb')
        self.sums = pickle.load(sums_file)
      except:
        # Cannot parse pickle for any reason. Not much we can do about it.
        pass
    finally:
      if sums_file:
        sums_file.close()

  def Save(self):
    try:
      sums_file = open(self.sums_file_name, 'wb')
      pickle.dump(self.sums, sums_file)
    except:
      # Failed to write pickle. Try to clean-up behind us.
      if sums_file:
        sums_file.close()
      try:
        os.unlink(self.sums_file_name)
      except:
        pass
    finally:
      sums_file.close()

  def FilterUnchangedFiles(self, files):
    changed_or_new = []
    for file in files:
      try:
        handle = open(file, "rb")
        file_sum = md5er(handle.read()).digest()
        if not file in self.sums or self.sums[file] != file_sum:
          changed_or_new.append(file)
          self.sums[file] = file_sum
      finally:
        handle.close()
    return changed_or_new

  def RemoveFile(self, file):
    if file in self.sums:
      self.sums.pop(file)


class SourceFileProcessor(object):
  """
  Utility class that can run through a directory structure, find all relevant
  files and invoke a custom check on the files.
  """

  def RunOnPath(self, path):
    """Runs processor on all files under the given path."""

    all_files = []
    for file in self.GetPathsToSearch():
      all_files += self.FindFilesIn(join(path, file))
    return self.ProcessFiles(all_files)

  def RunOnFiles(self, files):
    """Runs processor only on affected files."""

    # Helper for getting directory pieces.
    dirs = lambda f: dirname(f).split(os.sep)

    # Path offsets where to look (to be in sync with RunOnPath).
    # Normalize '.' to check for it with str.startswith.
    search_paths = [('' if p == '.' else p) for p in self.GetPathsToSearch()]

    all_files = [
      f.AbsoluteLocalPath()
      for f in files
      if (not self.IgnoreFile(f.LocalPath()) and
          self.IsRelevant(f.LocalPath()) and
          all(not self.IgnoreDir(d) for d in dirs(f.LocalPath())) and
          any(map(f.LocalPath().startswith, search_paths)))
    ]

    return self.ProcessFiles(all_files)

  def IgnoreDir(self, name):
    return (name.startswith('.') or
            name in ('buildtools', 'data', 'gmock', 'gtest', 'kraken',
                     'octane', 'sunspider', 'traces-arm64'))

  def IgnoreFile(self, name):
    return name.startswith('.')

  def FindFilesIn(self, path):
    result = []
    for (root, dirs, files) in os.walk(path):
      for ignored in [x for x in dirs if self.IgnoreDir(x)]:
        dirs.remove(ignored)
      for file in files:
        if not self.IgnoreFile(file) and self.IsRelevant(file):
          result.append(join(root, file))
    return result


class CacheableSourceFileProcessor(SourceFileProcessor):
  """Utility class that allows caching ProcessFiles() method calls.

  In order to use it, create a ProcessFilesWithoutCaching method that returns
  the files requiring intervention after processing the source files.
  """

  def __init__(self, use_cache, cache_file_path, file_type):
    self.use_cache = use_cache
    self.cache_file_path = cache_file_path
    self.file_type = file_type

  def GetProcessorWorker(self):
    """Expected to return the worker function to run the formatter."""
    raise NotImplementedError

  def GetProcessorScript(self):
    """Expected to return a tuple
    (path to the format processor script, list of arguments)."""
    raise NotImplementedError

  def GetProcessorCommand(self):
    format_processor, options = self.GetProcessorScript()
    if not format_processor:
      print('Could not find the formatter for % files' % self.file_type)
      sys.exit(1)

    command = [sys.executable, format_processor]
    command.extend(options)

    return command

  def ProcessFiles(self, files):
    if self.use_cache:
      cache = FileContentsCache(self.cache_file_path)
      cache.Load()
      files = cache.FilterUnchangedFiles(files)

    if len(files) == 0:
      print('No changes in %s files detected. Skipping check' % self.file_type)
      return True

    files_requiring_changes = self.DetectFilesToChange(files)
    print (
      'Total %s files found that require formatting: %d' %
      (self.file_type, len(files_requiring_changes)))
    if self.use_cache:
      for file in files_requiring_changes:
        cache.RemoveFile(file)

      cache.Save()

    return files_requiring_changes == []

  def DetectFilesToChange(self, files):
    command = self.GetProcessorCommand()
    worker = self.GetProcessorWorker()

    commands = [command + [file] for file in files]
    count = multiprocessing.cpu_count()
    pool = multiprocessing.Pool(count)
    try:
      results = pool.map_async(worker, commands).get(timeout=360)
    except KeyboardInterrupt:
      print("\nCaught KeyboardInterrupt, terminating workers.")
      pool.terminate()
      pool.join()
      sys.exit(1)

    unformatted_files = []
    for index, errors in enumerate(results):
      if errors > 0:
        unformatted_files.append(files[index])

    return unformatted_files


class CppLintProcessor(CacheableSourceFileProcessor):
  """
  Lint files to check that they follow the google code style.
  """

  def __init__(self, use_cache=True):
    super(CppLintProcessor, self).__init__(
      use_cache=use_cache, cache_file_path='.cpplint-cache', file_type='C/C++')

  def IsRelevant(self, name):
    return name.endswith('.cc') or name.endswith('.h')

  def IgnoreDir(self, name):
    return (super(CppLintProcessor, self).IgnoreDir(name)
            or (name == 'third_party'))

  IGNORE_LINT = [
    'export-template.h',
    'flag-definitions.h',
    'gay-fixed.cc',
    'gay-precision.cc',
    'gay-shortest.cc',
  ]

  def IgnoreFile(self, name):
    return (super(CppLintProcessor, self).IgnoreFile(name)
              or (name in CppLintProcessor.IGNORE_LINT))

  def GetPathsToSearch(self):
    dirs = ['include', 'samples', 'src']
    test_dirs = ['cctest', 'common', 'fuzzer', 'inspector', 'unittests']
    return dirs + [join('test', dir) for dir in test_dirs]

  def GetProcessorWorker(self):
    return CppLintWorker

  def GetProcessorScript(self):
    filters = ','.join([n for n in LINT_RULES])
    arguments = ['--filter', filters]

    cpplint = join(DEPS_DEPOT_TOOLS_PATH, 'cpplint.py')
    return cpplint, arguments


class TorqueLintProcessor(CacheableSourceFileProcessor):
  """
  Check .tq files to verify they follow the Torque style guide.
  """

  def __init__(self, use_cache=True):
    super(TorqueLintProcessor, self).__init__(
      use_cache=use_cache, cache_file_path='.torquelint-cache',
      file_type='Torque')

  def IsRelevant(self, name):
    return name.endswith('.tq')

  def GetPathsToSearch(self):
    dirs = ['third_party', 'src']
    test_dirs = ['torque']
    return dirs + [join('test', dir) for dir in test_dirs]

  def GetProcessorWorker(self):
    return TorqueLintWorker

  def GetProcessorScript(self):
    torque_tools = join(TOOLS_PATH, "torque")
    torque_path = join(torque_tools, "format-torque.py")
    arguments = ["-il"]
    if isfile(torque_path):
      return torque_path, arguments

    return None, arguments

class JSLintProcessor(CacheableSourceFileProcessor):
  """
  Check .{m}js file to verify they follow the JS Style guide.
  """
  def __init__(self, use_cache=True):
    super(JSLintProcessor, self).__init__(
      use_cache=use_cache, cache_file_path='.jslint-cache',
      file_type='JavaScript')

  def IsRelevant(self, name):
    return name.endswith('.js') or name.endswith('.mjs')

  def GetPathsToSearch(self):
    return ['tools/system-analyzer', 'tools/heap-layout', 'tools/js']

  def GetProcessorWorker(self):
    return JSLintWorker

  def GetProcessorScript(self):
    jslint = join(DEPS_DEPOT_TOOLS_PATH, 'clang_format.py')
    return jslint, []


COPYRIGHT_HEADER_PATTERN = re.compile(
    r'Copyright [\d-]*20[0-2][0-9] the V8 project authors. All rights reserved.')

class SourceProcessor(SourceFileProcessor):
  """
  Check that all files include a copyright notice and no trailing whitespaces.
  """

  RELEVANT_EXTENSIONS = ['.js', '.cc', '.h', '.py', '.c', '.status', '.tq', '.g4']

  def __init__(self):
    self.runtime_function_call_pattern = self.CreateRuntimeFunctionCallMatcher()

  def CreateRuntimeFunctionCallMatcher(self):
    runtime_h_path = join(dirname(TOOLS_PATH), 'src/runtime/runtime.h')
    pattern = re.compile(r'\s+F\(([^,]*),.*\)')
    runtime_functions = []
    with open(runtime_h_path) as f:
      for line in f.readlines():
        m = pattern.match(line)
        if m:
          runtime_functions.append(m.group(1))
    if len(runtime_functions) < 250:
      print ("Runtime functions list is suspiciously short. "
             "Consider updating the presubmit script.")
      sys.exit(1)
    str = '(\%\s+(' + '|'.join(runtime_functions) + '))[\s\(]'
    return re.compile(str)

  # Overwriting the one in the parent class.
  def FindFilesIn(self, path):
    if exists(path+'/.git'):
      output = subprocess.Popen('git ls-files --full-name',
                                stdout=PIPE, cwd=path, shell=True)
      result = []
      for file in decode(output.stdout.read()).split():
        for dir_part in dirname(file).replace(os.sep, '/').split('/'):
          if self.IgnoreDir(dir_part):
            break
        else:
          if (self.IsRelevant(file) and exists(file)
              and not self.IgnoreFile(file)):
            result.append(join(path, file))
      if output.wait() == 0:
        return result
    return super(SourceProcessor, self).FindFilesIn(path)

  def IsRelevant(self, name):
    for ext in SourceProcessor.RELEVANT_EXTENSIONS:
      if name.endswith(ext):
        return True
    return False

  def GetPathsToSearch(self):
    return ['.']

  def IgnoreDir(self, name):
    return (super(SourceProcessor, self).IgnoreDir(name) or
            name in ('third_party', 'out', 'obj', 'DerivedSources'))

  IGNORE_COPYRIGHTS = ['box2d.js',
                       'cpplint.py',
                       'copy.js',
                       'corrections.js',
                       'crypto.js',
                       'daemon.py',
                       'earley-boyer.js',
                       'fannkuch.js',
                       'fasta.js',
                       'injected-script.cc',
                       'injected-script.h',
                       'libraries.cc',
                       'libraries-empty.cc',
                       'lua_binarytrees.js',
                       'meta-123.js',
                       'memops.js',
                       'poppler.js',
                       'primes.js',
                       'raytrace.js',
                       'regexp-pcre.js',
                       'resources-123.js',
                       'sqlite.js',
                       'sqlite-change-heap.js',
                       'sqlite-pointer-masking.js',
                       'sqlite-safe-heap.js',
                       'v8-debugger-script.h',
                       'v8-inspector-impl.cc',
                       'v8-inspector-impl.h',
                       'v8-runtime-agent-impl.cc',
                       'v8-runtime-agent-impl.h',
                       'gnuplot-4.6.3-emscripten.js',
                       'zlib.js']
  IGNORE_TABS = IGNORE_COPYRIGHTS + ['unicode-test.js', 'html-comments.js']

  IGNORE_COPYRIGHTS_DIRECTORIES = [
      "test/test262/local-tests",
      "test/mjsunit/wasm/bulk-memory-spec",
  ]

  def EndOfDeclaration(self, line):
    return line == "}" or line == "};"

  def StartOfDeclaration(self, line):
    return line.find("//") == 0 or \
           line.find("/*") == 0 or \
           line.find(") {") != -1

  def ProcessContents(self, name, contents):
    result = True
    base = basename(name)
    if not base in SourceProcessor.IGNORE_TABS:
      if '\t' in contents:
        print("%s contains tabs" % name)
        result = False
    if not base in SourceProcessor.IGNORE_COPYRIGHTS and \
        not any(ignore_dir in name for ignore_dir
                in SourceProcessor.IGNORE_COPYRIGHTS_DIRECTORIES):
      if not COPYRIGHT_HEADER_PATTERN.search(contents):
        print("%s is missing a correct copyright header." % name)
        result = False
    if ' \n' in contents or contents.endswith(' '):
      line = 0
      lines = []
      parts = contents.split(' \n')
      if not contents.endswith(' '):
        parts.pop()
      for part in parts:
        line += part.count('\n') + 1
        lines.append(str(line))
      linenumbers = ', '.join(lines)
      if len(lines) > 1:
        print("%s has trailing whitespaces in lines %s." % (name, linenumbers))
      else:
        print("%s has trailing whitespaces in line %s." % (name, linenumbers))
      result = False
    if not contents.endswith('\n') or contents.endswith('\n\n'):
      print("%s does not end with a single new line." % name)
      result = False
    # Sanitize flags for fuzzer.
    if (".js" in name or ".mjs" in name) and ("mjsunit" in name or "debugger" in name):
      match = FLAGS_LINE.search(contents)
      if match:
        print("%s Flags should use '-' (not '_')" % name)
        result = False
      if (not "mjsunit/mjsunit.js" in name and
          not "mjsunit/mjsunit_numfuzz.js" in name):
        if ASSERT_OPTIMIZED_PATTERN.search(contents) and \
            not FLAGS_ENABLE_MAGLEV.search(contents) and \
            not FLAGS_ENABLE_TURBOFAN.search(contents):
          print("%s Flag --maglev or --turbofan should be set if " \
                "assertOptimized() is used" % name)
          result = False
        if ASSERT_UNOPTIMIZED_PATTERN.search(contents) and \
            not FLAGS_NO_ALWAYS_OPT.search(contents):
          print("%s Flag --no-always-turbofan should be set if " \
                "assertUnoptimized() is used" % name)
          result = False

      match = self.runtime_function_call_pattern.search(contents)
      if match:
        print("%s has unexpected spaces in a runtime call '%s'" % (name, match.group(1)))
        result = False
    return result

  def ProcessFiles(self, files):
    success = True
    violations = 0
    for file in files:
      try:
        handle = open(file, "rb")
        contents = decode(handle.read(), "ISO-8859-1")
        if len(contents) > 0 and not self.ProcessContents(file, contents):
          success = False
          violations += 1
      finally:
        handle.close()
    print("Total violating files: %s" % violations)
    return success

def _CheckStatusFileForDuplicateKeys(filepath):
  comma_space_bracket = re.compile(", *]")
  lines = []
  with open(filepath) as f:
    for line in f.readlines():
      # Skip all-comment lines.
      if line.lstrip().startswith("#"): continue
      # Strip away comments at the end of the line.
      comment_start = line.find("#")
      if comment_start != -1:
        line = line[:comment_start]
      line = line.strip()
      # Strip away trailing commas within the line.
      line = comma_space_bracket.sub("]", line)
      if len(line) > 0:
        lines.append(line)

  # Strip away trailing commas at line ends. Ugh.
  for i in range(len(lines) - 1):
    if (lines[i].endswith(",") and len(lines[i + 1]) > 0 and
        lines[i + 1][0] in ("}", "]")):
      lines[i] = lines[i][:-1]

  contents = "\n".join(lines)
  # JSON wants double-quotes.
  contents = contents.replace("'", '"')
  # Fill in keywords (like PASS, SKIP).
  for key in statusfile.KEYWORDS:
    contents = re.sub(r"\b%s\b" % key, "\"%s\"" % key, contents)

  status = {"success": True}
  def check_pairs(pairs):
    keys = {}
    for key, value in pairs:
      if key in keys:
        print("%s: Error: duplicate key %s" % (filepath, key))
        status["success"] = False
      keys[key] = True

  json.loads(contents, object_pairs_hook=check_pairs)
  return status["success"]


class StatusFilesProcessor(SourceFileProcessor):
  """Checks status files for incorrect syntax and duplicate keys."""

  def IsRelevant(self, name):
    # Several changes to files under the test directories could impact status
    # files.
    return True

  def GetPathsToSearch(self):
    return ['test', 'tools/testrunner']

  def ProcessFiles(self, files):
    success = True
    for status_file_path in sorted(self._GetStatusFiles(files)):
      success &= statusfile.PresubmitCheck(status_file_path)
      success &= _CheckStatusFileForDuplicateKeys(status_file_path)
    return success

  def _GetStatusFiles(self, files):
    test_path = join(dirname(TOOLS_PATH), 'test')
    testrunner_path = join(TOOLS_PATH, 'testrunner')
    status_files = set()

    for file_path in files:
      if file_path.startswith(testrunner_path):
        for suitepath in os.listdir(test_path):
          suitename = basename(suitepath)
          status_file = join(
              test_path, suitename, suitename + ".status")
          if exists(status_file):
            status_files.add(status_file)
        return status_files

    for file_path in files:
      if file_path.startswith(test_path):
        # Strip off absolute path prefix pointing to test suites.
        pieces = file_path[len(test_path):].lstrip(os.sep).split(os.sep)
        if pieces:
          # Infer affected status file name. Only care for existing status
          # files. Some directories under "test" don't have any.
          if not isdir(join(test_path, pieces[0])):
            continue
          status_file = join(test_path, pieces[0], pieces[0] + ".status")
          if not exists(status_file):
            continue
          status_files.add(status_file)
    return status_files


class GCMoleProcessor(SourceFileProcessor):
  """Check relevant BUILD.gn files for correct gcmole file pattern.

  The pattern must match the algorithm used in:
  tools/gcmole/gcmole.py::build_file_list()
  """
  gcmole_re = re.compile('### gcmole(.*)')
  arch_re = re.compile('\((.+)\) ###')

  def IsRelevant(self, name):
    return True

  def GetPathsToSearch(self):
    # TODO(https://crbug.com/v8/12660): These should be directories according
    # to the API, but in order to find the toplevel BUILD.gn, we'd need to walk
    # the entire project.
    return ['BUILD.gn', 'test/cctest/BUILD.gn']

  def ProcessFiles(self, files):
    success = True
    for path in files:
      with open(path) as f:
        gn_file_text = f.read()
      for suffix in self.gcmole_re.findall(gn_file_text):
        arch_match = self.arch_re.match(suffix)
        if not arch_match:
          print(f'{path}: Malformed gcmole suffix: {suffix}')
          success = False
          continue
        arch = arch_match.group(1)
        if arch not in [
            'all', 'ia32', 'x64', 'arm', 'arm64', 's390', 'ppc', 'ppc64',
            'mips64', 'mips64el', 'riscv32', 'riscv64', 'loong64'
        ]:
          print(f'{path}: Unknown architecture for gcmole: {arch}')
          success = False
    return success


def CheckDeps(workspace):
  checkdeps_py = join(workspace, 'buildtools', 'checkdeps', 'checkdeps.py')
  return subprocess.call([sys.executable, checkdeps_py, workspace]) == 0


def FindTests(workspace):
  scripts = []
  # TODO(almuthanna): unskip valid tests when they are properly migrated
  exclude = [
      'tools/clang',
      'tools/mb/mb_test.py',
      'tools/cppgc/gen_cmake_test.py',
      'tools/ignition/linux_perf_report_test.py',
      'tools/ignition/bytecode_dispatches_report_test.py',
      'tools/ignition/linux_perf_bytecode_annotate_test.py',
  ]
  scripts_without_excluded = []
  for root, dirs, files in os.walk(join(workspace, 'tools')):
    for f in files:
      if f.endswith('_test.py'):
        fullpath = join(root, f)
        scripts.append(fullpath)
  for script in scripts:
    if not any(exc_dir in script for exc_dir in exclude):
      scripts_without_excluded.append(script)
  return scripts_without_excluded


def PyTests(workspace):
  result = True
  for script in FindTests(workspace):
    print('Running ' + script)
    result &= subprocess.call(['vpython3', script], stdout=subprocess.PIPE) == 0

  return result


def GetOptions():
  result = optparse.OptionParser()
  result.add_option('--no-lint', help="Do not run cpplint", default=False,
                    action="store_true")
  result.add_option('--no-linter-cache', help="Do not cache linter results",
                    default=False, action="store_true")

  return result


class TeeIO:

  def __init__(self, source, dest):
    self.source = source
    self.dest = dest

  def write(self, obj):
    self.source.write(obj)
    self.dest.write(obj)

  def flush(self):
    self.source.flush()
    self.dest.flush()


class TeeOutput(AbstractContextManager):

  def __init__(self, new_target):
    self._new_target = new_target
    self.old_out = sys.stdout
    self.old_err = sys.stderr

  def __enter__(self):
    sys.stdout = TeeIO(self.old_out, self._new_target)
    sys.stderr = TeeIO(self.old_err, self._new_target)
    return self._new_target

  def __exit__(self, exctype, excinst, exctb):
    sys.stdout = self.old_out
    sys.stderr = self.old_err


def run_checks(checks, workspace):
  failures = []

  def capture_output(check_function):
    start_time = time.time()
    with TeeOutput(io.StringIO()) as f:
      success = check_function(workspace)
      return (rdb_wrapper.STATUS_PASS if success else rdb_wrapper.STATUS_FAIL,
              None if success else f.getvalue()[-1024:],
              time.time() - start_time)

  def run(check_function, named_object=None, sink=None):
    name = (named_object or check_function).__name__
    print('__________________')
    print(f'Running {name}...')
    status, output, duration = capture_output(check_function)
    if status == rdb_wrapper.STATUS_PASS:
      print(f'{name} SUCCEDED')
    else:
      failures.append(name)
      print(f'!!! {name} FAILED')
    if sink:
      sink.report(name, status, duration, output)

  with rdb_wrapper.client('v8:full_presubmit/') as sink:
    for check in checks:
      if callable(check):
        run(check, sink=sink)
      else:
        run(check.RunOnPath, check.__class__, sink=sink)

  return '\n'.join(failures)


def Main():
  workspace = abspath(join(dirname(sys.argv[0]), '..'))
  parser = GetOptions()
  (options, args) = parser.parse_args()
  use_linter_cache = not options.no_linter_cache
  checks = [
    CheckDeps,
    TorqueLintProcessor(use_cache=use_linter_cache),
    JSLintProcessor(use_cache=use_linter_cache),
    SourceProcessor(),
    StatusFilesProcessor(),
    PyTests,
    GCMoleProcessor(),
  ]
  if not options.no_lint:
    checks.append(CppLintProcessor(use_cache=use_linter_cache))


  failure_lines = run_checks(checks, workspace)
  if failure_lines:
    print('__________________')
    print('==================')
    print(f'Checks failed:\n{failure_lines}')
    return 1
  return 0


if __name__ == '__main__':
  sys.exit(Main())
