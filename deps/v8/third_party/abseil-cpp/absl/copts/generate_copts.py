#!/usr/bin/env python3
"""Generate Abseil compile compile option configs.

Usage: <path_to_absl>/copts/generate_copts.py

The configs are generated from copts.py.
"""

from os import path
import sys
from copts import COPT_VARS


# Helper functions
def file_header_lines():
  return [
      "GENERATED! DO NOT MANUALLY EDIT THIS FILE.", "",
      "(1) Edit absl/copts/copts.py.",
      "(2) Run `python <path_to_absl>/copts/generate_copts.py`."
  ]


def flatten(*lists):
  return [item for sublist in lists for item in sublist]


def relative_filename(filename):
  return path.join(path.dirname(__file__), filename)


# Style classes.  These contain all the syntactic styling needed to generate a
# copt file for different build tools.
class CMakeStyle(object):
  """Style object for CMake copts file."""

  def separator(self):
    return ""

  def list_introducer(self, name):
    return "list(APPEND " + name

  def list_closer(self):
    return ")\n"

  def docstring(self):
    return "\n".join((("# " + line).strip() for line in file_header_lines()))

  def filename(self):
    return "GENERATED_AbseilCopts.cmake"


class StarlarkStyle(object):
  """Style object for Starlark copts file."""

  def separator(self):
    return ","

  def list_introducer(self, name):
    return name + " = ["

  def list_closer(self):
    return "]\n"

  def docstring(self):
    docstring_quotes = "\"\"\""
    return docstring_quotes + "\n".join(
        flatten(file_header_lines(), [docstring_quotes]))

  def filename(self):
    return "GENERATED_copts.bzl"


def copt_list(name, arg_list, style):
  """Copt file generation."""

  make_line = lambda s: "    \"" + s + "\"" + style.separator()
  external_str_list = [make_line(s) for s in arg_list]

  return "\n".join(
      flatten(
          [style.list_introducer(name)],
          external_str_list,
          [style.list_closer()]))


def generate_copt_file(style):
  """Creates a generated copt file using the given style object.

  Args:
    style: either StarlarkStyle() or CMakeStyle()
  """
  with open(relative_filename(style.filename()), "w") as f:
    f.write(style.docstring())
    f.write("\n")
    for var_name, arg_list in sorted(COPT_VARS.items()):
      f.write("\n")
      f.write(copt_list(var_name, arg_list, style))


def main(argv):
  if len(argv) > 1:
    raise RuntimeError("generate_copts needs no command line args")

  generate_copt_file(StarlarkStyle())
  generate_copt_file(CMakeStyle())


if __name__ == "__main__":
  main(sys.argv)
