;; expected = R"---(;; This is a polyglot C++/WAT file.
;; Comment lines are ignored and not expected in the disassembler output.
(module
  (tag $tag0)
  (func $func0 (param $var0 i32) (result i32)
    block $label0
      try_table $label1 (result i32) catch $tag0 $label0
        i32.const 0
        br $label1
      end $label1
      br 1
    end $label0
    i32.const 0
  )
)
;;)---";
