#pragma once

#include <Arduino.h>

// Shared between midi_udp.cpp (writer, from the UDP receive loop) and
// midi_effects.h (readers, from effect render functions). Both run on
// WLED's single main-loop task, so no locking is needed here.

static constexpr uint8_t MIDI_NUM_NOTES  = 128;
static constexpr uint8_t MIDI_CC_SUSTAIN = 64;

struct MidiNote {
  uint8_t  velocity = 0;              // velocity captured at note-on; kept through release to scale the fade
  bool     held = false;              // true while the key is physically down (no note-off received yet)
  bool     releasing = false;         // true while fading out after note-off
  bool     releasedWithSustain = false; // sustain pedal state at the moment of note-off; picks the fade rate
  uint32_t releaseTime = 0;           // millis() at note-off; fade progress is computed from this
};

struct MidiNoteState {
  MidiNote notes[MIDI_NUM_NOTES];
  bool     sustain = false;   // current sustain pedal (CC64) state
  uint8_t  lastNote = 0;
  uint8_t  lastVelocity = 0;
  uint32_t lastNoteOnTime = 0;
  uint16_t noteOnCount = 0;   // bumped on every note-on; effects can diff this to detect new triggers cheaply
};

extern MidiNoteState midiState;
