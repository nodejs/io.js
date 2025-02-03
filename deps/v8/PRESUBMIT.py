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

"""Top-level presubmit script for V8.

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details about the presubmit API built into gcl.
"""

import ast
import json
import os
import re
import sys
import traceback

# This line is 'magic' in that git-cl looks for it to decide whether to
# use Python3 instead of Python2 when running the code in this file.
USE_PYTHON3 = True

_EXCLUDED_PATHS = (
    r"^test[\\\/].*",
    r"^testing[\\\/].*",
    r"^third_party[\\\/].*",
    r"^tools[\\\/].*",
)

_LICENSE_FILE = (
    r"LICENSE"
)

# Regular expression that matches code which should not be run through cpplint.
_NO_LINT_PATHS = (
    r'src[\\\/]base[\\\/]export-template\.h',
)


# Regular expression that matches code only used for test binaries
# (best effort).
_TEST_CODE_EXCLUDED_PATHS = (
    r'.+-unittest\.cc',
    # Has a method VisitForTest().
    r'src[\\\/]compiler[\\\/]ast-graph-builder\.cc',
    # Test extension.
    r'src[\\\/]extensions[\\\/]gc-extension\.cc',
    # Runtime functions used for testing.
    r'src[\\\/]runtime[\\\/]runtime-test\.cc',
    # Testing helpers.
    r'src[\\\/]heap[\\\/]cppgc[\\\/]testing\.cc',
)


_TEST_ONLY_WARNING = (
    'You might be calling functions intended only for testing from\n'
    'production code.  It is OK to ignore this warning if you know what\n'
    'you are doing, as the heuristics used to detect the situation are\n'
    'not perfect.  The commit queue will not block on this warning.')


def _V8PresubmitChecks(input_api, output_api):
  """Runs the V8 presubmit checks."""
  import sys
  sys.path.append(input_api.os_path.join(
        input_api.PresubmitLocalPath(), 'tools'))
  from v8_presubmit import CppLintProcessor
  from v8_presubmit import GCMoleProcessor
  from v8_presubmit import JSLintProcessor
  from v8_presubmit import TorqueLintProcessor
  from v8_presubmit import SourceProcessor
  from v8_presubmit import StatusFilesProcessor

  def FilterFile(affected_file):
    return input_api.FilterSourceFile(
      affected_file,
      files_to_check=None,
      files_to_skip=_NO_LINT_PATHS)

  def FilterTorqueFile(affected_file):
    return input_api.FilterSourceFile(
      affected_file,
      files_to_check=(r'.+\.tq'))

  def FilterJSFile(affected_file):
    return input_api.FilterSourceFile(
      affected_file,
      files_to_check=(r'.+\.m?js'))

  results = []
  if not CppLintProcessor().RunOnFiles(
      input_api.AffectedFiles(file_filter=FilterFile, include_deletes=False)):
    results.append(output_api.PresubmitError("C++ lint check failed"))
  if not TorqueLintProcessor().RunOnFiles(
      input_api.AffectedFiles(file_filter=FilterTorqueFile,
                              include_deletes=False)):
    results.append(output_api.PresubmitError("Torque format check failed"))
  if not JSLintProcessor().RunOnFiles(
      input_api.AffectedFiles(file_filter=FilterJSFile,
                              include_deletes=False)):
    results.append(output_api.PresubmitError("JS format check failed"))
  if not SourceProcessor().RunOnFiles(
      input_api.AffectedFiles(include_deletes=False)):
    results.append(output_api.PresubmitError(
        "Copyright header, trailing whitespaces and two empty lines " \
        "between declarations check failed"))
  if not StatusFilesProcessor().RunOnFiles(
      input_api.AffectedFiles(include_deletes=True)):
    results.append(output_api.PresubmitError("Status file check failed"))
  if not GCMoleProcessor().RunOnFiles(
      input_api.AffectedFiles(include_deletes=False)):
    results.append(output_api.PresubmitError("GCMole pattern check failed"))
  results.extend(input_api.canned_checks.CheckAuthorizedAuthor(
      input_api, output_api, bot_allowlist=[
        'v8-ci-autoroll-builder@chops-service-accounts.iam.gserviceaccount.com',
        'v8-ci-test262-import-export@chops-service-accounts.iam.gserviceaccount.com',
      ]))
  return results


def _CheckPythonLiterals(input_api, output_api):
  """Checks that all .pyl files are valid python literals."""
  affected_files = [
      af for af in input_api.AffectedFiles()
      if af.LocalPath().endswith('.pyl')
  ]

  results = []
  for af in affected_files:
    try:
      ast.literal_eval('\n'.join(af.NewContents()))
    except SyntaxError as e:
      results.append(output_api.PresubmitError(
          f'Failed to parse python literal {af.LocalPath()}:\n' +
          traceback.format_exc(0)
      ))

  return results



def _CheckUnwantedDependencies(input_api, output_api):
  """Runs checkdeps on #include statements added in this
  change. Breaking - rules is an error, breaking ! rules is a
  warning.
  """
  # We need to wait until we have an input_api object and use this
  # roundabout construct to import checkdeps because this file is
  # eval-ed and thus doesn't have __file__.
  original_sys_path = sys.path
  try:
    sys.path = sys.path + [input_api.os_path.join(
        input_api.PresubmitLocalPath(), 'buildtools', 'checkdeps')]
    import checkdeps
    from cpp_checker import CppChecker
    from rules import Rule
  finally:
    # Restore sys.path to what it was before.
    sys.path = original_sys_path

  def _FilesImpactedByDepsChange(files):
    all_files = [f.AbsoluteLocalPath() for f in files]
    deps_files = [p for p in all_files if IsDepsFile(p)]
    impacted_files = union([_CollectImpactedFiles(path) for path in deps_files])
    impacted_file_objs = [ImpactedFile(path) for path in impacted_files]
    return impacted_file_objs

  def IsDepsFile(p):
    return os.path.isfile(p) and os.path.basename(p) == 'DEPS'

  def union(list_of_lists):
    """Ensure no duplicates"""
    return set(sum(list_of_lists, []))

  def _CollectImpactedFiles(deps_file):
    # TODO(liviurau): Do not walk paths twice. Then we have no duplicates.
    # Higher level DEPS changes may dominate lower level DEPS changes.
    # TODO(liviurau): Check if DEPS changed in the right way.
    # 'include_rules' impact c++ files but 'vars' or 'deps' do not.
    # Maybe we just eval both old and new DEPS content and check
    # if the list are the same.
    result = []
    parent_dir = os.path.dirname(deps_file)
    for relative_f in input_api.change.AllFiles(parent_dir):
      abs_f = os.path.join(parent_dir, relative_f)
      if CppChecker.IsCppFile(abs_f):
        result.append(abs_f)
    return result

  class ImpactedFile(object):
    """Duck type version of AffectedFile needed to check files under directories
    where a DEPS file changed. Extend the interface along the line of
    AffectedFile if you need it for other checks."""

    def __init__(self, path):
      self._path = path

    def LocalPath(self):
      path = self._path.replace(os.sep, '/')
      return os.path.normpath(path)

    def ChangedContents(self):
      with open(self._path) as f:
        # TODO(liviurau): read only '#include' lines
        lines = f.readlines()
      return enumerate(lines, start=1)

  def _FilterDuplicates(impacted_files, affected_files):
    """"We include all impacted files but exclude affected files that are also
    impacted. Files impacted by DEPS changes take precedence before files
    affected by direct changes."""
    result = impacted_files[:]
    only_paths = set([imf.LocalPath() for imf in impacted_files])
    for af in affected_files:
      if not af.LocalPath() in only_paths:
        result.append(af)
    return result

  added_includes = []
  affected_files = input_api.AffectedFiles()
  impacted_by_deps = _FilesImpactedByDepsChange(affected_files)
  for f in _FilterDuplicates(impacted_by_deps, affected_files):
    if not CppChecker.IsCppFile(f.LocalPath()):
      continue

    changed_lines = [line for line_num, line in f.ChangedContents()]
    added_includes.append([f.LocalPath(), changed_lines])

  deps_checker = checkdeps.DepsChecker(input_api.PresubmitLocalPath())

  error_descriptions = []
  warning_descriptions = []
  for path, rule_type, rule_description in deps_checker.CheckAddedCppIncludes(
      added_includes):
    description_with_path = '{}\n    {}'.format(path, rule_description)
    if rule_type == Rule.DISALLOW:
      error_descriptions.append(description_with_path)
    else:
      warning_descriptions.append(description_with_path)

  results = []
  if error_descriptions:
    results.append(output_api.PresubmitError(
        'You added one or more #includes that violate checkdeps rules.',
        error_descriptions))
  if warning_descriptions:
    results.append(output_api.PresubmitPromptOrNotify(
        'You added one or more #includes of files that are temporarily\n'
        'allowed but being removed. Can you avoid introducing the\n'
        '#include? See relevant DEPS file(s) for details and contacts.',
        warning_descriptions))
  return results


def _CheckHeadersHaveIncludeGuards(input_api, output_api):
  """Ensures that all header files have include guards."""
  file_inclusion_pattern = r'src/.+\.h'

  def FilterFile(affected_file):
    files_to_skip = _EXCLUDED_PATHS + input_api.DEFAULT_FILES_TO_SKIP
    return input_api.FilterSourceFile(
      affected_file,
      files_to_check=(file_inclusion_pattern, ),
      files_to_skip=files_to_skip)

  leading_src_pattern = input_api.re.compile(r'^src[\\\/]')
  dash_dot_slash_pattern = input_api.re.compile(r'[-.\\\/]')

  def PathToGuardMacro(path):
    """Guards should be of the form V8_PATH_TO_FILE_WITHOUT_SRC_H_."""
    x = input_api.re.sub(leading_src_pattern, 'v8_', path)
    x = input_api.re.sub(dash_dot_slash_pattern, '_', x)
    x = x.upper() + "_"
    return x

  problems = []
  for f in input_api.AffectedSourceFiles(FilterFile):
    local_path = f.LocalPath()
    guard_macro = PathToGuardMacro(local_path)
    guard_patterns = [
            input_api.re.compile(r'^#ifndef ' + guard_macro + '$'),
            input_api.re.compile(r'^#define ' + guard_macro + '$'),
            input_api.re.compile(r'^#endif  // ' + guard_macro + '$')]
    skip_check_pattern = input_api.re.compile(
            r'^// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD')
    found_patterns = [ False, False, False ]
    file_omitted = False

    for line in f.NewContents():
      for i in range(len(guard_patterns)):
        if guard_patterns[i].match(line):
          found_patterns[i] = True
      if skip_check_pattern.match(line):
        file_omitted = True
        break

    if not file_omitted and not all(found_patterns):
      problems.append('{}: Missing include guard \'{}\''.format(
          local_path, guard_macro))

  if problems:
    return [output_api.PresubmitError(
        'You added one or more header files without an appropriate\n'
        'include guard. Add the include guard {#ifndef,#define,#endif}\n'
        'triplet or omit the check entirely through the magic comment:\n'
        '"// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD".', problems)]
  else:
    return []


def _CheckNoInlineHeaderIncludesInNormalHeaders(input_api, output_api):
  """Attempts to prevent inclusion of inline headers into normal header
  files. This tries to establish a layering where inline headers can be
  included by other inline headers or compilation units only."""
  file_inclusion_pattern = r'(?!.+-inl\.h).+\.h'
  include_directive_pattern = input_api.re.compile(r'#include ".+-inl.h"')
  include_error = (
    'You are including an inline header (e.g. foo-inl.h) within a normal\n'
    'header (e.g. bar.h) file.  This violates layering of dependencies.')

  def FilterFile(affected_file):
    files_to_skip = _EXCLUDED_PATHS + input_api.DEFAULT_FILES_TO_SKIP
    return input_api.FilterSourceFile(
      affected_file,
      files_to_check=(file_inclusion_pattern, ),
      files_to_skip=files_to_skip)

  problems = []
  for f in input_api.AffectedSourceFiles(FilterFile):
    local_path = f.LocalPath()
    for line_number, line in f.ChangedContents():
      if (include_directive_pattern.search(line)):
        problems.append('{}:{}\n    {}'.format(local_path, line_number,
                                               line.strip()))

  if problems:
    return [output_api.PresubmitError(include_error, problems)]
  else:
    return []


def _CheckNoProductionCodeUsingTestOnlyFunctions(input_api, output_api):
  """Attempts to prevent use of functions intended only for testing in
  non-testing code. For now this is just a best-effort implementation
  that ignores header files and may have some false positives. A
  better implementation would probably need a proper C++ parser.
  """
  # We only scan .cc files, as the declaration of for-testing functions in
  # header files are hard to distinguish from calls to such functions without a
  # proper C++ parser.
  file_inclusion_pattern = r'.+\.cc'

  base_function_pattern = r'[ :]test::[^\s]+|ForTest(ing)?|for_test(ing)?'
  inclusion_pattern = input_api.re.compile(
      r'({})\s*\('.format(base_function_pattern))
  comment_pattern = input_api.re.compile(
      r'//.*({})'.format(base_function_pattern))
  exclusion_pattern = input_api.re.compile(
      r'::[A-Za-z0-9_]+({})|({})[^;]+'.format(base_function_pattern,
                                              base_function_pattern) + '\{')

  def FilterFile(affected_file):
    files_to_skip = (_EXCLUDED_PATHS +
                     _TEST_CODE_EXCLUDED_PATHS +
                     input_api.DEFAULT_FILES_TO_SKIP)
    return input_api.FilterSourceFile(
      affected_file,
      files_to_check=(file_inclusion_pattern, ),
      files_to_skip=files_to_skip)

  problems = []
  for f in input_api.AffectedSourceFiles(FilterFile):
    local_path = f.LocalPath()
    for line_number, line in f.ChangedContents():
      if (inclusion_pattern.search(line) and
          not comment_pattern.search(line) and
          not exclusion_pattern.search(line)):
        problems.append('{}:{}\n    {}'.format(local_path, line_number,
                                               line.strip()))

  if problems:
    return [output_api.PresubmitPromptOrNotify(_TEST_ONLY_WARNING, problems)]
  else:
    return []


def _CheckGenderNeutralInLicenses(input_api, output_api):
  # License files are taken as is, even if they include gendered pronouns.
  def LicenseFilter(path):
    input_api.FilterSourceFile(path, files_to_skip=_LICENSE_FILE)

  return input_api.canned_checks.CheckGenderNeutral(
    input_api, output_api, source_file_filter=LicenseFilter)


def _RunTestsWithVPythonSpec(input_api, output_api):
  return input_api.RunTests(
    input_api.canned_checks.CheckVPythonSpec(input_api, output_api))


def _CommonChecks(input_api, output_api):
  """Checks common to both upload and commit."""
  # TODO(machenbach): Replace some of those checks, e.g. owners and copyright,
  # with the canned PanProjectChecks. Need to make sure that the checks all
  # pass on all existing files.
  checks = [
    input_api.canned_checks.CheckOwnersFormat,
    input_api.canned_checks.CheckOwners,
    _CheckCommitMessageBugEntry,
    input_api.canned_checks.CheckPatchFormatted,
    _CheckGenderNeutralInLicenses,
    _V8PresubmitChecks,
    _CheckUnwantedDependencies,
    _CheckNoProductionCodeUsingTestOnlyFunctions,
    _CheckHeadersHaveIncludeGuards,
    _CheckNoInlineHeaderIncludesInNormalHeaders,
    _CheckJSONFiles,
    _CheckNoexceptAnnotations,
    _RunTestsWithVPythonSpec,
    _CheckPythonLiterals,
  ]

  return sum([check(input_api, output_api) for check in checks], [])


def _SkipTreeCheck(input_api, output_api):
  """Check the env var whether we want to skip tree check.
     Only skip if include/v8-version.h has been updated."""
  src_version = 'include/v8-version.h'
  if not input_api.AffectedSourceFiles(
      lambda file: file.LocalPath() == src_version):
    return False
  return input_api.environ.get('PRESUBMIT_TREE_CHECK') == 'skip'


def _CheckCommitMessageBugEntry(input_api, output_api):
  """Check that bug entries are well-formed in commit message."""
  bogus_bug_msg = (
      'Bogus BUG entry: {}. Please specify prefix:number for v8 or chromium '
      '(e.g. chromium:12345) or b/number for buganizer.')
  results = []
  for bug in (input_api.change.BUG or '').split(','):
    bug = bug.strip()
    if 'none'.startswith(bug.lower()):
      continue
    if ':' not in bug and not bug.startswith('b/'):
      try:
        if int(bug) < 10000000:
          if int(bug) > 200000:
            prefix_guess = 'chromium'
          else:
            prefix_guess = 'v8'
          results.append(
              'BUG entry requires issue tracker prefix, e.g. {}:{}'.format(
                  prefix_guess, bug))
      except ValueError:
        results.append(bogus_bug_msg.format(bug))
    elif not re.match(r'\w+[:\/]\d+', bug):
      results.append(bogus_bug_msg.format(bug))
  return [output_api.PresubmitError(r) for r in results]


def _CheckJSONFiles(input_api, output_api):
  def FilterFile(affected_file):
    return input_api.FilterSourceFile(
        affected_file,
        files_to_check=(r'.+\.json',))

  results = []
  for f in input_api.AffectedFiles(
      file_filter=FilterFile, include_deletes=False):
    with open(f.LocalPath()) as j:
      try:
        json.load(j)
      except Exception as e:
        results.append('JSON validation failed for {}. Error:\n{}'.format(
            f.LocalPath(), e))

  return [output_api.PresubmitError(r) for r in results]


def _CheckNoexceptAnnotations(input_api, output_api):
  """
  Checks that all user-defined constructors and assignment operators are marked
  V8_NOEXCEPT.

  This is required for standard containers to pick the right constructors. Our
  macros (like MOVE_ONLY_WITH_DEFAULT_CONSTRUCTORS) add this automatically.
  Omitting it at some places can result in weird compiler errors if this is
  mixed with other classes that have the annotation.

  TODO(clemensb): This check should eventually be enabled for all files via
  tools/presubmit.py (https://crbug.com/v8/8616).
  """

  def FilterFile(affected_file):
    files_to_skip = _EXCLUDED_PATHS + (
        # Skip api.cc since we cannot easily add the 'noexcept' annotation to
        # public methods.
        r'src[\\\/]api[\\\/]api\.cc',
        # Skip src/bigint/ because it's meant to be V8-independent.
        r'src[\\\/]bigint[\\\/].*',
    )
    return input_api.FilterSourceFile(
        affected_file,
        files_to_check=(r'src[\\\/].*\.cc', r'src[\\\/].*\.h',
                        r'test[\\\/].*\.cc', r'test[\\\/].*\.h'),
        files_to_skip=files_to_skip)

  # matches any class name.
  class_name = r'\b([A-Z][A-Za-z0-9_:]*)(?:::\1)?'
  # initial class name is potentially followed by this to declare an assignment
  # operator.
  potential_assignment = r'(?:&\s+(?:\1::)?operator=)?\s*'
  # matches an argument list that contains only a reference to a class named
  # like the first capture group, potentially const.
  single_class_ref_arg = r'\(\s*(?:const\s+)?\1(?:::\1)?&&?[^,;)]*\)'
  # matches anything but a sequence of whitespaces followed by either
  # V8_NOEXCEPT or "= delete".
  not_followed_by_noexcept = r'(?!\s+(?:V8_NOEXCEPT|=\s+delete)\b)'
  full_pattern = r'^.*?' + class_name + potential_assignment + \
      single_class_ref_arg + not_followed_by_noexcept + '.*?$'
  regexp = input_api.re.compile(full_pattern, re.MULTILINE)

  errors = []
  for f in input_api.AffectedFiles(file_filter=FilterFile,
                                   include_deletes=False):
    with open(f.LocalPath()) as fh:
      for match in re.finditer(regexp, fh.read()):
        errors.append(f'in {f.LocalPath()}: {match.group().strip()}')

  if errors:
    return [output_api.PresubmitPromptOrNotify(
        'Copy constructors, move constructors, copy assignment operators and '
        'move assignment operators should be marked V8_NOEXCEPT.\n'
        'Please report false positives on https://crbug.com/v8/8616.',
        errors)]
  return []


def CheckChangeOnUpload(input_api, output_api):
  results = []
  results.extend(_CommonChecks(input_api, output_api))
  return results


def CheckChangeOnCommit(input_api, output_api):
  results = []
  results.extend(_CommonChecks(input_api, output_api))
  results.extend(input_api.canned_checks.CheckChangeHasDescription(
      input_api, output_api))
  if not _SkipTreeCheck(input_api, output_api):
    results.extend(input_api.canned_checks.CheckTreeIsOpen(
        input_api, output_api,
        json_url='http://v8-status.appspot.com/current?format=json'))
  return results
