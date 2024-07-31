  0x00, 0x61, 0x73, 0x6d,  // wasm magic
  0x01, 0x00, 0x00, 0x00,  // wasm version

  0x01,                    // section kind: Type
  0x09,                    // section length 9
  0x02,                    // types count 2
  0x60,                    //  kind: func
  0x00,                    // param count 0
  0x00,                    // return count 0
  0x60,                    //  kind: func
  0x01, 0x7f,              // param count 1:  i32
  0x01, 0x7f,              // return count 1:  i32

  0x03,                    // section kind: Function
  0x02,                    // section length 2
  0x01, 0x01,              // functions count 1: 0 $func0 (param i32) (result i32)

  0x0d,                    // section kind: Tag
  0x03,                    // section length 3
  0x01, 0x00, 0x00,        // tag count 1:

  0x0a,                    // section kind: Code
  0x16,                    // section length 22
  0x01,                    // functions count 1
                           // function #0 $func0
  0x14,                    // body size 20
  0x00,                    // 0 entries in locals list
  0x02, 0x40,              // block $label0
  0x1f, 0x7f, 0x01, 0x00, 0x00, 0x00,  // try_table (result i32) catch $tag0 $label0
  0x41, 0x00,              // i32.const 0
  0x0c, 0x00,              // br $label0
  0x0b,                    // end $label0
  0x0c, 0x01,              // br 1
  0x0b,                    // end
  0x41, 0x00,              // i32.const 0
  0x0b,                    // end
