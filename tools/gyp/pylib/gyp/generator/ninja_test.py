#!/usr/bin/env python3

# Copyright (c) 2012 Google Inc. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

""" Unit tests for the ninja.py file. """

from pathlib import Path
import sys
import unittest

import gyp.generator.ninja as ninja


class TestPrefixesAndSuffixes(unittest.TestCase):
    def test_BinaryNamesWindows(self):
        # These cannot run on non-Windows as they require a VS installation to
        # correctly handle variable expansion.
        if sys.platform.startswith("win"):
            writer = ninja.NinjaWriter(
                "foo", "wee", ".", ".", "build.ninja", ".", "build.ninja", "win"
            )
            spec = {"target_name": "wee"}
            self.assertTrue(
                writer.ComputeOutputFileName(spec, "executable").endswith(".exe")
            )
            self.assertTrue(
                writer.ComputeOutputFileName(spec, "shared_library").endswith(".dll")
            )
            self.assertTrue(
                writer.ComputeOutputFileName(spec, "static_library").endswith(".lib")
            )

    def test_BinaryNamesLinux(self):
        writer = ninja.NinjaWriter(
            "foo", "wee", ".", ".", "build.ninja", ".", "build.ninja", "linux"
        )
        spec = {"target_name": "wee"}
        self.assertTrue("." not in writer.ComputeOutputFileName(spec, "executable"))
        self.assertTrue(
            writer.ComputeOutputFileName(spec, "shared_library").startswith("lib")
        )
        self.assertTrue(
            writer.ComputeOutputFileName(spec, "static_library").startswith("lib")
        )
        self.assertTrue(
            writer.ComputeOutputFileName(spec, "shared_library").endswith(".so")
        )
        self.assertTrue(
            writer.ComputeOutputFileName(spec, "static_library").endswith(".a")
        )

    def test_GenerateCompileDBWithNinja(self):
        build_dir = (
            Path(__file__).resolve().parent.parent.parent.parent / "data" / "ninja"
        )
        compile_db = ninja.GenerateCompileDBWithNinja(build_dir)
        assert len(compile_db) == 1
        assert compile_db[0]["directory"] == str(build_dir)
        assert compile_db[0]["command"] == "cc my.in my.out"
        assert compile_db[0]["file"] == "my.in"
        assert compile_db[0]["output"] == "my.out"


if __name__ == "__main__":
    unittest.main()
