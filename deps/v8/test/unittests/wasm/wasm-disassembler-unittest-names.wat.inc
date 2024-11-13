;; expected = R"---(;; This is a polyglot C++/WAT file.
;; Comment lines are ignored and not expected in the disassembler output.
(module
  ;; TODO(jkummerow): This type name is missing from the disassembler ouput.
  ;; (type $type_with_name (;0;) (func (param f32)))
  (global $imported_global_with_name (;0;) (import "env" "imported_global") i32)
  (func $imported_function_with_name (;0;) (import "env" "imported_function"))
  (table $table_with_name (;0;) 0 funcref)
  (memory $memory_with_name (;0;) 0)
  (global $global_with_name (;1;) i32 (i32.const 0))
  (global $exported_global_with_name (;2;) (export "exported_global") i32 (i32.const 0))
  (elem $elem_with_name (;0;) (ref func) (ref.func $function_with_name))
  (func $function_with_name (;1;) (param $param_with_name_1 (;0;) i32) (param $param_with_name_2 (;1;) i32) (param $param_with_name_3 (;2;) i64)
    local.get $param_with_name_1
    drop
    local.get $param_with_name_2
    drop
    local.get $param_with_name_3
    drop
  )
  (func $exported_function_with_name (;2;) (export "exported_function")
    ;; Local variables.
    (local $local_with_name_1 i32)
    (local $local_with_name_2 i32)
    (local $local_with_name_3 i64)
    local.get $local_with_name_1
    drop
    local.get $local_with_name_2
    drop
    local.get $local_with_name_3
    drop
  )
  ;; TODO(jkummerow): Functions with a named type are printed with their type
  ;; inline instead of as follows.
  ;; (func $another_function (;3;) (type $type_with_name)
  ;; )
  ;; For compatibility with the legacy DevTools behavior, we don't print data
  ;; segment names. If we change that, uncomment the following line.
  ;; (data $data_with_name (;0;) (i32.const 0) "foo\0a\00")
  (data (i32.const 0) "foo\0a\00")
)
;;)---";
