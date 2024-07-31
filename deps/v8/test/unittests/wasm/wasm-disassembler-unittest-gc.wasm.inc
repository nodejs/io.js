  0x00, 0x61, 0x73, 0x6d,  // wasm magic
  0x01, 0x00, 0x00, 0x00,  // wasm version

  0x01,                    // section kind: Type
  0x4b,                    // section length 75
  0x10,                    // types count 16
  0x4e, 0x00,              // empty rec.group
  0x4e, 0x00,              // empty rec.group
  0x50, 0x00, 0x5f, 0x00,  // type #0 $type0 subtype, supertype count 0, kind: struct, field count 0
  0x5f, 0x01, 0x7f, 0x00,  // type #1 $type1 kind: struct, field count 1:  i32 immutable
  0x5f, 0x02,              // type #2 $type2 kind: struct, field count 2
  0x7f, 0x01,              // i32 mutable
  0x7e, 0x01,              // i64 mutable
  0x5f, 0x02,              // type #3 $type3 kind: struct, field count 2
  0x78, 0x00,              //  i8 immutable
  0x77, 0x01,              //  i16 mutable
  0x5e, 0x7e, 0x00,        // type #4 $type4 kind: array i64 immutable
  0x5e, 0x7e, 0x01,        // type #5 $type5 kind: array i64 mutable
  0x5e, 0x78, 0x00,        // type #6 $type6 kind: array i8 immutable
  0x5f, 0x01, 0x64, 0x00, 0x00,  // type #7 $type7 kind: struct, field count 1:  (ref $type0) immutable
  0x4e,                    // rec. group definition
  0x02,                    // recursive group size 2
  0x5f, 0x01, 0x64, 0x09, 0x00,  // type #8 $type8 kind: struct, field count 1:  (ref $type9) immutable
  0x5f, 0x01, 0x64, 0x08, 0x00,  // type #9 $type9 kind: struct, field count 1:  (ref $type8) immutable
  0x50, 0x01, 0x00,        // type #10 $type10 subtype, supertype count 1: supertype 0
  0x5f, 0x01, 0x7f, 0x00,  //  kind: struct, field count 1:  i32 immutable
  0x60,                    // type #11 $type11 kind: func
  0x02,                    // param count 2
  0x64, 0x01, 0x6d,        // (ref $type1) eqref
  0x00,                    // return count 0
  0x4f, 0x01, 0x00,        // type #12 $type12 final subtype, supertype count 1: supertype 0
  0x5f, 0x01, 0x7f, 0x01,  //  kind: struct, field count 1:  i32 mutable
  0x4e, 0x00,              // empty rec.group
  0x4e, 0x00,              // empty rec.group

  0x02,                    // section kind: Import
  0x30,                    // section length 48
  0x02,                    // imports count 2
                           // import #0
  0x03,                    // module name length: 3
  0x65, 0x6e, 0x76,        // module name: env
  0x0f,                    // field name length: 15
  0x69, 0x6d, 0x70, 0x6f, 0x72, 0x74, 0x65, 0x64,
  0x5f, 0x67, 0x6c, 0x6f, 0x62, 0x61, 0x6c,
                           // field name: imported_global
  0x03, 0x64, 0x07, 0x00,  // kind: global (ref $type7) immutable
                           // import #1
  0x03,                    // module name length: 3
  0x65, 0x6e, 0x76,        // module name: env
  0x0e,                    // field name length: 14
  0x61, 0x6e, 0x6f, 0x74, 0x68, 0x65, 0x72, 0x5f,
  0x67, 0x6c, 0x6f, 0x62, 0x61, 0x6c,
                           // field name: another_global
  0x03, 0x64, 0x08, 0x00,  // kind: global (ref $type8) immutable

  0x03,                    // section kind: Function
  0x02,                    // section length 2
  0x01, 0x0b,              // functions count 1: 0 $func0 (param (ref $type1) eqref)

  0x06,                    // section kind: Global
  0x0b,                    // section length 11
  0x02,                    // globals count 2
  0x6e, 0x00,              // global #2: anyref immutable
  0xd0, 0x71, 0x0b,        // ref.null none
  0x6d, 0x01,              // global #3: eqref mutable
  0xd0, 0x71, 0x0b,        // ref.null none

  0x0a,                    // section kind: Code
  0x9d, 0x01,              // section length 157
  0x01,                    // functions count 1
                           // function #0 $func0
  0x9a, 0x01,              // body size 154
  0x00,                    // 0 entries in locals list
  0xfb, 0x01, 0x01,        // struct.new_default $type1
  0xfb, 0x02, 0x01, 0x00,  // struct.get $type1 $field0
  0x1a,                    // drop
  0xfb, 0x01, 0x02,        // struct.new_default $type2
  0x41, 0x00,              // i32.const 0
  0xfb, 0x05, 0x02, 0x00,  // struct.set $type2 $field0
  0xfb, 0x01, 0x03,        // struct.new_default $type3
  0xfb, 0x03, 0x03, 0x00,  // struct.get_s $type3 $field0
  0x1a,                    // drop
  0xfb, 0x01, 0x03,        // struct.new_default $type3
  0xfb, 0x04, 0x03, 0x01,  // struct.get_u $type3 $field1
  0x1a,                    // drop
  0xfb, 0x08, 0x04, 0x00,  // array.new_fixed $type4 0
  0x1a,                    // drop
  0x41, 0x00,              // i32.const 0
  0xfb, 0x07, 0x04,        // array.new_default $type4
  0xfb, 0x0f,              // array.len
  0x1a,                    // drop
  0x42, 0x00,              // i64.const 0
  0x41, 0x00,              // i32.const 0
  0xfb, 0x06, 0x04,        // array.new $type4
  0x41, 0x00,              // i32.const 0
  0xfb, 0x0b, 0x04,        // array.get $type4
  0x1a,                    // drop
  0x41, 0x00,              // i32.const 0
  0xfb, 0x07, 0x05,        // array.new_default $type5
  0x41, 0x00,              // i32.const 0
  0x42, 0x00,              // i64.const 0
  0xfb, 0x0e, 0x05,        // array.set $type5
  0x41, 0x00,              // i32.const 0
  0xfb, 0x07, 0x06,        // array.new_default $type6
  0x41, 0x00,              // i32.const 0
  0xfb, 0x0c, 0x06,        // array.get_s $type6
  0x1a,                    // drop
  0x41, 0x00,              // i32.const 0
  0xfb, 0x07, 0x06,        // array.new_default $type6
  0x41, 0x00,              // i32.const 0
  0xfb, 0x0d, 0x06,        // array.get_u $type6
  0x1a,                    // drop
  0x20, 0x01,              // local.get $var1
  0x20, 0x01,              // local.get $var1
  0xd3,                    // ref.eq
  0x1a,                    // drop
  0x20, 0x01,              // local.get $var1
  0xfb, 0x14, 0x00,        // ref.test $type0
  0x1a,                    // drop
  0x20, 0x00,              // local.get $var0
  0xfb, 0x16, 0x00,        // ref.cast $type0
  0x1a,                    // drop
  0x20, 0x00,              // local.get $var0
  0xfb, 0x15, 0x00,        // ref.test null $type0
  0x1a,                    // drop
  0x20, 0x00,              // local.get $var0
  0xfb, 0x17, 0x00,        // ref.cast null $type0
  0x1a,                    // drop
  0x02, 0x64, 0x01,        // block (result (ref $type1)) $label0
  0x20, 0x00,              // local.get $var0
  0xd6, 0x00,              // br_on_non_null $label0
  0x20, 0x00,              // local.get $var0
  0xfb, 0x18, 0x01, 0x00, 0x00, 0x01,
                           // br_on_cast $label0 (ref null $type0) (ref $type1)
  0x1a,                    // drop
  0x20, 0x00,              // local.get $var0
  0xfb, 0x19, 0x02, 0x00, 0x00, 0x01,
                           // br_on_cast_fail $label0 (ref $type0)
                           //   (ref null $type1)
  0x1a,                    // drop
  0x20, 0x00,              // local.get $var0
  0x0b,                    // end $label0
  0x1a,                    // drop
  0x0b,                    // end
