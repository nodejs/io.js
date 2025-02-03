# Copyright 2021 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

workspace(name = "v8")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "bazel_skylib",
    sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
    urls = [
        "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
    ],
)

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")

bazel_skylib_workspace()

http_archive(
    name = "rules_python",
    sha256 = "a30abdfc7126d497a7698c29c46ea9901c6392d6ed315171a6df5ce433aa4502",
    strip_prefix = "rules_python-0.6.0",
    url = "https://github.com/bazelbuild/rules_python/archive/0.6.0.tar.gz",
)

load("@rules_python//python:pip.bzl", "pip_install")

pip_install(
    name = "v8_python_deps",
    extra_pip_args = ["--require-hashes"],
    requirements = "//:bazel/requirements.txt",
)

local_repository(
  name = "com_google_absl",
  path = "third_party/abseil-cpp",
)

bind(
    name = "absl_optional",
    actual = "@com_google_absl//absl/types:optional"
)

bind(
    name = "absl_btree",
    actual = "@com_google_absl//absl/container:btree"
)

bind(
    name = "absl_flat_hash_map",
    actual = "@com_google_absl//absl/container:flat_hash_map"
)

bind(
    name = "absl_flat_hash_set",
    actual = "@com_google_absl//absl/container:flat_hash_set"
)

new_local_repository(
    name = "com_googlesource_chromium_icu",
    build_file = ":bazel/BUILD.icu",
    path = "third_party/icu",
)

bind(
    name = "icu",
    actual = "@com_googlesource_chromium_icu//:icu",
)

http_archive(
    name = "intel_ittapi",
    add_prefix = "third_party/ittapi",
    build_file = "@//:bazel/BUILD.ittapi",
    sha256 = "36c42d3f2446ddfaa2d7dfa02dfaa79615933f1a68a72d7e4f1d70de7b56e2c9",
    strip_prefix = "ittapi-3.24.0",
    url = "https://github.com/intel/ittapi/archive/refs/tags/v3.24.0.tar.gz",
)

bind(
    name = "ittapi",
    actual = "@intel_ittapi//:lib_ittapi",
)
