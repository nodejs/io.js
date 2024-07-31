;; expected = R"---(;; This is a polyglot C++/WAT file.
;; Comment lines are ignored and not expected in the disassembler output.
(module
  ;; Empty recgroups are useless but supported.
  (rec)
  (rec)
  ;; Structs.
  (type $type0 (struct))
  (type $type1 (struct (field $field0 i32)))
  (type $type2 (struct (field $field0 (mut i32)) (field $field1 (mut i64))))
  (type $type3 (struct (field $field0 i8) (field $field1 (mut i16))))
  ;; Arrays.
  (type $type4 (array (field i64)))
  (type $type5 (array (field (mut i64))))
  (type $type6 (array (field i8)))
  ;; References to other types, mutual recursion.
  (type $type7 (struct (field $field0 (ref $type0))))
  (rec
    (type $type8 (struct (field $field0 (ref $type9))))
    (type $type9 (struct (field $field0 (ref $type8))))
  )
  ;; Subtyping constraints.
  (type $type10 (sub $type0 (struct (field $field0 i32))))
  (type $type12 (sub final $type0 (struct (field $field0 (mut i32)))))
  ;; Empty recgroups are useless but supported.
  (rec)
  (rec)
  ;; Globals using reference types.
  (global $env.imported_global (;0;) (import "env" "imported_global") (ref $type7))
  (global $env.another_global (;1;) (import "env" "another_global") (ref $type8))
  (global $global2 anyref (ref.null none))
  (global $global3 (mut eqref) (ref.null none))
  ;; Function with GC instructions and taking GC types as parameters.
  (func $func0 (param $var0 (ref $type1)) (param $var1 eqref)
    ;; Structs.
    struct.new_default $type1
    struct.get $type1 $field0
    drop
    struct.new_default $type2
    i32.const 0
    struct.set $type2 $field0
    struct.new_default $type3
    struct.get_s $type3 $field0
    drop
    struct.new_default $type3
    struct.get_u $type3 $field1
    drop
    ;; Arrays.
    array.new_fixed $type4 0
    drop
    i32.const 0
    array.new_default $type4
    array.len
    drop
    i64.const 0
    i32.const 0
    array.new $type4
    i32.const 0
    array.get $type4
    drop
    i32.const 0
    array.new_default $type5
    i32.const 0
    i64.const 0
    array.set $type5
    i32.const 0
    array.new_default $type6
    i32.const 0
    array.get_s $type6
    drop
    i32.const 0
    array.new_default $type6
    i32.const 0
    array.get_u $type6
    drop
    ;; References.
    local.get $var1
    local.get $var1
    ref.eq
    drop
    local.get $var1
    ref.test $type0
    drop
    local.get $var0
    ref.cast $type0
    drop
    local.get $var0
    ref.test null $type0
    drop
    local.get $var0
    ref.cast null $type0
    drop
    ;; Branches.
    block $label0 (result (ref $type1))
      local.get $var0
      br_on_non_null $label0
      local.get $var0
      br_on_cast $label0 (ref null $type0) (ref $type1)
      drop
      local.get $var0
      br_on_cast_fail $label0 (ref $type0) (ref null $type1)
      drop
      local.get $var0
    end $label0
    drop
  )
)
;;)---";
