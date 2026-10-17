# Future device support

Status: Proposed beyond configured WS2812B RGB outputs. Host, Pico/Pico W and
ESP32 C3/S3/C6 use the current typed bytecode runtime, native JSON configuration,
named program storage, independent instances and native scene composition.
See [configuration](../configuration.md) and [control/input requirements](feature-proposal-control-api.md).

## Starter capabilities

| Capability | Required behavior |
|---|---|
| Digital input | Pin, pull, active level, debounce; stable state plus ordered timestamped edges |
| Digital output | Pin, active polarity, safe off value; bounded pulses with native expiry |
| Analogue input | ADC pin, sampling cadence, endpoints, smoothing and validity; explicit units |
| PWM output | Pin, frequency, achieved resolution and safe duty; independent native waveform |
| Tone | Frequency limits, maximum duration, native completion and silence on stop |
| Positional servo | Calibration preset or pulse limits, angle range, period and release policy |
| Continuous rotation servo | Signed speed/neutral semantics, separate from positional angle |
| DAC output | Supported hardware, calibrated units and safe level; distinct from PWM |

Buttons, switches, potentiometers and light sensors should be editor presets
over these types. Wiring guides must specify resistors, drivers, power and common
ground; software capability does not imply direct GPIO suitability. Add I2C
sensors/displays later to establish bus sharing and typed sample/transfer rules.
General motor control, arbitrary bus access and cameras are outside the starter scope.

## Implementation requirements

Use one declarative device definition to generate configuration validation,
typed bindings and editor metadata. Native drivers own hardware resources;
programs bind logical names to checked opaque resources, never pin numbers or
native pointers. Keep physical drivers, logical frames, scene layers and A/B
slots distinct. Explicit RGBW output needs its own physical driver and budget;
PixelArray white channels do not imply such a driver is installed.

Validate shared pins, ADC/PWM/timer restrictions, bus addresses and combined
resource counts before activation. Reserve buffers and jobs before callbacks.
Candidate failure preserves active configuration and output. Safe stop, pause,
release, fault and reconnect behavior are defined per capability. No hardware
pulses should depend on guest sleeps or animation frame cadence.

Report compiled capabilities separately from configured instances and live
availability. Board pin aliases and sensible current defaults remain useful.
Unknown fields/unsupported devices fail clearly. Saved configuration from a
superseded contract requires explicit reprovisioning, not an adapter.

## Delivery and verification

Start with digital input/output and a button-controlled light; add ADC/PWM,
tone and servo projects in that order. Verify stable snapshots, ordered events,
overflow, units/ranges, native deadlines, safe release, stale generations,
aggregate admission and failure cleanup on host before each native port.
Cross-build every supported board and measure hardware timing separately.
The browser must explain unavailable features using actual discovery and
[build profile](feature-proposal-build-profiles.md) metadata.
