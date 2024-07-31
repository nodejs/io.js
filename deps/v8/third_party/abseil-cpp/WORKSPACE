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

workspace(name = "com_google_absl")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# GoogleTest/GoogleMock framework. Used by most unit-tests.
http_archive(
  name = "com_google_googletest",
  sha256 = "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7",
  strip_prefix = "googletest-1.14.0",
  # Keep this URL in sync with ABSL_GOOGLETEST_COMMIT in ci/cmake_common.sh and
  # ci/windows_msvc_cmake.bat.
  urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
)

# RE2 (the regular expression library used by GoogleTest)
http_archive(
    name = "com_googlesource_code_re2",
    sha256 = "828341ad08524618a626167bd320b0c2acc97bd1c28eff693a9ea33a7ed2a85f",
    strip_prefix = "re2-2023-11-01",
    urls = ["https://github.com/google/re2/releases/download/2023-11-01/re2-2023-11-01.zip"],
)

# Google benchmark.
http_archive(
    name = "com_github_google_benchmark",
    sha256 = "6bc180a57d23d4d9515519f92b0c83d61b05b5bab188961f36ac7b06b0d9e9ce",
    strip_prefix = "benchmark-1.8.3",
    urls = ["https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz"],
)

# Bazel Skylib.
http_archive(
  name = "bazel_skylib",
  sha256 = "cd55a062e763b9349921f0f5db8c3933288dc8ba4f76dd9416aac68acee3cb94",
  urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/1.5.0/bazel-skylib-1.5.0.tar.gz"],
)

# Bazel platform rules.
http_archive(
    name = "platforms",
    sha256 = "8150406605389ececb6da07cbcb509d5637a3ab9a24bc69b1101531367d89d74",
    urls = ["https://github.com/bazelbuild/platforms/releases/download/0.0.8/platforms-0.0.8.tar.gz"],
)
