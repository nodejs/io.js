# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from abc import ABC, abstractmethod
from enum import Enum
from pathlib import Path

import argparse
import logging
import re
import shutil
import sys

from blinkpy.common.system.log_utils import configure_logging
from blinkpy.w3c.common import read_credentials

TEST_FILE_REFERENCE_IN_STATUS_FILE = re.compile("^\s*'(.*)':.*,$")
TEST262_FAILURE_LINE = re.compile("=== test262/(.*) ===")
TEST262_PATTERN = re.compile('^test/(.*)\.[^\s]*$')
TEST262_RENAME_PATTERN = re.compile('^R[^\s]*\s*([^\s]*)\s*([^\s]*)$')
TEST262_REPO_URL = 'https://chromium.googlesource.com/external/github.com/tc39/test262'
V8_TEST262_ROLLS_META_BUG = 'v8:7834'

_log = logging.getLogger(__file__)


class GitFileStatus(Enum):
  ADDED = 'A'
  DELETED = 'D'
  MODIFIED = 'M'
  UNKNOWN = 'X'


class V8TestImporter():

  def __init__(self,
               phase,
               host,
               test262_github=None,
               test262_failure_file=None,
               v8_test262_last_rev=None):
    self.host = host
    self.github = test262_github
    self.project_config = host.project_config
    self.project_root = Path(self.project_config.project_root)
    self.test262_status_file = self.project_root / 'test' / 'test262' / 'test262.status'
    self.phase = phase
    self.test262_failure_file = test262_failure_file
    self.v8_test262_last_rev = v8_test262_last_rev

  def parse_args(self, argv):
        parser = argparse.ArgumentParser()
        parser.description = __doc__
        parser.add_argument(
            '-v',
            '--verbose',
            action='store_true',
            help='log extra details that may be helpful when debugging')
        parser.add_argument(
            '--credentials-json',
            help='A JSON file with GitHub credentials, '
            'generally not necessary on developer machines')

        return parser.parse_args(argv)

  def main(self, argv=None):
    options = self.parse_args(argv)
    self.verbose = options.verbose

    log_level = logging.DEBUG if self.verbose else logging.INFO
    configure_logging(logging_level=log_level, include_time=True)

    credentials = read_credentials(self.host, options.credentials_json)
    gh_user = credentials.get('GH_USER')
    gh_token = credentials.get('GH_TOKEN')
    if not gh_user or not gh_token:
      _log.warning('You have not set your GitHub credentials. This '
                   'script may fail with a network error when making '
                   'an API request to GitHub.')
      _log.warning('See https://chromium.googlesource.com/chromium/src'
                   '/+/main/docs/testing/web_platform_tests.md'
                   '#GitHub-credentials for instructions on how to set '
                   'your credentials up.')
    self.github = self.project_config.github_factory(self.host, gh_user,
                                                     gh_token)

    test262_rev = self.fetch_test262(gh_token)
    v8_test262_rev = (
        self.v8_test262_last_rev or self.find_current_test262_rev())

    if self.run_prebuild_phase():
      if test262_rev == v8_test262_rev:
        _log.info(f'No changes to import. {test262_rev} == {v8_test262_rev}')
        return False

      _log.info(f'Importing test262@{test262_rev} to V8')
      self.roll_as_dependency(test262_rev)
      self.overwrite_files()
      self.sync_folders(v8_test262_rev, test262_rev)

    failure_lines = []
    if self.run_build_phase():
      failure_lines = self.build_and_test()

    # We either have the lines from the build phase or we read them from a file
    # provided by the executor of POSTBUILD phase.
    failure_lines = failure_lines or self.failure_lines_from_file()
    if self.run_postbuild_phase():
      self.update_status_file(v8_test262_rev, test262_rev, failure_lines)
      # Output the import range for the recipe to pick up.
      print(f'{TEST262_REPO_URL}/+log/{v8_test262_rev[:8]}..{test262_rev[:8]}')

    if self.run_upload_phase():
      self.commit_and_upload_changes(v8_test262_rev, test262_rev)
      # TODO: Create bug if status update yields a skip block

    return True

  @property
  def test262_path(self):
    return Path(self.local_test262.path)

  def run_prebuild_phase(self):
    return self.phase in ['ALL', 'PREBUILD']

  def run_build_phase(self):
    return self.phase in ['ALL']

  def run_postbuild_phase(self):
    return self.phase in ['ALL', 'POSTBUILD']

  def run_upload_phase(self):
    return self.phase in ['ALL', 'UPLOAD']

  def fetch_test262(self, gh_token):
    _log.info(f'Fetching test262')
    self.local_test262 = self.project_config.local_repo_factory(
        self.host, gh_token=gh_token)
    self.local_test262.fetch()
    self.test262_git = self.host.git(self.test262_path)
    return self.test262_git.latest_git_commit()


  def find_current_test262_rev(self):
    _log.info(f'Finding current test262 revision in V8')
    return self.host.executive.run_command(
        ['gclient', 'getdep', '-r', 'test/test262/data'],
        cwd=self.project_root).splitlines()[-1].strip()

  def roll_as_dependency(self, test262_rev):
    self.host.executive.run_command(
        ['gclient', 'setdep', '-r', f'test/test262/data@{test262_rev}'],
        cwd=self.project_root)

  def overwrite_files(self):
    for file in self.project_config.files_to_copy:
      _log.info(f'Overwriting {file.destination} with {file.source}')
      shutil.copyfile(
          Path(self.local_test262.path) / file.source,
          self.project_root / file.destination)

  def sync_folders(self, v8_test262_rev, test262_rev):
    for folder in self.project_config.paths_to_sync:
      _log.info(f'Sync {folder.destination} with {folder.source}')
      destination = self.project_root / folder.destination
      for f in destination.glob('./**/*'):
        if f.is_file() and not self.is_git_sync_exception(f):
          relative_path = f.relative_to(destination)
          source_file = self.test262_path / folder.source / relative_path
          status = self.get_git_file_status(v8_test262_rev, test262_rev,
                                            source_file)
          self.update_file(f, status, source_file)

  def is_git_sync_exception(self, file):
    return file.name == 'features.txt'

  def get_git_file_status(self, v8_test262_rev, test262_rev, source_file):
    status_line = self.test262_git.run([
        'diff', '--name-status', v8_test262_rev, test262_rev, '--', source_file
    ]).splitlines()
    assert len(status_line) < 2, f'Expected zero or one line, got {status_line}'
    if len(status_line) == 0:
      return GitFileStatus.UNKNOWN
    return GitFileStatus(status_line[0][0])

  def update_file(self, local_file, status, source_file):
    gfs = GitFileStatus
    if status in [gfs.ADDED, gfs.DELETED, gfs.MODIFIED]:
      _log.info(f'{local_file} has counterpart in Test262. Deleting.')
      local_file.unlink()
    else:
      _log.warning(
          f'{local_file} has no counterpart in Test262. '
          'Maybe it was never exported?'
      )

  def build_and_test(self):
    """Builds and run test262 tests V8."""
    _log.info('Building V8')
    build_config = 'x64.release'
    self.host.executive.run_command(
        [sys.executable, 'tools/dev/gm.py', build_config],
        cwd=self.project_root)
    _log.info('Running test262 tests')
    test_results = self.host.executive.run_command(
        [
            sys.executable, 'tools/run-tests.py',
            f'--outdir=out/{build_config}', '--progress=verbose',
            '--exit-after-n-failures=0', 'test262'
        ],
        error_handler=testing_error_handler,
        cwd=self.project_root).splitlines()
    failure_matches = [TEST262_FAILURE_LINE.match(l) for l in test_results]
    return uniq(m.group(1) for m in failure_matches if m)

  def failure_lines_from_file(self):
    if not self.test262_failure_file:
      _log.warning('No failure file provided. Skipping.')
      return []
    with open(self.test262_failure_file, 'r') as r_file:
      return r_file.readlines()


  def update_status_file(self, v8_test262_rev, test262_rev, failure_lines):
    _log.info(f'Updating status file')
    updated_status = self.remove_and_rename(v8_test262_rev, test262_rev)

    added_lines = self.failed_tests_to_status_lines(failure_lines)
    if added_lines:
      updated_status = self.rewrite_status_file_content(updated_status,
                                                        added_lines,
                                                        v8_test262_rev,
                                                        test262_rev)
    with open(self.test262_status_file, 'w') as w_file:
      w_file.writelines(updated_status)

  def remove_and_rename(self, v8_test262_rev, test262_rev):
    remover = DeletedTestsRemover(self, v8_test262_rev, test262_rev)
    renamer = RenamedTestsUpdater(self, v8_test262_rev, test262_rev)

    def updated_line(line):
      return remover.updated_line(renamer.updated_line(line))

    updated_status = [updated_line(line) for line in self.status_file_lines()]
    return [line for line in updated_status if line is not None]

  def status_file_lines(self):
    with open(self.test262_status_file, 'r') as r_file:
      return r_file.readlines()


  def failed_tests_to_status_lines(self, failed_tests):
    # Transform the list of failed tests into a list of status file lines.
    return [f"  '{test}': [FAIL],\n" for test in failed_tests]

  def rewrite_status_file_content(self, updated_status, added_lines,
                                  v8_test262_rev, test262_rev):
    # Reassemble the status file with the new tests added.
    # TODO(liviurau): This is easy to unit test. Add unit tests.
    status_lines_before_eof = updated_status[:-2]
    eof_status_lines = updated_status[-2:]
    assert eof_status_lines == [
        '\n', ']\n'
    ], f'Unexpected status file eof. {eof_status_lines}'
    import_header_lines = [
        '\n####\n', f'# Import test262@{test262_rev[:8]}\n',
        f'# {TEST262_REPO_URL}/+log/{v8_test262_rev[:8]}..{test262_rev[:8]}\n'
    ]
    new_failing_tests_lines = ['[ALWAYS, {\n'] + added_lines + [
        '}],\n', f'# End import test262@{test262_rev[:8]}\n', '####\n'
    ]
    return (status_lines_before_eof + import_header_lines +
            new_failing_tests_lines + eof_status_lines)


  def commit_and_upload_changes(self, v8_test262_rev, test262_rev):
    _log.info('Committing changes.')
    self.project_git.run([
        'commit',
        '-a',
        '-m',
        '[test262] Roll test262',
        '-m',
        f'{TEST262_REPO_URL}/+log/{v8_test262_rev[:8]}..{test262_rev[:8]}',
        '-m',
        'no-export: true',
    ])

    _log.info('Uploading changes.')
    self.project_git.run([
        'cl',
        'upload',
        '--bypass-hooks',
        '-f',
        '-b',
        V8_TEST262_ROLLS_META_BUG,
        '-d',
    ])

    _log.info(f'Issue: {self.project_git.run(["cl", "issue"]).strip()}')


def uniq(lst):
  """Return a list with unique elements from the input list."""
  return sorted(set(lst))


def testing_error_handler(error):
  """Error handler that does nothing; used to suppress errors."""
  pass


def relative_test_name(name):
  """Remove the test262 prefix from the test name."""
  return re.sub(TEST262_PATTERN, r'\1', name)


class StatusFileUpdater(ABC):

  def __init__(self, importer, v8_test262_rev, test262_rev, diff_args):
    self.importer = importer
    self.diff_args = diff_args
    self.to_update = self.collect_updateable_tests(v8_test262_rev, test262_rev)

  def updated_line(self, line):
    test_name_result = TEST_FILE_REFERENCE_IN_STATUS_FILE.match(line)
    if test_name_result:
      return self.update_test_line(line, test_name_result)
    return line

  def collect_updateable_tests(self, v8_test262_rev, test262_rev):
    lines = self.importer.test262_git.run([
        'diff', *self.diff_args, v8_test262_rev, test262_rev, '--', 'test'
    ]).splitlines()
    return self.process_git_output(lines)

  @abstractmethod
  def process_git_output(self, lines):
    pass

  @abstractmethod
  def update_test_line(self, line, test_name_result):
    pass


class DeletedTestsRemover(StatusFileUpdater):

  def __init__(self, importer, v8_test262_rev, test262_rev):
    super().__init__(importer, v8_test262_rev, test262_rev,
                     ['--name-only', '--diff-filter=D'])

  def process_git_output(self, lines):
    return [
        relative_test_name(line)
        for line in lines
        if TEST262_PATTERN.match(line)
    ]

  def update_test_line(self, line, test_name_result):
    test_name = test_name_result.group(1)
    if test_name not in self.to_update:
      return line
    _log.info(f'... removing {test_name}')
    return None


class RenamedTestsUpdater(StatusFileUpdater):

  def __init__(self, importer, v8_test262_rev, test262_rev):
    super().__init__(importer, v8_test262_rev, test262_rev,
                     ['--name-status', '--diff-filter=R'])

  def process_git_output(self, lines):
    search_renames = [re.search(TEST262_RENAME_PATTERN, line) for line in lines]
    renames = {
        rename.group(1): rename.group(2) for rename in search_renames if rename
    }
    return {
        relative_test_name(key): relative_test_name(value)
        for key, value in renames.items()
        if TEST262_PATTERN.match(key) and TEST262_PATTERN.match(value)
    }

  def update_test_line(self, line, test_name_result):
    old_name = test_name_result.group(1)
    new_name = self.to_update.get(old_name)
    if not new_name:
      return line
    _log.info(f'... updating {old_name} to {new_name}')
    return line.replace(old_name, new_name)
