# CRDTP - Chrome DevTools Protocol Library.

[Canonical location for this library.](https://chromium.googlesource.com/deps/inspector_protocol/+/refs/heads/main)

This is a support library for the Chrome DevTools protocol implementation.

It's used from within the Jinja templates which we use for code generation
(see ../lib and ../templates) as well as from Chromium (headless,
chrome, content, blink), V8, and other code bases that use the DevTools
protocol.

The library is designed to be portable. The only allowed dependencies are:

- The C/C++ standard libraries, up to C++14.
  The litmus test is that it compiles and passes tests for all platforms
  supported by V8.

- For testing, we depend on mini_chromium and gtest. This is isolated
  into the `crdtp/test_platform.{h,cc}` library.

We support 32 bit and 64 bit architectures.

# Common types used in this library.

- `uint8_t`: a byte, e.g. for raw bytes or UTF8 characters

- `uint16_t`: two bytes, e.g. for UTF16 characters

For input parameters:

- `span<uint8_t>`: pointer to bytes and length

- `span<uint16_t>`: pointer to UTF16 chars and length

For output parameters:

- `std::vector<uint8_t>` - Owned segment of bytes / utf8 characters and length.

- `std::string` - Same, for compatibility, even though char is signed.

# Building and running the tests.

If you're familiar with
[Chromium's development process](https://www.chromium.org/developers/contributing-code)
and have the depot_tools installed, you may use these commands
to fetch the package (and dependencies) and build and run the tests:

    fetch inspector_protocol
    cd src
    gn gen out/Release
    ninja -C out/Release crdtp_test
    out/Release/crdtp_test

You'll probably also need to install g++, since Clang uses this to find the
standard C++ headers. E.g.,

    sudo apt-get install g++-8

# Purpose of the tests

crdtp comes with unittest coverage.

Upstream, in this standalone package, the unittests make development
more pleasant because they are very fast and light (try the previous
section to see).

Downstream (in Chromium, V8, etc.), they ensure that the library behaves
correctly within each specific code base. We have seen bugs from different
architectures / compilers / etc. in the past. We have also seen
that a tweaked downstream crdtp_platform library did not behave correctly,
becaues V8's strtod routine interprets out of range literals as 'inf'.
Thus, the unittests function as a conformance test suite for such code-base
specific tweaks downstream.

# Customization by downstream users (Chrome, V8, google3, etc.).

Downstream users may need to customize the library. We isolate these typical
customizations into two platform libraries (crdtp_plaform and
crdtp_test_platform), to reduce the chance of merge conflicts and grief when
rolling as much as possible. While customized platform libraries may
depend on the downstream code base (e.g. abseil, Chromium's base, V8's utility
functions, Boost, etc.), they are not exposed to the headers that
downstream code depends on.

## crdtp_platform

This platform library is only used by the crdtp library; it is not part of the
crdtp API. Thus far it consists only of json_platform.h and json_platform.cc,
because conversion between a `std::string` and a double is tricky, and different
code bases have different preferences in this regard. In this repository
(upstream), json_platform.cc provides a reference implementation which uses the
C++ standard library.

Downstream, in Chromium, json_platform_chromium.cc has a different
implementation that uses the routines in Chromium's //base, that is, it's a .cc
file that's specific to Chromium. Similarly, in V8, json_platform_v8.cc uses
V8's number conversion utilities, so it's a .cc file that's specific to V8. And
in google3, we use the absl library. crdtp/json_platform.cc is designed to be
easy to modify or replace, and the interface defined by its header is designed
to be stable.

## crdtp_test_platform

This platform library is only used by the tests. Upstream, it's setup to
use mini_chromium and gtest. Downstream, Chromium uses its //base libraries,
and V8 uses theirs; and a small amount of tweaking is needed in each code
base - e.g., Chromium, V8, and google3 each place `#include` declarations into
test_platform.h that are specific to their code base, and they have their
own routines in test_platform.cc which uses their own libraries.

The purpose of crdtp_test_platform is to isolate the tweaking to this small,
stable library (modifying test_platform.h and test_platform.cc). This avoids
having to modify the actual tests (json_test.cc, cbor_test.cc, ...)
when rolling changes downstream. We try to not use patch files.
