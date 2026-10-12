(module
  (import "console" "write" (func $write (param i32 i32)))
  (memory 1 1)
  (global $count (mut i32) (i32.const 0))
  (data (i32.const 0) "Counter module executed\0a")
  (func (export "run") (result i32)
    global.get $count
    i32.const 1
    i32.add
    global.set $count
    i32.const 0
    i32.const 24
    call $write
    global.get $count))
