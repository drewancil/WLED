#pragma once

#include <Arduino.h>

// Shared between midi_udp.cpp (writer, from the UDP receive loop) and
// midi_effects.h (readers/decay driver, from effect render functions). Both
// run on WLED's single main-loop task, so no locking is needed here.
//
// Brightness model: each note has a continuously-tracked `level` (0-255).
// note-on snaps level straight to the target brightness - this is what
// makes retriggering a note mid-fade instant, with no gap: there's no
// separate "release timestamp" to reconcile, just one number that gets set
// on note-on and drained by decay while released. note-off does *not*
// touch level at all, so there's no discontinuity at the moment of release
// either. Decay rate (fast vs. slow/sustain) is read from the live sustain
// flag every tick, so lifting the pedal mid-fade takes effect on the very
// next frame instead of being locked in at release time.

static constexpr uint8_t MIDI_NUM_NOTES  = 128;
static constexpr uint8_t MIDI_CC_SUSTAIN = 64;

struct MidiNote {
  uint8_t velocity = 0;   // velocity captured at (re)trigger, 0-127
  bool    held = false;   // true while the key is physically down
  float   level = 0.0f;   // current visual brightness, 0-255, continuously decayed
};

struct MidiNoteState {
  MidiNote notes[MIDI_NUM_NOTES];
  bool     sustain = false;   // live sustain pedal (CC64) state
  uint8_t  lastNote = 0;
  uint8_t  lastVelocity = 0;
  uint32_t lastNoteOnTime = 0;
  uint16_t noteOnCount = 0;   // bumped on every note-on; effects can diff this to detect new triggers cheaply
};

extern MidiNoteState midiState;

// Drains `deltaMs` worth of decay from every released, still-lit note,
// using the live sustain state to pick fast vs. slow rate. Call once per
// effect frame with the ms elapsed since that effect's last call (e.g. via
// SEGENV.step) - held notes are skipped so they never decay while the key
// is down.
inline void midiDecayTick(uint32_t deltaMs, float fastRatePerMs, float slowRatePerMs) {
  if (deltaMs == 0) return;
  const float rate = midiState.sustain ? slowRatePerMs : fastRatePerMs; // live, re-read every tick
  const float drop = rate * deltaMs;
  if (drop <= 0.0f) return;
  for (uint16_t i = 0; i < MIDI_NUM_NOTES; i++) {
    MidiNote &n = midiState.notes[i];
    if (n.held || n.level <= 0.0f) continue;
    n.level = (n.level > drop) ? n.level - drop : 0.0f;
  }
}
