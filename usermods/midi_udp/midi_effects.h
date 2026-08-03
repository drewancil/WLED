#pragma once

#include "wled.h"
#include "midi_state.h"

// paletteBlend: 0 - wrap when moving, 1 - always wrap, 2 - never wrap, 3 - none (undefined)
#define PALETTE_SOLID_WRAP (paletteBlend == 1 || paletteBlend == 3)

/////////////////////////////////////////////////////////////////////////////
// MIDI Keys: positional effect, one key = one point across the full segment.
// Held notes render at full brightness. On note-off the note fades out —
// quickly if the sustain pedal was up at that moment, very slowly if it was
// down. The fade rate is fixed at the instant of release (pedal changes
// afterwards don't retroactively speed up or slow down an in-progress fade).
/////////////////////////////////////////////////////////////////////////////
static void mode_midi_keys(void) {
  if (SEGLEN < 1) { SEGMENT.fill(SEGCOLOR(0)); return; }

  const uint8_t  noteLow  = SEGMENT.custom1;   // metadata default 21  (A0)
  const uint8_t  noteHigh = SEGMENT.custom2;   // metadata default 108 (C8)

  // Speed slider: higher = shorter (faster) release fade when sustain is up.
  const uint16_t fastFadeMs = map(SEGMENT.speed,     0, 255, 2000, 50);
  // Intensity slider: higher = longer (slower) fade while sustain is held.
  const uint16_t slowFadeMs = map(SEGMENT.intensity, 0, 255, 300, 15000);

  SEGMENT.fill(BLACK);

  if (noteHigh <= noteLow) return;

  for (uint16_t note = noteLow; note <= noteHigh; note++) {
    const MidiNote &n = midiState.notes[note];
    if (!n.held && !n.releasing) continue;

    uint8_t level; // 0-255 fade progress, 255 = fully on
    if (n.held) {
      level = 255;
    } else {
      const uint32_t fadeDuration = n.releasedWithSustain ? slowFadeMs : fastFadeMs;
      const uint32_t elapsed = strip.now - n.releaseTime;
      if (elapsed >= fadeDuration) continue; // fully decayed
      level = 255 - (uint8_t)((elapsed * 255) / fadeDuration);
    }

    const unsigned pos = map(note, noteLow, noteHigh, 0, SEGLEN - 1);
    const uint8_t  bri = scale8(level, (n.velocity * 2 > 255) ? 255 : n.velocity * 2);
    SEGMENT.setPixelColor(pos, SEGMENT.color_from_palette(pos, true, PALETTE_SOLID_WRAP, 0, bri));
  }
}
static const char _data_FX_MODE_MIDI_KEYS[] PROGMEM =
  "MIDI Keys@Release fade,Sustain fade,Note low,Note high;;!;1;sx=224,ix=200,c1=21,c2=108";

/////////////////////////////////////////////////////////////////////////////
// MIDI Flash: whole-segment flash on every note-on, scaled by velocity, then
// decays. Simplest possible effect - useful as a latency smoke test.
/////////////////////////////////////////////////////////////////////////////
static void mode_midi_flash(void) {
  if (SEGENV.aux0 != midiState.noteOnCount) {
    SEGENV.aux0 = midiState.noteOnCount;
    SEGENV.aux1 = midiState.lastVelocity * 2; // 0-254 flash brightness
  }

  const uint8_t decay = map(SEGMENT.speed, 0, 255, 1, 40); // higher speed = faster decay
  if (SEGENV.aux1 > 0) {
    SEGMENT.fill(SEGMENT.color_from_palette(0, false, PALETTE_SOLID_WRAP, 0, SEGENV.aux1));
    SEGENV.aux1 = (SEGENV.aux1 > decay) ? SEGENV.aux1 - decay : 0;
  } else {
    SEGMENT.fill(BLACK);
  }
}
static const char _data_FX_MODE_MIDI_FLASH[] PROGMEM =
  "MIDI Flash@Decay speed;;!;1;sx=200";
