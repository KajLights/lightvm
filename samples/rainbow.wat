;; Pixelblaze-style callbacks with fixed-point HSV and native LED output.
;; No WASM memory or pixel array is needed; the host owns the RGB buffer.
(module
  (import "led" "pixel_count" (func $pixel_count (result i32)))
  (import "led" "hsv" (func $hsv (param i32 i32 i32)))
  (global $count (mut i32) (i32.const 1))
  (global $phase (mut i32) (i32.const 0))

  (func $init
    call $pixel_count
    global.set $count)
  (start $init)

  (func (export "beforeRender") (param $delta_ms i32)
    global.get $phase
    local.get $delta_ms
    i32.const 20
    i32.mul
    i32.add
    i32.const 65535
    i32.and
    global.set $phase)

  (func (export "render") (param $index i32)
    (local $hue i32)
    (local $wave i32)
    ;; One hue revolution along the strip, scrolling over time.
    local.get $index
    i32.const 65536
    i32.mul
    global.get $count
    i32.div_u
    global.get $phase
    i32.add
    local.set $hue
    ;; Two moving brightness waves; a triangle replaces floating-point sine.
    local.get $index
    i32.const 131072
    i32.mul
    global.get $count
    i32.div_u
    global.get $phase
    i32.const 3
    i32.mul
    i32.add
    i32.const 65535
    i32.and
    i32.const 8
    i32.shr_u
    local.tee $wave
    i32.const 127
    i32.gt_u
    if
      i32.const 255
      local.get $wave
      i32.sub
      local.set $wave
    end
    local.get $hue
    i32.const 255
    local.get $wave
    i32.const 215
    i32.mul
    i32.const 127
    i32.div_u
    i32.const 40
    i32.add
    call $hsv))
