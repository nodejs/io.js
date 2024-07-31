#!/usr/bin/env python3

# Copyright 2014 by Sam Mikes.  All rights reserved.
# This code is governed by the BSD license found in the LICENSE file.

import unittest

import yaml

# add parent dir to search path
import sys
sys.path.append("src")

import _monkeyYaml as monkeyYaml

class TestMonkeyYAMLParsing(unittest.TestCase):

    def test_empty(self):
        self.assertEqual(monkeyYaml.load(""), yaml.load(""))

    def test_newline(self):
        self.assertEqual(monkeyYaml.load("\n"), yaml.load("\n"))

    def test_oneline(self):
        y = "foo: bar"
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_twolines(self):
        y = "foo: bar\nbaz_bletch : blith:er"
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_multiLine(self):
        y = "foo: >\n bar\nbaz: 3"
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_es5id(self):
        y = "es5id: 15.2.3.6-4-102"
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_Multiline_1(self):
        lines = [" foo"]
        value = ">"
        y = "\n".join([value] + lines)
        (lines, value) = monkeyYaml.myMultiline(lines, value)
        self.assertEqual(lines, [])
        self.assertEqual(value, yaml.load(y))

    def test_Multiline_2(self):
        lines = [" foo", " bar"]
        y = "\n".join([">"] + lines)
        (lines, value) = monkeyYaml.myMultiline(lines)
        self.assertEqual(lines, [])
        self.assertEqual(value, yaml.load(y))

    def test_Multiline_3(self):
        lines = ["  foo", "  bar"]
        y = "\n".join([">"] + lines)
        (lines, value) = monkeyYaml.myMultiline(lines)
        self.assertEqual(lines, [])
        self.assertEqual(value, yaml.load(y))

    def test_Multiline_4(self):
        lines = ["    foo", "    bar", "  other: 42"]
        (lines, value) = monkeyYaml.myMultiline(lines)
        self.assertEqual(lines, ["  other: 42"])
        self.assertEqual(value, "foo bar")

    def test_myLeading(self):
        self.assertEqual(2, monkeyYaml.myLeadingSpaces("  foo"))
        self.assertEqual(2, monkeyYaml.myLeadingSpaces("  "))
        self.assertEqual(0, monkeyYaml.myLeadingSpaces("\t  "))

    def test_includes_flow(self):
        y = "includes: [a.js,b.js, c_with_wings.js]\n"
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_myFlowList_1(self):
        y = "[a.js,b.js, c_with_wings.js, 3, 4.12]"
        self.assertEqual(monkeyYaml.myFlowList(y), ['a.js', 'b.js', 'c_with_wings.js', 3, 4.12])

    def test_multiline_list_1(self):
        y = "foo:\n - bar\n - baz"
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_multiline_list2(self):
        self.assertEqual(monkeyYaml.myRemoveListHeader(2, "  - foo"), "foo")

    def test_multiline_list3(self):
        (lines, value) = monkeyYaml.myMultilineList([" - foo", " - bar", "baz: bletch"], "")
        self.assertEqual(lines, ["baz: bletch"])
        self.assertEqual(value, ["foo", "bar"])

    def test_multiline_list_carriage_return(self):
        y = "foo:\r\n - bar\r\n - baz"
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_oneline_indented(self):
        y = "  foo: bar\n  baz: baf\n"
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))


    def test_indentation_215(self):
        self.maxDiff = None
        y = """
  description: >
      The method should exist on the Array prototype, and it should be writable
      and configurable, but not enumerable.
  includes: [propertyHelper.js]
  es6id: 22.1.3.13
 """
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_indentation_215_2(self):
        self.maxDiff = None
        y = """
  description: >
   The method should exist
  includes: [propertyHelper.js]
  es6id: 22.1.3.13
 """
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_line_folding(self):
        self.maxDiff = None
        y = """
description: aaa
             bbb
es6id:  19.1.2.1
"""
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_line_folding_2(self):
        self.maxDiff = None
        y = """
description: ccc

             ddd

es6id:  19.1.2.1
"""
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_line_folding_3(self):
        self.maxDiff = None
        y = """
description: eee


             fff
es6id:  19.1.2.1
"""
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_line_folding_4(self):
        self.maxDiff = None
        y = """
description: ggg

             hhh
             iii

             jjj
es6id:  19.1.2.1
"""
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_no_folding(self):
        y = """
description: |
  This is text that, naively parsed, would appear

  to: have
  nested: data
"""
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_value_multiline(self):
        y = """
description:
  This is a multi-line value

  whose trailing newline should be stripped
"""
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_nested_1(self):
        y = """
es61d: 19.1.2.1
negative:
    stage: early
    type: ReferenceError
description: foo
"""
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

    def test_nested_2(self):
        y = """
es61d: 19.1.2.1
first:
    second_a:
        third: 1
    second_b: 3
description: foo
"""
        self.assertEqual(monkeyYaml.load(y), yaml.load(y))

if __name__ == '__main__':
    unittest.main()
