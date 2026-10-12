;; Repeating red, green, blue pixels for checking physical strip wire order.
(module
  (import "led" "hsv" (func $hsv (param i32 i32 i32)))
  (func (export "render") (param $index i32)
    local.get $index
    i32.const 3
    i32.rem_u
    i32.const 21846
    i32.mul
    i32.const 255
    i32.const 255
    call $hsv))
