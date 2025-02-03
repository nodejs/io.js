;; expected = R"---(;; This is a polyglot C++/WAT file.
;; Comment lines are ignored and not expected in the disassembler output.
(module
  ;; Imports.
  (global $env.imported_global (;0;) (import "env" "imported_global") i32)
  (func $env.imported_function (;0;) (import "env" "imported_function"))
  ;; Table and memory sections.
  (table $table0 4 funcref)
  (memory $memory0 0 1)
  ;; Non-imported globals: mutable, non-mutable, exported.
  (global $global1 (mut i32) (i32.const 0))
  (global $global2 i32 (global.get $env.imported_global))
  (global $exported_global (;3;) (export "exported_global") i64 (i64.const 0))
  ;; Element section for table initialization.
  (elem $elem0 (i32.const 0) (ref func) (ref.func $env.imported_function) (ref.func $func1) (ref.func $func1) (ref.func $func3))
  ;; Instructions on globals, locals, parameters.
  (func $func1 (param $var0 i32)
    (local $var1 i64)
    local.get $var0
    local.tee $var0
    local.set $var0
    local.get $var1
    local.tee $var1
    local.set $var1
    global.get $global1
    global.set $global1
  )
  ;; i32 operations.
  (func $func2 (result i32)
    ;; Comparisons and constant literals.
    i32.const 0
    i32.eqz
    i32.const 1
    i32.eq
    i32.const -1
    i32.ne
    i32.const 2147483647
    i32.lt_s
    i32.const -2147483648
    i32.lt_u
    i32.const 0
    i32.gt_s
    i32.const 0
    i32.gt_u
    i32.const 0
    i32.le_s
    i32.const 0
    i32.le_u
    i32.const 0
    i32.ge_s
    i32.const 0
    i32.ge_u
    ;; Bitcounting.
    i32.clz
    i32.ctz
    i32.popcnt
    ;; Arithmetic and logic.
    i32.const 0
    i32.add
    i32.const 0
    i32.sub
    i32.const 0
    i32.mul
    i32.const 0
    i32.div_s
    i32.const 0
    i32.div_u
    i32.const 0
    i32.rem_s
    i32.const 0
    i32.rem_u
    i32.const 0
    i32.and
    i32.const 0
    i32.or
    i32.const 0
    i32.xor
    i32.const 0
    i32.shl
    i32.const 0
    i32.shr_s
    i32.const 0
    i32.shr_u
    i32.const 0
    i32.rotl
    i32.const 0
    i32.rotr
    drop
    ;; Conversions.
    i64.const 0
    i32.wrap_i64
    drop
    f32.const 0.0
    i32.trunc_f32_s
    drop
    f32.const 0.0
    i32.trunc_f32_u
    drop
    f64.const 0.0
    i32.trunc_f64_s
    drop
    f64.const 0.0
    i32.trunc_f64_u
    drop
    f32.const 0.0
    i32.reinterpret_f32
    i32.extend8_s
    i32.extend16_s
  )
  ;; i64 operations.
  (func $func3 (result i64)
    ;; Comparisons and constant literals.
    i64.const 0
    i64.eqz
    drop
    i64.const 1
    i64.const -1
    i64.eq
    drop
    i64.const -9223372036854775808
    i64.const 9223372036854775807
    i64.ne
    drop
    i64.const 0
    i64.const 0
    i64.lt_s
    drop
    i64.const 0
    i64.const 0
    i64.lt_u
    drop
    i64.const 0
    i64.const 0
    i64.gt_s
    drop
    i64.const 0
    i64.const 0
    i64.gt_u
    drop
    i64.const 0
    i64.const 0
    i64.le_s
    drop
    i64.const 0
    i64.const 0
    i64.le_u
    drop
    i64.const 0
    i64.const 0
    i64.ge_s
    drop
    i64.const 0
    i64.const 0
    i64.ge_u
    drop
    ;; Bitcounting.
    i64.const 0
    i64.clz
    i64.ctz
    i64.popcnt
    ;; Arithmetic and logic.
    i64.const 0
    i64.add
    i64.const 0
    i64.sub
    i64.const 0
    i64.mul
    i64.const 0
    i64.div_s
    i64.const 0
    i64.div_u
    i64.const 0
    i64.rem_s
    i64.const 0
    i64.rem_u
    i64.const 0
    i64.and
    i64.const 0
    i64.or
    i64.const 0
    i64.xor
    i64.const 0
    i64.shl
    i64.const 0
    i64.shr_s
    i64.const 0
    i64.shr_u
    i64.const 0
    i64.rotl
    i64.const 0
    i64.rotr
    drop
    ;; Conversions.
    i32.const 0
    i64.extend_i32_s
    drop
    i32.const 0
    i64.extend_i32_u
    drop
    f32.const 0.0
    i64.trunc_f32_s
    drop
    f32.const 0.0
    i64.trunc_f32_u
    drop
    f64.const 0.0
    i64.trunc_f64_s
    drop
    f64.const 0.0
    i64.trunc_f64_u
    drop
    f64.const 0.0
    i64.reinterpret_f64
    i64.extend8_s
    i64.extend16_s
    i64.extend32_s
  )
  ;; f32 operations.
  (func $func4 (result f32)
    ;; Comparisons and constant literals.
    f32.const 0.0
    f32.const -0.0
    f32.eq
    drop
    f32.const 1
    f32.const -1
    f32.ne
    drop
    f32.const inf
    f32.const -inf
    f32.lt
    drop
    f32.const nan
    f32.const -nan
    f32.gt
    drop
    ;; Non-canonical NaN encodings.
    f32.const +nan:0x1
    f32.const +nan:0xfffff
    f32.le
    drop
    ;; TODO(dlehmann): Change to `0.1`, once `ImmediatesPrinter` is improved to
    ;; print floats as shortest round-trippable decimal representation.
    f32.const 0.100000001
    f32.const 1234567.5
    f32.ge
    drop
    ;; Arithmetic.
    f32.const 0.0
    f32.abs
    f32.neg
    f32.ceil
    f32.floor
    f32.trunc
    f32.nearest
    f32.sqrt
    f32.const 0.0
    f32.add
    f32.const 0.0
    f32.sub
    f32.const 0.0
    f32.mul
    f32.const 0.0
    f32.div
    f32.const 0.0
    f32.min
    f32.const 0.0
    f32.max
    f32.const 0.0
    f32.copysign
    drop
    ;; Conversions.
    i32.const 0
    f32.convert_i32_s
    drop
    i32.const 0
    f32.convert_i32_u
    drop
    i64.const 0
    f32.convert_i64_s
    drop
    i64.const 0
    f32.convert_i64_u
    drop
    f64.const 0.0
    f32.demote_f64
    drop
    i32.const 0
    f32.reinterpret_i32
  )
  ;; f64 operations.
  (func $func5 (result f64)
    ;; Comparisons and constant literals.
    f64.const 0.0
    f64.const -0.0
    f64.eq
    drop
    f64.const 1
    f64.const -1
    f64.ne
    drop
    f64.const inf
    f64.const -inf
    f64.lt
    drop
    f64.const nan
    f64.const -nan
    f64.gt
    drop
    ;; Non-canonical NaN encodings.
    f64.const +nan:0x1
    f64.const +nan:0xfffffffffffff
    f64.le
    drop
    f64.const 0.1
    f64.const 1234567.5
    f64.ge
    drop
    ;; Arithmetic.
    f64.const 0.0
    f64.abs
    f64.neg
    f64.ceil
    f64.floor
    f64.trunc
    f64.nearest
    f64.sqrt
    f64.const 0.0
    f64.add
    f64.const 0.0
    f64.sub
    f64.const 0.0
    f64.mul
    f64.const 0.0
    f64.div
    f64.const 0.0
    f64.min
    f64.const 0.0
    f64.max
    f64.const 0.0
    f64.copysign
    drop
    ;; Conversions.
    i32.const 0
    f64.convert_i32_s
    drop
    i32.const 0
    f64.convert_i32_u
    drop
    i64.const 0
    f64.convert_i64_s
    drop
    i64.const 0
    f64.convert_i64_u
    drop
    f32.const 0.0
    f64.promote_f32
    drop
    i64.const 0
    f64.reinterpret_i64
  )
  ;; Control-flow.
  (func $func6
    ;; Calls and return.
    call $func7
    i64.const 0
    i32.const 0
    call_indirect (param i64) (result f64)
    return
    ;; Blocks and loops, with and without block type.
    block $label0
      loop $label1
        block (result i64)
          loop $label2 (result i64)
            ;; Branches
            br $label0
            i32.const 0
            br_if $label1
            i32.const 0
            br_table $label0 $label1 $label1 $label2
            i64.const 0
          end $label2
        end
        drop
      end $label1
    end $label0
    ;; Select and if.
    i64.const 0
    i64.const 1
    i32.const 0
    select
    drop
    i32.const 0
    if (result i64)
      i64.const 0
    else
      i64.const 1
    end
    drop
  )
  ;; Memory operations.
  (func $func7
    ;; Loads.
    i32.const 0
    i32.load
    drop
    i32.const 0
    ;; Non-default memargs.
    i64.load offset=3
    drop
    i32.const 0
    f32.load align=2
    drop
    i32.const 0
    f64.load offset=3 align=4
    drop
    i32.const 0
    i32.load8_s
    drop
    i32.const 0
    i32.load8_u
    drop
    i32.const 0
    i32.load16_s
    drop
    i32.const 0
    i32.load16_u
    drop
    i32.const 0
    i64.load8_s
    drop
    i32.const 0
    i64.load8_u
    drop
    i32.const 0
    i64.load16_s
    drop
    i32.const 0
    i64.load16_u
    drop
    i32.const 0
    i64.load32_s
    drop
    i32.const 0
    i64.load32_u
    drop
    ;; Stores.
    i32.const 0
    i32.const 0
    i32.store
    i32.const 0
    i64.const 0
    i64.store
    i32.const 0
    f32.const 0.0
    f32.store
    i32.const 0
    f64.const 0.0
    f64.store
    i32.const 0
    i32.const 0
    i32.store8
    i32.const 0
    i32.const 0
    i32.store16
    i32.const 0
    i64.const 0
    i64.store8
    i32.const 0
    i64.const 0
    i64.store16
    i32.const 0
    i64.const 0
    i64.store32
    ;; Other memory instructions.
    memory.size
    memory.grow
    drop
  )
  ;; Other instructions. (Also an exported function.)
  (func $exported_function (;8;) (export "exported_function")
    nop
    unreachable
  )
  ;; Data and element sections.
  (data (global.get $env.imported_global) "foo\0a\00")
)
;;)---";
