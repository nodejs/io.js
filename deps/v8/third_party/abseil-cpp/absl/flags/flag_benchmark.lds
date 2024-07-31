/* This linker script forces the flags used by flags_benchmark
 * into a separate page-aligned section. This isn't necessary for
 * correctness but ensures that the benchmark results are more
 * reproducible across unrelated code changes.
 */
SECTIONS {
  .benchmark_flags : {
    . = ALIGN(0x1000);
    * (.benchmark_flags);
  }
}

INSERT AFTER .data
