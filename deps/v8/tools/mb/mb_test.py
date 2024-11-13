#!/usr/bin/python3
# Copyright 2016 the V8 project authors. All rights reserved.
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Tests for mb.py."""

import json
import io
import os
import sys
import unittest

import mb


class FakeMBW(mb.MetaBuildWrapper):

  def __init__(self, win32=False):
    super().__init__()

    # Override vars for test portability.
    if win32:
      self.chromium_src_dir = 'c:\\fake_src'
      self.default_config = 'c:\\fake_src\\tools\\mb\\mb_config.pyl'
      self.default_isolate_map = ('c:\\fake_src\\testing\\buildbot\\'
                                  'gn_isolate_map.pyl')
      self.platform = 'win32'
      self.executable = 'c:\\python\\python.exe'
      self.sep = '\\'
    else:
      self.chromium_src_dir = '/fake_src'
      self.default_config = '/fake_src/tools/mb/mb_config.pyl'
      self.default_isolate_map = '/fake_src/testing/buildbot/gn_isolate_map.pyl'
      self.executable = '/usr/bin/python'
      self.platform = 'linux2'
      self.sep = '/'

    self.files = {}
    self.calls = []
    self.cmds = []
    self.cross_compile = None
    self.out = ''
    self.err = ''
    self.rmdirs = []

  def ExpandUser(self, path):
    return '$HOME/%s' % path

  def Exists(self, path):
    return self.files.get(path) is not None

  def MaybeMakeDirectory(self, path):
    self.files[path] = True

  def PathJoin(self, *comps):
    return self.sep.join(comps)

  def ReadFile(self, path):
    return self.files[path]

  def WriteFile(self, path, contents, force_verbose=False):
    if self.args.dryrun or self.args.verbose or force_verbose:
      self.Print('\nWriting """\\\n%s""" to %s.\n' % (contents, path))
    self.files[path] = contents

  def Call(self, cmd, env=None, buffer_output=True):
    self.calls.append(cmd)
    if self.cmds:
      return self.cmds.pop(0)
    return 0, '', ''

  def Print(self, *args, **kwargs):
    sep = kwargs.get('sep', ' ')
    end = kwargs.get('end', '\n')
    f = kwargs.get('file', sys.stdout)
    if f == sys.stderr:
      self.err += sep.join(args) + end
    else:
      self.out += sep.join(args) + end

  def TempFile(self, mode='w'):
    return FakeFile(self.files)

  def RemoveFile(self, path):
    del self.files[path]

  def RemoveDirectory(self, abs_path):
    self.rmdirs.append(abs_path)
    files_to_delete = [f for f in self.files if f.startswith(abs_path)]
    for f in files_to_delete:
      self.files[f] = None


class FakeFile():

  def __init__(self, files):
    self.name = '/tmp/file'
    self.buf = ''
    self.files = files

  def write(self, contents):
    self.buf += contents

  def close(self):
    self.files[self.name] = self.buf


TEST_CONFIG = """\
{
  'builder_groups': {
    'chromium': {},
    'fake_builder_group': {
      'fake_builder': 'rel_bot',
      'fake_debug_builder': 'debug_remoteexec',
      'fake_args_bot': '//build/args/bots/fake_builder_group/fake_args_bot.gn',
      'fake_multi_phase': { 'phase_1': 'phase_1', 'phase_2': 'phase_2'},
      'fake_args_file': 'args_file_remoteexec',
      'fake_args_file_twice': 'args_file_twice',
    },
  },
  'configs': {
    'args_file_remoteexec': ['args_file', 'remoteexec'],
    'args_file_twice': ['args_file', 'args_file'],
    'rel_bot': ['rel', 'remoteexec', 'fake_feature1'],
    'debug_remoteexec': ['debug', 'remoteexec'],
    'phase_1': ['phase_1'],
    'phase_2': ['phase_2'],
  },
  'mixins': {
    'fake_feature1': {
      'gn_args': 'enable_doom_melon=true',
    },
    'remoteexec': {
      'gn_args': 'use_remoteexec=true',
    },
    'args_file': {
      'args_file': '//build/args/fake.gn',
    },
    'phase_1': {
      'gn_args': 'phase=1',
    },
    'phase_2': {
      'gn_args': 'phase=2',
    },
    'rel': {
      'gn_args': 'is_debug=false',
    },
    'debug': {
      'gn_args': 'is_debug=true',
    },
  },
}
"""

TRYSERVER_CONFIG = """\
{
  'builder_groups': {
    'not_a_tryserver': {
      'fake_builder': 'fake_config',
    },
    'tryserver.chromium.linux': {
      'try_builder': 'fake_config',
    },
    'tryserver.chromium.mac': {
      'try_builder2': 'fake_config',
    },
  },
  'luci_tryservers': {
    'luci_tryserver1': ['luci_builder1'],
    'luci_tryserver2': ['luci_builder2'],
  },
  'configs': {},
  'mixins': {},
}
"""


class UnitTest(unittest.TestCase):

  def fake_mbw(self, files=None, win32=False):
    mbw = FakeMBW(win32=win32)
    mbw.files.setdefault(mbw.default_config, TEST_CONFIG)
    mbw.files.setdefault(
        mbw.ToAbsPath('//testing/buildbot/gn_isolate_map.pyl'), '''{
        "foo_unittests": {
          "label": "//foo:foo_unittests",
          "type": "console_test_launcher",
          "args": [],
        },
      }''')
    mbw.files.setdefault(
        mbw.ToAbsPath('//build/args/bots/fake_builder_group/fake_args_bot.gn'),
        'is_debug = false\n')
    if files:
      for path, contents in files.items():
        mbw.files[path] = contents
    return mbw

  def check(self, args, mbw=None, files=None, out=None, err=None, ret=None):
    if not mbw:
      mbw = self.fake_mbw(files)

    actual_ret = mbw.Main(args)

    self.assertEqual(actual_ret, ret)
    if out is not None:
      self.assertEqual(mbw.out, out)
    if err is not None:
      self.assertEqual(mbw.err, err)
    return mbw

  def test_analyze(self):
    files = {
        '/tmp/in.json':
            '''{\
               "files": ["foo/foo_unittest.cc"],
               "test_targets": ["foo_unittests"],
               "additional_compile_targets": ["all"]
             }''',
        '/tmp/out.json.gn':
            '''{\
               "status": "Found dependency",
               "compile_targets": ["//foo:foo_unittests"],
               "test_targets": ["//foo:foo_unittests"]
             }'''
    }

    mbw = self.fake_mbw(files)
    mbw.Call = lambda cmd, env=None, buffer_output=True: (0, '', '')

    self.check([
        'analyze', '-c', 'debug_remoteexec', '//out/Default', '/tmp/in.json',
        '/tmp/out.json'
    ],
               mbw=mbw,
               ret=0)
    out = json.loads(mbw.files['/tmp/out.json'])
    self.assertEqual(
        out, {
            'status': 'Found dependency',
            'compile_targets': ['foo:foo_unittests'],
            'test_targets': ['foo_unittests']
        })

  def test_analyze_optimizes_compile_for_all(self):
    files = {
        '/tmp/in.json':
            '''{\
               "files": ["foo/foo_unittest.cc"],
               "test_targets": ["foo_unittests"],
               "additional_compile_targets": ["all"]
             }''',
        '/tmp/out.json.gn':
            '''{\
               "status": "Found dependency",
               "compile_targets": ["//foo:foo_unittests", "all"],
               "test_targets": ["//foo:foo_unittests"]
             }'''
    }

    mbw = self.fake_mbw(files)
    mbw.Call = lambda cmd, env=None, buffer_output=True: (0, '', '')

    self.check([
        'analyze', '-c', 'debug_remoteexec', '//out/Default', '/tmp/in.json',
        '/tmp/out.json'
    ],
               mbw=mbw,
               ret=0)
    out = json.loads(mbw.files['/tmp/out.json'])

    # check that 'foo_unittests' is not in the compile_targets
    self.assertEqual(['all'], out['compile_targets'])

  def test_analyze_handles_other_toolchains(self):
    files = {
        '/tmp/in.json':
            '''{\
               "files": ["foo/foo_unittest.cc"],
               "test_targets": ["foo_unittests"],
               "additional_compile_targets": ["all"]
             }''',
        '/tmp/out.json.gn':
            '''{\
               "status": "Found dependency",
               "compile_targets": ["//foo:foo_unittests",
                                   "//foo:foo_unittests(bar)"],
               "test_targets": ["//foo:foo_unittests"]
             }'''
    }

    mbw = self.fake_mbw(files)
    mbw.Call = lambda cmd, env=None, buffer_output=True: (0, '', '')

    self.check([
        'analyze', '-c', 'debug_remoteexec', '//out/Default', '/tmp/in.json',
        '/tmp/out.json'
    ],
               mbw=mbw,
               ret=0)
    out = json.loads(mbw.files['/tmp/out.json'])

    # crbug.com/736215: If GN returns a label containing a toolchain,
    # MB (and Ninja) don't know how to handle it; to work around this,
    # we give up and just build everything we were asked to build. The
    # output compile_targets should include all of the input test_targets and
    # additional_compile_targets.
    self.assertEqual(['all', 'foo_unittests'], out['compile_targets'])

  def test_analyze_handles_way_too_many_results(self):
    too_many_files = ', '.join(['"//foo:foo%d"' % i for i in range(4 * 1024)])
    files = {
        '/tmp/in.json':
            '''{\
               "files": ["foo/foo_unittest.cc"],
               "test_targets": ["foo_unittests"],
               "additional_compile_targets": ["all"]
             }''',
        '/tmp/out.json.gn':
            '''{\
               "status": "Found dependency",
               "compile_targets": [''' + too_many_files + '''],
               "test_targets": ["//foo:foo_unittests"]
             }'''
    }

    mbw = self.fake_mbw(files)
    mbw.Call = lambda cmd, env=None, buffer_output=True: (0, '', '')

    self.check([
        'analyze', '-c', 'debug_remoteexec', '//out/Default', '/tmp/in.json',
        '/tmp/out.json'
    ],
               mbw=mbw,
               ret=0)
    out = json.loads(mbw.files['/tmp/out.json'])

    # If GN returns so many compile targets that we might have command-line
    # issues, we should give up and just build everything we were asked to
    # build. The output compile_targets should include all of the input
    # test_targets and additional_compile_targets.
    self.assertEqual(['all', 'foo_unittests'], out['compile_targets'])

  def test_gen(self):
    mbw = self.fake_mbw()
    self.check(['gen', '-c', 'debug_remoteexec', '//out/Default'],
               mbw=mbw,
               ret=0)
    self.assertMultiLineEqual(mbw.files['/fake_src/out/Default/args.gn'],
                              ('is_debug = true\n'
                               'use_remoteexec = true\n'))

    # Make sure we log both what is written to args.gn and the command line.
    self.assertIn('Writing """', mbw.out)
    self.assertIn('/fake_src/buildtools/linux64/gn gen //out/Default --check',
                  mbw.out)

    mbw = self.fake_mbw(win32=True)
    self.check(['gen', '-c', 'debug_remoteexec', '//out/Debug'], mbw=mbw, ret=0)
    self.assertMultiLineEqual(mbw.files['c:\\fake_src\\out\\Debug\\args.gn'],
                              ('is_debug = true\n'
                               'use_remoteexec = true\n'))
    self.assertIn(
        'c:\\fake_src\\buildtools\\win\\gn.exe gen //out/Debug '
        '--check\n', mbw.out)

    mbw = self.fake_mbw()
    self.check([
        'gen', '-m', 'fake_builder_group', '-b', 'fake_args_bot', '//out/Debug'
    ],
               mbw=mbw,
               ret=0)
    # TODO(almuthanna): disable test temporarily to
    #   solve this issue https://crbug.com/v8/11102
    # self.assertEqual(
    #     mbw.files['/fake_src/out/Debug/args.gn'],
    #     'import("//build/args/bots/fake_builder_group/fake_args_bot.gn")\n')

  def test_gen_args_file_mixins(self):
    mbw = self.fake_mbw()
    self.check([
        'gen', '-m', 'fake_builder_group', '-b', 'fake_args_file', '//out/Debug'
    ],
               mbw=mbw,
               ret=0)

    self.assertEqual(mbw.files['/fake_src/out/Debug/args.gn'],
                     ('import("//build/args/fake.gn")\n'
                      'use_remoteexec = true\n'))

    mbw = self.fake_mbw()
    self.check([
        'gen', '-m', 'fake_builder_group', '-b', 'fake_args_file_twice',
        '//out/Debug'
    ],
               mbw=mbw,
               ret=1)

  def test_gen_fails(self):
    mbw = self.fake_mbw()
    mbw.Call = lambda cmd, env=None, buffer_output=True: (1, '', '')
    self.check(['gen', '-c', 'debug_remoteexec', '//out/Default'],
               mbw=mbw,
               ret=1)

  def test_gen_swarming(self):
    files = {
        '/tmp/swarming_targets':
            'base_unittests\n',
        '/fake_src/testing/buildbot/gn_isolate_map.pyl':
            ("{'base_unittests': {"
             "  'label': '//base:base_unittests',"
             "  'type': 'raw',"
             "  'args': [],"
             "}}\n"),
        '/fake_src/out/Default/base_unittests.runtime_deps':
            ("base_unittests\n"),
    }
    mbw = self.fake_mbw(files)
    self.check([
        'gen', '-c', 'debug_remoteexec', '--swarming-targets-file',
        '/tmp/swarming_targets', '//out/Default'
    ],
               mbw=mbw,
               ret=0)
    self.assertIn('/fake_src/out/Default/base_unittests.isolate', mbw.files)
    self.assertIn('/fake_src/out/Default/base_unittests.isolated.gen.json',
                  mbw.files)

  def test_gen_swarming_script(self):
    files = {
        '/tmp/swarming_targets':
            'cc_perftests\n',
        '/fake_src/testing/buildbot/gn_isolate_map.pyl':
            ("{'cc_perftests': {"
             "  'label': '//cc:cc_perftests',"
             "  'type': 'script',"
             "  'script': '/fake_src/out/Default/test_script.py',"
             "  'args': [],"
             "}}\n"),
        'c:\\fake_src\out\Default\cc_perftests.exe.runtime_deps':
            ("cc_perftests\n"),
    }
    mbw = self.fake_mbw(files=files, win32=True)
    self.check([
        'gen', '-c', 'debug_remoteexec', '--swarming-targets-file',
        '/tmp/swarming_targets', '--isolate-map-file',
        '/fake_src/testing/buildbot/gn_isolate_map.pyl', '//out/Default'
    ],
               mbw=mbw,
               ret=0)
    self.assertIn('c:\\fake_src\\out\\Default\\cc_perftests.isolate', mbw.files)
    self.assertIn('c:\\fake_src\\out\\Default\\cc_perftests.isolated.gen.json',
                  mbw.files)

  def test_multiple_isolate_maps(self):
    files = {
        '/tmp/swarming_targets':
            'cc_perftests\n',
        '/fake_src/testing/buildbot/gn_isolate_map.pyl':
            ("{'cc_perftests': {"
             "  'label': '//cc:cc_perftests',"
             "  'type': 'raw',"
             "  'args': [],"
             "}}\n"),
        '/fake_src/testing/buildbot/gn_isolate_map2.pyl':
            ("{'cc_perftests2': {"
             "  'label': '//cc:cc_perftests',"
             "  'type': 'raw',"
             "  'args': [],"
             "}}\n"),
        'c:\\fake_src\out\Default\cc_perftests.exe.runtime_deps':
            ("cc_perftests\n"),
    }
    mbw = self.fake_mbw(files=files, win32=True)
    self.check([
        'gen', '-c', 'debug_remoteexec', '--swarming-targets-file',
        '/tmp/swarming_targets', '--isolate-map-file',
        '/fake_src/testing/buildbot/gn_isolate_map.pyl', '--isolate-map-file',
        '/fake_src/testing/buildbot/gn_isolate_map2.pyl', '//out/Default'
    ],
               mbw=mbw,
               ret=0)
    self.assertIn('c:\\fake_src\\out\\Default\\cc_perftests.isolate', mbw.files)
    self.assertIn('c:\\fake_src\\out\\Default\\cc_perftests.isolated.gen.json',
                  mbw.files)

  def test_duplicate_isolate_maps(self):
    files = {
        '/tmp/swarming_targets':
            'cc_perftests\n',
        '/fake_src/testing/buildbot/gn_isolate_map.pyl':
            ("{'cc_perftests': {"
             "  'label': '//cc:cc_perftests',"
             "  'type': 'raw',"
             "  'args': [],"
             "}}\n"),
        '/fake_src/testing/buildbot/gn_isolate_map2.pyl':
            ("{'cc_perftests': {"
             "  'label': '//cc:cc_perftests',"
             "  'type': 'raw',"
             "  'args': [],"
             "}}\n"),
        'c:\\fake_src\out\Default\cc_perftests.exe.runtime_deps':
            ("cc_perftests\n"),
    }
    mbw = self.fake_mbw(files=files, win32=True)
    # Check that passing duplicate targets into mb fails.
    self.check([
        'gen', '-c', 'debug_remoteexec', '--swarming-targets-file',
        '/tmp/swarming_targets', '--isolate-map-file',
        '/fake_src/testing/buildbot/gn_isolate_map.pyl', '--isolate-map-file',
        '/fake_src/testing/buildbot/gn_isolate_map2.pyl', '//out/Default'
    ],
               mbw=mbw,
               ret=1)

  def test_isolate(self):
    files = {
        '/fake_src/out/Default/toolchain.ninja':
            "",
        '/fake_src/testing/buildbot/gn_isolate_map.pyl':
            ("{'base_unittests': {"
             "  'label': '//base:base_unittests',"
             "  'type': 'raw',"
             "  'args': [],"
             "}}\n"),
        '/fake_src/out/Default/base_unittests.runtime_deps':
            ("base_unittests\n"),
    }
    self.check([
        'isolate', '-c', 'debug_remoteexec', '//out/Default', 'base_unittests'
    ],
               files=files,
               ret=0)

    # test running isolate on an existing build_dir
    files['/fake_src/out/Default/args.gn'] = 'is_debug = True\n'
    self.check(['isolate', '//out/Default', 'base_unittests'],
               files=files,
               ret=0)

    self.check(['isolate', '//out/Default', 'base_unittests'],
               files=files,
               ret=0)

  def test_run(self):
    files = {
        '/fake_src/testing/buildbot/gn_isolate_map.pyl':
            ("{'base_unittests': {"
             "  'label': '//base:base_unittests',"
             "  'type': 'raw',"
             "  'args': [],"
             "}}\n"),
        '/fake_src/out/Default/base_unittests.runtime_deps':
            ("base_unittests\n"),
    }
    self.check(
        ['run', '-c', 'debug_remoteexec', '//out/Default', 'base_unittests'],
        files=files,
        ret=0)

  def test_lookup(self):
    self.check(['lookup', '-c', 'debug_remoteexec'],
               ret=0,
               out=('\n'
                    'Writing """\\\n'
                    'is_debug = true\n'
                    'use_remoteexec = true\n'
                    '""" to _path_/args.gn.\n\n'
                    '/fake_src/buildtools/linux64/gn gen _path_\n'))

  def test_quiet_lookup(self):
    self.check(['lookup', '-c', 'debug_remoteexec', '--quiet'],
               ret=0,
               out=('is_debug = true\n'
                    'use_remoteexec = true\n'))

  def test_help(self):
    orig_stdout = sys.stdout
    try:
      sys.stdout = io.StringIO()
      self.assertRaises(SystemExit, self.check, ['-h'])
      self.assertRaises(SystemExit, self.check, ['help'])
      self.assertRaises(SystemExit, self.check, ['help', 'gen'])
    finally:
      sys.stdout = orig_stdout

  def test_multiple_phases(self):
    # Check that not passing a --phase to a multi-phase builder fails.
    mbw = self.check(
        ['lookup', '-m', 'fake_builder_group', '-b', 'fake_multi_phase'], ret=1)
    self.assertIn('Must specify a build --phase', mbw.out)

    # Check that passing a --phase to a single-phase builder fails.
    mbw = self.check([
        'lookup', '-m', 'fake_builder_group', '-b', 'fake_builder', '--phase',
        'phase_1'
    ],
                     ret=1)
    self.assertIn('Must not specify a build --phase', mbw.out)

    # Check that passing a wrong phase key to a multi-phase builder fails.
    mbw = self.check([
        'lookup', '-m', 'fake_builder_group', '-b', 'fake_multi_phase',
        '--phase', 'wrong_phase'
    ],
                     ret=1)
    self.assertIn('Phase wrong_phase doesn\'t exist', mbw.out)

    # Check that passing a correct phase key to a multi-phase builder passes.
    mbw = self.check([
        'lookup', '-m', 'fake_builder_group', '-b', 'fake_multi_phase',
        '--phase', 'phase_1'
    ],
                     ret=0)
    self.assertIn('phase = 1', mbw.out)

    mbw = self.check([
        'lookup', '-m', 'fake_builder_group', '-b', 'fake_multi_phase',
        '--phase', 'phase_2'
    ],
                     ret=0)
    self.assertIn('phase = 2', mbw.out)

  def test_recursive_lookup(self):
    files = {
        '/fake_src/build/args/fake.gn': ('enable_doom_melon = true\n'
                                         'enable_antidoom_banana = true\n')
    }
    self.check([
        'lookup', '-m', 'fake_builder_group', '-b', 'fake_args_file',
        '--recursive'
    ],
               files=files,
               ret=0,
               out=('enable_antidoom_banana = true\n'
                    'enable_doom_melon = true\n'
                    'use_remoteexec = true\n'))

  def test_validate(self):
    mbw = self.fake_mbw()
    self.check(['validate'], mbw=mbw, ret=0)

  def test_buildbucket(self):
    mbw = self.fake_mbw()
    mbw.files[mbw.default_config] = TRYSERVER_CONFIG
    self.check(['gerrit-buildbucket-config'],
               mbw=mbw,
               ret=0,
               out=('# This file was generated using '
                    '"tools/mb/mb.py gerrit-buildbucket-config".\n'
                    '[bucket "luci.luci_tryserver1"]\n'
                    '\tbuilder = luci_builder1\n'
                    '[bucket "luci.luci_tryserver2"]\n'
                    '\tbuilder = luci_builder2\n'
                    '[bucket "builder_group.tryserver.chromium.linux"]\n'
                    '\tbuilder = try_builder\n'
                    '[bucket "builder_group.tryserver.chromium.mac"]\n'
                    '\tbuilder = try_builder2\n'))


if __name__ == '__main__':
  unittest.main()
