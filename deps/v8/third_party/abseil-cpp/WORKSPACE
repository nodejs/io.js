#
# Copyright 2019 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

workspace(name = "abseil-cpp")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# GoogleTest/GoogleMock framework. Used by most unit-tests.
http_archive(
    name = "googletest",
    sha256 = "7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926",
    strip_prefix = "googletest-1.15.2",
    # Keep this URL in sync with the version in ci/cmake_common.sh and
    # ci/windows_msvc_cmake.bat.
    urls = ["https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz"],
    # Now that Abseil is using the canonical names from the Bazel Central Registry, map
    # GoogleTest's old names to the new canonical names.
    repo_mapping = {
        "@com_google_absl": "@",
        "@com_googlesource_code_re2": "@re2",
    },
)

# RE2 (the regular expression library used by GoogleTest)
http_archive(
    name = "re2",
    sha256 = "eb2df807c781601c14a260a507a5bb4509be1ee626024cb45acbd57cb9d4032b",
    strip_prefix = "re2-2024-07-02",
    urls = ["https://github.com/google/re2/releases/download/2024-07-02/re2-2024-07-02.tar.gz"],
)

# Google benchmark.
http_archive(
    name = "google_benchmark",
    sha256 = "d26789a2b46d8808a48a4556ee58ccc7c497fcd4c0af9b90197674a81e04798a",
    strip_prefix = "benchmark-1.8.5",
    urls = ["https://github.com/google/benchmark/archive/refs/tags/v1.8.5.tar.gz"],
)

# Bazel Skylib.
http_archive(
    name = "bazel_skylib",
    sha256 = "bc283cdfcd526a52c3201279cda4bc298652efa898b10b4db0837dc51652756f",
    urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz"],
)

# C++ rules for Bazel
http_archive(
    name = "rules_cc",
    urls = ["https://github.com/bazelbuild/rules_cc/releases/download/0.1.0/rules_cc-0.1.0.tar.gz"],
    sha256 = "4b12149a041ddfb8306a8fd0e904e39d673552ce82e4296e96fac9cbf0780e59",
    strip_prefix = "rules_cc-0.1.0",
)

# Bazel platform rules.
http_archive(
    name = "platforms",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/platforms/releases/download/0.0.10/platforms-0.0.10.tar.gz",
        "https://github.com/bazelbuild/platforms/releases/download/0.0.10/platforms-0.0.10.tar.gz",
    ],
    sha256 = "218efe8ee736d26a3572663b374a253c012b716d8af0c07e842e82f238a0a7ee",
)
