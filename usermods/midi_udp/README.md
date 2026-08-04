# MIDI UDP

A usermod that turns a WLED-controlled LED strip into a lightweight MIDI visualizer. A small companion script forwards raw MIDI messages from any class-compliant MIDI controller to the WLED device over UDP; the usermod listens for those packets and drives a set of custom-coded effects (currently four, more can be added) that render played notes across a segment - held notes glow, releases fade, the sustain pedal is respected, and note pitch maps to strip position and palette color.

High level features:

* A dedicated, low-latency UDP listener (separate from WLED's own network traffic) parsing raw 3-byte MIDI messages
* Full note-on / note-off / velocity / sustain-pedal (CC64) handling, with a continuously-decayed per-note brightness model (no timestamp snapshots, no "double flash" on release, retriggering a note mid-fade is instant)
* Four bundled effects - `MIDI Keys`, `MIDI Flash`, `MIDI Puddles`, `MIDI Comet` - each a normal WLED effect with its own Speed/Intensity/Custom sliders, usable like any built-in effect (presets, segments, etc. all work normally)
* A configurable note range (`Note low` / `Note high`) that tiles the segment from the lowest to the highest configured note, so it adapts to any size keyboard/controller
* A small, dependency-light Python bridge script (`tools/midi_to_udp.py`) to get MIDI onto the network - no DAW or MIDI-to-network hardware required

## Table of Contents

* [Hardware](#hardware)
* [How it works](#how-it-works)
* [Compiling](#compiling)
* [Sending MIDI to the device](#sending-midi-to-the-device)
  * [Network protocol](#network-protocol)
  * [Using the included Python bridge](#using-the-included-python-bridge)
* [Usermod configuration](#usermod-configuration)
* [Effects](#effects)
  * [MIDI Keys](#midi-keys)
  * [MIDI Flash](#midi-flash)
  * [MIDI Puddles](#midi-puddles)
  * [MIDI Comet](#midi-comet)
* [Known limitations](#known-limitations)
* [Potential modifications](#potential-modifications)
* [License](#license)

## Hardware

No special hardware is required beyond what any WLED build needs - this was developed and tested on an ESP32. Any MIDI controller that a computer can see as a class-compliant MIDI input device works (the bridge script uses [`python-rtmidi`](https://github.com/SpotlightKid/python-rtmidi), which supports the usual OS-level MIDI backends).

## How it works

The usermod opens its own `WiFiUDP` socket (separate from WLED's built-in notifier socket) on a configurable port and parses incoming packets synchronously in `loop()`, on WLED's normal single-threaded main loop - there's no separate task and no locking, since note state is only ever written from `loop()` and read from effect render functions, both on the same thread. Each MIDI note has a single continuously-updated brightness value that's snapped to a target on note-on and drained by a decay curve while released; effects read this shared state directly rather than through any generic WLED usermod-data mechanism, since the usermod and its effects are compiled together.

## Compiling

`midi_udp` is a normal WLED usermod, but it isn't part of any environment's default usermod list, so it needs to be added manually. In `platformio.ini`, add `midi_udp` to the `custom_usermods` line of whichever environment you're building for, e.g.:

```ini
[env:esp32dev]
...
custom_usermods = ${common.default_usermods} midi_udp
```

(Or use a `platformio_override.ini` if you'd rather not edit `platformio.ini` directly.) Then build/upload that environment as usual.

## Sending MIDI to the device

### Network protocol

The usermod listens on a single UDP port (default `6969`, configurable in the usermod's settings page) for raw MIDI messages, sent one message per UDP packet with no framing, batching, or acknowledgement - this keeps latency minimal. Only exactly-3-byte packets are accepted (status byte + 2 data bytes); anything else is silently dropped. Recognized status bytes:

| Status (high nibble) | Meaning | Data 1 | Data 2 |
| --- | --- | --- | --- |
| `0x90` | Note on (velocity 0 counts as note off, per MIDI convention) | note number (0-127) | velocity (0-127) |
| `0x80` | Note off | note number (0-127) | ignored |
| `0xB0` | Control change | controller number | value (>=64 is "on" for CC64/sustain) |

Only CC64 (sustain pedal) is currently handled; other CC messages, program changes, sysex, etc. are ignored. Since this is just raw MIDI bytes over UDP, you're not limited to the bundled Python script - anything that can read a local MIDI device and send a UDP packet works (a DAW with a network-MIDI plugin, a microcontroller with a MIDI input, etc.).

### Using the included Python bridge

`tools/midi_to_udp.py` is a self-contained script (using [PEP 723](https://peps.python.org/pep-0723/) inline dependency metadata, so it runs standalone with [`uv`](https://docs.astral.sh/uv/) without needing a tracked project):

```
uv run tools/midi_to_udp.py --list                                    # find your MIDI input's port index
uv run tools/midi_to_udp.py --port-index <N> --wled-ip <device-ip>    # start forwarding
```

Add `--udp-port <port>` if you changed the port from the default `6969` in the usermod's settings.

## Usermod configuration

The usermod itself has two settings, under Usermods in WLED's settings page:

* **Enabled** - turns the UDP listener on/off (default on)
* **UDP port** - the port to listen for MIDI packets on (default `6969`)

## Effects

All four effects are normal WLED effects - they show up in the effects list, can be assigned to any segment, and support presets like any other effect. None of them define their own colors; all rendering goes through the segment's selected **palette**, so pick a palette (a custom gradient works well) to control the note-to-color mapping.

Every effect that has a note range uses `Note low` / `Note high` to define which MIDI notes map across the segment - the lowest configured note lands at the start of the segment/palette, the highest at the end, tiled proportionally in between. Set these to match your controller's actual note range for the full range to be used. `Velocity floor` (0-31, scaled internally to a 0-248 brightness floor) sets a minimum brightness for held notes, so quiet playing can still read as fully lit if desired - drag it to maximum for every note to appear full brightness regardless of how hard it's played.

### MIDI Keys

Divides the segment into one contiguous section per note in the configured range, tiling the full strip start to end. A section lights up at that note's current brightness and fades out on release.

| Slider | Meaning |
| --- | --- |
| Speed ("Release fade") | How fast a note fades out after release |
| Intensity ("Sustain fade") | How fast a note fades out after release *while the sustain pedal is held* |
| Custom 1 ("Note low") | Lowest MIDI note in range (default 21 / A0) |
| Custom 2 ("Note high") | Highest MIDI note in range (default 108 / C8) |
| Custom 3 ("Velocity floor") | Minimum brightness for held notes |

### MIDI Flash

A whole-segment flash driven by the loudest currently-active note (not just the last one played), so retriggering any note while another is fading immediately pulls the flash back up. Doesn't use a note range - always flashes the segment as a whole, using palette index 0.

| Slider | Meaning |
| --- | --- |
| Speed ("Release fade") | How fast the flash fades out after release |
| Intensity ("Sustain fade") | How fast the flash fades out after release while sustain is held |
| Custom 3 ("Velocity floor") | Minimum brightness for held notes |

### MIDI Puddles

A `MIDI Keys` clone (same held/fade section per note) plus a ripple that radiates out from a note's position the instant it's struck, like a drop landing in a pond - reuses the propagation/amplitude curve from WLED's built-in `Ripple` effect.

| Slider | Meaning |
| --- | --- |
| Speed ("Ripple speed") | How fast ripples propagate outward |
| Intensity ("Ripple width") | Ripple width |
| Custom 1 ("Note low") | Lowest MIDI note in range |
| Custom 2 ("Note high") | Highest MIDI note in range |
| Custom 3 ("Velocity floor") | Minimum brightness for held notes |
| Check 1 ("Trails") | Blur between frames instead of clearing, so ripples smear and overlap |

### MIDI Comet

Every fresh note strike launches a short comet from that note's position. Direction is melodic, not positional: it's the *reverse* of the interval from the previous note played - play a higher note than the last one and the comet flies down; play a lower note and it flies up (the very first note, with no prior note yet, defaults to down).

| Slider | Meaning |
| --- | --- |
| Speed ("Comet speed") | How fast comets travel across the strip |
| Intensity ("Tail length") | Comet tail length / lifetime |
| Custom 1 ("Note low") | Lowest MIDI note in range |
| Custom 2 ("Note high") | Highest MIDI note in range |
| Custom 3 ("Velocity floor") | Minimum brightness for held notes |
| Check 1 ("Long trails") | Subtler per-frame fade for longer-lingering streaks |

## Known limitations

* Note state is a single shared global, not per-segment - if the same MIDI input drives multiple segments/effects simultaneously, decay is applied once per segment per real frame against the same underlying note brightness, which compounds the decay rate. Fine for the common case of one segment following one controller.
* Only CC64 (sustain) is handled; other controllers (mod wheel, expression, etc.) are read off the wire but ignored.
* WLED's per-effect UI is hard-capped at 5 numeric sliders (Speed, Intensity, Custom 1-3) plus 3 checkboxes, so tuning is necessarily limited to what's exposed above.

## Potential modifications

This was built around one keyboard driving one strip, but the underlying design (shared note state + small effect-side helpers) is meant to make adding more effects cheap - a few natural directions:

* Per-note color assignment instead of range-mapped palette position (e.g. a fixed color per pitch class, piano-roll style)
* Chord/interval-aware effects, now that comet already demonstrates tracking melodic movement between notes
* Exposing more than the built-in 5-slider limit via the usermod's own settings page or a custom web UI, for effects that want more tunables than WLED's stock effect UI allows

## License

Like the rest of WLED, this usermod is licensed under the [EUPL v1.2 or later](https://github.com/wled-dev/WLED/blob/main/LICENSE).
