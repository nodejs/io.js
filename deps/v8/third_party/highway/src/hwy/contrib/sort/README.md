# Vectorized and performance-portable Quicksort

## Introduction

As of 2022-06-07 this sorts large arrays of built-in types about ten times as
fast as LLVM's `std::sort`. Note that other algorithms such as pdqsort can be
about twice as fast as LLVM's std::sort as of 2023-06.

See also our
[blog post](https://opensource.googleblog.com/2022/06/Vectorized%20and%20performance%20portable%20Quicksort.html)
and [paper](https://arxiv.org/abs/2205.05982).

## Instructions

Here are instructions for reproducing our results with cross-platform CMake,
Linux, or AWS (SVE, NEON).

### CMake, any platform

Please first ensure that Clang (tested with 13.0.1 and 15.0.6) is installed, and
if it is not the default compiler, point the CC and CXX environment variables to
it, e.g.

```
export CC=clang-15
export CXX=clang++-15
```

Then run the usual CMake workflow, also documented in the Highway README, e.g.:

```
mkdir -p build && cd build && cmake .. && make -j
taskset -c 2 tests/bench_sort
```

The optional `taskset -c 2` part reduces the variability of measurements by
preventing the OS from migrating the benchmark between cores.

### Linux

Please first ensure golang, and Clang (tested with 13.0.1) are installed via
your system's package manager.

```
go install github.com/bazelbuild/bazelisk@latest
git clone https://github.com/google/highway
cd highway
CC=clang CXX=clang++ ~/go/bin/bazelisk build -c opt hwy/contrib/sort:all
bazel-bin/hwy/contrib/sort/sort_test
bazel-bin/hwy/contrib/sort/bench_sort
```

### AWS Graviton3

Instance config: amazon linux 5.10 arm64, c7g.8xlarge (largest allowed config is
32 vCPU). Initial launch will fail. Wait a few minutes for an email saying the
config is verified, then re-launch. See IPv4 hostname in list of instances.

`ssh -i /path/key.pem ec2-user@hostname`

Note that the AWS CMake package is too old for llvm, so we build it first:
```
wget https://cmake.org/files/v3.23/cmake-3.23.2.tar.gz
tar -xvzf cmake-3.23.2.tar.gz && cd cmake-3.23.2/
./bootstrap -- -DCMAKE_USE_OPENSSL=OFF
make -j8 && sudo make install
cd ..
```

AWS clang is at version 11.1, which generates unnecessary `AND` instructions
which slow down the sort by 1.15x. We tested with clang trunk as of June 13
(which reports Git hash 8f6512fea000c3a0d394864bb94e524bee375069). To build:

```
git clone --depth 1 https://github.com/llvm/llvm-project.git
cd llvm-project
mkdir -p build && cd build
/usr/local/bin/cmake ../llvm -DLLVM_ENABLE_PROJECTS="clang" -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" -DCMAKE_BUILD_TYPE=Release
make -j32 && sudo make install
```

```
sudo yum install go
go install github.com/bazelbuild/bazelisk@latest
git clone https://github.com/google/highway
cd highway
CC=/usr/local/bin/clang CXX=/usr/local/bin/clang++ ~/go/bin/bazelisk build -c opt --copt=-march=armv8.2-a+sve hwy/contrib/sort:all
bazel-bin/hwy/contrib/sort/sort_test
bazel-bin/hwy/contrib/sort/bench_sort
```

The above command line enables SVE, which is currently only available on
Graviton 3. You can also test NEON on the same processor, or other Arm CPUs, by
changing the `-march=` option to `--copt=-march=armv8.2-a+crypto`. Note that
such flags will be unnecessary once Clang supports `#pragma target` for NEON and
SVE intrinsics, as it does for x86.

## Results

`bench_sort` outputs the instruction set (AVX3 refers to AVX-512), the sort
algorithm (std for `std::sort`, vq for our vqsort), the type of keys being
sorted (f32 is float), the distribution of keys (uniform32 for uniform random
with range 0-2^32), the number of keys, then the throughput of sorted keys (i.e.
number of key bytes output per second).

Example excerpt from Xeon 6154 (Skylake-X) CPU clocked at 3 GHz:

```
[ RUN      ] BenchSortGroup/BenchSort.BenchAllSort/AVX3
      AVX3:          std:     f32: uniform32: 1.00E+06   54 MB/s ( 1 threads)
      AVX3:           vq:     f32: uniform32: 1.00E+06 1143 MB/s ( 1 threads)
```

## Additional results

Thanks to Lukas Bergdoll, who did a thorough [performance analysis](https://github.com/Voultapher/sort-research-rs/blob/main/writeup/intel_avx512/text.md)
on various sort implementations. This helped us identify a performance bug,
caused by obtaining entropy from the OS on each call. This was fixed in #1334
and we look forward to the updated results.

### Optimizations for small arrays

Our initial focus was on large arrays. Since the VQSort paper was published,
we have improved its performance for small arrays:

-   Previously, each call to VQSort obtained entropy from the OS. Unpredictable
    seeding does help avoid worst-cases, and the cost is negligible when the
    input size is at least 100K elements. However, the overhead is very costly
    for arrays of just 100 or 1000, so we now obtain entropy only once per
    thread and cache the seeds in TLS. This significantly improves the
    performance on subsequent calls. Users can also explicitly initialize the
    random generator.

-   We also improved the efficiency of our sorting network for inputs shorter
    than half its size. Our approach avoids costly transposes by interpreting
    inputs as a 2D matrix. Previously, we always used 16 rows, which means only
    a single vector lane is active for up to 16 elements. We have added 8x2 and
    8x4 networks which use more lanes when available, and also 4x1 and 8x1
    networks for very small inputs.

-   Previously we also loaded (overlapping) full vectors, with the offsets
    determined by the number of columns. Now we use the minimum vector size
    sufficient for the number of columns, which enables higher IPC on Skylake
    and reduces the cost of unaligned loads.

    Unfortunately this decreases code reuse; VQSort now consists of about 1500
    instructions (https://gcc.godbolt.org/z/ojYKfjPe6). The size of sorting
    networks has nearly doubled to 10.8 KiB, 70% of the total. Although large,
    this still fits comfortably within 32 KiB instruction caches, and possibly
    even in micro-op caches (DSB, 1500-2300 micro-ops), especially given that
    not all instructions are guaranteed to execute.

### Study of AVX-512 downclocking
We study whether AVX-512 downclocking affects performance. Using the GHz
reported by perf, we find an upper bound on the effects of downclocking, and
observe that its effect is negligible when compared to scalar code.

This issue has somehow attracted far more attention than seems warranted. An
attempt by Daniel Lemire to measure the
[worst-case](https://lemire.me/blog/2018/08/15/the-dangers-of-avx-512-throttling-a-3-impact/)
only saw a **3% decrease**, and Intel CPUs since Icelake, as well as AMD Zen4,
are much less impacted by throttling, if at all. By contrast, "Silver" and
"Bronze" Intel Xeons have more severe throttling and would require a large(r)
speedup from AVX-512 to outweigh the downclocking. However, these CPUs are
marketed towards "entry compute, network and storage" and "small business and
storage server solutions", and are thus less suitable for the high-performance
workloads we consider.

Our test workstation runs Linux (6.1.20-2rodete1-amd64) and has the same Xeon
Gold 6154 CPU used in our paper because its Skylake microarchitecture is the
most (potentially) affected. The compiler is a Clang similar to the LLVM trunk.

We added a new 'cold' benchmark that initializes random seeds, fills an array
with a constant except at one random index, calls VQSort, and then prints a
random element to ensure the computations are not elided. To run it, we build
bench_sort with `-DSORT_ONLY_COLD=1` and then invoke
`taskset -c 6 setarch -R x86_64 perf stat -r 15 -d bench_sort`. The taskset and
setarch serve to reduce variability by avoiding thread migration, and disabling
address space randomization. `-r 15` requests 15 runs so that perf can display
the variability of the measurements: < 1% for cycles, instructions, L1 dcache
loads; LLC miss variability is much higher (> 10%) presumably due to the
remaining background activity on this machine.

For our measurements, we use the GHz value reported by `perf`. This does not
include time spent in the kernel, and is thus noisy for short runtimes. Note
that running `perf` under `sudo` is not an option because it results in
"Workload failed: Cannot allocate memory". We see results between 2.6 - 2.9 GHz
when running AVX-512 code. This is relative to 3.0 GHz nominal; we disabled
Turbo Boost via MSR and ran `sudo cpupower frequency-set --governor performance`
to prevent unnecessary frequency reductions. To the best of our knowledge, the
remaining gap is explained by time spent in the kernel (in particular handling
page faults) and downclocking. Thus an *upper-bound* for the latter is
(3 - 2.9)/3 to (3 - 2.6)/3, or **1.03 - 1.13x**. Such a frequency reduction
would already be negligible compared to the 2-4x increase in work per cycle from
512-bit SIMD relative to 256 or 128-bit SIMD, which is typically less or not at
all affected by downclocking.

To further tighten this bound, we compare AVX-512 code vs. non-AVX-512 code, in
the form of `std::sort`. Ensuring the remainder of the binary does not use
AVX-512 is nontrivial. Library functions such as `memset` are known to use
AVX-512, and they would not show up in a disassembly of our binary. Neither
would they raise exceptions if run on a CPU lacking AVX-512 support, because
software typically verifies CPU support before running AVX-512. As a first step,
we take care to avoid calls to such library functions in our test, which is more
feasible with a self-contained small binary. In particular, array
zero-initialization typically compiles to `memset` (verified with clang-16), so
we manually initialize the array to the return value of an `Unpredictable1`
function whose implementation is not visible to the compiler. This indeed
compiles to a scalar loop. To further increase confidence that the binary lacks
AVX-512 instructions before VQSort, we replace the initialization loop with
AVX-512 stores. This indeed raises the measured throughput from a fairly
consistent 9 GB/s to 9-15 GB/s, likely because some of the AVX-512 startup now
occurs outside of our timings. We examine this effect in the next section, but
for now we can conclude that because adding AVX-512 makes a difference, the
binary was otherwise not using it. Now we can revert to scalar initialization
and compare the GHz reported for VQSort vs. `std::sort`. Across three runs, the
ranges are 2.8-2.9 and 2.8-2.8 GHz. Thus we conclude: if there is any
downclocking for a single core running AVX-512 on this Skylake-X CPU, the effect
is **under the noise floor of our measurement**, and certainly far below any
speedup one can reasonably predict from 512-bit SIMD. We expect this result to
generalize to AMD Zen4 and any Gold/Platinum Intel Xeon.

### Study of AVX-512 startup overhead

In the previous section, we saw that downclocking is negligible on our system,
but there is a noticeable benefit to warming up AVX-512 before the sort. To
understand why, we refer to Travis Downs' excellent
[measurements](https://travisdowns.github.io/blog/2020/01/17/avxfreq1.html#summary)
of how Skylake reacts to an AVX-512 instruction: 8-20 us of reduced instruction
throughput, an additional potential halt of 10 us, and then downclocking.
Note that downclocking is negligible on a single core per the previous section.

We choose the array length of 10K unsigned 64-bit keys such that VQSort
completes in 7-10 us. Thus in this benchmark, VQSort (almost) finishes before
AVX-512 is fully warmed up, and the speedup is reduced because the startup costs
are amortized over relatively little data. Across five series of 15 runs, the
average of average throughputs is 9.3 GB/s, implying a runtime of 8.6 us
including startup costs.

Note that the two-valued, almost all-equal input distribution is quite skewed.
The above throughput does not reflect the performance attainable on other
distributions, especially uniform random. However, this choice is deliberate
because Quicksort can terminate early if all values in a partition are equal.
When measuring such a 'best-case' input, we are more likely to observe the cost
of startup overhead in surrounding code. Otherwise, this overhead might be
hidden by the increase in sorting time.

Now let us compare this throughput to the previously mentioned measurement with
AVX-512 warmed up (via slow scatter instructions so that initialization takes
about 100 us, well in excess of the warmup period): 15.2 GB/s, or 5.3 us without
startup cost. It appears the 10 us halt is not happening, possibly because we do
not use SIMD floating-point nor multiplication instructions. Thus we only
experience reduced instruction throughput and/or increased latency. The ratio
between cold and warmed-up time is only 1.6, which is plausible if the Skylake
throttling is actually rounding latencies up to a multiple of four cycles, as
Downs speculates. Indeed a large fraction of the SIMD instructions especially in
the VQSort base case are cross-lane or 64-bit min/max operations with latencies
of 3 cycles on Skylake, so their slowdown might only be 1.3x. The measured 1.6x
could plausibly derive from 7/8 of 1.3x and 1/8 of 4x for single-cycle latency
instructions.

Assuming this understanding of AVX-512 startup cost is valid, how long does it
remain active before the CPU reverts to the previous settings? The CPU cannot
know what future instructions are coming, and to prevent unnecessary
transitions, it has a hysteresis (delay after the last AVX-512 instruction
before shutting down) which Downs measures as 680 us. Thus our benchmark
subsequently sleeps for 100 ms to ensure the next run of the binary sees the
original CPU state. Indeed we find for the five series that the slopes of the
lines of best fit are negative in one case, positive in two, and flat in two,
indicating there is no consistent pattern of benefit for earlier or later runs.

What are the implications for users of VQSort? If the surrounding code executes
an AVX-512 instruction at least every 500 us, then AVX-512 remains active and
**any call to VQSort will benefit from it, no matter how small the input**.
This is a reasonable expectation for modern systems whose designers were aware
of data-oriented programming principles, because many (though not all) domains
and operations can benefit from SIMD. By contrast, consider the case of dropping
VQSort into an existing legacy system that does not yet use SIMD. In the case of
10K input sizes, we still observe a 2.3x speedup vs. `std::sort`. However, the
following code may have to deal with throttling for the remainder of the 20 us
startup period. With VQSort we have 8.6 us runtime plus up to 11.4 us throttled
code (potentially running at quarter speed) plus the remaining 3/4 of 11.4 for a
total of 28.6. With `std::sort` we have 19.5 us runtime plus 20 us of normal
subsequent code, or 39.5 us. Thus the overall speedup for the 20 us region plus
VQSort **shrinks to 1.4x**, and it is possible to imagine an actual slowdown for
sufficiently small inputs, when factoring in the throttling of subsequent code.
This unfortunate 'beggar thy neighbor' effect cannot be solved at the level of
individual building blocks such as a sort, and must instead be addressed at the
system level. For example:

-   vectorizing more and more parts of the code to amortize startup cost;
-   relying on newer CPUs than Skylake (launched 2015!) which have little or no
    AVX-512 startup overhead, such as Intel Icelake (2021) or AMD Zen4 (2022);
-   ensuring sorts (or anything else using AVX-512) process at least 100 KiB
    of data, such that the expected speedup outweighs any startup cost.

Any of these solutions are sufficient to render AVX-512 startup overhead a
non-issue.

### Comparison with Intel's x86-simd-sort and vxsort

Our May 2022 paper compared performance with `ips4o` and `std::sort`. We now add
results for Intel's [x86-simd-sort](https://github.com/intel/x86-simd-sort),
released as open source around October 2022, and
[vxsort](https://github.com/damageboy/vxsort-cpp/tree/master). We find that
VQSort is generally about 1.4 times as fast as either, and in a few cases equal
or up to 2% slower.

Note that vxsort was open-sourced around May 2020; we were unaware of it at the
time of writing because it had been published in the form of a blog series. We
imported both from Github on 2023-06-06 at about 10:15 UTC. Both are integrated
into our bench_sort, running on the same Linux OS and Xeon 6154 CPU mentioned
above. We use uniform random inputs, because vxsort and x86-simd-sort appear to
have much less robust handling of skewed input distributions. They choose the
pivot as the median of three keys, or of 64 bytes, respectively. By contrast,
VQSort draws a 384 byte sample and analyzes their distribution, which improves
load balance and prevents recursing into all-equal partitions. Lacking this, the
other algorithms are more vulnerable to worst-cases. Choosing uniform random
thus prevents disadvantaging the other algorithms.

We sample performance across a range of input sizes and types:

-   To isolate the performance of the sorting networks used by all three
    algorithms, we start with powers of two up to 128. VQSort is generally the
    fastest for 64-bit keys with the following exceptions: tie with vxsort at
    N=2 (537 MB/s), slower than vxsort at N=16 (2114 vs. 2147), tie with
    x86-simd-sort at N=32 (2643 MB/s). Note that VQSort is about 1.6 times as
    fast as both others for N=128; possibly because its 2D structure enables
    larger networks.

-   The `kPow10` mode in bench_sort measures power of ten input sizes between
    10 and 100K. Note that this covers non-power of two sizes, as well as the
    crossover point between sorting networks and Quicksort recursion. The
    speedups of VQSort relative to x86-simd-sort range from 1.33 to 1.81
    (32-bit keys), and 1.25 to 1.68 (64-bit keys), with geomeans of 1.48 and
    1.44. The speedups of VQSort relative to vxsort range from 1.08 to 2.10
    (32-bit keys), and 1.00 to 1.47 (64-bit keys), with geomeans of 1.41 and
    1.20. Note that vxsort matches VQSort at 10 64-bit elements; in all other
    cases, VQSort is strictly faster.

-   Finally, we study the effect of key type at a fixed input size of 10K
    elements. x86-simd-sort requires AVX512-VBMI2 for int16, which our CPU does
    not support. Also, both other algorithms do not support 128-bit keys, thus
    we only consider 32/64-bit integer and float types. The results in MB/s are:

    |Type|VQSort|x86-simd-sort|vxsort|
    |---|---|---|---|
    |f32|**1551**| 798| 823|
    |f64|**1773**|1147| 745|
    |i32|**1509**|1042| 968|
    |i64|**1365**|1043|1145|

    VQSort is the fastest for each type, in some cases even about twice as fast.
    Interestingly, vxsort performs at its best on i64, whereas the others are at
    their best for f64. A potential explanation is that this CPU can execute two
    f64 min/max per cycle, but only one i64.

In conclusion, VQSort is generally more efficient than vxsort and x86-simd-sort
across a range of input sizes and types. Occasionally, it is up to 2% slower,
but the geomean of its speedup (32-bit keys and power-of-ten sizes) vs. vxsort
is **1.41**, and **1.48** vs. x86-simd-sort.
