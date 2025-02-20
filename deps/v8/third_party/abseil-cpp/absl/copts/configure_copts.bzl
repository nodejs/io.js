"""absl specific copts.

This file simply selects the correct options from the generated files.  To
change Abseil copts, edit absl/copts/copts.py
"""

load(
    "//absl:copts/GENERATED_copts.bzl",
    "ABSL_CLANG_CL_FLAGS",
    "ABSL_CLANG_CL_TEST_FLAGS",
    "ABSL_GCC_FLAGS",
    "ABSL_GCC_TEST_FLAGS",
    "ABSL_LLVM_FLAGS",
    "ABSL_LLVM_TEST_FLAGS",
    "ABSL_MSVC_FLAGS",
    "ABSL_MSVC_LINKOPTS",
    "ABSL_MSVC_TEST_FLAGS",
)

ABSL_DEFAULT_COPTS = select({
    "@rules_cc//cc/compiler:msvc-cl": ABSL_MSVC_FLAGS,
    "@rules_cc//cc/compiler:clang-cl": ABSL_CLANG_CL_FLAGS,
    "@rules_cc//cc/compiler:clang": ABSL_LLVM_FLAGS,
    "@rules_cc//cc/compiler:gcc": ABSL_GCC_FLAGS,
    "//conditions:default": [],
})

ABSL_TEST_COPTS = select({
    "@rules_cc//cc/compiler:msvc-cl": ABSL_MSVC_TEST_FLAGS,
    "@rules_cc//cc/compiler:clang-cl": ABSL_CLANG_CL_TEST_FLAGS,
    "@rules_cc//cc/compiler:clang": ABSL_LLVM_TEST_FLAGS,
    "@rules_cc//cc/compiler:gcc": ABSL_GCC_TEST_FLAGS,
    "//conditions:default": [],
})

ABSL_DEFAULT_LINKOPTS = select({
    "@rules_cc//cc/compiler:msvc-cl": ABSL_MSVC_LINKOPTS,
    "//conditions:default": [],
})
