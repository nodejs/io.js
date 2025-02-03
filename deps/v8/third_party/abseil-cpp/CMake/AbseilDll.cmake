include(CMakeParseArguments)
include(GNUInstallDirs)

set(ABSL_INTERNAL_DLL_FILES
  "algorithm/algorithm.h"
  "algorithm/container.h"
  "base/attributes.h"
  "base/call_once.h"
  "base/casts.h"
  "base/config.h"
  "base/const_init.h"
  "base/dynamic_annotations.h"
  "base/internal/atomic_hook.h"
  "base/internal/cycleclock.cc"
  "base/internal/cycleclock.h"
  "base/internal/cycleclock_config.h"
  "base/internal/direct_mmap.h"
  "base/internal/endian.h"
  "base/internal/errno_saver.h"
  "base/internal/fast_type_id.h"
  "base/internal/hide_ptr.h"
  "base/internal/identity.h"
  "base/internal/invoke.h"
  "base/internal/inline_variable.h"
  "base/internal/low_level_alloc.cc"
  "base/internal/low_level_alloc.h"
  "base/internal/low_level_scheduling.h"
  "base/internal/nullability_impl.h"
  "base/internal/per_thread_tls.h"
  "base/internal/poison.cc"
  "base/internal/poison.h"
  "base/prefetch.h"
  "base/internal/pretty_function.h"
  "base/internal/raw_logging.cc"
  "base/internal/raw_logging.h"
  "base/internal/scheduling_mode.h"
  "base/internal/scoped_set_env.cc"
  "base/internal/scoped_set_env.h"
  "base/internal/strerror.h"
  "base/internal/strerror.cc"
  "base/internal/spinlock.cc"
  "base/internal/spinlock.h"
  "base/internal/spinlock_wait.cc"
  "base/internal/spinlock_wait.h"
  "base/internal/sysinfo.cc"
  "base/internal/sysinfo.h"
  "base/internal/thread_identity.cc"
  "base/internal/thread_identity.h"
  "base/internal/throw_delegate.cc"
  "base/internal/throw_delegate.h"
  "base/internal/tracing.cc"
  "base/internal/tracing.h"
  "base/internal/tsan_mutex_interface.h"
  "base/internal/unaligned_access.h"
  "base/internal/unscaledcycleclock.cc"
  "base/internal/unscaledcycleclock.h"
  "base/internal/unscaledcycleclock_config.h"
  "base/log_severity.cc"
  "base/log_severity.h"
  "base/macros.h"
  "base/no_destructor.h"
  "base/nullability.h"
  "base/optimization.h"
  "base/options.h"
  "base/policy_checks.h"
  "base/port.h"
  "base/thread_annotations.h"
  "cleanup/cleanup.h"
  "cleanup/internal/cleanup.h"
  "container/btree_map.h"
  "container/btree_set.h"
  "container/hash_container_defaults.h"
  "container/fixed_array.h"
  "container/flat_hash_map.h"
  "container/flat_hash_set.h"
  "container/inlined_vector.h"
  "container/internal/btree.h"
  "container/internal/btree_container.h"
  "container/internal/common.h"
  "container/internal/common_policy_traits.h"
  "container/internal/compressed_tuple.h"
  "container/internal/container_memory.h"
  "container/internal/hash_function_defaults.h"
  "container/internal/hash_policy_traits.h"
  "container/internal/hashtable_debug.h"
  "container/internal/hashtable_debug_hooks.h"
  "container/internal/hashtablez_sampler.cc"
  "container/internal/hashtablez_sampler.h"
  "container/internal/hashtablez_sampler_force_weak_definition.cc"
  "container/internal/inlined_vector.h"
  "container/internal/layout.h"
  "container/internal/node_slot_policy.h"
  "container/internal/raw_hash_map.h"
  "container/internal/raw_hash_set.cc"
  "container/internal/raw_hash_set.h"
  "container/internal/tracked.h"
  "container/node_hash_map.h"
  "container/node_hash_set.h"
  "crc/crc32c.cc"
  "crc/crc32c.h"
  "crc/internal/cpu_detect.cc"
  "crc/internal/cpu_detect.h"
  "crc/internal/crc32c.h"
  "crc/internal/crc32c_inline.h"
  "crc/internal/crc32_x86_arm_combined_simd.h"
  "crc/internal/crc.cc"
  "crc/internal/crc.h"
  "crc/internal/crc_cord_state.cc"
  "crc/internal/crc_cord_state.h"
  "crc/internal/crc_internal.h"
  "crc/internal/crc_x86_arm_combined.cc"
  "crc/internal/crc_memcpy_fallback.cc"
  "crc/internal/crc_memcpy.h"
  "crc/internal/crc_memcpy_x86_arm_combined.cc"
  "crc/internal/crc_non_temporal_memcpy.cc"
  "crc/internal/crc_x86_arm_combined.cc"
  "crc/internal/non_temporal_arm_intrinsics.h"
  "crc/internal/non_temporal_memcpy.h"
  "debugging/failure_signal_handler.cc"
  "debugging/failure_signal_handler.h"
  "debugging/leak_check.h"
  "debugging/stacktrace.cc"
  "debugging/stacktrace.h"
  "debugging/symbolize.cc"
  "debugging/symbolize.h"
  "debugging/internal/address_is_readable.cc"
  "debugging/internal/address_is_readable.h"
  "debugging/internal/bounded_utf8_length_sequence.h"
  "debugging/internal/decode_rust_punycode.cc"
  "debugging/internal/decode_rust_punycode.h"
  "debugging/internal/demangle.cc"
  "debugging/internal/demangle.h"
  "debugging/internal/demangle_rust.cc"
  "debugging/internal/demangle_rust.h"
  "debugging/internal/elf_mem_image.cc"
  "debugging/internal/elf_mem_image.h"
  "debugging/internal/examine_stack.cc"
  "debugging/internal/examine_stack.h"
  "debugging/internal/stack_consumption.cc"
  "debugging/internal/stack_consumption.h"
  "debugging/internal/stacktrace_config.h"
  "debugging/internal/symbolize.h"
  "debugging/internal/utf8_for_code_point.cc"
  "debugging/internal/utf8_for_code_point.h"
  "debugging/internal/vdso_support.cc"
  "debugging/internal/vdso_support.h"
  "functional/any_invocable.h"
  "functional/internal/front_binder.h"
  "functional/bind_front.h"
  "functional/function_ref.h"
  "functional/internal/any_invocable.h"
  "functional/internal/function_ref.h"
  "functional/overload.h"
  "hash/hash.h"
  "hash/internal/city.h"
  "hash/internal/city.cc"
  "hash/internal/hash.h"
  "hash/internal/hash.cc"
  "hash/internal/spy_hash_state.h"
  "hash/internal/low_level_hash.h"
  "hash/internal/low_level_hash.cc"
  "log/absl_check.h"
  "log/absl_log.h"
  "log/absl_vlog_is_on.h"
  "log/check.h"
  "log/die_if_null.cc"
  "log/die_if_null.h"
  "log/globals.cc"
  "log/globals.h"
  "log/internal/append_truncated.h"
  "log/internal/check_impl.h"
  "log/internal/check_op.cc"
  "log/internal/check_op.h"
  "log/internal/conditions.cc"
  "log/internal/conditions.h"
  "log/internal/config.h"
  "log/internal/fnmatch.h"
  "log/internal/fnmatch.cc"
  "log/internal/globals.cc"
  "log/internal/globals.h"
  "log/internal/log_format.cc"
  "log/internal/log_format.h"
  "log/internal/log_impl.h"
  "log/internal/log_message.cc"
  "log/internal/log_message.h"
  "log/internal/log_sink_set.cc"
  "log/internal/log_sink_set.h"
  "log/internal/nullguard.cc"
  "log/internal/nullguard.h"
  "log/internal/nullstream.h"
  "log/internal/proto.h"
  "log/internal/proto.cc"
  "log/internal/strip.h"
  "log/internal/structured.h"
  "log/internal/vlog_config.cc"
  "log/internal/vlog_config.h"
  "log/internal/voidify.h"
  "log/initialize.cc"
  "log/initialize.h"
  "log/log.h"
  "log/log_entry.cc"
  "log/log_entry.h"
  "log/log_sink.cc"
  "log/log_sink.h"
  "log/log_sink_registry.h"
  "log/log_streamer.h"
  "log/structured.h"
  "log/vlog_is_on.h"
  "memory/memory.h"
  "meta/type_traits.h"
  "numeric/bits.h"
  "numeric/int128.cc"
  "numeric/int128.h"
  "numeric/internal/bits.h"
  "numeric/internal/representation.h"
  "profiling/internal/exponential_biased.cc"
  "profiling/internal/exponential_biased.h"
  "profiling/internal/periodic_sampler.cc"
  "profiling/internal/periodic_sampler.h"
  "profiling/internal/sample_recorder.h"
  "random/bernoulli_distribution.h"
  "random/beta_distribution.h"
  "random/bit_gen_ref.h"
  "random/discrete_distribution.cc"
  "random/discrete_distribution.h"
  "random/distributions.h"
  "random/exponential_distribution.h"
  "random/gaussian_distribution.cc"
  "random/gaussian_distribution.h"
  "random/internal/distribution_caller.h"
  "random/internal/fastmath.h"
  "random/internal/fast_uniform_bits.h"
  "random/internal/generate_real.h"
  "random/internal/iostream_state_saver.h"
  "random/internal/nonsecure_base.h"
  "random/internal/pcg_engine.h"
  "random/internal/platform.h"
  "random/internal/pool_urbg.cc"
  "random/internal/pool_urbg.h"
  "random/internal/randen.cc"
  "random/internal/randen.h"
  "random/internal/randen_detect.cc"
  "random/internal/randen_detect.h"
  "random/internal/randen_engine.h"
  "random/internal/randen_hwaes.cc"
  "random/internal/randen_hwaes.h"
  "random/internal/randen_round_keys.cc"
  "random/internal/randen_slow.cc"
  "random/internal/randen_slow.h"
  "random/internal/randen_traits.h"
  "random/internal/salted_seed_seq.h"
  "random/internal/seed_material.cc"
  "random/internal/seed_material.h"
  "random/internal/sequence_urbg.h"
  "random/internal/traits.h"
  "random/internal/uniform_helper.h"
  "random/internal/wide_multiply.h"
  "random/log_uniform_int_distribution.h"
  "random/poisson_distribution.h"
  "random/random.h"
  "random/seed_gen_exception.cc"
  "random/seed_gen_exception.h"
  "random/seed_sequences.cc"
  "random/seed_sequences.h"
  "random/uniform_int_distribution.h"
  "random/uniform_real_distribution.h"
  "random/zipf_distribution.h"
  "status/internal/status_internal.h"
  "status/internal/status_internal.cc"
  "status/internal/statusor_internal.h"
  "status/status.h"
  "status/status.cc"
  "status/statusor.h"
  "status/statusor.cc"
  "status/status_payload_printer.h"
  "status/status_payload_printer.cc"
  "strings/ascii.cc"
  "strings/ascii.h"
  "strings/charconv.cc"
  "strings/charconv.h"
  "strings/charset.h"
  "strings/cord.cc"
  "strings/cord.h"
  "strings/cord_analysis.cc"
  "strings/cord_analysis.h"
  "strings/cord_buffer.cc"
  "strings/cord_buffer.h"
  "strings/escaping.cc"
  "strings/escaping.h"
  "strings/internal/charconv_bigint.cc"
  "strings/internal/charconv_bigint.h"
  "strings/internal/charconv_parse.cc"
  "strings/internal/charconv_parse.h"
  "strings/internal/cord_data_edge.h"
  "strings/internal/cord_internal.cc"
  "strings/internal/cord_internal.h"
  "strings/internal/cord_rep_btree.cc"
  "strings/internal/cord_rep_btree.h"
  "strings/internal/cord_rep_btree_navigator.cc"
  "strings/internal/cord_rep_btree_navigator.h"
  "strings/internal/cord_rep_btree_reader.cc"
  "strings/internal/cord_rep_btree_reader.h"
  "strings/internal/cord_rep_crc.cc"
  "strings/internal/cord_rep_crc.h"
  "strings/internal/cord_rep_consume.h"
  "strings/internal/cord_rep_consume.cc"
  "strings/internal/cord_rep_flat.h"
  "strings/internal/cordz_functions.cc"
  "strings/internal/cordz_functions.h"
  "strings/internal/cordz_handle.cc"
  "strings/internal/cordz_handle.h"
  "strings/internal/cordz_info.cc"
  "strings/internal/cordz_info.h"
  "strings/internal/cordz_sample_token.cc"
  "strings/internal/cordz_sample_token.h"
  "strings/internal/cordz_statistics.h"
  "strings/internal/cordz_update_scope.h"
  "strings/internal/cordz_update_tracker.h"
  "strings/internal/damerau_levenshtein_distance.h"
  "strings/internal/damerau_levenshtein_distance.cc"
  "strings/internal/stl_type_traits.h"
  "strings/internal/string_constant.h"
  "strings/internal/stringify_sink.h"
  "strings/internal/stringify_sink.cc"
  "strings/has_absl_stringify.h"
  "strings/has_ostream_operator.h"
  "strings/match.cc"
  "strings/match.h"
  "strings/numbers.cc"
  "strings/numbers.h"
  "strings/str_format.h"
  "strings/str_cat.cc"
  "strings/str_cat.h"
  "strings/str_join.h"
  "strings/str_replace.cc"
  "strings/str_replace.h"
  "strings/str_split.cc"
  "strings/str_split.h"
  "strings/string_view.cc"
  "strings/string_view.h"
  "strings/strip.h"
  "strings/substitute.cc"
  "strings/substitute.h"
  "strings/internal/escaping.h"
  "strings/internal/escaping.cc"
  "strings/internal/memutil.cc"
  "strings/internal/memutil.h"
  "strings/internal/ostringstream.cc"
  "strings/internal/ostringstream.h"
  "strings/internal/pow10_helper.cc"
  "strings/internal/pow10_helper.h"
  "strings/internal/resize_uninitialized.h"
  "strings/internal/str_format/arg.cc"
  "strings/internal/str_format/arg.h"
  "strings/internal/str_format/bind.cc"
  "strings/internal/str_format/bind.h"
  "strings/internal/str_format/checker.h"
  "strings/internal/str_format/constexpr_parser.h"
  "strings/internal/str_format/extension.cc"
  "strings/internal/str_format/extension.h"
  "strings/internal/str_format/float_conversion.cc"
  "strings/internal/str_format/float_conversion.h"
  "strings/internal/str_format/output.cc"
  "strings/internal/str_format/output.h"
  "strings/internal/str_format/parser.cc"
  "strings/internal/str_format/parser.h"
  "strings/internal/str_join_internal.h"
  "strings/internal/str_split_internal.h"
  "strings/internal/utf8.cc"
  "strings/internal/utf8.h"
  "synchronization/barrier.cc"
  "synchronization/barrier.h"
  "synchronization/blocking_counter.cc"
  "synchronization/blocking_counter.h"
  "synchronization/mutex.cc"
  "synchronization/mutex.h"
  "synchronization/notification.cc"
  "synchronization/notification.h"
  "synchronization/internal/create_thread_identity.cc"
  "synchronization/internal/create_thread_identity.h"
  "synchronization/internal/futex.h"
  "synchronization/internal/futex_waiter.h"
  "synchronization/internal/futex_waiter.cc"
  "synchronization/internal/graphcycles.cc"
  "synchronization/internal/graphcycles.h"
  "synchronization/internal/kernel_timeout.h"
  "synchronization/internal/kernel_timeout.cc"
  "synchronization/internal/per_thread_sem.cc"
  "synchronization/internal/per_thread_sem.h"
  "synchronization/internal/pthread_waiter.h"
  "synchronization/internal/pthread_waiter.cc"
  "synchronization/internal/sem_waiter.h"
  "synchronization/internal/sem_waiter.cc"
  "synchronization/internal/stdcpp_waiter.h"
  "synchronization/internal/stdcpp_waiter.cc"
  "synchronization/internal/thread_pool.h"
  "synchronization/internal/waiter.h"
  "synchronization/internal/waiter_base.h"
  "synchronization/internal/waiter_base.cc"
  "synchronization/internal/win32_waiter.h"
  "synchronization/internal/win32_waiter.cc"
  "time/civil_time.cc"
  "time/civil_time.h"
  "time/clock.cc"
  "time/clock.h"
  "time/duration.cc"
  "time/format.cc"
  "time/time.cc"
  "time/time.h"
  "time/internal/cctz/include/cctz/civil_time.h"
  "time/internal/cctz/include/cctz/civil_time_detail.h"
  "time/internal/cctz/include/cctz/time_zone.h"
  "time/internal/cctz/include/cctz/zone_info_source.h"
  "time/internal/cctz/src/civil_time_detail.cc"
  "time/internal/cctz/src/time_zone_fixed.cc"
  "time/internal/cctz/src/time_zone_fixed.h"
  "time/internal/cctz/src/time_zone_format.cc"
  "time/internal/cctz/src/time_zone_if.cc"
  "time/internal/cctz/src/time_zone_if.h"
  "time/internal/cctz/src/time_zone_impl.cc"
  "time/internal/cctz/src/time_zone_impl.h"
  "time/internal/cctz/src/time_zone_info.cc"
  "time/internal/cctz/src/time_zone_info.h"
  "time/internal/cctz/src/time_zone_libc.cc"
  "time/internal/cctz/src/time_zone_libc.h"
  "time/internal/cctz/src/time_zone_lookup.cc"
  "time/internal/cctz/src/time_zone_posix.cc"
  "time/internal/cctz/src/time_zone_posix.h"
  "time/internal/cctz/src/tzfile.h"
  "time/internal/cctz/src/zone_info_source.cc"
  "types/any.h"
  "types/bad_any_cast.cc"
  "types/bad_any_cast.h"
  "types/bad_optional_access.cc"
  "types/bad_optional_access.h"
  "types/bad_variant_access.cc"
  "types/bad_variant_access.h"
  "types/compare.h"
  "types/internal/variant.h"
  "types/optional.h"
  "types/internal/optional.h"
  "types/span.h"
  "types/internal/span.h"
  "types/variant.h"
  "utility/internal/if_constexpr.h"
  "utility/utility.h"
  "debugging/leak_check.cc"
)

if(NOT MSVC)
  list(APPEND ABSL_INTERNAL_DLL_FILES
    "flags/commandlineflag.cc"
    "flags/commandlineflag.h"
    "flags/config.h"
    "flags/declare.h"
    "flags/flag.h"
    "flags/internal/commandlineflag.cc"
    "flags/internal/commandlineflag.h"
    "flags/internal/flag.cc"
    "flags/internal/flag.h"
    "flags/internal/parse.h"
    "flags/internal/path_util.h"
    "flags/internal/private_handle_accessor.cc"
    "flags/internal/private_handle_accessor.h"
    "flags/internal/program_name.cc"
    "flags/internal/program_name.h"
    "flags/internal/registry.h"
    "flags/internal/sequence_lock.h"
    "flags/internal/usage.cc"
    "flags/internal/usage.h"
    "flags/marshalling.cc"
    "flags/marshalling.h"
    "flags/parse.cc"
    "flags/parse.h"
    "flags/reflection.cc"
    "flags/reflection.h"
    "flags/usage.cc"
    "flags/usage.h"
    "flags/usage_config.cc"
    "flags/usage_config.h"
    "log/flags.cc"
    "log/flags.h"
    "log/internal/flags.h"
  )
endif()

set(ABSL_INTERNAL_DLL_TARGETS
  "absl_check"
  "absl_log"
  "absl_vlog_is_on"
  "algorithm"
  "algorithm_container"
  "any"
  "any_invocable"
  "atomic_hook"
  "bad_any_cast"
  "bad_any_cast_impl"
  "bad_optional_access"
  "bad_variant_access"
  "base"
  "base_internal"
  "bind_front"
  "bits"
  "btree"
  "check"
  "city"
  "civil_time"
  "compare"
  "compressed_tuple"
  "config"
  "container"
  "container_common"
  "container_memory"
  "cord"
  "cord_internal"
  "cordz_functions"
  "cordz_handle"
  "cordz_info"
  "cordz_sample_token"
  "core_headers"
  "counting_allocator"
  "crc_cord_state"
  "crc_cpu_detect"
  "crc_internal"
  "crc32c"
  "debugging"
  "debugging_internal"
  "demangle_internal"
  "die_if_null"
  "dynamic_annotations"
  "endian"
  "examine_stack"
  "exponential_biased"
  "failure_signal_handler"
  "fixed_array"
  "flat_hash_map"
  "flat_hash_set"
  "function_ref"
  "graphcycles_internal"
  "hash"
  "hash_function_defaults"
  "hash_policy_traits"
  "hashtable_debug"
  "hashtable_debug_hooks"
  "hashtablez_sampler"
  "inlined_vector"
  "inlined_vector_internal"
  "int128"
  "kernel_timeout_internal"
  "layout"
  "leak_check"
  "log_internal_check_impl"
  "log_internal_check_op"
  "log_internal_conditions"
  "log_internal_config"
  "log_internal_fnmatch"
  "log_internal_format"
  "log_internal_globals"
  "log_internal_log_impl"
  "log_internal_proto"
  "log_internal_message"
  "log_internal_log_sink_set"
  "log_internal_nullguard"
  "log_internal_nullstream"
  "log_internal_strip"
  "log_internal_voidify"
  "log_internal_append_truncated"
  "log_globals"
  "log_initialize"
  "log"
  "log_entry"
  "log_sink"
  "log_sink_registry"
  "log_streamer"
  "log_internal_structured"
  "log_severity"
  "log_structured"
  "low_level_hash"
  "malloc_internal"
  "memory"
  "meta"
  "node_hash_map"
  "node_hash_set"
  "node_slot_policy"
  "non_temporal_arm_intrinsics"
  "non_temporal_memcpy"
  "numeric"
  "optional"
  "periodic_sampler"
  "pow10_helper"
  "pretty_function"
  "random_bit_gen_ref"
  "random_distributions"
  "random_internal_distribution_caller"
  "random_internal_distributions"
  "random_internal_explicit_seed_seq"
  "random_internal_fastmath"
  "random_internal_fast_uniform_bits"
  "random_internal_generate_real"
  "random_internal_iostream_state_saver"
  "random_internal_nonsecure_base"
  "random_internal_pcg_engine"
  "random_internal_platform"
  "random_internal_pool_urbg"
  "random_internal_randen"
  "random_internal_randen_engine"
  "random_internal_randen_hwaes"
  "random_internal_randen_hwaes_impl"
  "random_internal_randen_slow"
  "random_internal_salted_seed_seq"
  "random_internal_seed_material"
  "random_internal_sequence_urbg"
  "random_internal_traits"
  "random_internal_uniform_helper"
  "random_internal_wide_multiply"
  "random_random"
  "random_seed_gen_exception"
  "random_seed_sequences"
  "raw_hash_map"
  "raw_hash_set"
  "raw_logging_internal"
  "sample_recorder"
  "scoped_set_env"
  "span"
  "spinlock_wait"
  "spy_hash_state"
  "stack_consumption"
  "stacktrace"
  "status"
  "statusor"
  "str_format"
  "str_format_internal"
  "strerror"
  "strings"
  "strings_internal"
  "string_view"
  "symbolize"
  "synchronization"
  "thread_pool"
  "throw_delegate"
  "time"
  "time_zone"
  "tracked"
  "type_traits"
  "utility"
  "variant"
  "vlog_config_internal"
  "vlog_is_on"
)

if(NOT MSVC)
  list(APPEND ABSL_INTERNAL_DLL_TARGETS
    "flags"
    "flags_commandlineflag"
    "flags_commandlineflag_internal"
    "flags_config"
    "flags_internal"
    "flags_marshalling"
    "flags_parse"
    "flags_path_util"
    "flags_private_handle_accessor"
    "flags_program_name"
    "flags_reflection"
    "flags_usage"
    "flags_usage_internal"
    "log_internal_flags"
    "log_flags"
  )
endif()

set(ABSL_INTERNAL_TEST_DLL_FILES
  "hash/hash_testing.h"
  "log/scoped_mock_log.cc"
  "log/scoped_mock_log.h"
  "random/internal/chi_square.cc"
  "random/internal/chi_square.h"
  "random/internal/distribution_test_util.cc"
  "random/internal/distribution_test_util.h"
  "random/internal/mock_helpers.h"
  "random/internal/mock_overload_set.h"
  "random/mocking_bit_gen.h"
  "random/mock_distributions.h"
  "status/status_matchers.h"
  "status/internal/status_matchers.cc"
  "status/internal/status_matchers.h"
  "strings/cordz_test_helpers.h"
  "strings/cord_test_helpers.h"
)

set(ABSL_INTERNAL_TEST_DLL_TARGETS
  "cord_test_helpers"
  "cordz_test_helpers"
  "hash_testing"
  "random_mocking_bit_gen"
  "random_internal_distribution_test_util"
  "random_internal_mock_overload_set"
  "scoped_mock_log"
  "status_matchers"
)

include(CheckCXXSourceCompiles)

check_cxx_source_compiles(
  [==[
#ifdef _MSC_VER
#  if _MSVC_LANG < 201703L
#    error "The compiler defaults or is configured for C++ < 17"
#  endif
#elif __cplusplus < 201703L
#  error "The compiler defaults or is configured for C++ < 17"
#endif
int main() { return 0; }
]==]
  ABSL_INTERNAL_AT_LEAST_CXX17)

check_cxx_source_compiles(
  [==[
#ifdef _MSC_VER
#  if _MSVC_LANG < 202002L
#    error "The compiler defaults or is configured for C++ < 20"
#  endif
#elif __cplusplus < 202002L
#  error "The compiler defaults or is configured for C++ < 20"
#endif
int main() { return 0; }
]==]
  ABSL_INTERNAL_AT_LEAST_CXX20)

if(ABSL_INTERNAL_AT_LEAST_CXX20)
  set(ABSL_INTERNAL_CXX_STD_FEATURE cxx_std_20)
elseif(ABSL_INTERNAL_AT_LEAST_CXX17)
  set(ABSL_INTERNAL_CXX_STD_FEATURE cxx_std_17)
else()
  set(ABSL_INTERNAL_CXX_STD_FEATURE cxx_std_14)
endif()

function(absl_internal_dll_contains)
  cmake_parse_arguments(ABSL_INTERNAL_DLL
    ""
    "OUTPUT;TARGET"
    ""
    ${ARGN}
  )

  STRING(REGEX REPLACE "^absl::" "" _target ${ABSL_INTERNAL_DLL_TARGET})

  if (_target IN_LIST ABSL_INTERNAL_DLL_TARGETS)
    set(${ABSL_INTERNAL_DLL_OUTPUT} 1 PARENT_SCOPE)
  else()
    set(${ABSL_INTERNAL_DLL_OUTPUT} 0 PARENT_SCOPE)
  endif()
endfunction()

function(absl_internal_test_dll_contains)
  cmake_parse_arguments(ABSL_INTERNAL_TEST_DLL
    ""
    "OUTPUT;TARGET"
    ""
    ${ARGN}
  )

  STRING(REGEX REPLACE "^absl::" "" _target ${ABSL_INTERNAL_TEST_DLL_TARGET})

  if (_target IN_LIST ABSL_INTERNAL_TEST_DLL_TARGETS)
    set(${ABSL_INTERNAL_TEST_DLL_OUTPUT} 1 PARENT_SCOPE)
  else()
    set(${ABSL_INTERNAL_TEST_DLL_OUTPUT} 0 PARENT_SCOPE)
  endif()
endfunction()

function(absl_internal_dll_targets)
  cmake_parse_arguments(ABSL_INTERNAL_DLL
  ""
  "OUTPUT"
  "DEPS"
  ${ARGN}
  )

  set(_deps "")
  foreach(dep IN LISTS ABSL_INTERNAL_DLL_DEPS)
    absl_internal_dll_contains(TARGET ${dep} OUTPUT _dll_contains)
    absl_internal_test_dll_contains(TARGET ${dep} OUTPUT _test_dll_contains)
    if (_dll_contains)
      list(APPEND _deps abseil_dll)
    elseif (_test_dll_contains)
      list(APPEND _deps abseil_test_dll)
    else()
      list(APPEND _deps ${dep})
    endif()
  endforeach()

  # Because we may have added the DLL multiple times
  list(REMOVE_DUPLICATES _deps)
  set(${ABSL_INTERNAL_DLL_OUTPUT} "${_deps}" PARENT_SCOPE)
endfunction()

function(absl_make_dll)
  cmake_parse_arguments(ABSL_INTERNAL_MAKE_DLL
  ""
  "TEST"
  ""
  ${ARGN}
  )

  if (ABSL_INTERNAL_MAKE_DLL_TEST)
    set(_dll "abseil_test_dll")
    set(_dll_files ${ABSL_INTERNAL_TEST_DLL_FILES})
    set(_dll_libs "abseil_dll" "GTest::gtest" "GTest::gmock")
    set(_dll_compile_definitions "GTEST_LINKED_AS_SHARED_LIBRARY=1")
    set(_dll_includes ${absl_gtest_src_dir}/googletest/include ${absl_gtest_src_dir}/googlemock/include)
    set(_dll_consume "ABSL_CONSUME_TEST_DLL")
    set(_dll_build "ABSL_BUILD_TEST_DLL")
  else()
    set(_dll "abseil_dll")
    set(_dll_files ${ABSL_INTERNAL_DLL_FILES})
    set(_dll_libs
      Threads::Threads
      # TODO(#1495): Use $<LINK_LIBRARY:FRAMEWORK,CoreFoundation> once our
      # minimum CMake version >= 3.24
      $<$<PLATFORM_ID:Darwin>:-Wl,-framework,CoreFoundation>
    )
    set(_dll_compile_definitions "")
    set(_dll_includes "")
    set(_dll_consume "ABSL_CONSUME_DLL")
    set(_dll_build "ABSL_BUILD_DLL")
  endif()

  add_library(
    ${_dll}
    SHARED
      ${_dll_files}
  )
  target_link_libraries(
    ${_dll}
    PRIVATE
      ${_dll_libs}
      ${ABSL_DEFAULT_LINKOPTS}
  )
  set_target_properties(${_dll} PROPERTIES
    LINKER_LANGUAGE "CXX"
    SOVERSION ${ABSL_SOVERSION}
  )
  target_include_directories(
    ${_dll}
    PUBLIC
      "$<BUILD_INTERFACE:${ABSL_COMMON_INCLUDE_DIRS}>"
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      ${_dll_includes}
  )

  target_compile_options(
    ${_dll}
    PRIVATE
      ${ABSL_DEFAULT_COPTS}
  )

  foreach(cflag ${ABSL_CC_LIB_COPTS})
    if(${cflag} MATCHES "^(-Wno|/wd)")
      # These flags are needed to suppress warnings that might fire in our headers.
      set(PC_CFLAGS "${PC_CFLAGS} ${cflag}")
    elseif(${cflag} MATCHES "^(-W|/w[1234eo])")
      # Don't impose our warnings on others.
    else()
      set(PC_CFLAGS "${PC_CFLAGS} ${cflag}")
    endif()
  endforeach()
  string(REPLACE ";" " " PC_LINKOPTS "${ABSL_CC_LIB_LINKOPTS}")

  FILE(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/lib/pkgconfig/${_dll}.pc" CONTENT "\
prefix=${CMAKE_INSTALL_PREFIX}\n\
exec_prefix=\${prefix}\n\
libdir=${CMAKE_INSTALL_FULL_LIBDIR}\n\
includedir=${CMAKE_INSTALL_FULL_INCLUDEDIR}\n\
\n\
Name: ${_dll}\n\
Description: Abseil DLL library\n\
URL: https://abseil.io/\n\
Version: ${absl_VERSION}\n\
Libs: -L\${libdir} $<$<NOT:$<BOOL:${ABSL_CC_LIB_IS_INTERFACE}>>:-l${_dll}> ${PC_LINKOPTS}\n\
Cflags: -I\${includedir}${PC_CFLAGS}\n")
  INSTALL(FILES "${CMAKE_BINARY_DIR}/lib/pkgconfig/${_dll}.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

  target_compile_definitions(
    ${_dll}
    PUBLIC
      ${_dll_compile_definitions}
    PRIVATE
      ${_dll_build}
      NOMINMAX
    INTERFACE
      ${ABSL_CC_LIB_DEFINES}
      ${_dll_consume}
  )

  if(ABSL_PROPAGATE_CXX_STD)
    # Abseil libraries require C++14 as the current minimum standard. When
    # compiled with a higher minimum (either because it is the compiler's
    # default or explicitly requested), then Abseil requires that standard.
    target_compile_features(${_dll} PUBLIC ${ABSL_INTERNAL_CXX_STD_FEATURE})
  endif()

  install(TARGETS ${_dll} EXPORT ${PROJECT_NAME}Targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  add_library(absl::${_dll} ALIAS ${_dll})
endfunction()
