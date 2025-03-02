#!/usr/bin/env python3
# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from pathlib import Path

import collections
import os
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

import gcmole

GCMOLE_PATH = Path(__file__).parent.absolute()
TESTDATA_PATH = GCMOLE_PATH / 'testdata' / 'v8'

Options = collections.namedtuple(
    'Options', ['v8_root_dir', 'v8_target_cpu', 'shard_count', 'shard_index',
                'test_run'])


def abs_test_file(f):
  return TESTDATA_PATH / f


class FilesTest(unittest.TestCase):

  def testFileList_for_testing(self):
    options = Options(TESTDATA_PATH, 'x64', 1, 0, True)
    self.assertEqual(
        gcmole.build_file_list(options),
        list(map(abs_test_file, ['tools/gcmole/gcmole-test.cc'])))

  def testFileList_x64(self):
    options = Options(TESTDATA_PATH, 'x64', 1, 0, False)
    expected = [
        'file1.cc',
        'file2.cc',
        'x64/file1.cc',
        'x64/file2.cc',
        'file3.cc',
        'file4.cc',
        'test/cctest/test-x64-file1.cc',
        'test/cctest/test-x64-file2.cc',
    ]
    self.assertEqual(
        gcmole.build_file_list(options),
        list(map(abs_test_file, expected)))

  def testFileList_x64_shard0(self):
    options = Options(TESTDATA_PATH, 'x64', 2, 0, False)
    expected = [
        'file1.cc',
        'x64/file1.cc',
        'file3.cc',
        'test/cctest/test-x64-file1.cc',
    ]
    self.assertEqual(
        gcmole.build_file_list(options),
        list(map(abs_test_file, expected)))

  def testFileList_x64_shard1(self):
    options = Options(TESTDATA_PATH, 'x64', 2, 1, False)
    expected = [
        'file2.cc',
        'x64/file2.cc',
        'file4.cc',
        'test/cctest/test-x64-file2.cc',
    ]
    self.assertEqual(
        gcmole.build_file_list(options),
        list(map(abs_test_file, expected)))

  def testFileList_arm(self):
    options = Options(TESTDATA_PATH, 'arm', 1, 0, False)
    expected = [
        'file1.cc',
        'file2.cc',
        'file3.cc',
        'file4.cc',
        'arm/file1.cc',
        'arm/file2.cc',
    ]
    self.assertEqual(
        gcmole.build_file_list(options),
        list(map(abs_test_file, expected)))


GC = 'Foo,NowCollectAllTheGarbage'
SP = 'Bar,SafepointSlowPath'
WF = 'Baz,WriteField'


class OutputLines:
  CALLERS_RE = re.compile(r'([\w,]+)\s*→\s*(.*)')

  def __init__(self, *callee_list):
    """Construct a test data placeholder for output lines of one invocation of
    the GCMole plugin.

    Args:
        callee_list: Strings, each containing a caller/calle relationship
            formatted as "A → B C", meaning A calls B and C. For GC,
            Safepoint and a allow-listed function use GC, SP and WF
            constants above respectivly.
            Methods not calling anything are formatted as "A →".
    """
    self.callee_list = callee_list

  def lines(self):
    result = []
    for str_rep in self.callee_list:
      match = self.CALLERS_RE.match(str_rep)
      assert match
      result.append(match.group(1))
      for callee in (match.group(2) or '').split():
        result.append('\t' + callee)
    return result


class SuspectCollectorTest(unittest.TestCase):

  def create_callgraph(self, *outputs):
    call_graph = gcmole.CallGraph()
    for output in outputs:
      call_graph.parse(output.lines())
    return call_graph

  def testCallGraph(self):
    call_graph = self.create_callgraph(OutputLines())
    self.assertDictEqual(call_graph.funcs, {})

    call_graph = self.create_callgraph(OutputLines('A →'))
    self.assertDictEqual(call_graph.funcs, {'A': set()})

    call_graph = self.create_callgraph(OutputLines('A → B'))
    self.assertDictEqual(call_graph.funcs, {'A': set(), 'B': set('A')})

    call_graph = self.create_callgraph(
        OutputLines('A → B C', 'B → C D', 'D →'))
    self.assertDictEqual(
        call_graph.funcs,
        {'A': set(), 'B': set('A'), 'C': set(['A', 'B']), 'D': set('B')})

    call_graph = self.create_callgraph(
        OutputLines('B → C D', 'D →'), OutputLines('A → B C'))
    self.assertDictEqual(
        call_graph.funcs,
        {'A': set(), 'B': set('A'), 'C': set(['A', 'B']), 'D': set('B')})

  def testCallGraphMerge(self):
    """Test serializing, deserializing and merging call graphs."""
    temp_dir = Path(tempfile.mkdtemp('gcmole_test'))

    call_graph1 = self.create_callgraph(
        OutputLines('B → C D E', 'D →'), OutputLines('A → B C'))
    self.assertDictEqual(
        call_graph1.funcs,
        {'A': set(), 'B': set('A'), 'C': set(['A', 'B']), 'D': set('B'),
         'E': set('B')})

    call_graph2 = self.create_callgraph(
        OutputLines('E → A'), OutputLines('C → D F'))
    self.assertDictEqual(
        call_graph2.funcs,
        {'A': set('E'), 'C': set(), 'D': set('C'), 'E': set(), 'F': set('C')})

    file1 = temp_dir / 'file1.bin'
    file2 = temp_dir / 'file2.bin'
    call_graph1.to_file(file1)
    call_graph2.to_file(file2)

    expected = {'A': set(['E']), 'B': set('A'), 'C': set(['A', 'B']),
                'D': set(['B', 'C']), 'E': set(['B']), 'F': set(['C'])}

    call_graph = gcmole.CallGraph.from_files(file1, file2)
    self.assertDictEqual(call_graph.funcs, expected)

    call_graph = gcmole.CallGraph.from_files(file2, file1)
    self.assertDictEqual(call_graph.funcs, expected)

    call_graph3 = self.create_callgraph(
        OutputLines('F → G'), OutputLines('G →'))
    self.assertDictEqual(
        call_graph3.funcs,
        {'G': set('F'), 'F': set()})

    file3 = temp_dir / 'file3.bin'
    call_graph3.to_file(file3)

    call_graph = gcmole.CallGraph.from_files(file1, file2, file3)
    self.assertDictEqual(call_graph.funcs, dict(G=set('F'), **expected))

  def create_collector(self, outputs):
    Options = collections.namedtuple('OptionsForCollector', ['allowlist'])
    options = Options(True)
    call_graph = self.create_callgraph(*outputs)
    collector = gcmole.GCSuspectsCollector(options, call_graph.funcs)
    collector.propagate()
    return collector

  def check(self, outputs, expected_gc, expected_gc_caused):
    """Verify the GCSuspectsCollector propagation and outputs against test
    data.

    Args:
      outputs: List of OutputLines object simulating the lines returned by
          the GCMole plugin in drop-callees mode. Each output lines object
          represents one plugin invocation.
      expected_gc: Mapping as expected by GCSuspectsCollector.gc.
      expected_gc_caused: Mapping as expected by GCSuspectsCollector.gc_caused.
    """
    collector = self.create_collector(outputs)
    self.assertDictEqual(collector.gc, expected_gc)
    self.assertDictEqual(collector.gc_caused, expected_gc_caused)

  def testNoGC(self):
    self.check(
        outputs=[OutputLines()],
        expected_gc={},
        expected_gc_caused={},
    )
    self.check(
        outputs=[OutputLines('A →')],
        expected_gc={},
        expected_gc_caused={},
    )
    self.check(
        outputs=[OutputLines('A →', 'B →')],
        expected_gc={},
        expected_gc_caused={},
    )
    self.check(
        outputs=[OutputLines('A → B C')],
        expected_gc={},
        expected_gc_caused={},
    )
    self.check(
        outputs=[OutputLines('A → B', 'B → C')],
        expected_gc={},
        expected_gc_caused={},
    )
    self.check(
        outputs=[OutputLines('A → B C', 'B → D', 'D → A', 'C →')],
        expected_gc={},
        expected_gc_caused={},
    )
    self.check(
        outputs=[OutputLines('A →'), OutputLines('B →')],
        expected_gc={},
        expected_gc_caused={},
    )
    self.check(
        outputs=[OutputLines('A → B'), OutputLines('B → C')],
        expected_gc={},
        expected_gc_caused={},
    )
    self.check(
        outputs=[OutputLines('A → B C'),
                 OutputLines('B → D', 'D → A'),
                 OutputLines('C →')],
        expected_gc={},
        expected_gc_caused={},
    )

  def testGCOneFile(self):
    self.check(
        outputs=[OutputLines(f'{GC} →')],
        expected_gc={GC: True},
        expected_gc_caused={GC: {'<GC>'}},
    )
    self.check(
        outputs=[OutputLines(f'A → {GC}')],
        expected_gc={GC: True, 'A': True},
        expected_gc_caused={'A': {GC}, GC: {'<GC>'}},
    )
    self.check(
        outputs=[OutputLines(f'A → {GC}', 'B → A')],
        expected_gc={GC: True, 'A': True, 'B': True},
        expected_gc_caused={'B': {'A'}, 'A': {GC}, GC: {'<GC>'}},
    )
    self.check(
        outputs=[OutputLines('B → A', f'A → {GC}')],
        expected_gc={GC: True, 'A': True, 'B': True},
        expected_gc_caused={'B': {'A'}, 'A': {GC}, GC: {'<GC>'}},
    )
    self.check(
        outputs=[OutputLines(f'A → B {GC}', 'B →', 'C → B A')],
        expected_gc={GC: True, 'A': True, 'C': True},
        expected_gc_caused={'C': {'A'}, 'A': {GC}, GC: {'<GC>'}},
    )
    self.check(
        outputs=[OutputLines(f'A → {GC}', 'B → A', 'C → A', 'D → B C')],
        expected_gc={GC: True, 'A': True, 'B': True, 'C': True, 'D': True},
        expected_gc_caused={'C': {'A'}, 'A': {GC}, 'B': {'A'}, 'D': {'B', 'C'},
                            GC: {'<GC>'}},
    )

  def testAllowListOneFile(self):
    self.check(
        outputs=[OutputLines(f'{WF} →')],
        expected_gc={WF: False},
        expected_gc_caused={},
    )
    self.check(
        outputs=[OutputLines(f'{WF} → {GC}')],
        expected_gc={GC: True, WF: False},
        expected_gc_caused={WF: {GC}, GC: {'<GC>'}},
    )
    self.check(
        outputs=[OutputLines(f'A → {GC}', f'{WF} → A B', 'D → A B',
                             f'E → {WF}')],
        expected_gc={GC: True, WF: False, 'A': True, 'D': True},
        expected_gc_caused={'A': {GC}, WF: {'A'}, 'D': {'A'}, GC: {'<GC>'}},
    )

  def testSafepointOneFile(self):
    self.check(
        outputs=[OutputLines(f'{SP} →')],
        expected_gc={SP: True},
        expected_gc_caused={SP: {'<Safepoint>'}},
    )
    self.check(
        outputs=[OutputLines('B → A', f'A → {SP}')],
        expected_gc={SP: True, 'A': True, 'B': True},
        expected_gc_caused={'B': {'A'}, 'A': {SP}, SP: {'<Safepoint>'}},
    )

  def testCombinedOneFile(self):
    self.check(
        outputs=[OutputLines(f'{GC} →', f'{SP} →')],
        expected_gc={SP: True, GC: True},
        expected_gc_caused={SP: {'<Safepoint>'}, GC: {'<GC>'}},
    )
    self.check(
        outputs=[OutputLines(f'A → {GC}', f'B → {SP}')],
        expected_gc={GC: True, SP: True, 'A': True, 'B': True},
        expected_gc_caused={'B': {SP}, 'A': {GC}, SP: {'<Safepoint>'},
                            GC: {'<GC>'}},
    )
    self.check(
        outputs=[OutputLines(f'A → {GC}', f'B → {SP}', 'C → D A B')],
        expected_gc={GC: True, SP: True, 'A': True, 'B': True, 'C': True},
        expected_gc_caused={'B': {SP}, 'A': {GC}, 'C': {'A', 'B'},
                            SP: {'<Safepoint>'}, GC: {'<GC>'}},
    )

  def testCombinedMoreFiles(self):
    self.check(
        outputs=[OutputLines(f'A → {GC}'), OutputLines(f'B → {SP}')],
        expected_gc={GC: True, SP: True, 'A': True, 'B': True},
        expected_gc_caused={'B': {SP}, 'A': {GC}, SP: {'<Safepoint>'},
                            GC: {'<GC>'}},
    )
    self.check(
        outputs=[OutputLines(f'A → {GC}'), OutputLines(f'B → {SP}'),
                 OutputLines('C → D A B')],
        expected_gc={GC: True, SP: True, 'A': True, 'B': True, 'C': True},
        expected_gc_caused={'B': {SP}, 'A': {GC}, 'C': {'A', 'B'},
                            SP: {'<Safepoint>'}, GC: {'<GC>'}},
    )

  def testWriteGCMoleResults(self):
    temp_dir = Path(tempfile.mkdtemp('gcmole_test'))
    Options = collections.namedtuple('OptionsForWriting', ['v8_target_cpu'])
    collector = self.create_collector(
        [OutputLines(f'A → {GC}'), OutputLines(f'B → {SP}')])
    gcmole.write_gcmole_results(collector, Options('x64'), temp_dir)

    gcsuspects_expected = textwrap.dedent(f"""\
      {GC}
      {SP}
      A
      B
    """)

    with open(temp_dir / 'gcsuspects') as f:
      self.assertEqual(f.read(), gcsuspects_expected)

    gccauses_expected = textwrap.dedent(f"""
      {GC}
      start,nested
      <GC>
      end,nested
      {SP}
      start,nested
      <Safepoint>
      end,nested
      A
      start,nested
      {GC}
      end,nested
      B
      start,nested
      {SP}
      end,nested
    """).strip()

    with open(temp_dir / 'gccauses') as f:
      self.assertEqual(f.read().strip(), gccauses_expected)


class ArgsTest(unittest.TestCase):

  def testArgs(self):
    """Test argument retrieval using a fake v8 file system and build dir."""
    with tempfile.TemporaryDirectory('gcmole_args_test') as temp_dir:
      temp_dir = Path(temp_dir)
      temp_out = temp_dir / 'out'
      temp_gcmole = temp_dir / 'tools' / 'gcmole' / 'gcmole_args.py'

      shutil.copytree(abs_test_file('out'), temp_out)
      os.makedirs(temp_gcmole.parent)
      shutil.copy(GCMOLE_PATH / 'gcmole_args.py', temp_gcmole)

      # Simulate a ninja call relative to the build dir.
      gn_sysroot = '//build/linux/debian_bullseye_amd64-sysroot'
      subprocess.check_call(
          [sys.executable, temp_gcmole, gn_sysroot], cwd=temp_out)

      with open(temp_dir / 'out' / 'v8_gcmole.args') as f:
        self.assertEqual(f.read().split(), [
            '-DUSE_GLIB=1',
            '-DV8_TARGET_ARCH_X64',
            '-I.',
            '-Iout/gen',
            '-Iinclude',
            '-Iout/gen/include',
            '-isystem../buildtools/third_party/libc++/trunk/include',
            '--sysroot=build/linux/debian_bullseye_amd64-sysroot',
        ])


if __name__ == '__main__':
  unittest.main()
