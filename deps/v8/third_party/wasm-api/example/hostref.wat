(module
  (import "" "f" (func $fun (param externref) (result externref)))

  (global $glob (export "global") (mut externref) (ref.null))
  (table $tab (export "table") 10 externref)

  (func (export "global.set") (param $r externref)
    (global.set $glob (local.get $r))
  )
  (func (export "global.get") (result externref)
    (global.get $glob)
  )

  (func (export "table.set") (param $i i32) (param $r externref)
    (table.set $tab (local.get $i) (local.get $r))
  )
  (func (export "table.get") (param $i i32) (result externref)
    (table.get $tab (local.get $i))
  )

  (func (export "func.call") (param $r externref) (result externref)
    (call $fun (local.get $r))
  )
)
