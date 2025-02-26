#!/usr/bin/env python3
# Copyright 2016 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import os.path
import sys
import argparse
import collections
import functools
import re
import copy
try:
  import json
except ImportError:
  import simplejson as json

import pdl

try:
  unicode
except NameError:
  # Define unicode for Py3
  def unicode(s, *_):
    return s

# Path handling for libraries and templates
# Paths have to be normalized because Jinja uses the exact template path to
# determine the hash used in the cache filename, and we need a pre-caching step
# to be concurrency-safe. Use absolute path because __file__ is absolute if
# module is imported, and relative if executed directly.
# If paths differ between pre-caching and individual file compilation, the cache
# is regenerated, which causes a race condition and breaks concurrent build,
# since some compile processes will try to read the partially written cache.
module_path, module_filename = os.path.split(os.path.realpath(__file__))

def read_config():
  # pylint: disable=W0703
  def json_to_object(data, output_base, config_base):
    def json_object_hook(object_dict):
      items = [(k, os.path.join(config_base, v) if k == "path" else v)
               for (k, v) in object_dict.items()]
      items = [(k, os.path.join(output_base, v) if k == "output" else v)
               for (k, v) in items]
      keys, values = list(zip(*items))
      # 'async' is a keyword since Python 3.7.
      # Avoid namedtuple(rename=True) for compatibility with Python 2.X.
      keys = tuple('async_' if k == 'async' else k for k in keys)
      return collections.namedtuple('X', keys)(*values)
    return json.loads(data, object_hook=json_object_hook)

  def init_defaults(config_tuple, path, defaults):
    keys = list(config_tuple._fields)  # pylint: disable=E1101
    values = [getattr(config_tuple, k) for k in keys]
    for i in range(len(keys)):
      if hasattr(values[i], "_fields"):
        values[i] = init_defaults(values[i], path + "." + keys[i], defaults)
    for optional in defaults:
      if optional.find(path + ".") != 0:
        continue
      optional_key = optional[len(path) + 1:]
      if optional_key.find(".") == -1 and optional_key not in keys:
        keys.append(optional_key)
        values.append(defaults[optional])
    return collections.namedtuple('X', keys)(*values)

  try:
    cmdline_parser = argparse.ArgumentParser()
    cmdline_parser.add_argument("--output_base", type=unicode, required=True)
    cmdline_parser.add_argument("--jinja_dir", type=unicode, required=True)
    cmdline_parser.add_argument("--config", type=unicode, required=True)
    cmdline_parser.add_argument("--config_value", default=[], action="append")
    cmdline_parser.add_argument(
      "--inspector_protocol_dir", type=unicode, required=True,
      help=("directory with code_generator.py and C++ encoding / binding "
          "libraries, relative to the root of the source tree."))
    arg_options = cmdline_parser.parse_args()
    jinja_dir = arg_options.jinja_dir
    output_base = arg_options.output_base
    config_file = arg_options.config
    config_base = os.path.dirname(config_file)
    config_values = arg_options.config_value
    inspector_protocol_dir = arg_options.inspector_protocol_dir.lstrip('/')
  except Exception:
    # Work with python 2 and 3 http://docs.python.org/py3k/howto/pyporting.html
    exc = sys.exc_info()[1]
    sys.stderr.write("Failed to parse command-line arguments: %s\n\n" % exc)
    exit(1)

  try:
    config_json_file = open(config_file, "r")
    config_json_string = config_json_file.read()
    config_partial = json_to_object(config_json_string, output_base,
                                    config_base)
    config_json_file.close()
    defaults = {
        ".use_snake_file_names": False,
        ".use_title_case_methods": False,
        ".use_embedder_types": False,
        ".imported": False,
        ".imported.export_macro": "",
        ".imported.export_header": False,
        ".imported.header": False,
        ".imported.package": False,
        ".imported.options": False,
        ".protocol.export_macro": "",
        ".protocol.export_header": False,
        ".protocol.options": False,
        ".protocol.file_name_prefix": "",
        ".exported": False,
        ".exported.export_macro": "",
        ".exported.export_header": False,
        ".lib": False,
        ".lib.export_macro": "",
        ".lib.export_header": False,
        ".crdtp": False,
        ".crdtp.dir": os.path.join(inspector_protocol_dir, "crdtp"),
        ".crdtp.namespace": "crdtp",
    }
    for key_value in config_values:
      parts = key_value.split("=")
      if len(parts) == 2:
        defaults["." + parts[0]] = parts[1]
    return (jinja_dir, config_file, init_defaults(config_partial, "", defaults))
  except Exception:
    # Work with python 2 and 3 http://docs.python.org/py3k/howto/pyporting.html
    exc = sys.exc_info()[1]
    sys.stderr.write("Failed to parse config file: %s\n\n" % exc)
    exit(1)


# ---- Begin of utilities exposed to generator ----


def to_title_case(name):
  return name[:1].upper() + name[1:]


def dash_to_camelcase(word):
  prefix = ""
  if word[0] == "-":
    prefix = "Negative"
    word = word[1:]
  return prefix + "".join(to_title_case(x) or "-" for x in word.split("-"))


def to_snake_case(name):
  name = re.sub(r"([A-Z]{2,})([A-Z][a-z])", r"\1_\2", name)
  return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name, count=sys.maxsize).lower()


def to_method_case(config, name):
  if config.use_title_case_methods:
    return to_title_case(name)
  return name


def join_arrays(dict, keys):
  result = []
  for key in keys:
    if key in dict:
      result += dict[key]
  return result


def format_include(config, header, file_name=None):
  if file_name is not None:
    header = header + "/" + file_name + ".h"
  header = "\"" + header + "\"" if header[0] not in "<\"" else header
  if config.use_snake_file_names:
    header = to_snake_case(header)
  return header


def format_domain_include(config, header, file_name):
  return format_include(config, header,
                        config.protocol.file_name_prefix + file_name)


def to_file_name(config, file_name):
  if config.use_snake_file_names:
    return to_snake_case(file_name).replace(".cpp", ".cc")
  return file_name


# ---- End of utilities exposed to generator ----


def initialize_jinja_env(jinja_dir, cache_dir, config):
  # pylint: disable=F0401
  sys.path.insert(1, os.path.abspath(jinja_dir))
  import jinja2

  jinja_env = jinja2.Environment(
    loader=jinja2.FileSystemLoader(module_path),
    # Bytecode cache is not concurrency-safe unless pre-cached:
    # if pre-cached this is read-only, but writing creates a race condition.
    bytecode_cache=jinja2.FileSystemBytecodeCache(cache_dir),
    keep_trailing_newline=True,  # newline-terminate generated files
    lstrip_blocks=True,  # so can indent control flow tags
    trim_blocks=True)
  jinja_env.filters.update({
      "to_title_case": to_title_case,
      "dash_to_camelcase": dash_to_camelcase,
      "to_method_case": functools.partial(to_method_case, config)})
  jinja_env.add_extension("jinja2.ext.loopcontrols")
  return jinja_env


def create_imported_type_definition(domain_name, type, imported_namespace):
  # pylint: disable=W0622
  return {
    "return_type": "std::unique_ptr<%s::%s::API::%s>" % (
        imported_namespace, domain_name, type["id"]),
    "pass_type": "std::unique_ptr<%s::%s::API::%s>" % (
        imported_namespace, domain_name, type["id"]),
    "to_raw_type": "%s.get()",
    "to_pass_type": "std::move(%s)",
    "to_rvalue": "std::move(%s)",
    "type": "std::unique_ptr<%s::%s::API::%s>" % (
        imported_namespace, domain_name, type["id"]),
    "raw_type": "%s::%s::API::%s" % (
        imported_namespace, domain_name, type["id"]),
    "raw_pass_type": "%s::%s::API::%s*" % (
        imported_namespace, domain_name, type["id"]),
    "raw_return_type": "%s::%s::API::%s*" % (
        imported_namespace, domain_name, type["id"]),
  }


def create_user_type_definition(domain_name, type):
  # pylint: disable=W0622
  return {
    "return_type": "std::unique_ptr<protocol::%s::%s>" % (
        domain_name, type["id"]),
    "pass_type": "std::unique_ptr<protocol::%s::%s>" % (
        domain_name, type["id"]),
    "to_raw_type": "%s.get()",
    "to_pass_type": "std::move(%s)",
    "to_rvalue": "std::move(%s)",
    "type": "std::unique_ptr<protocol::%s::%s>" % (domain_name, type["id"]),
    "raw_type": "protocol::%s::%s" % (domain_name, type["id"]),
    "raw_pass_type": "protocol::%s::%s*" % (domain_name, type["id"]),
    "raw_return_type": "protocol::%s::%s*" % (domain_name, type["id"]),
  }


def create_object_type_definition():
  # pylint: disable=W0622
  return {
    "return_type": "std::unique_ptr<protocol::DictionaryValue>",
    "pass_type": "std::unique_ptr<protocol::DictionaryValue>",
    "to_raw_type": "%s.get()",
    "to_pass_type": "std::move(%s)",
    "to_rvalue": "std::move(%s)",
    "type": "std::unique_ptr<protocol::DictionaryValue>",
    "raw_type": "protocol::DictionaryValue",
    "raw_pass_type": "protocol::DictionaryValue*",
    "raw_return_type": "protocol::DictionaryValue*",
  }


def create_any_type_definition():
  # pylint: disable=W0622
  return {
    "return_type": "std::unique_ptr<protocol::Value>",
    "pass_type": "std::unique_ptr<protocol::Value>",
    "to_raw_type": "%s.get()",
    "to_pass_type": "std::move(%s)",
    "to_rvalue": "std::move(%s)",
    "type": "std::unique_ptr<protocol::Value>",
    "raw_type": "protocol::Value",
    "raw_pass_type": "protocol::Value*",
    "raw_return_type": "protocol::Value*",
  }


def create_string_type_definition():
  # pylint: disable=W0622
  return {
      "return_type": "String",
      "pass_type": "const String&",
      "to_pass_type": "%s",
      "to_raw_type": "%s",
      "to_rvalue": "%s",
      "type": "String",
      "raw_type": "String",
      "raw_pass_type": "const String&",
      "raw_return_type": "String",
      "is_primitive": True
  }


def create_binary_type_definition():
  # pylint: disable=W0622
  return {
      "return_type": "Binary",
      "pass_type": "const Binary&",
      "to_pass_type": "%s",
      "to_raw_type": "%s",
      "to_rvalue": "%s",
      "type": "Binary",
      "raw_type": "Binary",
      "raw_pass_type": "const Binary&",
      "raw_return_type": "Binary",
      "is_primitive": True
  }


def create_primitive_type_definition(type):
  # pylint: disable=W0622
  typedefs = {
    "number": "double",
    "integer": "int",
    "boolean": "bool"
  }
  defaults = {
    "number": "0",
    "integer": "0",
    "boolean": "false"
  }
  jsontypes = {
    "number": "TypeDouble",
    "integer": "TypeInteger",
    "boolean": "TypeBoolean",
  }
  return {
      "return_type": typedefs[type],
      "pass_type": typedefs[type],
      "to_pass_type": "%s",
      "to_raw_type": "%s",
      "to_rvalue": "%s",
      "type": typedefs[type],
      "raw_type": typedefs[type],
      "raw_pass_type": typedefs[type],
      "raw_return_type": typedefs[type],
      "default_value": defaults[type],
      "is_primitive": True
  }

def wrap_array_definition(type):
  # pylint: disable=W0622
  return {
    "return_type": "std::unique_ptr<protocol::Array<%s>>" % type["raw_type"],
    "pass_type": "std::unique_ptr<protocol::Array<%s>>" % type["raw_type"],
    "to_raw_type": "%s.get()",
    "to_pass_type": "std::move(%s)",
    "to_rvalue": "std::move(%s)",
    "type": "std::unique_ptr<protocol::Array<%s>>" % type["raw_type"],
    "raw_type": "protocol::Array<%s>" % type["raw_type"],
    "raw_pass_type": "protocol::Array<%s>*" % type["raw_type"],
    "raw_return_type": "protocol::Array<%s>*" % type["raw_type"],
    "out_type": "protocol::Array<%s>&" % type["raw_type"],
  }


class Protocol(object):

  def __init__(self, config):
    self.config = config
    self.json_api = {"domains": []}
    self.imported_domains = []
    self.exported_domains = []
    self.generate_domains = self.read_protocol_file(config.protocol.path)

    if config.protocol.options:
      self.generate_domains = [rule.domain for rule in config.protocol.options]
      self.exported_domains = [rule.domain for rule in config.protocol.options
                               if hasattr(rule, "exported")]

    if config.imported:
      self.imported_domains = self.read_protocol_file(config.imported.path)
      if config.imported.options:
        self.imported_domains = [rule.domain
                                 for rule in config.imported.options]

    self.patch_full_qualified_refs()
    self.create_type_definitions()
    self.generate_used_types()

  def read_protocol_file(self, file_name):
    input_file = open(file_name, "r")
    parsed_json = pdl.loads(input_file.read(), file_name)
    input_file.close()
    version = '%s.%s' % (parsed_json["version"]["major"],
                         parsed_json["version"]["minor"])
    domains = []
    for domain in parsed_json["domains"]:
      domains.append(domain["domain"])
      domain["version"] = version
    self.json_api["domains"] += parsed_json["domains"]
    return domains

  def patch_full_qualified_refs(self):
    def patch_full_qualified_refs_in_domain(json, domain_name):
      if isinstance(json, list):
        for item in json:
          patch_full_qualified_refs_in_domain(item, domain_name)
      if not isinstance(json, dict):
        return
      for key in json:
        if key == "type" and json[key] == "string":
          json[key] = domain_name + ".string"
        if key != "$ref":
          patch_full_qualified_refs_in_domain(json[key], domain_name)
          continue
        if json["$ref"].find(".") == -1:
          json["$ref"] = domain_name + "." + json["$ref"]
      return

    for domain in self.json_api["domains"]:
      patch_full_qualified_refs_in_domain(domain, domain["domain"])

  def all_references(self, json):
    refs = set()
    if isinstance(json, list):
      for item in json:
        refs |= self.all_references(item)
    if not isinstance(json, dict):
      return refs
    for key in json:
      if key != "$ref":
        refs |= self.all_references(json[key])
      else:
        refs.add(json["$ref"])
    return refs

  def check_if_dependency_declared(self, domain, refs):
    dependencies = domain.get('dependencies', set())
    for ref in refs:
      type_definition = self.type_definitions[ref]
      if type_definition.get('is_primitive', False):
        continue
      domain_match = re.match(r'^(.*)[.]', ref)
      if domain_match:
        referenced_domain_name = domain_match.group(1)
        if referenced_domain_name != domain[
            'domain'] and not referenced_domain_name in dependencies:
          sys.stderr.write((
              "Domains [%s] uses type [%s] from domain [%s], but did not declare the dependency\n\n"
          ) % (domain["domain"], ref, referenced_domain_name))
          exit(1)

  def generate_used_types(self):
    all_refs = set()
    for domain in self.json_api["domains"]:
      domain_name = domain["domain"]
      if "commands" in domain:
        for command in domain["commands"]:
          if self.generate_command(domain_name, command["name"]):
            all_refs_command = self.all_references(command)
            # If the command has a redirect, it is as if it didn't exist on this domain.
            if not command.get('redirect', False):
              self.check_if_dependency_declared(domain, all_refs_command)
            all_refs |= all_refs_command

      if "events" in domain:
        for event in domain["events"]:
          if self.generate_event(domain_name, event["name"]):
            all_refs_event = self.all_references(event)
            self.check_if_dependency_declared(domain, all_refs_event)
            all_refs |= all_refs_event


    dependencies = self.generate_type_dependencies()
    queue = set(all_refs)
    while len(queue):
      ref = queue.pop()
      if ref in dependencies:
        queue |= dependencies[ref] - all_refs
        all_refs |= dependencies[ref]
    self.used_types = all_refs

  def generate_type_dependencies(self):
    dependencies = dict()
    domains_with_types = (x for x in self.json_api["domains"] if "types" in x)
    for domain in domains_with_types:
      domain_name = domain["domain"]
      for type in domain["types"]:
        related_types = self.all_references(type)
        if len(related_types):
          dependencies[domain_name + "." + type["id"]] = related_types
    return dependencies

  def create_type_definitions(self):
    imported_namespace = ""
    if self.config.imported:
      imported_namespace = "::".join(self.config.imported.namespace)
    self.type_definitions = {}
    self.type_definitions["number"] = create_primitive_type_definition("number")
    self.type_definitions["integer"] = create_primitive_type_definition("integer")
    self.type_definitions["boolean"] = create_primitive_type_definition("boolean")
    self.type_definitions["object"] = create_object_type_definition()
    self.type_definitions["any"] = create_any_type_definition()
    self.type_definitions["binary"] = create_binary_type_definition()
    for domain in self.json_api["domains"]:
      self.type_definitions[domain["domain"] + ".string"] = (
          create_string_type_definition())
      self.type_definitions[domain["domain"] + ".binary"] = (
          create_binary_type_definition())
      if not ("types" in domain):
        continue
      for type in domain["types"]:
        type_name = domain["domain"] + "." + type["id"]
        if type["type"] == "object" and domain["domain"] in self.imported_domains:
          self.type_definitions[type_name] = create_imported_type_definition(
              domain["domain"], type, imported_namespace)
        elif type["type"] == "object":
          self.type_definitions[type_name] = create_user_type_definition(
              domain["domain"], type)
        elif type["type"] == "array":
          self.type_definitions[type_name] = self.resolve_type(type)
        elif type["type"] == domain["domain"] + ".string":
          self.type_definitions[type_name] = create_string_type_definition()
        elif type["type"] == domain["domain"] + ".binary":
          self.type_definitions[type_name] = create_binary_type_definition()
        else:
          self.type_definitions[type_name] = create_primitive_type_definition(
              type["type"])

  def check_options(self, options, domain, name, include_attr, exclude_attr,
                    default):
    for rule in options:
      if rule.domain != domain:
        continue
      if include_attr and hasattr(rule, include_attr):
        return name in getattr(rule, include_attr)
      if exclude_attr and hasattr(rule, exclude_attr):
        return name not in getattr(rule, exclude_attr)
      return default
    return False


  # ---- Begin of methods exposed to generator

  def type_definition(self, name):
    return self.type_definitions[name]

  def resolve_type(self, prop):
    if "$ref" in prop:
      return self.type_definitions[prop["$ref"]]
    if prop["type"] == "array":
      return wrap_array_definition(self.resolve_type(prop["items"]))
    return self.type_definitions[prop["type"]]

  def optional_type(self, prop):
    type = self.resolve_type(prop)
    template = ("std::optional<{}>"
                if type.get('is_primitive', False) else "std::unique_ptr<{}>")
    return template.format(type.get("raw_type"))

  def generate_command(self, domain, command):
    if not self.config.protocol.options:
      return domain in self.generate_domains
    return self.check_options(self.config.protocol.options, domain, command,
                              "include", "exclude", True)

  def generate_event(self, domain, event):
    if not self.config.protocol.options:
      return domain in self.generate_domains
    return self.check_options(self.config.protocol.options, domain, event,
                              "include_events", "exclude_events", True)

  def generate_type(self, domain, typename):
    return domain + "." + typename in self.used_types

  def is_async_command(self, domain, command):
    if not self.config.protocol.options:
      return False
    return self.check_options(self.config.protocol.options, domain, command,
                              "async_", None, False)

  def is_exported(self, domain, name):
    if not self.config.protocol.options:
      return False
    return self.check_options(self.config.protocol.options, domain, name,
                              "exported", None, False)

  def is_imported(self, domain, name):
    if not self.config.imported:
      return False
    if not self.config.imported.options:
      return domain in self.imported_domains
    return self.check_options(self.config.imported.options, domain, name,
                              "imported", None, False)

  def is_exported_domain(self, domain):
    return domain in self.exported_domains

  def generate_disable(self, domain):
    if "commands" not in domain:
      return True
    for command in domain["commands"]:
      if command["name"] == "disable" and self.generate_command(
          domain["domain"], "disable"):
        return False
    return True

  def is_imported_dependency(self, domain):
    return domain in self.generate_domains or domain in self.imported_domains


def main():
  jinja_dir, config_file, config = read_config()

  protocol = Protocol(config)

  if not config.exported and len(protocol.exported_domains):
    sys.stderr.write(("Domains [%s] are exported, but config is missing export "
                      "entry\n\n") % ", ".join(protocol.exported_domains))
    exit(1)

  if not os.path.exists(config.protocol.output):
    os.mkdir(config.protocol.output)
  if len(protocol.exported_domains) and not os.path.exists(
      config.exported.output):
    os.mkdir(config.exported.output)
  jinja_env = initialize_jinja_env(jinja_dir, config.protocol.output, config)

  inputs = []
  inputs.append(__file__)
  inputs.append(config_file)
  inputs.append(config.protocol.path)
  if config.imported:
    inputs.append(config.imported.path)
  templates_dir = os.path.join(module_path, "templates")
  inputs.append(os.path.join(templates_dir, "TypeBuilder_h.template"))
  inputs.append(os.path.join(templates_dir, "TypeBuilder_cpp.template"))
  inputs.append(os.path.join(templates_dir, "Exported_h.template"))
  inputs.append(os.path.join(templates_dir, "Imported_h.template"))

  h_template = jinja_env.get_template("templates/TypeBuilder_h.template")
  cpp_template = jinja_env.get_template("templates/TypeBuilder_cpp.template")
  exported_template = jinja_env.get_template("templates/Exported_h.template")
  imported_template = jinja_env.get_template("templates/Imported_h.template")

  outputs = dict()

  for domain in protocol.json_api["domains"]:
    class_name = domain["domain"]
    file_name = config.protocol.file_name_prefix + class_name
    template_context = {
      "protocol": protocol,
      "config": config,
      "domain": domain,
      "join_arrays": join_arrays,
      "format_include": functools.partial(format_include, config),
      "format_domain_include": functools.partial(format_domain_include, config),
    }

    if domain["domain"] in protocol.generate_domains:
      outputs[os.path.join(config.protocol.output, to_file_name(
          config, file_name + ".h"))] = h_template.render(template_context)
      outputs[os.path.join(config.protocol.output, to_file_name(
          config, file_name + ".cpp"))] = cpp_template.render(template_context)
      if domain["domain"] in protocol.exported_domains:
        outputs[os.path.join(config.exported.output, to_file_name(
            config, file_name + ".h"))] = exported_template.render(
                template_context)
    if domain["domain"] in protocol.imported_domains:
      outputs[os.path.join(config.protocol.output, to_file_name(
          config, file_name + ".h"))] = imported_template.render(
              template_context)

  if config.lib:
    template_context = {
      "config": config,
      "format_include": functools.partial(format_include, config),
    }

    lib_templates_dir = os.path.join(module_path, "lib")
    # Note these should be sorted in the right order.

    # TODO(dgozman): sort them programmatically based on commented includes.

    forward_h_templates = [
      "Forward_h.template",
    ]

    protocol_h_templates = []
    protocol_cpp_templates = []

    if not config.use_embedder_types:
      protocol_h_templates += [
          "Values_h.template",
          "Object_h.template",
          "ValueConversions_h.template",
      ]
      protocol_cpp_templates += [
          "Protocol_cpp.template",
          "Values_cpp.template",
          "Object_cpp.template",
          "ValueConversions_cpp.template",
      ]
    else:
      protocol_h_templates += [
          "Forward_h.template",
      ]

    def generate_lib_file(file_name, template_files):
      parts = []
      for template_file in template_files:
        inputs.append(os.path.join(lib_templates_dir, template_file))
        template = jinja_env.get_template("lib/" + template_file)
        parts.append(template.render(template_context))
      outputs[file_name] = "\n\n".join(parts)

    generate_lib_file(os.path.join(config.lib.output, to_file_name(
        config, "Forward.h")), forward_h_templates)
    generate_lib_file(os.path.join(config.lib.output, to_file_name(
        config, "Protocol.h")), protocol_h_templates)

    if not config.use_embedder_types:
      generate_lib_file(
          os.path.join(config.lib.output, to_file_name(config, "Protocol.cpp")),
          protocol_cpp_templates)

  # Make gyp / make generatos happy, otherwise make rebuilds world.
  inputs_ts = max(map(os.path.getmtime, inputs))
  up_to_date = True
  for output_file in outputs.keys():
    if (not os.path.exists(output_file)
        or os.path.getmtime(output_file) < inputs_ts):
      up_to_date = False
      break
  if up_to_date:
    sys.exit()

  for file_name, content in outputs.items():
    # Remove output file first to account for potential case changes.
    try:
      os.remove(file_name)
    except OSError:
      pass
    out_file = open(file_name, "w")
    out_file.write(content)
    out_file.close()


if __name__ == "__main__":
  main()
