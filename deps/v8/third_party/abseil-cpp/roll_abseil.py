#!/usr/bin/env python3

"""Script to do the first step of Abseil roll into chromium.
"""

import logging
import os
import re
import subprocess
import tempfile
from datetime import datetime

ABSL_URI = 'https://github.com/abseil/abseil-cpp.git'

def _PullAbseil(abseil_dir):
  logging.info('Updating abseil...')
  subprocess.check_call(['git', 'clone', ABSL_URI],
                        cwd=abseil_dir)

def _SyncChromium(chromium_dir):
  logging.info('Updating chromium...')
  subprocess.check_call(['git', 'checkout', 'main'], cwd=chromium_dir)
  subprocess.check_call(['git', 'pull', '--rebase'], cwd=chromium_dir)
  subprocess.check_call(['gclient', 'sync'], cwd=chromium_dir)


def _UpdateChromiumReadme(readme_filename, abseil_dir):
  logging.info('Updating ' + readme_filename)

  stdout = subprocess.check_output(['git', 'log', '-n1', '--pretty=short'],
                                   cwd=abseil_dir)
  new_revision = re.search('commit\\s(.{40})', str(stdout)).group(1)

  with open(readme_filename, 'r+') as f:
    content = f.read()
    prefix = 'Revision: '
    pos = content.find(prefix)
    assert(pos > 0)
    pos = pos + len(prefix)
    old_revision = content[pos:pos+40]
    f.seek(pos)
    f.write(new_revision)

  logging.info('Abseil old revision is ' + old_revision)
  logging.info('Abseil new revision is ' + new_revision)
  return old_revision[0:10] + '..' + new_revision[0:10]


def _UpdateAbseilInChromium(abseil_dir, chromium_dir):
 logging.info('Syncing abseil in chromium/src/third_party...')
 exclude = [
   '*BUILD.gn',
   'DIR_METADATA',
   'README.chromium',
   'OWNERS',
   '.gitignore',
   '.git',
   '*.gni',
   '*clang-format',
   'patches/*',
   'patches',
   'absl_hardening_test.cc',
   'roll_abseil.py',
   'generate_def_files.py',
   '*.def',
 ]
 params = ['rsync', '-aP', abseil_dir, os.path.join(chromium_dir, 'third_party'), '--delete']
 for e in exclude:
   params.append('--exclude={}'.format(e))
 subprocess.check_call(params, cwd=chromium_dir)


def _PatchAbseil(abseil_in_chromium_dir):
  logging.info('Patching abseil...')
  for patch in os.listdir(os.path.join(abseil_in_chromium_dir, 'patches')):
    subprocess.check_call(['patch', '--strip', '1', '-i', os.path.join(abseil_in_chromium_dir, 'patches', patch)])

  os.remove(os.path.join(abseil_in_chromium_dir, 'absl', 'base', 'internal', 'dynamic_annotations.h'))


def _Commit(chromium_dir, hash_diff):
  logging.info('Commit...')
  desc="""Roll abseil_revision {0}

Change Log:
https://chromium.googlesource.com/external/github.com/abseil/abseil-cpp/+log/{0}
Full diff:
https://chromium.googlesource.com/external/github.com/abseil/abseil-cpp/+/{0}
Bug: None""".format(hash_diff)

  subprocess.check_call(['git', 'add', 'third_party/abseil-cpp'], cwd=chromium_dir)
  subprocess.check_call(['git', 'commit', '-m', desc], cwd=chromium_dir)

  logging.info('Upload...')
  subprocess.check_call(['git', 'cl', 'upload', '-m', desc, '--bypass-hooks'], cwd=chromium_dir)


def _Roll():
  chromium_dir = os.getcwd()
  abseil_in_chromium_dir = os.path.join(chromium_dir, 'third_party', 'abseil-cpp')
  _SyncChromium(chromium_dir)

  branch_name = datetime.today().strftime('rolling-absl-%Y%m%d')
  logging.info('Creating branch ' + branch_name + ' for the roll...')
  subprocess.check_call(['git', 'checkout', '-b', branch_name], cwd=chromium_dir)

  with tempfile.TemporaryDirectory() as abseil_root:
    _PullAbseil(abseil_root)
    abseil_dir = os.path.join(abseil_root, 'abseil-cpp')
    _UpdateAbseilInChromium(abseil_dir, chromium_dir)
    hash_diff = _UpdateChromiumReadme(os.path.join(abseil_in_chromium_dir, 'README.chromium'),
                                      abseil_dir)

  _PatchAbseil(abseil_in_chromium_dir)
  _Commit(chromium_dir, hash_diff)


if __name__ == '__main__':
  logging.getLogger().setLevel(logging.INFO)

  if os.getcwd().endswith('src') and os.path.exists('chrome/browser'):
    _Roll()

    logging.info("Next step is manual: Fix BUILD.gn files to match BUILD.bazel changes.")
    logging.info("After that run generate_def_files.py. ")
  else:
    logging.error('Run this script from a chromium/src/ directory.')


