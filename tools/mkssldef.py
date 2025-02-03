#!/usr/bin/env python

from __future__ import print_function
import re
import sys

categories = []
defines = []
excludes = []
bases = []

if __name__ == '__main__':
  out = sys.stdout
  filenames = sys.argv[1:]

  while filenames and filenames[0].startswith('-'):
    option = filenames.pop(0)
    if option == '-o': out = open(filenames.pop(0), 'w')
    elif option.startswith('-C'): categories += option[2:].split(',')
    elif option.startswith('-D'): defines += option[2:].split(',')
    elif option.startswith('-X'): excludes += option[2:].split(',')
    elif option.startswith('-B'): bases += option[2:].split(',')

  excludes = [re.compile(exclude) for exclude in excludes]
  exported = []

  for filename in filenames:
    for line in open(filename).readlines():
      name, _, _, meta, _ = re.split(r'\s+', line)
      if any(p.match(name) for p in excludes): continue
      meta = meta.split(':')
      assert meta[0] in ('EXIST', 'NOEXIST')
      assert meta[2] in ('FUNCTION', 'VARIABLE')
      if meta[0] != 'EXIST': continue
      if meta[2] != 'FUNCTION': continue
      def satisfy(expr, rules):
        def test(expr):
          if expr.startswith('!'): return not expr[1:] in rules
          return expr == '' or expr in rules
        return all(map(test, expr.split(',')))
      if not satisfy(meta[1], defines): continue
      if not satisfy(meta[3], categories): continue
      exported.append(name)

  for filename in bases:
    for line in open(filename).readlines():
      line = line.strip()
      if line == 'EXPORTS': continue
      if line[0] == ';': continue
      exported.append(line)

  print('EXPORTS', file=out)
  for name in sorted(exported): print('    ', name, file=out)
