# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Contains empty mocks for all blinkpy dependencies. When importing this module
all the necessary blinkpy imports are resolved to these mocks."""

import sys

sys.modules['blinkpy.w3c.local_wpt'] = __import__('mocks')


class LocalRepo:
  pass


sys.modules['blinkpy.w3c.wpt_github'] = __import__('mocks')


class GitHubRepo:
  pass


PROVISIONAL_PR_LABEL = 'provisional'

sys.modules['blinkpy.w3c.chromium_finder'] = __import__('mocks')


def absolute_chromium_dir():
  pass


sys.modules['blinkpy.w3c.chromium_configs'] = __import__('mocks')


class ProjectConfig:
  pass


sys.modules['blinkpy.common.system.log_utils'] = __import__('mocks')


def configure_logging():
  pass


sys.modules['blinkpy.w3c.common'] = __import__('mocks')


def read_credentials():
  pass


sys.modules['blinkpy.w3c.test_importer'] = __import__('mocks')


class TestImporter:

  def __init__(self, *args, **kwargs):
    pass
