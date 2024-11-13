#!/usr/bin/env python3
# Copyright 2011 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Inspector protocol validator.
#
# Tests that subsequent protocol changes are not breaking backwards compatibility.
# Following violations are reported:
#
#   - Domain has been removed
#   - Command has been removed
#   - Required command parameter was added or changed from optional
#   - Required response parameter was removed or changed to optional
#   - Event has been removed
#   - Required event parameter was removed or changed to optional
#   - Parameter type has changed.
#
# For the parameters with composite types the above checks are also applied
# recursively to every property of the type.
#
# Adding --show_changes to the command line prints out a list of valid public API changes.

from __future__ import print_function
import copy
import os.path
import optparse
import sys

import pdl

try:
  import json
except ImportError:
  import simplejson as json


def list_to_map(items, key):
  result = {}
  for item in items:
    if "experimental" not in item and "hidden" not in item:
      result[item[key]] = item
  return result


def named_list_to_map(container, name, key):
  if name in container:
    return list_to_map(container[name], key)
  return {}


def removed(reverse):
  if reverse:
    return "added"
  return "removed"


def required(reverse):
  if reverse:
    return "optional"
  return "required"


def compare_schemas(d_1, d_2, reverse):
  errors = []
  domains_1 = copy.deepcopy(d_1)
  domains_2 = copy.deepcopy(d_2)
  types_1 = normalize_types_in_schema(domains_1)
  types_2 = normalize_types_in_schema(domains_2)

  domains_by_name_1 = list_to_map(domains_1, "domain")
  domains_by_name_2 = list_to_map(domains_2, "domain")

  for name in domains_by_name_1:
    domain_1 = domains_by_name_1[name]
    if name not in domains_by_name_2:
      errors.append("%s: domain has been %s" % (name, removed(reverse)))
      continue
    compare_domains(domain_1, domains_by_name_2[name], types_1, types_2, errors,
                    reverse)
  return errors


def compare_domains(domain_1, domain_2, types_map_1, types_map_2, errors, reverse):
  domain_name = domain_1["domain"]
  commands_1 = named_list_to_map(domain_1, "commands", "name")
  commands_2 = named_list_to_map(domain_2, "commands", "name")
  for name in commands_1:
    command_1 = commands_1[name]
    if name not in commands_2:
      errors.append("%s.%s: command has been %s" %
                    (domain_1["domain"], name, removed(reverse)))
      continue
    compare_commands(domain_name, command_1, commands_2[name], types_map_1,
                     types_map_2, errors, reverse)

  events_1 = named_list_to_map(domain_1, "events", "name")
  events_2 = named_list_to_map(domain_2, "events", "name")
  for name in events_1:
    event_1 = events_1[name]
    if name not in events_2:
      errors.append("%s.%s: event has been %s" %
                    (domain_1["domain"], name, removed(reverse)))
      continue
    compare_events(domain_name, event_1, events_2[name], types_map_1,
                   types_map_2, errors, reverse)


def compare_commands(domain_name, command_1, command_2, types_map_1, types_map_2, errors, reverse):
  context = domain_name + "." + command_1["name"]

  params_1 = named_list_to_map(command_1, "parameters", "name")
  params_2 = named_list_to_map(command_2, "parameters", "name")
  # Note the reversed order: we allow removing but forbid adding parameters.
  compare_params_list(context, "parameter", params_2, params_1, types_map_2,
                      types_map_1, 0, errors, not reverse)

  returns_1 = named_list_to_map(command_1, "returns", "name")
  returns_2 = named_list_to_map(command_2, "returns", "name")
  compare_params_list(context, "response parameter", returns_1, returns_2,
                      types_map_1, types_map_2, 0, errors, reverse)


def compare_events(domain_name, event_1, event_2, types_map_1, types_map_2, errors, reverse):
  context = domain_name + "." + event_1["name"]
  params_1 = named_list_to_map(event_1, "parameters", "name")
  params_2 = named_list_to_map(event_2, "parameters", "name")
  compare_params_list(context, "parameter", params_1, params_2, types_map_1,
                      types_map_2, 0, errors, reverse)


def compare_params_list(context, kind, params_1, params_2, types_map_1, types_map_2, depth, errors, reverse):
  for name in params_1:
    param_1 = params_1[name]
    if name not in params_2:
      if "optional" not in param_1:
        errors.append("%s.%s: required %s has been %s" %
                      (context, name, kind, removed(reverse)))
      continue

    param_2 = params_2[name]
    if param_2 and "optional" in param_2 and "optional" not in param_1:
      errors.append(
          "%s.%s: %s %s is now %s" %
          (context, name, required(reverse), kind, required(not reverse)))
      continue
    type_1 = extract_type(param_1, types_map_1, errors)
    type_2 = extract_type(param_2, types_map_2, errors)
    compare_types(context + "." + name, kind, type_1, type_2, types_map_1,
                  types_map_2, depth, errors, reverse)


def compare_types(context, kind, type_1, type_2, types_map_1, types_map_2, depth, errors, reverse):
  if depth > 5:
    return

  base_type_1 = type_1["type"]
  base_type_2 = type_2["type"]

  # Binary and string have the same wire representation in JSON.
  if ((base_type_1 == "string" and base_type_2 == "binary") or
      (base_type_2 == "string" and base_type_1 == "binary")):
    return

  if base_type_1 != base_type_2:
    errors.append("%s: %s base type mismatch, '%s' vs '%s'" %
                  (context, kind, base_type_1, base_type_2))
  elif base_type_1 == "object":
    params_1 = named_list_to_map(type_1, "properties", "name")
    params_2 = named_list_to_map(type_2, "properties", "name")
    # If both parameters have the same named type use it in the context.
    if "id" in type_1 and "id" in type_2 and type_1["id"] == type_2["id"]:
      type_name = type_1["id"]
    else:
      type_name = "<object>"
    context += " %s->%s" % (kind, type_name)
    compare_params_list(context, "property", params_1, params_2, types_map_1,
                        types_map_2, depth + 1, errors, reverse)
  elif base_type_1 == "array":
    item_type_1 = extract_type(type_1["items"], types_map_1, errors)
    item_type_2 = extract_type(type_2["items"], types_map_2, errors)
    compare_types(context, kind, item_type_1, item_type_2, types_map_1,
                  types_map_2, depth + 1, errors, reverse)


def extract_type(typed_object, types_map, errors):
  if "type" in typed_object:
    result = {"id": "<transient>", "type": typed_object["type"]}
    if typed_object["type"] == "object":
      result["properties"] = []
    elif typed_object["type"] == "array":
      result["items"] = typed_object["items"]
    return result
  elif "$ref" in typed_object:
    ref = typed_object["$ref"]
    if ref not in types_map:
      errors.append("Can not resolve type: %s" % ref)
      types_map[ref] = {"id": "<transient>", "type": "object"}
    return types_map[ref]


def normalize_types_in_schema(domains):
  types = {}
  for domain in domains:
    domain_name = domain["domain"]
    normalize_types(domain, domain_name, types)
  return types


def normalize_types(obj, domain_name, types):
  if isinstance(obj, list):
    for item in obj:
      normalize_types(item, domain_name, types)
  elif isinstance(obj, dict):
    for key, value in obj.items():
      if key == "$ref" and value.find(".") == -1:
        obj[key] = "%s.%s" % (domain_name, value)
      elif key == "id":
        obj[key] = "%s.%s" % (domain_name, value)
        types[obj[key]] = obj
      else:
        normalize_types(value, domain_name, types)


def load_schema(file_name, domains):
  # pylint: disable=W0613
  if not os.path.isfile(file_name):
    return
  input_file = open(file_name, "r")
  parsed_json = pdl.loads(input_file.read(), file_name)
  input_file.close()
  domains += parsed_json["domains"]
  return parsed_json["version"]


def self_test():

  def create_test_schema_1():
    return [{
        "domain":
            "Network",
        "types": [{
            "id": "LoaderId",
            "type": "string"
        }, {
            "id": "Headers",
            "type": "object"
        }, {
            "id":
                "Request",
            "type":
                "object",
            "properties": [
                {
                    "name": "url",
                    "type": "string"
                },
                {
                    "name": "method",
                    "type": "string"
                },
                {
                    "name": "headers",
                    "$ref": "Headers"
                },
                {
                    "name": "becameOptionalField",
                    "type": "string"
                },
                {
                    "name": "removedField",
                    "type": "string"
                },
            ]
        }],
        "commands": [{
            "name": "removedCommand",
        }, {
            "name":
                "setExtraHTTPHeaders",
            "parameters": [
                {
                    "name": "headers",
                    "$ref": "Headers"
                },
                {
                    "name": "mismatched",
                    "type": "string"
                },
                {
                    "name": "becameOptional",
                    "$ref": "Headers"
                },
                {
                    "name": "removedRequired",
                    "$ref": "Headers"
                },
                {
                    "name": "becameRequired",
                    "$ref": "Headers",
                    "optional": True
                },
                {
                    "name": "removedOptional",
                    "$ref": "Headers",
                    "optional": True
                },
            ],
            "returns": [
                {
                    "name": "mimeType",
                    "type": "string"
                },
                {
                    "name": "becameOptional",
                    "type": "string"
                },
                {
                    "name": "removedRequired",
                    "type": "string"
                },
                {
                    "name": "becameRequired",
                    "type": "string",
                    "optional": True
                },
                {
                    "name": "removedOptional",
                    "type": "string",
                    "optional": True
                },
            ]
        }],
        "events": [{
            "name":
                "requestWillBeSent",
            "parameters": [
                {
                    "name": "frameId",
                    "type": "string",
                    "experimental": True
                },
                {
                    "name": "request",
                    "$ref": "Request"
                },
                {
                    "name": "becameOptional",
                    "type": "string"
                },
                {
                    "name": "removedRequired",
                    "type": "string"
                },
                {
                    "name": "becameRequired",
                    "type": "string",
                    "optional": True
                },
                {
                    "name": "removedOptional",
                    "type": "string",
                    "optional": True
                },
            ]
        }, {
            "name":
                "removedEvent",
            "parameters": [{
                "name": "errorText",
                "type": "string"
            }, {
                "name": "canceled",
                "type": "boolean",
                "optional": True
            }]
        }]
    }, {
        "domain": "removedDomain"
    }]

  def create_test_schema_2():
    return [{
        "domain":
            "Network",
        "types": [{
            "id": "LoaderId",
            "type": "string"
        }, {
            "id":
                "Request",
            "type":
                "object",
            "properties": [
                {
                    "name": "url",
                    "type": "string"
                },
                {
                    "name": "method",
                    "type": "string"
                },
                {
                    "name": "headers",
                    "type": "object"
                },
                {
                    "name": "becameOptionalField",
                    "type": "string",
                    "optional": True
                },
            ]
        }],
        "commands": [{
            "name": "addedCommand",
        }, {
            "name":
                "setExtraHTTPHeaders",
            "parameters": [
                {
                    "name": "headers",
                    "type": "object"
                },
                {
                    "name": "mismatched",
                    "type": "object"
                },
                {
                    "name": "becameOptional",
                    "type": "object",
                    "optional": True
                },
                {
                    "name": "addedRequired",
                    "type": "object"
                },
                {
                    "name": "becameRequired",
                    "type": "object"
                },
                {
                    "name": "addedOptional",
                    "type": "object",
                    "optional": True
                },
            ],
            "returns": [
                {
                    "name": "mimeType",
                    "type": "string"
                },
                {
                    "name": "becameOptional",
                    "type": "string",
                    "optional": True
                },
                {
                    "name": "addedRequired",
                    "type": "string"
                },
                {
                    "name": "becameRequired",
                    "type": "string"
                },
                {
                    "name": "addedOptional",
                    "type": "string",
                    "optional": True
                },
            ]
        }],
        "events": [{
            "name":
                "requestWillBeSent",
            "parameters": [
                {
                    "name": "request",
                    "$ref": "Request"
                },
                {
                    "name": "becameOptional",
                    "type": "string",
                    "optional": True
                },
                {
                    "name": "addedRequired",
                    "type": "string"
                },
                {
                    "name": "becameRequired",
                    "type": "string"
                },
                {
                    "name": "addedOptional",
                    "type": "string",
                    "optional": True
                },
            ]
        }, {
            "name": "addedEvent"
        }]
    }, {
        "domain": "addedDomain"
    }]

  expected_errors = [
      "removedDomain: domain has been removed",
      "Network.removedCommand: command has been removed",
      "Network.removedEvent: event has been removed",
      "Network.setExtraHTTPHeaders.mismatched: parameter base type mismatch, 'object' vs 'string'",
      "Network.setExtraHTTPHeaders.addedRequired: required parameter has been added",
      "Network.setExtraHTTPHeaders.becameRequired: optional parameter is now required",
      "Network.setExtraHTTPHeaders.removedRequired: required response parameter has been removed",
      "Network.setExtraHTTPHeaders.becameOptional: required response parameter is now optional",
      "Network.requestWillBeSent.removedRequired: required parameter has been removed",
      "Network.requestWillBeSent.becameOptional: required parameter is now optional",
      "Network.requestWillBeSent.request parameter->Network.Request.removedField: required property has been removed",
      "Network.requestWillBeSent.request parameter->Network.Request.becameOptionalField: required property is now optional",
  ]

  expected_errors_reverse = [
      "addedDomain: domain has been added",
      "Network.addedEvent: event has been added",
      "Network.addedCommand: command has been added",
      "Network.setExtraHTTPHeaders.mismatched: parameter base type mismatch, 'string' vs 'object'",
      "Network.setExtraHTTPHeaders.removedRequired: required parameter has been removed",
      "Network.setExtraHTTPHeaders.becameOptional: required parameter is now optional",
      "Network.setExtraHTTPHeaders.addedRequired: required response parameter has been added",
      "Network.setExtraHTTPHeaders.becameRequired: optional response parameter is now required",
      "Network.requestWillBeSent.becameRequired: optional parameter is now required",
      "Network.requestWillBeSent.addedRequired: required parameter has been added",
  ]

  def is_subset(subset, superset, message):
    for i in range(len(subset)):
      if subset[i] not in superset:
        sys.stderr.write("%s error: %s\n" % (message, subset[i]))
        return False
    return True

  def errors_match(expected, actual):
    return (is_subset(actual, expected, "Unexpected") and
            is_subset(expected, actual, "Missing"))

  return (errors_match(
      expected_errors,
      compare_schemas(create_test_schema_1(), create_test_schema_2(),
                      False)) and errors_match(
                          expected_errors_reverse,
                          compare_schemas(create_test_schema_2(),
                                          create_test_schema_1(), True)))


def load_domains_and_baselines(file_name, domains, baseline_domains):
  version = load_schema(os.path.normpath(file_name), domains)
  suffix = "-%s.%s.json" % (version["major"], version["minor"])
  baseline_file = file_name.replace(".json", suffix)
  baseline_file = file_name.replace(".pdl", suffix)
  load_schema(os.path.normpath(baseline_file), baseline_domains)
  return version


def main():
  if not self_test():
    sys.stderr.write("Self-test failed")
    return 1

  cmdline_parser = optparse.OptionParser()
  cmdline_parser.add_option("--show_changes")
  cmdline_parser.add_option("--expected_errors")
  cmdline_parser.add_option("--stamp")
  arg_options, arg_values = cmdline_parser.parse_args()

  if len(arg_values) < 1:
    sys.stderr.write(
        "Usage: %s [--show_changes] <protocol-1> [, <protocol-2>...]\n" %
        sys.argv[0])
    return 1

  domains = []
  baseline_domains = []
  version = load_domains_and_baselines(arg_values[0], domains, baseline_domains)
  for dependency in arg_values[1:]:
    load_domains_and_baselines(dependency, domains, baseline_domains)

  expected_errors = []
  if arg_options.expected_errors:
    expected_errors_file = open(arg_options.expected_errors, "r")
    expected_errors = json.loads(expected_errors_file.read())["errors"]
    expected_errors_file.close()

  errors = compare_schemas(baseline_domains, domains, False)
  unexpected_errors = []
  for i in range(len(errors)):
    if errors[i] not in expected_errors:
      unexpected_errors.append(errors[i])
  if len(unexpected_errors) > 0:
    sys.stderr.write("  Compatibility checks FAILED\n")
    for error in unexpected_errors:
      sys.stderr.write("    %s\n" % error)
    return 1

  if arg_options.show_changes:
    changes = compare_schemas(domains, baseline_domains, True)
    if len(changes) > 0:
      print("  Public changes since %s:" % version)
      for change in changes:
        print("    %s" % change)

  if arg_options.stamp:
    with open(arg_options.stamp, 'a') as _:
      pass

if __name__ == '__main__':
  sys.exit(main())
