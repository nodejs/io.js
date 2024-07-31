;; expected = R"---(;; This is a polyglot C++/WAT file.
;; Comment lines are ignored and not expected in the disassembler output.
(module
  (rec
    (type $type0 (func))
  )
  (string "foo" (;0;))
  (func $func0
    string.const "foo" (;0;)
    drop
  )
)
;;)---";
