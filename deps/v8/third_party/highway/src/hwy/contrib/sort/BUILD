package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
)

licenses(["notice"])

# Unused on Bazel builds, where this is not defined/known; Copybara replaces
# usages with an empty list.
COMPAT = [
    "//buildenv/target:non_prod",  # includes mobile/vendor.
]

cc_library(
    name = "intel",
    # hdrs = select({
    #     "//third_party/bazel_platforms/cpu:x86_64": [
    #        "avx512-16bit-common.h",
    #        "avx512-16bit-qsort.hpp",
    #        "avx512-32bit-qsort.hpp",
    #        "avx512-64bit-common.h",
    #        "avx512-64bit-qsort.hpp",
    #        "avx512-common-qsort.h",
    #     ],
    #     "//conditions:default": [],
    # }),
    compatible_with = [],
)

cc_library(
    name = "vxsort",
    srcs = [
        # "vxsort/isa_detection.cpp",
        # "vxsort/isa_detection_msvc.cpp",
        # "vxsort/isa_detection_sane.cpp",
        # "vxsort/machine_traits.avx2.cpp",
        # "vxsort/smallsort/avx2_load_mask_tables.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX2.double.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX2.float.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX2.int32_t.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX2.int64_t.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX2.uint32_t.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX2.uint64_t.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX512.double.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX512.float.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX512.int32_t.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX512.int64_t.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX512.uint32_t.generated.cpp",
        # "vxsort/smallsort/bitonic_sort.AVX512.uint64_t.generated.cpp",
        # "vxsort/vxsort_stats.cpp",
    ],
    hdrs = [
        # "vxsort/alignment.h",
        # "vxsort/defs.h",
        # "vxsort/isa_detection.h",
        # "vxsort/machine_traits.avx2.h",
        # "vxsort/machine_traits.avx512.h",
        # "vxsort/machine_traits.h",
        # "vxsort/packer.h",
        # "vxsort/smallsort/bitonic_sort.AVX2.double.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX2.float.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX2.int32_t.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX2.int64_t.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX2.uint32_t.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX2.uint64_t.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX512.double.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX512.float.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX512.int32_t.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX512.int64_t.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX512.uint32_t.generated.h",
        # "vxsort/smallsort/bitonic_sort.AVX512.uint64_t.generated.h",
        # "vxsort/smallsort/bitonic_sort.h",
        # "vxsort/vxsort.h",
        # "vxsort/vxsort_stats.h",
    ],
    compatible_with = [],
    textual_hdrs = [
        # "vxsort/vxsort_targets_disable.h",
        # "vxsort/vxsort_targets_enable_avx2.h",
        # "vxsort/vxsort_targets_enable_avx512.h",
    ],
)

VQSORT_SRCS = [
    "vqsort.cc",
    # Split into separate files to reduce MSVC build time.
    "vqsort_128a.cc",
    "vqsort_128d.cc",
    "vqsort_f16a.cc",
    "vqsort_f16d.cc",
    "vqsort_f32a.cc",
    "vqsort_f32d.cc",
    "vqsort_f64a.cc",
    "vqsort_f64d.cc",
    "vqsort_i16a.cc",
    "vqsort_i16d.cc",
    "vqsort_i32a.cc",
    "vqsort_i32d.cc",
    "vqsort_i64a.cc",
    "vqsort_i64d.cc",
    "vqsort_kv64a.cc",
    "vqsort_kv64d.cc",
    "vqsort_kv128a.cc",
    "vqsort_kv128d.cc",
    "vqsort_u16a.cc",
    "vqsort_u16d.cc",
    "vqsort_u32a.cc",
    "vqsort_u32d.cc",
    "vqsort_u64a.cc",
    "vqsort_u64d.cc",
]

VQSORT_TEXTUAL_HDRS = [
    "shared-inl.h",
    "sorting_networks-inl.h",
    "traits-inl.h",
    "traits128-inl.h",
    "vqsort-inl.h",
    # Placeholder for internal instrumentation. Do not remove.
]

cc_library(
    name = "vqsort",
    srcs = VQSORT_SRCS,
    hdrs = [
        "order.h",  # part of public interface, included by vqsort.h
        "vqsort.h",  # public interface
    ],
    compatible_with = [],
    local_defines = ["hwy_contrib_EXPORTS"],
    textual_hdrs = VQSORT_TEXTUAL_HDRS,
    deps = [
        ":intel",  # required if HAVE_INTEL
        ":vxsort",  # required if HAVE_VXSORT
        "//:algo",
        "//:hwy",
    ],
)

# -----------------------------------------------------------------------------
# Internal-only targets

# Same as vqsort, but add HWY_COMPILE_ALL_ATTAINABLE to ensure we cover all
# targets. Do not enable this in the main vqsort because it increases
# compile times.
cc_library(
    name = "vqsort_for_test",
    srcs = VQSORT_SRCS,
    hdrs = [
        "order.h",  # part of public interface, included by vqsort.h
        "vqsort.h",  # public interface
    ],
    compatible_with = [],
    local_defines = [
        "hwy_contrib_EXPORTS",
        # Build for all targets because sort_test will dynamic-dispatch to all.
        "HWY_COMPILE_ALL_ATTAINABLE",
    ],
    textual_hdrs = VQSORT_TEXTUAL_HDRS,
    deps = [
        "//:algo",
        "//:hwy",
    ],
)

cc_library(
    name = "helpers",
    testonly = 1,
    textual_hdrs = [
        "algo-inl.h",
        "result-inl.h",
    ],
    deps = [
        ":vqsort",
        "//:nanobenchmark",
        # Required for HAVE_PDQSORT, but that is unused and this is
        # unavailable to Bazel builds, hence commented out.
        # "//third_party/boost/allowed",
        # Avoid ips4o and thus TBB to work around hwloc build failure.
    ],
)

cc_binary(
    name = "print_network",
    testonly = 1,
    srcs = ["print_network.cc"],
    deps = [
        ":helpers",
        ":vqsort",
        "//:hwy",
    ],
)

TEST_MAIN = select({
    "//:compiler_msvc": [],
    "//conditions:default": ["@com_google_googletest//:gtest_main"],
})

cc_test(
    name = "sort_unit_test",
    size = "small",
    srcs = ["sort_unit_test.cc"],
    # Do not enable fully_static_link (pthread crash on bazel)
    local_defines = ["HWY_IS_TEST"],
    # for test_suite.
    tags = ["hwy_ops_test"],
    deps = [
        ":helpers",
        ":vqsort_for_test",
        "//:hwy",
        "//:hwy_test_util",
    ] + TEST_MAIN,
)

cc_test(
    name = "sort_test",
    size = "medium",
    timeout = "long",
    srcs = ["sort_test.cc"],
    # Do not enable fully_static_link (pthread crash on bazel)
    local_defines = ["HWY_IS_TEST"],
    # for test_suite.
    tags = ["hwy_ops_test"],
    deps = [
        ":helpers",
        ":vqsort_for_test",
        "//:hwy",
        "//:hwy_test_util",
        "//:thread_pool",
        "//:topology",
    ] + TEST_MAIN,
)

cc_test(
    name = "bench_sort",
    size = "medium",
    srcs = ["bench_sort.cc"],
    # Do not enable fully_static_link (pthread crash on bazel)
    local_defines = ["HWY_IS_TEST"],
    # for test_suite.
    tags = ["hwy_ops_test"],
    deps = [
        ":helpers",
        ":vqsort",
        "//:hwy",
        "//:hwy_test_util",
        "//:nanobenchmark",
    ] + TEST_MAIN,
)

cc_binary(
    name = "bench_parallel",
    testonly = 1,
    srcs = ["bench_parallel.cc"],
    # Do not enable fully_static_link (pthread crash on bazel)
    local_defines = ["HWY_IS_TEST"],
    deps = [
        ":helpers",
        ":vqsort",
        "//:hwy",
        "//:hwy_test_util",
        "//:nanobenchmark",
    ] + TEST_MAIN,
)
