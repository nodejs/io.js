# Unroller

All contents of the `unroller` folder are experimental and subject to changes.

`Unroller` is a templated function that automatically implements common optimizations that are usually handled by compilers when writing scalar code. Modern CPUs operate much more efficiently when non-dependent calculations are packed into an instruction pipeline. For scalar code, this often means a compiler will take a one-line loop, and compile it down to hundreds of lines of machine code in order to fully capture these efficiencies. 

As of today (2023-07-06), compilers are not nearly as good at implementing these optimizations for code written in SIMD intrinsics. `Unroller` is a templated function that takes in an `UnrollerUnit` of SIMD instructions, and then implements unrolling, reordering, hoisting and tail-handling (URHT optimizations) of arrays of data being processed with SIMD intrinsics. 

### `UnrollerUnit`

`UnrollerUnit` and `UnrollerUnit2D` are a base classes of functions that `Unroller` needs implemented in order to properly handle URHT. `UnrollerUnit` has default implementations for all but the `Func` method, which defines the SIMD operation to be applied. Many examples of how to implement these functions are in the tests. 

### Doubling values of an array example

```
struct DoubleUnit : UnrollerUnit<DoubleUnit, int, int> {
  using TT = ScalableTag<int>;
  inline Vec<TT> Func(ptrdiff_t idx, Vec<TT> x, Vec<TT> y) {
    TT d;
    return Mul(x, Set(d, 2));
  }
};
```

Leaving all other methods in their default state, the following code will double all the values in array `a` and place them in `r`

```
DoubleUnit dblunit;
int r[N];
Unroller(dblunit, a, r, N);
```