#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""This script generates abseil.podspec from all BUILD.bazel files.

This is expected to run on abseil git repository with Bazel 1.0 on Linux.
It recursively analyzes BUILD.bazel files using query command of Bazel to
dump its build rules in XML format. From these rules, it constructs podspec
structure.
"""

import argparse
import collections
import os
import re
import subprocess
import xml.etree.ElementTree

# Template of root podspec.
SPEC_TEMPLATE = """
# This file has been automatically generated from a script.
# Please make modifications to `abseil.podspec.gen.py` instead.
Pod::Spec.new do |s|
  s.name     = 'abseil'
  s.version  = '${version}'
  s.summary  = 'Abseil Common Libraries (C++) from Google'
  s.homepage = 'https://abseil.io'
  s.license  = 'Apache License, Version 2.0'
  s.authors  = { 'Abseil Team' => 'abseil-io@googlegroups.com' }
  s.source = {
    :git => 'https://github.com/abseil/abseil-cpp.git',
    :tag => '${tag}',
  }
  s.resource_bundles = {
    s.module_name => 'PrivacyInfo.xcprivacy',
  }
  s.module_name = 'absl'
  s.header_mappings_dir = 'absl'
  s.header_dir = 'absl'
  s.libraries = 'c++'
  s.compiler_flags = '-Wno-everything'
  s.pod_target_xcconfig = {
    'USER_HEADER_SEARCH_PATHS' => '$(inherited) "$(PODS_TARGET_SRCROOT)"',
    'USE_HEADERMAP' => 'NO',
    'ALWAYS_SEARCH_USER_PATHS' => 'NO',
  }
  s.ios.deployment_target = '9.0'
  s.osx.deployment_target = '10.11'
  s.tvos.deployment_target = '9.0'
  s.watchos.deployment_target = '2.0'
  s.subspec 'xcprivacy' do |ss|
    ss.resource_bundles = {
      ss.module_name => 'PrivacyInfo.xcprivacy',
    }
  end
"""

# Rule object representing the rule of Bazel BUILD.
Rule = collections.namedtuple(
    "Rule", "type name package srcs hdrs textual_hdrs deps visibility testonly")


def get_elem_value(elem, name):
  """Returns the value of XML element with the given name."""
  for child in elem:
    if child.attrib.get("name") != name:
      continue
    if child.tag == "string":
      return child.attrib.get("value")
    if child.tag == "boolean":
      return child.attrib.get("value") == "true"
    if child.tag == "list":
      return [nested_child.attrib.get("value") for nested_child in child]
    raise "Cannot recognize tag: " + child.tag
  return None


def normalize_paths(paths):
  """Returns the list of normalized path."""
  # e.g. ["//absl/strings:dir/header.h"] -> ["absl/strings/dir/header.h"]
  return [path.lstrip("/").replace(":", "/") for path in paths]


def parse_rule(elem, package):
  """Returns a rule from bazel XML rule."""
  return Rule(
      type=elem.attrib["class"],
      name=get_elem_value(elem, "name"),
      package=package,
      srcs=normalize_paths(get_elem_value(elem, "srcs") or []),
      hdrs=normalize_paths(get_elem_value(elem, "hdrs") or []),
      textual_hdrs=normalize_paths(get_elem_value(elem, "textual_hdrs") or []),
      deps=get_elem_value(elem, "deps") or [],
      visibility=get_elem_value(elem, "visibility") or [],
      testonly=get_elem_value(elem, "testonly") or False)


def read_build(package):
  """Runs bazel query on given package file and returns all cc rules."""
  result = subprocess.check_output(
      ["bazel", "query", package + ":all", "--output", "xml"])
  root = xml.etree.ElementTree.fromstring(result)
  return [
      parse_rule(elem, package)
      for elem in root
      if elem.tag == "rule" and elem.attrib["class"].startswith("cc_")
  ]


def collect_rules(root_path):
  """Collects and returns all rules from root path recursively."""
  rules = []
  for cur, _, _ in os.walk(root_path):
    build_path = os.path.join(cur, "BUILD.bazel")
    if os.path.exists(build_path):
      rules.extend(read_build("//" + cur))
  return rules


def relevant_rule(rule):
  """Returns true if a given rule is relevant when generating a podspec."""
  return (
      # cc_library only (ignore cc_test, cc_binary)
      rule.type == "cc_library" and
      # ignore empty rule
      (rule.hdrs + rule.textual_hdrs + rule.srcs) and
      # ignore test-only rule
      not rule.testonly)


def get_spec_var(depth):
  """Returns the name of variable for spec with given depth."""
  return "s" if depth == 0 else "s{}".format(depth)


def get_spec_name(label):
  """Converts the label of bazel rule to the name of podspec."""
  assert label.startswith("//absl/"), "{} doesn't start with //absl/".format(
      label)
  # e.g. //absl/apple/banana -> abseil/apple/banana
  return "abseil/" + label[7:]


def write_podspec(f, rules, args):
  """Writes a podspec from given rules and args."""
  rule_dir = build_rule_directory(rules)["abseil"]
  # Write root part with given arguments
  spec = re.sub(r"\$\{(\w+)\}", lambda x: args[x.group(1)],
                SPEC_TEMPLATE).lstrip()
  f.write(spec)
  # Write all target rules
  write_podspec_map(f, rule_dir, 0)
  f.write("end\n")


def build_rule_directory(rules):
  """Builds a tree-style rule directory from given rules."""
  rule_dir = {}
  for rule in rules:
    cur = rule_dir
    for frag in get_spec_name(rule.package).split("/"):
      cur = cur.setdefault(frag, {})
    cur[rule.name] = rule
  return rule_dir


def write_podspec_map(f, cur_map, depth):
  """Writes podspec from rule map recursively."""
  for key, value in sorted(cur_map.items()):
    indent = "  " * (depth + 1)
    f.write("{indent}{var0}.subspec '{key}' do |{var1}|\n".format(
        indent=indent,
        key=key,
        var0=get_spec_var(depth),
        var1=get_spec_var(depth + 1)))
    if isinstance(value, dict):
      write_podspec_map(f, value, depth + 1)
    else:
      write_podspec_rule(f, value, depth + 1)
    f.write("{indent}end\n".format(indent=indent))


def write_podspec_rule(f, rule, depth):
  """Writes podspec from given rule."""
  indent = "  " * (depth + 1)
  spec_var = get_spec_var(depth)
  # Puts all files in hdrs, textual_hdrs, and srcs into source_files.
  # Since CocoaPods treats header_files a bit differently from bazel,
  # this won't generate a header_files field so that all source_files
  # are considered as header files.
  srcs = sorted(set(rule.hdrs + rule.textual_hdrs + rule.srcs))
  write_indented_list(
      f, "{indent}{var}.source_files = ".format(indent=indent, var=spec_var),
      srcs)
  # Writes dependencies of this rule.
  for dep in sorted(rule.deps):
    name = get_spec_name(dep.replace(":", "/"))
    f.write("{indent}{var}.dependency '{dep}'\n".format(
        indent=indent, var=spec_var, dep=name))
  # Writes dependency to xcprivacy
  f.write(
      "{indent}{var}.dependency '{dep}'\n".format(
          indent=indent, var=spec_var, dep="abseil/xcprivacy"
      )
  )


def write_indented_list(f, leading, values):
  """Writes leading values in an indented style."""
  f.write(leading)
  f.write((",\n" + " " * len(leading)).join("'{}'".format(v) for v in values))
  f.write("\n")


def generate(args):
  """Generates a podspec file from all BUILD files under absl directory."""
  rules = filter(relevant_rule, collect_rules("absl"))
  with open(args.output, "wt") as f:
    write_podspec(f, rules, vars(args))


def main():
  parser = argparse.ArgumentParser(
      description="Generates abseil.podspec from BUILD.bazel")
  parser.add_argument(
      "-v", "--version", help="The version of podspec", required=True)
  parser.add_argument(
      "-t",
      "--tag",
      default=None,
      help="The name of git tag (default: version)")
  parser.add_argument(
      "-o",
      "--output",
      default="abseil.podspec",
      help="The name of output file (default: abseil.podspec)")
  args = parser.parse_args()
  if args.tag is None:
    args.tag = args.version
  generate(args)


if __name__ == "__main__":
  main()
