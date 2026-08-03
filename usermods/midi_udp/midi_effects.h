#pragma once

#include "wled.h"
#include "midi_state.h"

// paletteBlend: 0 - wrap when moving, 1 - always wrap, 2 - never wrap, 3 - none (undefined)
#define PALETTE_SOLID_WRAP (paletteBlend == 1 || paletteBlend == 3)

// ms since this effect instance's last frame, using the standard WLED
// SEGENV.step/SEGENV.call idiom; also advances SEGENV.step for next time.
static uint32_t midiFrameDeltaMs() {
  const uint32_t nowMs = strip.now;
  const uint32_t deltaMs = (SEGENV.call == 0) ? 0 : (nowMs - SEGENV.step);
  SEGENV.step = nowMs;
  return deltaMs;
}

/////////////////////////////////////////////////////////////////////////////
// MIDI Keys: the segment is divided into one contiguous section per note in
// [Note low, Note high], tiling the full strip start to end. A section
// lights up at that note's current level - see midi_state.h for the decay
// model (instant attack/retrigger, live sustain-aware release decay, no
// jumps between "held" and "just released").
/////////////////////////////////////////////////////////////////////////////
static void mode_midi_keys(void) {
  if (SEGLEN < 1) { SEGMENT.fill(SEGCOLOR(0)); return; }

  const uint8_t noteLow  = SEGMENT.custom1;   // metadata default 21  (A0)
  const uint8_t noteHigh = SEGMENT.custom2;   // metadata default 108 (C8)

  const uint16_t fastFadeMs = map(SEGMENT.speed,     0, 255, 2000, 50);   // Speed: higher = faster release fade
  const uint16_t slowFadeMs = map(SEGMENT.intensity, 0, 255, 300, 15000); // Intensity: higher = slower sustain fade
  midiDecayTick(midiFrameDeltaMs(), 255.0f / fastFadeMs, 255.0f / slowFadeMs);

  SEGMENT.fill(BLACK);
  if (noteHigh <= noteLow) return;

  const uint16_t numNotes = (uint16_t)(noteHigh - noteLow) + 1;

  // Raises `level` itself (not just what's displayed) while a note is held,
  // so a fade started after release picks up from the floor, not the raw
  // velocity value. Never touches a note that's already releasing/fading.
  const uint8_t velocityFloor = SEGMENT.custom3 * 8; // 0-31 slider -> 0-248

  for (uint16_t note = noteLow; note <= noteHigh; note++) {
    MidiNote &n = midiState.notes[note];
    if (n.held && n.level < velocityFloor) n.level = velocityFloor;
    if (!n.held && n.level <= 0.0f) continue;

    unsigned startPx = (unsigned)(note - noteLow)     * SEGLEN / numNotes;
    unsigned endPx   = (unsigned)(note - noteLow + 1) * SEGLEN / numNotes;
    if (endPx <= startPx) endPx = startPx + 1; // guarantee every note gets >=1 pixel even if SEGLEN < numNotes
    if (endPx > SEGLEN) endPx = SEGLEN;

    const uint8_t  bri        = (n.level > 255.0f) ? 255 : (uint8_t)n.level;
    const uint8_t  palettePos = map(note, noteLow, noteHigh, 0, 255);
    const uint32_t color      = SEGMENT.color_from_palette(palettePos, true, PALETTE_SOLID_WRAP, 0, bri);
    for (unsigned px = startPx; px < endPx; px++) SEGMENT.setPixelColor(px, color);
  }
}
static const char _data_FX_MODE_MIDI_KEYS[] PROGMEM =
  "MIDI Keys@Release fade,Sustain fade,Note low,Note high,Velocity floor;;!;1;sx=224,ix=200,c1=21,c2=108,c3=0";

/////////////////////////////////////////////////////////////////////////////
// MIDI Flash: whole-segment flash driven by the loudest currently-active
// note's level (max across all notes, not just "the last one played"), so
// retriggering any note while another is fading immediately pulls the
// flash back up. Uses the same shared decay model as MIDI Keys.
/////////////////////////////////////////////////////////////////////////////
static void mode_midi_flash(void) {
  const uint16_t fastFadeMs = map(SEGMENT.speed,     0, 255, 2000, 50);
  const uint16_t slowFadeMs = map(SEGMENT.intensity, 0, 255, 300, 15000);
  midiDecayTick(midiFrameDeltaMs(), 255.0f / fastFadeMs, 255.0f / slowFadeMs);

  // Same held-only floor as MIDI Keys, applied to `level` itself so a fade
  // started after release picks up from the floor, not the raw velocity.
  const uint8_t velocityFloor = SEGMENT.custom3 * 8; // 0-31 slider -> 0-248
  float maxLevel = 0.0f;
  for (MidiNote &n : midiState.notes) {
    if (n.held && n.level < velocityFloor) n.level = velocityFloor;
    if (n.level > maxLevel) maxLevel = n.level;
  }

  const uint8_t bri = (maxLevel > 255.0f) ? 255 : (uint8_t)maxLevel;
  if (bri == 0) { SEGMENT.fill(BLACK); return; }
  SEGMENT.fill(SEGMENT.color_from_palette(0, false, PALETTE_SOLID_WRAP, 0, bri));
}
static const char _data_FX_MODE_MIDI_FLASH[] PROGMEM =
  "MIDI Flash@Release fade,Sustain fade,,,Velocity floor;;!;1;sx=224,ix=200,c3=0";
