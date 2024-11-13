#!/usr/bin/env python3
# Copyright 2016 the V8 project authors. All rights reserved.
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""MB - the Meta-Build wrapper around GN.

MB is a wrapper script for GN that can be used to generate build files
for sets of canned configurations and analyze them.
"""

# for py2/py3 compatibility
from __future__ import print_function

import argparse
import ast
import errno
import json
import os
import pipes
import platform
import pprint
import re
import shutil
import sys
import subprocess
import tempfile
import traceback

# for py2/py3 compatibility
try:
  from urllib.parse import quote
except ImportError:
  from urllib2 import quote
try:
  from urllib.request import urlopen
except ImportError:
  from urllib2 import urlopen

CHROMIUM_SRC_DIR = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path = [os.path.join(CHROMIUM_SRC_DIR, 'build')] + sys.path

import gn_helpers

try:
  cmp              # Python 2
except NameError:  # Python 3
  def cmp(x, y):   # pylint: disable=redefined-builtin
    return (x > y) - (x < y)


def _v8_builder_fallback(builder, builder_group):
  """Fallback to V8 builder names before splitting builder/tester.

  This eases splitting builders and testers on release branches and
  can be removed as soon as all builder have been split and all MB configs
  exist on all branches.
  """
  builders = [builder]
  if builder.endswith(' - builder'):
    builders.append(builder[:-len(' - builder')])
  elif builder.endswith(' builder'):
    builders.append(builder[:-len(' builder')])

  for b in builders:
    if b in builder_group:
      return builder_group[b]
  return None


def main(args):
  mbw = MetaBuildWrapper()
  return mbw.Main(args)


class MetaBuildWrapper():
  def __init__(self):
    self.chromium_src_dir = CHROMIUM_SRC_DIR
    self.default_config = os.path.join(self.chromium_src_dir, 'infra', 'mb',
                                       'mb_config.pyl')
    self.default_isolate_map = os.path.join(self.chromium_src_dir, 'infra',
                                            'mb', 'gn_isolate_map.pyl')
    self.executable = sys.executable
    self.platform = sys.platform
    self.sep = os.sep
    self.args = argparse.Namespace()
    self.configs = {}
    self.luci_tryservers = {}
    self.builder_groups = {}
    self.mixins = {}
    self.isolate_exe = 'isolate.exe' if self.platform.startswith(
        'win') else 'isolate'

  def Main(self, args):
    self.ParseArgs(args)
    try:
      ret = self.args.func()
      if ret:
        self.DumpInputFiles()
      return ret
    except KeyboardInterrupt:
      self.Print('interrupted, exiting')
      return 130
    except Exception:
      self.DumpInputFiles()
      s = traceback.format_exc()
      for l in s.splitlines():
        self.Print(l)
      return 1

  def ParseArgs(self, argv):
    def AddCommonOptions(subp):
      subp.add_argument('-b', '--builder',
                        help='builder name to look up config from')
      subp.add_argument(
          '-m',  '--builder-group',
          help='builder group name to look up config from')
      subp.add_argument('-c', '--config',
                        help='configuration to analyze')
      subp.add_argument('--phase',
                        help='optional phase name (used when builders '
                             'do multiple compiles with different '
                             'arguments in a single build)')
      subp.add_argument('-f', '--config-file', metavar='PATH',
                        default=self.default_config,
                        help='path to config file '
                             '(default is %(default)s)')
      subp.add_argument(
          '-i',
          '--isolate-map-file',
          metavar='PATH',
          help='path to isolate map file '
          '(default is %(default)s)',
          default=[],
          action='append',
          dest='isolate_map_files')
      subp.add_argument(
          '--android-version-code',
          help='Sets GN arg android_default_version_code')
      subp.add_argument('--android-version-name',
                        help='Sets GN arg android_default_version_name')
      subp.add_argument('-n', '--dryrun', action='store_true',
                        help='Do a dry run (i.e., do nothing, just print '
                             'the commands that will run)')
      subp.add_argument('-v', '--verbose', action='store_true',
                        help='verbose logging')

    parser = argparse.ArgumentParser(prog='mb')
    subps = parser.add_subparsers()

    subp = subps.add_parser('analyze',
                            help='analyze whether changes to a set of files '
                                 'will cause a set of binaries to be rebuilt.')
    AddCommonOptions(subp)
    subp.add_argument('path', nargs=1,
                      help='path build was generated into.')
    subp.add_argument('input_path', nargs=1,
                      help='path to a file containing the input arguments '
                           'as a JSON object.')
    subp.add_argument('output_path', nargs=1,
                      help='path to a file containing the output arguments '
                           'as a JSON object.')
    subp.add_argument('--json-output',
                      help='Write errors to json.output')
    subp.set_defaults(func=self.CmdAnalyze)

    subp = subps.add_parser('export',
                            help='print out the expanded configuration for'
                                 'each builder as a JSON object')
    subp.add_argument(
        '-f',
        '--config-file',
        metavar='PATH',
        default=self.default_config,
        help='path to config file (default is %(default)s)')
    subp.set_defaults(func=self.CmdExport)

    subp = subps.add_parser('gen',
                            help='generate a new set of build files')
    AddCommonOptions(subp)
    subp.add_argument('--swarming-targets-file',
                      help='save runtime dependencies for targets listed '
                           'in file.')
    subp.add_argument('--json-output',
                      help='Write errors to json.output')
    subp.add_argument('path', nargs=1,
                      help='path to generate build into')
    subp.set_defaults(func=self.CmdGen)

    subp = subps.add_parser('isolate',
                            help='generate the .isolate files for a given'
                                 'binary')
    AddCommonOptions(subp)
    subp.add_argument('path', nargs=1,
                      help='path build was generated into')
    subp.add_argument('target', nargs=1,
                      help='ninja target to generate the isolate for')
    subp.set_defaults(func=self.CmdIsolate)

    subp = subps.add_parser('lookup',
                            help='look up the command for a given config or '
                                 'builder')
    AddCommonOptions(subp)
    subp.add_argument('--quiet', default=False, action='store_true',
                      help='Print out just the arguments, '
                           'do not emulate the output of the gen subcommand.')
    subp.add_argument('--recursive', default=False, action='store_true',
                      help='Lookup arguments from imported files, '
                           'implies --quiet')
    subp.set_defaults(func=self.CmdLookup)

    subp = subps.add_parser(
        'run',
        help='build and run the isolated version of a '
             'binary',
        formatter_class=argparse.RawDescriptionHelpFormatter)
    subp.description = (
        'Build, isolate, and run the given binary with the command line\n'
        'listed in the isolate. You may pass extra arguments after the\n'
        'target; use "--" if the extra arguments need to include switches.\n'
        '\n'
        'Examples:\n'
        '\n'
        '  % tools/mb/mb.py run -m chromium.linux -b "Linux Builder" \\\n'
        '    //out/Default content_browsertests\n'
        '\n'
        '  % tools/mb/mb.py run out/Default content_browsertests\n'
        '\n'
        '  % tools/mb/mb.py run out/Default content_browsertests -- \\\n'
        '    --test-launcher-retry-limit=0'
        '\n'
    )
    AddCommonOptions(subp)
    subp.add_argument('-j', '--jobs', dest='jobs', type=int,
                      help='Number of jobs to pass to ninja')
    subp.add_argument('--no-build', dest='build', default=True,
                      action='store_false',
                      help='Do not build, just isolate and run')
    subp.add_argument('path', nargs=1,
                      help=('path to generate build into (or use).'
                            ' This can be either a regular path or a '
                            'GN-style source-relative path like '
                            '//out/Default.'))
    subp.add_argument('-d', '--dimension', default=[], action='append', nargs=2,
                      dest='dimensions', metavar='FOO bar',
                      help='dimension to filter on')
    subp.add_argument('--no-default-dimensions', action='store_false',
                      dest='default_dimensions', default=True,
                      help='Do not automatically add dimensions to the task')
    subp.add_argument('target', nargs=1,
                      help='ninja target to build and run')
    subp.add_argument('extra_args', nargs='*',
                      help=('extra args to pass to the isolate to run. Use '
                            '"--" as the first arg if you need to pass '
                            'switches'))
    subp.set_defaults(func=self.CmdRun)

    subp = subps.add_parser('validate',
                            help='validate the config file')
    subp.add_argument('-f', '--config-file', metavar='PATH',
                      default=self.default_config,
                      help='path to config file (default is %(default)s)')
    subp.set_defaults(func=self.CmdValidate)

    subp = subps.add_parser('gerrit-buildbucket-config',
                            help='Print buildbucket.config for gerrit '
                            '(see MB user guide)')
    subp.add_argument('-f', '--config-file', metavar='PATH',
                      default=self.default_config,
                      help='path to config file (default is %(default)s)')
    subp.set_defaults(func=self.CmdBuildbucket)

    subp = subps.add_parser('help',
                            help='Get help on a subcommand.')
    subp.add_argument(nargs='?', action='store', dest='subcommand',
                      help='The command to get help for.')
    subp.set_defaults(func=self.CmdHelp)

    self.args = parser.parse_args(argv)

  def DumpInputFiles(self):

    def DumpContentsOfFilePassedTo(arg_name, path):
      if path and self.Exists(path):
        self.Print("\n# To recreate the file passed to %s:" % arg_name)
        self.Print("%% cat > %s <<EOF" % path)
        contents = self.ReadFile(path)
        self.Print(contents)
        self.Print("EOF\n%\n")

    if getattr(self.args, 'input_path', None):
      DumpContentsOfFilePassedTo(
          'argv[0] (input_path)', self.args.input_path[0])
    if getattr(self.args, 'swarming_targets_file', None):
      DumpContentsOfFilePassedTo(
          '--swarming-targets-file', self.args.swarming_targets_file)

  def CmdAnalyze(self):
    vals = self.Lookup()
    return self.RunGNAnalyze(vals)

  def CmdExport(self):
    self.ReadConfigFile()
    obj = {}
    for builder_group, builders in self.builder_groups.items():
      obj[builder_group] = {}
      for builder in builders:
        config = self.builder_groups[builder_group][builder]
        if not config:
          continue

        if isinstance(config, dict):
          args = {k: self.FlattenConfig(v)['gn_args']
                  for k, v in config.items()}
        elif config.startswith('//'):
          args = config
        else:
          args = self.FlattenConfig(config)['gn_args']
          if 'error' in args:
            continue

        obj[builder_group][builder] = args

    # Dump object and trim trailing whitespace.
    s = '\n'.join(l.rstrip() for l in
                  json.dumps(obj, sort_keys=True, indent=2).splitlines())
    self.Print(s)
    return 0

  def CmdGen(self):
    vals = self.Lookup()
    return self.RunGNGen(vals)

  def CmdHelp(self):
    if self.args.subcommand:
      self.ParseArgs([self.args.subcommand, '--help'])
    else:
      self.ParseArgs(['--help'])

  def CmdIsolate(self):
    vals = self.GetConfig()
    if not vals:
      return 1
    return self.RunGNIsolate()

  def CmdLookup(self):
    vals = self.Lookup()
    gn_args = self.GNArgs(vals, expand_imports=self.args.recursive)
    if self.args.quiet or self.args.recursive:
      self.Print(gn_args, end='')
    else:
      cmd = self.GNCmd('gen', '_path_')
      self.Print('\nWriting """\\\n%s""" to _path_/args.gn.\n' % gn_args)
      env = None

      self.PrintCmd(cmd, env)
    return 0

  def CmdRun(self):
    vals = self.GetConfig()
    if not vals:
      return 1

    build_dir = self.args.path[0]
    target = self.args.target[0]

    if self.args.build:
      ret = self.Build(target)
      if ret:
        return ret
    ret = self.RunGNIsolate()
    if ret:
      return ret

    return self._RunLocallyIsolated(build_dir, target)

  def _RunLocallyIsolated(self, build_dir, target):
    cmd = [
        self.PathJoin(self.chromium_src_dir, 'tools', 'luci-go',
                      self.isolate_exe),
        'run',
        '-i',
        self.ToSrcRelPath('%s/%s.isolate' % (build_dir, target)),
      ]
    if self.args.extra_args:
      cmd += ['--'] + self.args.extra_args
    ret, _, _ = self.Run(cmd, force_verbose=True, buffer_output=False)
    return ret

  def _DefaultDimensions(self):
    if not self.args.default_dimensions:
      return []

    # This code is naive and just picks reasonable defaults per platform.
    if self.platform == 'darwin':
      os_dim = ('os', 'Mac-10.12')
    elif self.platform.startswith('linux'):
      os_dim = ('os', 'Ubuntu-16.04')
    elif self.platform == 'win32':
      os_dim = ('os', 'Windows-10')
    else:
      raise MBErr('unrecognized platform string "%s"' % self.platform)

    return [('pool', 'Chrome'),
            ('cpu', 'x86-64'),
            os_dim]

  def CmdBuildbucket(self):
    self.ReadConfigFile()

    self.Print('# This file was generated using '
               '"tools/mb/mb.py gerrit-buildbucket-config".')

    for luci_tryserver in sorted(self.luci_tryservers):
      self.Print('[bucket "luci.%s"]' % luci_tryserver)
      for bot in sorted(self.luci_tryservers[luci_tryserver]):
        self.Print('\tbuilder = %s' % bot)

    for builder_group in sorted(self.builder_groups):
      if builder_group.startswith('tryserver.'):
        self.Print('[bucket "builder_group.%s"]' % builder_group)
        for bot in sorted(self.builder_groups[builder_group]):
          self.Print('\tbuilder = %s' % bot)

    return 0

  def CmdValidate(self, print_ok=True):
    errs = []

    # Read the file to make sure it parses.
    self.ReadConfigFile()

    # Build a list of all of the configs referenced by builders.
    all_configs = {}
    for builder_group in self.builder_groups:
      for config in self.builder_groups[builder_group].values():
        if isinstance(config, dict):
          for c in config.values():
            all_configs[c] = builder_group
        else:
          all_configs[config] = builder_group

    # Check that every referenced args file or config actually exists.
    for config, loc in all_configs.items():
      if config.startswith('//'):
        if not self.Exists(self.ToAbsPath(config)):
          errs.append('Unknown args file "%s" referenced from "%s".' %
                      (config, loc))
      elif not config in self.configs:
        errs.append('Unknown config "%s" referenced from "%s".' %
                    (config, loc))

    # Check that every actual config is actually referenced.
    for config in self.configs:
      if not config in all_configs:
        errs.append('Unused config "%s".' % config)

    # Figure out the whole list of mixins, and check that every mixin
    # listed by a config or another mixin actually exists.
    referenced_mixins = set()
    for config, mixins in self.configs.items():
      for mixin in mixins:
        if not mixin in self.mixins:
          errs.append('Unknown mixin "%s" referenced by config "%s".' %
                      (mixin, config))
        referenced_mixins.add(mixin)

    for mixin in self.mixins:
      for sub_mixin in self.mixins[mixin].get('mixins', []):
        if not sub_mixin in self.mixins:
          errs.append('Unknown mixin "%s" referenced by mixin "%s".' %
                      (sub_mixin, mixin))
        referenced_mixins.add(sub_mixin)

    # Check that every mixin defined is actually referenced somewhere.
    for mixin in self.mixins:
      if not mixin in referenced_mixins:
        errs.append('Unreferenced mixin "%s".' % mixin)

    if errs:
      raise MBErr(('mb config file %s has problems:' % self.args.config_file) +
                    '\n  ' + '\n  '.join(errs))

    if print_ok:
      self.Print('mb config file %s looks ok.' % self.args.config_file)
    return 0

  def GetConfig(self):
    build_dir = self.args.path[0]

    vals = self.DefaultVals()
    if self.args.builder or self.args.builder_group or self.args.config:
      vals = self.Lookup()
      # Re-run gn gen in order to ensure the config is consistent with the
      # build dir.
      self.RunGNGen(vals)
      return vals

    toolchain_path = self.PathJoin(self.ToAbsPath(build_dir),
                                   'toolchain.ninja')
    if not self.Exists(toolchain_path):
      self.Print('Must either specify a path to an existing GN build dir '
                 'or pass in a -m/-b pair or a -c flag to specify the '
                 'configuration')
      return {}

    vals['gn_args'] = self.GNArgsFromDir(build_dir)
    return vals

  def GNArgsFromDir(self, build_dir):
    args_contents = ""
    gn_args_path = self.PathJoin(self.ToAbsPath(build_dir), 'args.gn')
    if self.Exists(gn_args_path):
      args_contents = self.ReadFile(gn_args_path)
    gn_args = []
    for l in args_contents.splitlines():
      fields = l.split(' ')
      name = fields[0]
      val = ' '.join(fields[2:])
      gn_args.append('%s=%s' % (name, val))

    return ' '.join(gn_args)

  def Lookup(self):
    vals = self.ReadIOSBotConfig()
    if not vals:
      self.ReadConfigFile()
      config = self.ConfigFromArgs()
      if config.startswith('//'):
        if not self.Exists(self.ToAbsPath(config)):
          raise MBErr('args file "%s" not found' % config)
        vals = self.DefaultVals()
        vals['args_file'] = config
      else:
        if not config in self.configs:
          raise MBErr('Config "%s" not found in %s' %
                      (config, self.args.config_file))
        vals = self.FlattenConfig(config)
    return vals

  def ReadIOSBotConfig(self):
    if not self.args.builder_group or not self.args.builder:
      return {}
    path = self.PathJoin(self.chromium_src_dir, 'ios', 'build', 'bots',
                         self.args.builder_group, self.args.builder + '.json')
    if not self.Exists(path):
      return {}

    contents = json.loads(self.ReadFile(path))
    gn_args = ' '.join(contents.get('gn_args', []))

    vals = self.DefaultVals()
    vals['gn_args'] = gn_args
    return vals

  def ReadConfigFile(self):
    if not self.Exists(self.args.config_file):
      raise MBErr('config file not found at %s' % self.args.config_file)

    try:
      contents = ast.literal_eval(self.ReadFile(self.args.config_file))
    except SyntaxError as e:
      raise MBErr('Failed to parse config file "%s": %s' %
                 (self.args.config_file, e)) from e

    self.configs = contents['configs']
    self.luci_tryservers = contents.get('luci_tryservers', {})
    self.builder_groups = contents['builder_groups']
    self.mixins = contents['mixins']

  def ReadIsolateMap(self):
    if not self.args.isolate_map_files:
      self.args.isolate_map_files = [self.default_isolate_map]

    for f in self.args.isolate_map_files:
      if not self.Exists(f):
        raise MBErr('isolate map file not found at %s' % f)
    isolate_maps = {}
    for isolate_map in self.args.isolate_map_files:
      try:
        isolate_map = ast.literal_eval(self.ReadFile(isolate_map))
        duplicates = set(isolate_map).intersection(isolate_maps)
        if duplicates:
          raise MBErr(
              'Duplicate targets in isolate map files: %s.' %
              ', '.join(duplicates))
        isolate_maps.update(isolate_map)
      except SyntaxError as e:
        raise MBErr('Failed to parse isolate map file "%s": %s' %
                    (isolate_map, e)) from e
    return isolate_maps

  def ConfigFromArgs(self):
    if self.args.config:
      if self.args.builder_group or self.args.builder:
        raise MBErr(
          'Can not specific both -c/--config and -m/--builder-group or '
          '-b/--builder')

      return self.args.config

    if not self.args.builder_group or not self.args.builder:
      raise MBErr('Must specify either -c/--config or '
                  '(-m/--builder-group and -b/--builder)')

    if not self.args.builder_group in self.builder_groups:
      raise MBErr('Builder groups name "%s" not found in "%s"' %
                  (self.args.builder_group, self.args.config_file))

    config = _v8_builder_fallback(
        self.args.builder, self.builder_groups[self.args.builder_group])

    if not config:
      raise MBErr(
        'Builder name "%s"  not found under builder_groups[%s] in "%s"' %
        (self.args.builder, self.args.builder_group, self.args.config_file))

    if isinstance(config, dict):
      if self.args.phase is None:
        raise MBErr('Must specify a build --phase for %s on %s' %
                    (self.args.builder, self.args.builder_group))
      phase = str(self.args.phase)
      if phase not in config:
        raise MBErr('Phase %s doesn\'t exist for %s on %s' %
                    (phase, self.args.builder, self.args.builder_group))
      return config[phase]

    if self.args.phase is not None:
      raise MBErr('Must not specify a build --phase for %s on %s' %
                  (self.args.builder, self.args.builder_group))
    return config

  def FlattenConfig(self, config):
    mixins = self.configs[config]
    vals = self.DefaultVals()

    visited = []
    self.FlattenMixins(mixins, vals, visited)
    return vals

  def DefaultVals(self):
    return {
      'args_file': '',
      'cros_passthrough': False,
      'gn_args': '',
    }

  def FlattenMixins(self, mixins, vals, visited):
    for m in mixins:
      if m not in self.mixins:
        raise MBErr('Unknown mixin "%s"' % m)

      visited.append(m)

      mixin_vals = self.mixins[m]

      if 'cros_passthrough' in mixin_vals:
        vals['cros_passthrough'] = mixin_vals['cros_passthrough']
      if 'args_file' in mixin_vals:
        if vals['args_file']:
          raise MBErr('args_file specified multiple times in mixins '
                      'for %s on %s' %
                      (self.args.builder, self.args.builder_group))
        vals['args_file'] = mixin_vals['args_file']
      if 'gn_args' in mixin_vals:
        if vals['gn_args']:
          vals['gn_args'] += ' ' + mixin_vals['gn_args']
        else:
          vals['gn_args'] = mixin_vals['gn_args']

      if 'mixins' in mixin_vals:
        self.FlattenMixins(mixin_vals['mixins'], vals, visited)
    return vals

  def RunGNGen(self, vals, compute_grit_inputs_for_analyze=False):
    build_dir = self.args.path[0]

    cmd = self.GNCmd('gen', build_dir, '--check')
    gn_args = self.GNArgs(vals)
    if compute_grit_inputs_for_analyze:
      gn_args += ' compute_grit_inputs_for_analyze=true'

    # Since GN hasn't run yet, the build directory may not even exist.
    self.MaybeMakeDirectory(self.ToAbsPath(build_dir))

    gn_args_path = self.ToAbsPath(build_dir, 'args.gn')
    self.WriteFile(gn_args_path, gn_args, force_verbose=True)

    swarming_targets = []
    if getattr(self.args, 'swarming_targets_file', None):
      # We need GN to generate the list of runtime dependencies for
      # the compile targets listed (one per line) in the file so
      # we can run them via swarming. We use gn_isolate_map.pyl to convert
      # the compile targets to the matching GN labels.
      path = self.args.swarming_targets_file
      if not self.Exists(path):
        self.WriteFailureAndRaise('"%s" does not exist' % path,
                                  output_path=None)
      contents = self.ReadFile(path)
      swarming_targets = set(contents.splitlines())

      isolate_map = self.ReadIsolateMap()
      err, labels = self.MapTargetsToLabels(isolate_map, swarming_targets)
      if err:
        raise MBErr(err)

      gn_runtime_deps_path = self.ToAbsPath(build_dir, 'runtime_deps')
      self.WriteFile(gn_runtime_deps_path, '\n'.join(labels) + '\n')
      cmd.append('--runtime-deps-list-file=%s' % gn_runtime_deps_path)

    ret, output, _ = self.Run(cmd)
    if ret:
      if self.args.json_output:
        # write errors to json.output
        self.WriteJSON({'output': output}, self.args.json_output)
      # If `gn gen` failed, we should exit early rather than trying to
      # generate isolates. Run() will have already logged any error output.
      self.Print('GN gen failed: %d' % ret)
      return ret

    android = 'target_os="android"' in vals['gn_args']
    for target in swarming_targets:
      if android:
        # Android targets may be either android_apk or executable. The former
        # will result in runtime_deps associated with the stamp file, while the
        # latter will result in runtime_deps associated with the executable.
        label = isolate_map[target]['label']
        runtime_deps_targets = [
            target + '.runtime_deps',
            'obj/%s.stamp.runtime_deps' % label.replace(':', '/')]
      elif (isolate_map[target]['type'] == 'script' or
            isolate_map[target].get('label_type') == 'group'):
        # For script targets, the build target is usually a group,
        # for which gn generates the runtime_deps next to the stamp file
        # for the label, which lives under the obj/ directory, but it may
        # also be an executable.
        label = isolate_map[target]['label']
        runtime_deps_targets = [
            'obj/%s.stamp.runtime_deps' % label.replace(':', '/')]
        if self.platform == 'win32':
          runtime_deps_targets += [ target + '.exe.runtime_deps' ]
        else:
          runtime_deps_targets += [ target + '.runtime_deps' ]
      elif self.platform == 'win32':
        runtime_deps_targets = [target + '.exe.runtime_deps']
      else:
        runtime_deps_targets = [target + '.runtime_deps']

      for r in runtime_deps_targets:
        runtime_deps_path = self.ToAbsPath(build_dir, r)
        if self.Exists(runtime_deps_path):
          break
      else:
        raise MBErr('did not generate any of %s' %
                    ', '.join(runtime_deps_targets))

      runtime_deps = self.ReadFile(runtime_deps_path).splitlines()

      self.WriteIsolateFiles(build_dir, target, runtime_deps)

    return 0

  def RunGNIsolate(self):
    target = self.args.target[0]
    isolate_map = self.ReadIsolateMap()
    err, labels = self.MapTargetsToLabels(isolate_map, [target])
    if err:
      raise MBErr(err)
    label = labels[0]

    build_dir = self.args.path[0]

    cmd = self.GNCmd('desc', build_dir, label, 'runtime_deps')
    ret, out, _ = self.Call(cmd)
    if ret:
      if out:
        self.Print(out)
      return ret

    runtime_deps = out.splitlines()

    self.WriteIsolateFiles(build_dir, target, runtime_deps)

    ret, _, _ = self.Run([
        self.PathJoin(self.chromium_src_dir, 'tools', 'luci-go',
                      self.isolate_exe),
        'check',
        '-i',
        self.ToSrcRelPath('%s/%s.isolate' % (build_dir, target))],
        buffer_output=False)

    return ret

  def WriteIsolateFiles(self, build_dir, target, runtime_deps):
    isolate_path = self.ToAbsPath(build_dir, target + '.isolate')
    self.WriteFile(isolate_path,
      pprint.pformat({
        'variables': {
          'files': sorted(runtime_deps),
        }
      }) + '\n')

    self.WriteJSON(
      {
        'args': [
          '--isolate',
          self.ToSrcRelPath('%s/%s.isolate' % (build_dir, target)),
        ],
        'dir': self.chromium_src_dir,
        'version': 1,
      },
      isolate_path + 'd.gen.json',
    )

  def MapTargetsToLabels(self, isolate_map, targets):
    labels = []
    err = ''

    for target in targets:
      if target == 'all':
        labels.append(target)
      elif target.startswith('//'):
        labels.append(target)
      else:
        if target in isolate_map:
          if isolate_map[target]['type'] == 'unknown':
            err += ('test target "%s" type is unknown\n' % target)
          else:
            labels.append(isolate_map[target]['label'])
        else:
          err += ('target "%s" not found in '
                  '//infra/mb/gn_isolate_map.pyl\n' % target)

    return err, labels

  def GNCmd(self, subcommand, path, *args):
    if self.platform.startswith('linux'):
      subdir, exe = 'linux64', 'gn'
    elif self.platform == 'darwin':
      subdir, exe = 'mac', 'gn'
    else:
      subdir, exe = 'win', 'gn.exe'

    arch = platform.machine()
    if (arch.startswith('s390') or arch.startswith('ppc') or
        self.platform.startswith('aix')):
      # use gn in PATH
      gn_path = 'gn'
    else:
      gn_path = self.PathJoin(self.chromium_src_dir, 'buildtools', subdir, exe)
    return [gn_path, subcommand, path] + list(args)


  def GNArgs(self, vals, expand_imports=False):
    if vals['cros_passthrough']:
      if not 'GN_ARGS' in os.environ:
        raise MBErr('MB is expecting GN_ARGS to be in the environment')
      gn_args = os.environ['GN_ARGS']
      if not re.search('target_os.*=.*"chromeos"', gn_args):
        raise MBErr('GN_ARGS is missing target_os = "chromeos": (GN_ARGS=%s)' %
                    gn_args)
    else:
      gn_args = vals['gn_args']

    android_version_code = self.args.android_version_code
    if android_version_code:
      gn_args += ' android_default_version_code="%s"' % android_version_code

    android_version_name = self.args.android_version_name
    if android_version_name:
      gn_args += ' android_default_version_name="%s"' % android_version_name

    args_gn_lines = []
    parsed_gn_args = {}

    args_file = vals.get('args_file', None)
    if args_file:
      if expand_imports:
        content = self.ReadFile(self.ToAbsPath(args_file))
        parsed_gn_args = gn_helpers.FromGNArgs(content)
      else:
        args_gn_lines.append('import("%s")' % args_file)

    # Canonicalize the arg string into a sorted, newline-separated list
    # of key-value pairs, and de-dup the keys if need be so that only
    # the last instance of each arg is listed.
    parsed_gn_args.update(gn_helpers.FromGNArgs(gn_args))
    args_gn_lines.append(gn_helpers.ToGNString(parsed_gn_args))

    return '\n'.join(args_gn_lines)

  def ToAbsPath(self, build_path, *comps):
    return self.PathJoin(self.chromium_src_dir,
                         self.ToSrcRelPath(build_path),
                         *comps)

  def ToSrcRelPath(self, path):
    """Returns a relative path from the top of the repo."""
    if path.startswith('//'):
      return path[2:].replace('/', self.sep)
    return self.RelPath(path, self.chromium_src_dir)

  def RunGNAnalyze(self, vals):
    # Analyze runs before 'gn gen' now, so we need to run gn gen
    # in order to ensure that we have a build directory.
    ret = self.RunGNGen(vals, compute_grit_inputs_for_analyze=True)
    if ret:
      return ret

    build_path = self.args.path[0]
    input_path = self.args.input_path[0]
    gn_input_path = input_path + '.gn'
    output_path = self.args.output_path[0]
    gn_output_path = output_path + '.gn'

    inp = self.ReadInputJSON(['files', 'test_targets',
                              'additional_compile_targets'])
    if self.args.verbose:
      self.Print()
      self.Print('analyze input:')
      self.PrintJSON(inp)
      self.Print()


    # This shouldn't normally happen, but could due to unusual race conditions,
    # like a try job that gets scheduled before a patch lands but runs after
    # the patch has landed.
    if not inp['files']:
      self.Print('Warning: No files modified in patch, bailing out early.')
      self.WriteJSON({
            'status': 'No dependency',
            'compile_targets': [],
            'test_targets': [],
          }, output_path)
      return 0

    gn_inp = {}
    gn_inp['files'] = ['//' + f for f in inp['files'] if not f.startswith('//')]

    isolate_map = self.ReadIsolateMap()
    err, gn_inp['additional_compile_targets'] = self.MapTargetsToLabels(
        isolate_map, inp['additional_compile_targets'])
    if err:
      raise MBErr(err)

    err, gn_inp['test_targets'] = self.MapTargetsToLabels(
        isolate_map, inp['test_targets'])
    if err:
      raise MBErr(err)
    labels_to_targets = {}
    for i, label in enumerate(gn_inp['test_targets']):
      labels_to_targets[label] = inp['test_targets'][i]

    try:
      self.WriteJSON(gn_inp, gn_input_path)
      cmd = self.GNCmd('analyze', build_path, gn_input_path, gn_output_path)
      ret, output, _ = self.Run(cmd, force_verbose=True)
      if ret:
        if self.args.json_output:
          # write errors to json.output
          self.WriteJSON({'output': output}, self.args.json_output)
        return ret

      gn_outp_str = self.ReadFile(gn_output_path)
      try:
        gn_outp = json.loads(gn_outp_str)
      except Exception as e:
        self.Print("Failed to parse the JSON string GN returned: %s\n%s"
                   % (repr(gn_outp_str), str(e)))
        raise

      outp = {}
      if 'status' in gn_outp:
        outp['status'] = gn_outp['status']
      if 'error' in gn_outp:
        outp['error'] = gn_outp['error']
      if 'invalid_targets' in gn_outp:
        outp['invalid_targets'] = gn_outp['invalid_targets']
      if 'compile_targets' in gn_outp:
        all_input_compile_targets = sorted(
            set(inp['test_targets'] + inp['additional_compile_targets']))

        # If we're building 'all', we can throw away the rest of the targets
        # since they're redundant.
        if 'all' in gn_outp['compile_targets']:
          outp['compile_targets'] = ['all']
        else:
          outp['compile_targets'] = gn_outp['compile_targets']

        # crbug.com/736215: When GN returns targets back, for targets in
        # the default toolchain, GN will have generated a phony ninja
        # target matching the label, and so we can safely (and easily)
        # transform any GN label into the matching ninja target. For
        # targets in other toolchains, though, GN doesn't generate the
        # phony targets, and we don't know how to turn the labels into
        # compile targets. In this case, we also conservatively give up
        # and build everything. Probably the right thing to do here is
        # to have GN return the compile targets directly.
        if any("(" in target for target in outp['compile_targets']):
          self.Print('WARNING: targets with non-default toolchains were '
                     'found, building everything instead.')
          outp['compile_targets'] = all_input_compile_targets
        else:
          outp['compile_targets'] = [
              label.replace('//', '') for label in outp['compile_targets']]

        # Windows has a maximum command line length of 8k; even Linux
        # maxes out at 128k; if analyze returns a *really long* list of
        # targets, we just give up and conservatively build everything instead.
        # Probably the right thing here is for ninja to support response
        # files as input on the command line
        # (see https://github.com/ninja-build/ninja/issues/1355).
        if len(' '.join(outp['compile_targets'])) > 7*1024:
          self.Print('WARNING: Too many compile targets were affected.')
          self.Print('WARNING: Building everything instead to avoid '
                     'command-line length issues.')
          outp['compile_targets'] = all_input_compile_targets


      if 'test_targets' in gn_outp:
        outp['test_targets'] = [
          labels_to_targets[label] for label in gn_outp['test_targets']]

      if self.args.verbose:
        self.Print()
        self.Print('analyze output:')
        self.PrintJSON(outp)
        self.Print()

      self.WriteJSON(outp, output_path)

    finally:
      if self.Exists(gn_input_path):
        self.RemoveFile(gn_input_path)
      if self.Exists(gn_output_path):
        self.RemoveFile(gn_output_path)

    return 0

  def ReadInputJSON(self, required_keys):
    path = self.args.input_path[0]
    output_path = self.args.output_path[0]
    if not self.Exists(path):
      self.WriteFailureAndRaise('"%s" does not exist' % path, output_path)

    try:
      inp = json.loads(self.ReadFile(path))
    except Exception as e:
      self.WriteFailureAndRaise('Failed to read JSON input from "%s": %s' %
                                (path, e), output_path)

    for k in required_keys:
      if not k in inp:
        self.WriteFailureAndRaise('input file is missing a "%s" key' % k,
                                  output_path)

    return inp

  def WriteFailureAndRaise(self, msg, output_path):
    if output_path:
      self.WriteJSON({'error': msg}, output_path, force_verbose=True)
    raise MBErr(msg)

  def WriteJSON(self, obj, path, force_verbose=False):
    try:
      self.WriteFile(path, json.dumps(obj, indent=2, sort_keys=True) + '\n',
                     force_verbose=force_verbose)
    except Exception as e:
      raise MBErr('Error %s writing to the output path "%s"' %
                 (e, path)) from e

  def CheckCompile(self, builder_group, builder):
    url_template = self.args.url_template + '/{builder}/builds/_all?as_text=1'
    url = quote(
            url_template.format(builder_group=builder_group, builder=builder),
            safe=':/()?=')
    try:
      builds = json.loads(self.Fetch(url))
    except Exception as e:
      return str(e)
    successes = sorted(
        [int(x) for x in builds.keys() if "text" in builds[x] and
          cmp(builds[x]["text"][:2], ["build", "successful"]) == 0],
        reverse=True)
    if not successes:
      return "no successful builds"
    build = builds[str(successes[0])]
    step_names = {step["name"] for step in build["steps"]}
    compile_indicators = set(["compile", "compile (with patch)", "analyze"])
    if compile_indicators & step_names:
      return "compiles"
    return "does not compile"

  def PrintCmd(self, cmd, env):
    if self.platform == 'win32':
      env_prefix = 'set '
      env_quoter = QuoteForSet
      shell_quoter = QuoteForCmd
    else:
      env_prefix = ''
      env_quoter = pipes.quote
      shell_quoter = pipes.quote

    def print_env(var):
      if env and var in env:
        self.Print('%s%s=%s' % (env_prefix, var, env_quoter(env[var])))

    print_env('LLVM_FORCE_HEAD_REVISION')

    if cmd[0] == self.executable:
      cmd = ['python'] + cmd[1:]
    self.Print(*[shell_quoter(arg) for arg in cmd])

  def PrintJSON(self, obj):
    self.Print(json.dumps(obj, indent=2, sort_keys=True))

  def Build(self, target):
    build_dir = self.ToSrcRelPath(self.args.path[0])
    ninja_cmd = ['ninja', '-C', build_dir]
    if self.args.jobs:
      ninja_cmd.extend(['-j', '%d' % self.args.jobs])
    ninja_cmd.append(target)
    ret, _, _ = self.Run(ninja_cmd, force_verbose=False, buffer_output=False)
    return ret

  def Run(self, cmd, env=None, force_verbose=True, buffer_output=True):
    # This function largely exists so it can be overridden for testing.
    if self.args.dryrun or self.args.verbose or force_verbose:
      self.PrintCmd(cmd, env)
    if self.args.dryrun:
      return 0, '', ''

    ret, out, err = self.Call(cmd, env=env, buffer_output=buffer_output)
    if self.args.verbose or force_verbose:
      if ret:
        self.Print('  -> returned %d' % ret)
      if out:
        self.Print(out, end='')
      if err:
        self.Print(err, end='', file=sys.stderr)
    return ret, out, err

  def Call(self, cmd, env=None, buffer_output=True):
    if buffer_output:
      p = subprocess.Popen(cmd, shell=False, cwd=self.chromium_src_dir,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           env=env)
      out, err = p.communicate()
      out = out.decode('utf-8')
      err = err.decode('utf-8')
    else:
      p = subprocess.Popen(cmd, shell=False, cwd=self.chromium_src_dir,
                           env=env)
      p.wait()
      out = err = ''
    return p.returncode, out, err

  def ExpandUser(self, path):
    # This function largely exists so it can be overridden for testing.
    return os.path.expanduser(path)

  def Exists(self, path):
    # This function largely exists so it can be overridden for testing.
    return os.path.exists(path)

  def Fetch(self, url):
    # This function largely exists so it can be overridden for testing.
    f = urlopen(url)
    contents = f.read()
    f.close()
    return contents

  def MaybeMakeDirectory(self, path):
    try:
      os.makedirs(path)
    except OSError as e:
      if e.errno != errno.EEXIST:
        raise

  def PathJoin(self, *comps):
    # This function largely exists so it can be overriden for testing.
    return os.path.join(*comps)

  def Print(self, *args, **kwargs):
    # This function largely exists so it can be overridden for testing.
    print(*args, **kwargs)
    if kwargs.get('stream', sys.stdout) == sys.stdout:
      sys.stdout.flush()

  def ReadFile(self, path):
    # This function largely exists so it can be overriden for testing.
    with open(path) as fp:
      return fp.read()

  def RelPath(self, path, start='.'):
    # This function largely exists so it can be overriden for testing.
    return os.path.relpath(path, start)

  def RemoveFile(self, path):
    # This function largely exists so it can be overriden for testing.
    os.remove(path)

  def RemoveDirectory(self, abs_path):
    if self.platform == 'win32':
      # In other places in chromium, we often have to retry this command
      # because we're worried about other processes still holding on to
      # file handles, but when MB is invoked, it will be early enough in the
      # build that their should be no other processes to interfere. We
      # can change this if need be.
      self.Run(['cmd.exe', '/c', 'rmdir', '/q', '/s', abs_path])
    else:
      shutil.rmtree(abs_path, ignore_errors=True)

  def TempFile(self, mode='w'):
    # This function largely exists so it can be overriden for testing.
    return tempfile.NamedTemporaryFile(mode=mode, delete=False)

  def WriteFile(self, path, contents, force_verbose=False):
    # This function largely exists so it can be overriden for testing.
    if self.args.dryrun or self.args.verbose or force_verbose:
      self.Print('\nWriting """\\\n%s""" to %s.\n' % (contents, path))
    with open(path, 'w') as fp:
      return fp.write(contents)


class MBErr(Exception):
  pass


# See http://goo.gl/l5NPDW and http://goo.gl/4Diozm for the painful
# details of this next section, which handles escaping command lines
# so that they can be copied and pasted into a cmd window.
UNSAFE_FOR_SET = set('^<>&|')
UNSAFE_FOR_CMD = UNSAFE_FOR_SET.union(set('()%'))
ALL_META_CHARS = UNSAFE_FOR_CMD.union(set('"'))


def QuoteForSet(arg):
  if any(a in UNSAFE_FOR_SET for a in arg):
    arg = ''.join('^' + a if a in UNSAFE_FOR_SET else a for a in arg)
  return arg


def QuoteForCmd(arg):
  # First, escape the arg so that CommandLineToArgvW will parse it properly.
  if arg == '' or ' ' in arg or '"' in arg:
    quote_re = re.compile(r'(\\*)"')
    arg = '"%s"' % (quote_re.sub(lambda mo: 2 * mo.group(1) + '\\"', arg))

  # Then check to see if the arg contains any metacharacters other than
  # double quotes; if it does, quote everything (including the double
  # quotes) for safety.
  if any(a in UNSAFE_FOR_CMD for a in arg):
    arg = ''.join('^' + a if a in ALL_META_CHARS else a for a in arg)
  return arg


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
