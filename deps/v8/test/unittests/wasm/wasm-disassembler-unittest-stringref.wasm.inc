  0x00, 0x61, 0x73, 0x6d,  // wasm magic
  0x01, 0x00, 0x00, 0x00,  // wasm version

  0x01,                    // section kind: Type
  0x06,                    // section length 6
  0x01, 0x4e,              // types count 1: rec. group definition
  0x01, 0x60,              // recursive group size 1:  kind: func
  0x00,                    // param count 0
  0x00,                    // return count 0

  0x03,                    // section kind: Function
  0x02,                    // section length 2
  0x01, 0x00,              // functions count 1: 0 $func0

  0x0e,                    // section kind: StringRef
  0x06,                    // section length 6
  0x00,                    // deferred string literal count 0
  0x01, 0x03,              // string literal count 1: string literal length: 3
  0x66, 0x6f, 0x6f,        // string literal: foo

  0x0a,                    // section kind: Code
  0x09,                    // section length 9
  0x01,                    // functions count 1
                           // function #0 $func0
  0x07,                    // body size 7
  0x00,                    // 0 entries in locals list
  0xfb, 0x82, 0x01, 0x00,  // string.const "foo" (;0;)
  0x1a,                    // drop
  0x0b,                    // end
