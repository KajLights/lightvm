# Pico performance regression repair and repeat comparison

22 October 2026. **Status: regression repair validated on the attached Pico W; optimized firmware and original saved state restored.**

This follow-up preserves the [first hardware comparison](pico-performance-hardware-2026-10-22.md), including its failed gates. Work is tracked in the [implementation plan](pico-performance-plan.md). The frozen repair firmware passes the software checks, primary and secondary comparisons, replay, HA, endurance, return-baseline and restoration checks. Every measured regression-repair gate passes; the broader plan's unisolated telemetry and additional-hardware gates remain explicit below.

## What changed

The repair addresses three different costs rather than treating output presentation count as animation progress:

- **Rendering and publication:** constant register values are cached in charged VM metadata; binary32 validity checks avoid soft-float comparisons; selected Pico W image/map/HSV kernels run from budgeted SRAM. Transient map chunks amortize setup over 64 pixels when both VM workers are occupied. Admission-based round robin replaces the repeated age/cost scan, and bounded composition/transition work publishes ready contributions before another traversal can consume the epoch. Changed reusable inputs can render ahead in their independent layer buffer while a completed composite waits.
- **Preparation and controls:** five bounded preparation turns precede one render turn. The outer target is 4 ms, capped at 32 units, and each guest resume uses the existing 2 ms interpreter quantum with a 4,096-dispatch ceiling. Cancellation and revision checks remain between units. Progress replies batch JSON into protocol frames. Status formatting, wire CRC and bulk CDC reads reduce repeated control work. Pico services actual USB and digital-input acquisition after both network work and engine work.
- **Memory:** JSON tokens use bounded 16-bit offsets and eight bytes per token. Hardware configuration allocates only the input descriptors present, and acquisition borrows those immutable descriptors. Applying a replacement rebinds retained adapters before freeing their old descriptors. This reduces both validation scratch and static duplication while retaining the 64 KiB VM heap, 16/8 KiB stacks, driver reservation and 16 KiB operating reserve.

The full-loop counter `fg10` is retained. New `io10`/`ingress_*` counters measure the added, real USB/digital-input service points. A full loop can exceed 10 ms while these intervening service points keep control acquisition responsive. The two measures are reported separately; neither proves an optical timing bound or a bound on an individual SDK Wi-Fi callback.

## Method and identities

One Pico W, RP2040 B2, serial `E6614103E7236D37`, runs at 125 MHz with 200 physical WS2812 pixels on GP16. Logical 500/1,024-pixel cases use the same physical wire. Both builds use two workers and a 2 ms cooperative VM slice. Original saved programs, presets, configuration and private network settings were backed up before testing.

The exact preserved baseline code image is authoritative; its association with source commit `59b7c52` is inferred. Its SHA-256 is `7fcadd399912be0c60246bb3508282cd332029c3220642d2eb61426f89c7bc6d`. The frozen repair UF2 SHA-256 is `ace68ff719e5f098812b941c0e47123142b25e72303b9ac744aa6deb25e52c4f`; its source base is `bdc1045`, with the complete repair patch and source hashes recorded in the evidence. UF2 target addresses end at `0x100a2d00`, below protected state at `0x100fe000`.

Twenty-seven archived benchmark programs were installed once and verified by USB readback. A private snapshot preserved this catalog with the original firmware. Firmware-only flashing keeps the exact package payload bytes and extents for both blocks. Post-campaign readback verifies all 27 extents unchanged and matches all 2,605 firmware payload blocks to the frozen UF2. Private flash snapshots and credentials are excluded from public evidence.

Primary cases use five 30-second trials after seven seconds of warmup. Case order reverses on alternate trials. Two/eight-map and 500/1,024-pixel cases use three 20-second trials. STATUS polls run at 20 Hz; detailed state readbacks occur at boundaries. Boundary-only observer runs are reported separately. Bootstrap intervals resample trial medians 5,000 times with a fixed seed; five trials do not quantify systematic uncertainty. The later baseline return block is a separate drift check and does not replace the primary denominator.

## Screening evidence

The expanded final screen passed all dynamic workload rates, USB latency, dirty-input readback, preparation and memory checks. Five heavy preparations completed in 1.440–1.463 seconds, versus a fresh baseline median of 2.002 seconds, with zero observed ingress-service gaps over 10 ms. Input-to-composite readback p95 was 51.14 ms, versus 61.30 ms baseline. These short screens are diagnostic evidence; acceptance uses the full campaign below.

Earlier attempts remain recorded. Larger FIFOs and batched replies alone did not close preparation and responsiveness together. Long preparation turns reduced completion time but exposed combined engine, USB and Wi-Fi intervals over 10 ms. Temporary timing instrumentation isolated that accumulation and was removed before freezing the final firmware. The final port change adds actual service opportunities rather than merely relabelling the old loop counter.

## Final comparison

The five-trial primary block is complete. Rates below count completed dynamic
layer renders, not output refreshes. The solid backgrounds in the first two
cases reuse unchanged contributions; their zero repeat-render rate is excluded
from the dynamic comparison.

| Workload | Baseline updates/s | Repair updates/s | Change | Repair USB p95 / p99 |
| --- | --- | --- | --- | --- |
| Fixed twelve sprites | 26.65 | 56.00 | +110.1% | 5.05 / 5.53 ms |
| Time-indexed sprites | 33.84 | 65.77 | +94.3% | 5.19 / 5.66 ms |
| Two image layers, each | 46.82 | 49.63 | +6.0% | 7.48 / 7.91 ms |
| Asymmetric map / scalar | 89.57 / 4.23 | 121.69 / 5.03 | +35.9% / +18.9% | 6.70 / 7.04 ms |
| Four scalar layers, each | 3.33 | 4.33 | +30.0% | 6.64 / 7.08 ms |
| A/B layers | 3.40–3.46 | 4.16–4.20 | +20.2–22.6% | 6.73 / 7.35 ms |

All 30 primary trials passed the rate and USB gates. Across 18,030 status
samples, the steady ingress-over-10-ms deltas were zero. Individual host-side
RTT maxima still include host scheduling/USB delay; the largest was 16.75 ms.

The secondary matrix also passes every dynamic-rate and USB gate. Two map
layers gain 11.6–12.6%; the eight-map case gains 2.0–3.9% across all layers,
with fair progress. At 500 pixels, fixed sprites gain 91.0%, images gain
17.2–17.5%, and four scalar layers gain 28.6%. At 1,024 pixels the corresponding
gains are 9.8%, 15.9% and 30.8%. All 24 secondary trials have zero observed
steady ingress-over-10-ms deltas. Image trials at 500 pixels range from 23.93 to
27.22 updates/s, all above the baseline median; the raw variation is retained.

Five unchanged saved-stars runs improve the dynamic layer median by 64.6%;
their trial-resampling interval is wide (+40.6% to +101.8%), reflecting the
variable real animation workload. The invariant background performs no further
renders while its contribution remains valid. Deterministic fixed and seeded
cases are the stronger regression controls.

## Memory, preparation and controls

The 1,024-pixel/four-scalar scene holds 58,568 VM heap bytes. In each of three
trials, an invalid configuration is rejected with the same live heap, unchanged
layer generations and continuing frame counts. An empty configuration then
replaces it successfully in 98.95–104.14 ms without explicitly clearing the
composition or rebooting. The separate 1,024-pixel timing block also changes configurations
without the old manual-release workaround.

| Measurement | Baseline | Repair |
| --- | --- | --- |
| Heavy preparation, median completion | 2,001.88 ms | 1,408.80 ms (29.6% faster) |
| Preparation acceptance p95 | 2,017.56 ms | 4.59 ms |
| USB p95 / p99 while preparing | Synchronous request blocks observation | 7.06 / 7.55 ms, 605 samples |
| Input-to-composite readback p95 | 61.30 ms | 51.18 ms |
| 1,000 HA dimming commands, p95 / maximum | 65.93 / 382.13 ms | 53.07 / 110.13 ms |
| 62 recall/power commands, p95 / maximum | 371.31 / 570.09 ms | 236.80 / 297.79 ms |

All ten preparation cancellations preserve the old generation. In each of the
five heavy replacements, 12–13 HTTP observations of the old generation record
15 additional completed frames before activation. The five heavy
preparation trials have zero observed ingress gaps over 10 ms; their original
full-loop counter still records 26–32 such intervals per trial. Concurrent HTTP
readback p95 is 94.80 ms. These are different observation boundaries, not a
claim that every network callback or HTTP transaction completes within 10 ms.
The repair has zero sampled output retry errors; the baseline has thirteen
retry observations, retained in the evidence. The broker-observed command order
matches all 1,062 scripted operations and the final restoration command.

The repair's internal dirty-to-driver-completion p95 is 29.15 ms. The baseline
does not expose that timestamp, so the comparable improvement uses host pixel
readback instead. Neither is an optical measurement.

All 1,400 manual frames match the baseline byte for byte: fixed sprite counts
and source variants, seeded animated stars through brightness changes, and
650 time-indexed frames. Exposed clocks, validity flags, contribution and
output frame counts, input revisions/values and settings also match. Private
guest arrays and RNG state are not directly exposed.

The CDC test passes 180 requests across exact 64-byte packet boundaries up to
528 bytes, both contiguous and fragmented. Responses preserve configuration,
and the controller does not reset.

## Source work, observer cost and remaining pair waits

On the repaired firmware, the five-trial fixed-twelve source block measures
55.96 updates/s with frame-time geometry, 61.04/s with creation-time geometry
(+9.1%), and 72.18/s with creation-time geometry and constant style (+29.0%).
These gains are separate from the firmware comparison using identical bytecode.

One boundary-only 30-second trial per case gives a descriptive check on the
20 Hz STATUS observer. Relative to that trial, the primary medians are lower by
6.0%, 8.0% and 10.0% on baseline fixed sprites/images/scalars, and 7.2%, 15.0%
and 7.9% on the repair. The image result shows that the observer is material;
this sparse check is not a controlled internal-telemetry-off experiment.
Both firmware comparisons use the same observer regime.

Median join waiting is 0.1–0.2% of epoch time for fixed/timed sprites,
1.7% for scalar and A/B cases, 21.2% for two images and 10.9% for the
asymmetric pair. Image and asymmetric throughput now exceed baseline, but their
wait fractions still cross P6's investigation threshold. Each has only two
eligible VMs; a larger queue alone cannot supply an additional independent VM
while one core waits. The optional shared-batch investigation stays open and
requires an end-to-end prototype benefit before replacing the simple executor.

## Endurance and evidence integrity

The candidate completes **3,693.11 seconds (61.55 minutes) on one boot**,
verified across 234 controller descriptions. The final stress block adds 62
recall/power commands and 28 periodic preset switches with no sampled retry
errors. Live heap stays at 29,000 bytes throughout both HA stress blocks. The
highest observed VM heap peak across the campaign is 61,708/65,536 bytes.

The firmware readback and unchanged package extents close the code/layout
identity check. The first attempt to load the return baseline found no USB
device because `picotool save -f` had just requested application reboot; the
retry waits for re-enumeration. That pre-measurement failure log is retained.

The return baseline repeats all six primary cases three times after the
candidate. Dynamic medians differ from the first baseline by −2.98% to +0.92%:
fixed sprites −0.15%, timed sprites −0.19%, images −2.98%, asymmetric map/scalar
−0.28%/−0.02%, four scalars approximately unchanged, and A/B −0.02%/+0.92%.
The repair still exceeds every required dynamic layer in this second baseline:
fixed sprites +110.4%, timed sprites +94.7%, images +9.3%, scalar layers about
+30.0%, asymmetric layers +18.9–36.3%, and A/B +19.1–22.6%.
This A/B/A check does not substitute for independent boards or fully randomized
firmware order, but the observed drift does not explain away the gains.

The exact original full-flash backup was restored and verified first. The
user-selected optimized UF2 was then flashed without touching its saved state.
All 13 original objects are restored; ten public program/preset hashes were
checked again over USB. Active and saved configuration, selected rainbow preset,
power, brightness, colour and colour-control settings match the original. MQTT
is connected, and the final check observes 888 additional frames over seven
seconds with no errors or reset.

The public bundle passes its file/hash audit: 460 allowlisted files, including
the exact ELF and UF2, with no saved credential values found. All 242 frozen
source/test/tool hashes still match the validated build. The full repair diff
was reviewed for descriptor ownership, parser bounds, native fault ordering,
composition publication, fair admission, USB framing and stack callback paths.
No further firmware changes were made after the frozen comparison.

## Software and stack validation

The final host build passes 16 native contracts and runs 311 Python tests successfully, with two skips. ASan/UBSan native and focused configuration/composition/input checks pass. LeakSanitizer is unavailable on this macOS host; no LeakSanitizer coverage is claimed. A missing include in the newly added status-buffer contract was corrected before the complete final rerun; the failed build is retained and did not change the Pico firmware.

Strict Pico W, Pico 2 W and no-network Pico builds close the revised callback graphs, including the SDK USB read/write targets and the new service function. The linked bounds include interrupt allowances:

| Profile | Main bound / reservation | Main margin | Worker bound / reservation | Worker margin |
| --- | --- | --- | --- | --- |
| Pico W | 12,048 / 16,384 B | 4,336 B | 5,384 / 8,192 B | 2,808 B |
| Pico 2 W | 12,168 / 16,384 B | 4,216 B | 5,156 / 8,192 B | 3,036 B |
| Minimal Pico | 12,044 / 16,384 B | 4,340 B | 5,372 / 8,192 B | 2,820 B |

Pico W static RAM is 132,976 bytes, leaving 400 bytes after all existing reservations. SRAM kernels and CDC FIFOs are included in this total. The attached B2 ROM audit from the preceding campaign remains applicable to this unchanged physical device. Same-boot stress watermarks are **5,248/16,384 bytes main** and **2,676/8,192 bytes worker**, leaving 11,136 and 5,516 bytes respectively. These observations sit below the conditional linked bounds; they do not certify every possible path or ROM revision.

## Limits and remaining plan items

Optical/electrical timing, Pico 2 W, APA102 and multiple physical outputs still require their hardware. Unmodified baseline firmware lacks comparable displayed-age stamps, stack watermarks and steady ingress counters. Basic internal telemetry overhead has not been isolated from necessary bookkeeping; observer overhead is a separate measurement. Optional shared dispatch/cache work requires a demonstrated residual benefit, not just a larger executor.

## Reproduction evidence

The [structured comparison](../benchmarks/pico-performance-repair-2026-10-22.json)
records per-layer samples, trial-resampling intervals and explicit gate results.
The [raw bundle](../benchmarks/pico-performance-repair-2026-10-22-raw.tar.gz)
contains the baseline/candidate/return runs, failed screens, final ELF/UF2,
source patch and hashes, build/stack reports, archived bytecode and reproduction
scripts. Its [manifest](../benchmarks/pico-performance-repair-2026-10-22-manifest.json)
hashes every included file. Private full-flash backups and credentials remain
outside the bundle. The earlier report and its evidence are preserved unchanged.
