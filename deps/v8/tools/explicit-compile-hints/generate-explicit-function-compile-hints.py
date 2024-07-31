#!/usr/bin/env python3

# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can
# be found in the LICENSE file.

import base64
import sys

script_id_to_name = {}
script_id_to_start_positions = {}


def varint(n):
  orig_mask = (1 << 7) - 1
  mask = (1 << 7) - 1
  res = []
  shift = 0
  while n > mask:
    mask = mask << 7
    shift += 7

  while mask > orig_mask:
    # This byte will start with 1
    res.append((1 << 7) | ((n & mask) >> shift))

    mask = mask >> 7
    shift -= 7

  assert (mask == orig_mask)
  # This byte will start with 0
  res.append(n & orig_mask)
  return res


def encode_delta_varint_base64(start_positions):
  bytes = []
  prev_pos = 0
  for p in start_positions:
    bytes.extend(varint(p - prev_pos))
    prev_pos = p
  return base64.b64encode(bytearray(bytes)).decode()


def decode_varints(bytes):
  ints = []
  current = 0
  for b in bytes:
    if b & (1 << 7):
      # Not the last byte
      current = (current << 7) | (b & ((1 << 7) - 1))
    else:
      # The last byte
      current = (current << 7) | b
      ints.append(current)
      current = 0
  return ints


def decode_delta_varint_base64(comment):
  bytes = base64.b64decode(comment)
  deltas = decode_varints(bytes)
  prev_pos = 0
  positions = []
  for d in deltas:
    prev_pos += d
    positions.append(prev_pos)
  return positions


if len(sys.argv) < 2 or '--help' in sys.argv:
  print('Usage: python3 generate-explicit-function-compile-hints.py v8.log')
  exit(1)

with open(sys.argv[1], 'r') as log_file:
  for line in log_file:
    fields = line.strip().split(',')

    if len(fields) >= 3:
      if fields[0] == 'script-details':
        script_id = int(fields[1])
        script_name = fields[2]

        script_id_to_name[script_id] = script_name
      elif fields[0] == 'function' and fields[1] == 'parse-function':
        script_id = int(fields[2])
        start_position = int(fields[3])

        if script_id in script_id_to_start_positions:
          script_id_to_start_positions[script_id].add(start_position)
        else:
          script_id_to_start_positions[script_id] = {start_position}

for script_id, script_name in script_id_to_name.items():
  if script_id in script_id_to_start_positions:
    if not script_name.startswith('http') and not script_name.startswith(
        'file'):
      continue
    start_positions = list(script_id_to_start_positions[script_id])
    start_positions.sort()
    print(script_name)
    magic_comment = encode_delta_varint_base64(start_positions)
    back = decode_delta_varint_base64(magic_comment)
    if start_positions != back:
      print('Decoding failed!')
      exit(1)
    print(f"//# experimentalChromiumCompileHintsData={magic_comment}\n")
