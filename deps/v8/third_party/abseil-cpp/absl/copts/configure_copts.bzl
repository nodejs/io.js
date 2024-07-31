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
    "ABSL_RANDOM_HWAES_ARM32_FLAGS",
    "ABSL_RANDOM_HWAES_ARM64_FLAGS",
    "ABSL_RANDOM_HWAES_MSVC_X64_FLAGS",
    "ABSL_RANDOM_HWAES_X64_FLAGS",
)

ABSL_DEFAULT_COPTS = select({
    "//absl:msvc_compiler": ABSL_MSVC_FLAGS,
    "//absl:clang-cl_compiler": ABSL_CLANG_CL_FLAGS,
    "//absl:clang_compiler": ABSL_LLVM_FLAGS,
    "//absl:gcc_compiler": ABSL_GCC_FLAGS,
    "//conditions:default": ABSL_GCC_FLAGS,
})

ABSL_TEST_COPTS = select({
    "//absl:msvc_compiler": ABSL_MSVC_TEST_FLAGS,
    "//absl:clang-cl_compiler": ABSL_CLANG_CL_TEST_FLAGS,
    "//absl:clang_compiler": ABSL_LLVM_TEST_FLAGS,
    "//absl:gcc_compiler": ABSL_GCC_TEST_FLAGS,
    "//conditions:default": ABSL_GCC_TEST_FLAGS,
})

ABSL_DEFAULT_LINKOPTS = select({
    "//absl:msvc_compiler": ABSL_MSVC_LINKOPTS,
    "//conditions:default": [],
})

# ABSL_RANDOM_RANDEN_COPTS blaze copts flags which are required by each
# environment to build an accelerated RandenHwAes library.
ABSL_RANDOM_RANDEN_COPTS = select({
    # APPLE
    ":cpu_darwin_x86_64": ABSL_RANDOM_HWAES_X64_FLAGS,
    ":cpu_darwin": ABSL_RANDOM_HWAES_X64_FLAGS,
    ":cpu_x64_windows_msvc": ABSL_RANDOM_HWAES_MSVC_X64_FLAGS,
    ":cpu_x64_windows": ABSL_RANDOM_HWAES_MSVC_X64_FLAGS,
    ":cpu_k8": ABSL_RANDOM_HWAES_X64_FLAGS,
    ":cpu_ppc": ["-mcrypto"],
    ":cpu_aarch64": ABSL_RANDOM_HWAES_ARM64_FLAGS,

    # Supported by default or unsupported.
    "//conditions:default": [],
})

# absl_random_randen_copts_init:
#  Initialize the config targets based on cpu, os, etc. used to select
#  the required values for ABSL_RANDOM_RANDEN_COPTS
def absl_random_randen_copts_init():
    """Initialize the config_settings used by ABSL_RANDOM_RANDEN_COPTS."""

    # CPU configs.
    # These configs have consistent flags to enable HWAES intsructions.
    cpu_configs = [
        "ppc",
        "k8",
        "darwin_x86_64",
        "darwin",
        "x64_windows_msvc",
        "x64_windows",
        "aarch64",
    ]
    for cpu in cpu_configs:
        native.config_setting(
            name = "cpu_%s" % cpu,
            values = {"cpu": cpu},
        )
