(module
  (import "console" "write" (func $write (param i32 i32)))
  (memory 1 1)
  (data (i32.const 0) "Hello from dynamically uploaded WASM!\0a")
  (func (export "run") (result i32)
    i32.const 0
    i32.const 38
    call $write
    i32.const 42))
