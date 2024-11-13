#!/usr/bin/env python3

# Copyright (c) 2012 Google Inc. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for the common.py file."""

import gyp.common
import unittest
import sys
import os
from unittest.mock import patch, MagicMock

class TestTopologicallySorted(unittest.TestCase):
    def test_Valid(self):
        """Test that sorting works on a valid graph with one possible order."""
        graph = {
            "a": ["b", "c"],
            "b": [],
            "c": ["d"],
            "d": ["b"],
        }

        def GetEdge(node):
            return tuple(graph[node])

        assert gyp.common.TopologicallySorted(
            graph.keys(), GetEdge) == ["a", "c", "d", "b"]

    def test_Cycle(self):
        """Test that an exception is thrown on a cyclic graph."""
        graph = {
            "a": ["b"],
            "b": ["c"],
            "c": ["d"],
            "d": ["a"],
        }

        def GetEdge(node):
            return tuple(graph[node])

        self.assertRaises(
            gyp.common.CycleError, gyp.common.TopologicallySorted, graph.keys(), GetEdge
        )


class TestGetFlavor(unittest.TestCase):
    """Test that gyp.common.GetFlavor works as intended"""

    original_platform = ""

    def setUp(self):
        self.original_platform = sys.platform

    def tearDown(self):
        sys.platform = self.original_platform

    def assertFlavor(self, expected, argument, param):
        sys.platform = argument
        assert expected == gyp.common.GetFlavor(param)

    def test_platform_default(self):
        self.assertFlavor("freebsd", "freebsd9", {})
        self.assertFlavor("freebsd", "freebsd10", {})
        self.assertFlavor("openbsd", "openbsd5", {})
        self.assertFlavor("solaris", "sunos5", {})
        self.assertFlavor("solaris", "sunos", {})
        self.assertFlavor("linux", "linux2", {})
        self.assertFlavor("linux", "linux3", {})
        self.assertFlavor("linux", "linux", {})

    def test_param(self):
        self.assertFlavor("foobar", "linux2", {"flavor": "foobar"})

    class MockCommunicate:
        def __init__(self, stdout):
            self.stdout = stdout

        def decode(self, encoding):
            return self.stdout

    @patch("os.close")
    @patch("os.unlink")
    @patch("tempfile.mkstemp")
    def test_GetCrossCompilerPredefines(self, mock_mkstemp, mock_unlink, mock_close):
        mock_close.return_value = None
        mock_unlink.return_value = None
        mock_mkstemp.return_value = (0, "temp.c")

        def mock_run(env, defines_stdout, expected_cmd):
            with patch("subprocess.run") as mock_run:
                mock_process = MagicMock()
                mock_process.returncode = 0
                mock_process.stdout = TestGetFlavor.MockCommunicate(defines_stdout)
                mock_run.return_value = mock_process
                expected_input = "temp.c" if sys.platform == "win32" else "/dev/null"
                with patch.dict(os.environ, env):
                    defines = gyp.common.GetCrossCompilerPredefines()
                    flavor = gyp.common.GetFlavor({})
                if env.get("CC_target"):
                    mock_run.assert_called_with(
                        [
                            *expected_cmd,
                            "-dM", "-E", "-x", "c", expected_input
                        ],
                        shell=sys.platform == "win32",
                        capture_output=True, check=True)
                return [defines, flavor]

        [defines1, _] = mock_run({}, "", [])
        assert {} == defines1

        [defines2, flavor2] = mock_run(
            { "CC_target": "/opt/wasi-sdk/bin/clang" },
            "#define __wasm__ 1\n#define __wasi__ 1\n",
            ["/opt/wasi-sdk/bin/clang"]
        )
        assert { "__wasm__": "1", "__wasi__": "1" } == defines2
        assert flavor2 == "wasi"

        [defines3, flavor3] = mock_run(
            { "CC_target": "/opt/wasi-sdk/bin/clang --target=wasm32" },
            "#define __wasm__ 1\n",
            ["/opt/wasi-sdk/bin/clang", "--target=wasm32"]
        )
        assert { "__wasm__": "1" } == defines3
        assert flavor3 == "wasm"

        [defines4, flavor4] = mock_run(
            { "CC_target": "/emsdk/upstream/emscripten/emcc" },
            "#define __EMSCRIPTEN__ 1\n",
            ["/emsdk/upstream/emscripten/emcc"]
        )
        assert { "__EMSCRIPTEN__": "1" } == defines4
        assert flavor4 == "emscripten"

        # Test path which include white space
        [defines5, flavor5] = mock_run(
            {
                "CC_target": "\"/Users/Toyo Li/wasi-sdk/bin/clang\" -O3",
                "CFLAGS": "--target=wasm32-wasi-threads -pthread"
            },
            "#define __wasm__ 1\n#define __wasi__ 1\n#define _REENTRANT 1\n",
            [
                "/Users/Toyo Li/wasi-sdk/bin/clang",
                "-O3",
                "--target=wasm32-wasi-threads",
                "-pthread"
            ]
        )
        assert {
            "__wasm__": "1",
            "__wasi__": "1",
            "_REENTRANT": "1"
        } == defines5
        assert flavor5 == "wasi"

        original_sep = os.sep
        os.sep = "\\"
        [defines6, flavor6] = mock_run(
            { "CC_target": "\"C:\\Program Files\\wasi-sdk\\clang.exe\"" },
            "#define __wasm__ 1\n#define __wasi__ 1\n",
            ["C:/Program Files/wasi-sdk/clang.exe"]
        )
        os.sep = original_sep
        assert { "__wasm__": "1", "__wasi__": "1" } == defines6
        assert flavor6 == "wasi"

if __name__ == "__main__":
    unittest.main()
