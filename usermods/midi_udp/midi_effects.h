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

// level -> displayable brightness, clamped to 0-255.
static inline uint8_t midiLevelToBri(float level) {
  return (level > 255.0f) ? 255 : (uint8_t)level;
}

// Raises a held note's level to the floor (never lowers it) so a fade that
// starts after release picks up from the floor, not the raw velocity value.
static inline void midiApplyHeldFloor(MidiNote &n, uint8_t floor) {
  if (n.held && n.level < floor) n.level = floor;
}

static inline uint8_t midiFloorVelocity(uint8_t velocity, uint8_t floor) {
  return max(velocity, (uint8_t)(floor / 2));
}

// midiFrameDeltaMs() + midiDecayTick() in one call, since every effect
// applies decay the same way, just with different fast/slow ms.
static inline void midiApplyFadeMs(float fastMs, float slowMs) {
  midiDecayTick(midiFrameDeltaMs(), 255.0f / fastMs, 255.0f / slowMs);
}

// Per-note section-tiled render pass shared by any effect that shows a
// contiguous "key" per note across [noteLow, noteHigh]: applies the held
// floor, skips fully-decayed released notes, computes each note's pixel
// range, and fills it from the palette. No-op if noteHigh <= noteLow.
static void midiRenderKeySections(uint8_t noteLow, uint8_t noteHigh, uint8_t velocityFloor) {
  if (noteHigh <= noteLow) return;
  const uint16_t numNotes = (uint16_t)(noteHigh - noteLow) + 1;
  for (uint16_t note = noteLow; note <= noteHigh; note++) {
    MidiNote &n = midiState.notes[note];
    midiApplyHeldFloor(n, velocityFloor);
    if (!n.held && n.level <= 0.0f) continue;

    unsigned startPx = (unsigned)(note - noteLow)     * SEGLEN / numNotes;
    unsigned endPx   = (unsigned)(note - noteLow + 1) * SEGLEN / numNotes;
    if (endPx <= startPx) endPx = startPx + 1; // guarantee every note gets >=1 pixel even if SEGLEN < numNotes
    if (endPx > SEGLEN) endPx = SEGLEN;

    // mapping=true does the pixel-index -> 0-255 palette normalization for us
    // (using this note's own section midpoint), so it's always in lockstep
    // with where the section sits on the strip.
    const unsigned midPx = (startPx + endPx - 1) / 2;
    const uint8_t  bri   = midiLevelToBri(n.level);
    const uint32_t color = SEGMENT.color_from_palette(midPx, true, PALETTE_SOLID_WRAP, 0, bri);
    for (unsigned px = startPx; px < endPx; px++) SEGMENT.setPixelColor(px, color);
  }
}

// Shared "is this triggered note in range, and what are its render params"
// prep for particle-spawning effects (Puddles, Comet, ...): range-checks the
// note and derives its strip position and floored velocity. Returns false
// (leaving `out` untouched) if out of range. Callers pass `pos` straight
// into color_from_palette(..., mapping=true, ...) at render time to get a
// palette color that matches exactly where the particle sits on the strip.
struct MidiTriggerParams { float pos; uint8_t velocity; };
static bool midiTriggerParamsFor(uint8_t note, uint8_t noteLow, uint8_t noteHigh,
                                  uint8_t velocityFloor, MidiTriggerParams &out) {
  if (note < noteLow || note > noteHigh) return false;
  out.pos      = map(note, noteLow, noteHigh, 0, SEGLEN - 1);
  out.velocity = midiFloorVelocity(midiState.notes[note].velocity, velocityFloor);
  return true;
}

// SEGENV.allocateData + cast + first-frame reset boilerplate, shared by any
// effect that keeps a per-segment-instance particle pool. Returns nullptr on
// allocation failure so the caller can fall back to a plain fill.
template <typename T>
static T* midiGetEffectData() {
  if (!SEGENV.allocateData(sizeof(T))) return nullptr;
  T &d = *reinterpret_cast<T*>(SEGENV.data);
  if (SEGENV.call == 0) d = T();
  return &d;
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

  // Speed: higher = faster release fade. Intensity: higher = slower sustain fade.
  midiApplyFadeMs(map(SEGMENT.speed, 0, 255, 2000, 50), map(SEGMENT.intensity, 0, 255, 300, 15000));

  SEGMENT.fill(BLACK);
  midiRenderKeySections(noteLow, noteHigh, SEGMENT.custom3 * 8); // 0-31 slider -> 0-248
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
  midiApplyFadeMs(map(SEGMENT.speed, 0, 255, 2000, 50), map(SEGMENT.intensity, 0, 255, 300, 15000));

  // Same held-only floor as MIDI Keys, applied to `level` itself so a fade
  // started after release picks up from the floor, not the raw velocity.
  const uint8_t velocityFloor = SEGMENT.custom3 * 8; // 0-31 slider -> 0-248
  float maxLevel = 0.0f;
  for (MidiNote &n : midiState.notes) {
    midiApplyHeldFloor(n, velocityFloor);
    if (n.level > maxLevel) maxLevel = n.level;
  }

  const uint8_t bri = midiLevelToBri(maxLevel);
  if (bri == 0) { SEGMENT.fill(BLACK); return; }
  SEGMENT.fill(SEGMENT.color_from_palette(0, false, PALETTE_SOLID_WRAP, 0, bri));
}
static const char _data_FX_MODE_MIDI_FLASH[] PROGMEM =
  "MIDI Flash@Release fade,Sustain fade,,,Velocity floor;;!;1;sx=224,ix=200,c3=0";

/////////////////////////////////////////////////////////////////////////////
// MIDI Puddles: a MIDI Keys clone (same held/fade section per note, same
// Velocity floor) plus a ripple that radiates out from a note's position
// the instant it's struck - like a drop landing in a pond. Reuses the decay
// curve from WLED's own built-in "Ripple" effect (FX.cpp, mode_ripple):
// state 1-254 drives both propagation distance and amplitude, so a ripple
// naturally travels a bit, peaks, and fades - a released/held key doesn't
// affect it further, it's a one-shot splash. Optional "Trails" checkbox
// blurs between frames instead of clearing, so ripples smear and overlap
// like real water.
/////////////////////////////////////////////////////////////////////////////
static constexpr uint8_t MAX_MIDI_RIPPLES = 24;

struct MidiRipple {
  uint8_t  state = 0;   // 0 = inactive; 1-254 = age, drives radius + amplitude
  uint8_t  velocity = 0;
  uint16_t origin = 0;  // pixel the ripple started from - also its color_from_palette(mapping=true) index
};

struct MidiPuddleData {
  uint8_t     prevHeld[16] = {0};
  MidiRipple  ripples[MAX_MIDI_RIPPLES];
};

static void midiSpawnRipple(MidiPuddleData &d, uint16_t origin, uint8_t velocity) {
  int freeSlot = -1, oldestSlot = 0;
  uint8_t oldestState = 0;
  for (int i = 0; i < MAX_MIDI_RIPPLES; i++) {
    if (d.ripples[i].state == 0 && freeSlot < 0) freeSlot = i;
    if (d.ripples[i].state >= oldestState) { oldestState = d.ripples[i].state; oldestSlot = i; }
  }
  MidiRipple &r = d.ripples[(freeSlot >= 0) ? freeSlot : oldestSlot]; // steal the oldest/furthest-decayed if the pool is full
  r.state = 1; r.velocity = velocity; r.origin = origin;
}

static void mode_midi_puddles(void) {
  if (SEGLEN < 1) { SEGMENT.fill(SEGCOLOR(0)); return; }
  MidiPuddleData *dp = midiGetEffectData<MidiPuddleData>();
  if (!dp) { SEGMENT.fill(SEGCOLOR(0)); return; }
  MidiPuddleData &d = *dp;

  const uint8_t noteLow  = SEGMENT.custom1;   // metadata default 21  (A0)
  const uint8_t noteHigh = SEGMENT.custom2;   // metadata default 108 (C8)
  const uint8_t velocityFloor = SEGMENT.custom3 * 8; // 0-31 slider -> 0-248, same idea as MIDI Keys

  if (SEGMENT.check1) SEGMENT.fadeToBlackBy(40); // Trails
  else                SEGMENT.fill(BLACK);

  // Base glow (the "clone of MIDI Keys" part) uses a fixed decay curve -
  // ripple tuning gets the slider budget instead, since that's the point
  // of this effect.
  midiApplyFadeMs(300.0f, 4000.0f);

  if (noteHigh > noteLow) {
    midiForEachNewTrigger(d.prevHeld, [&](uint8_t note) {
      MidiTriggerParams p;
      if (!midiTriggerParamsFor(note, noteLow, noteHigh, velocityFloor, p)) return;
      midiSpawnRipple(d, (uint16_t)p.pos, p.velocity);
    });

    midiRenderKeySections(noteLow, noteHigh, velocityFloor);
  }

  const uint8_t rippleWidth = (uint8_t)constrain((int)map(SEGMENT.intensity, 0, 255, 2, 12), 2, 12);

  for (int i = 0; i < MAX_MIDI_RIPPLES; i++) {
    MidiRipple &r = d.ripples[i];
    if (!r.state) continue;

    const unsigned rippleDecay  = (SEGMENT.speed >> 4) + 1; // faster propagation = faster decay, mirrors mode_ripple
    const unsigned propagation  = ((r.state / rippleDecay - 1) * (SEGMENT.speed + 1));
    const int      propI        = propagation >> 8;
    const unsigned propF        = propagation & 0xFF;
    const unsigned baseAmp      = (r.state < 17) ? triwave8((r.state - 1) * 8) : map(r.state, 17, 255, 255, 2);
    const unsigned amp          = scale8(baseAmp, (r.velocity * 2u > 255u) ? 255 : r.velocity * 2u);
    const uint32_t col          = SEGMENT.color_from_palette(r.origin, true, PALETTE_SOLID_WRAP, 0);
    const int      left         = (int)r.origin - propI - (rippleWidth / 2) - 1;
    const int      right        = (int)r.origin + propI + (rippleWidth / 2) + 1;

    for (int v = 0; v < rippleWidth; v++) {
      const uint8_t mag = scale8(cubicwave8((uint8_t)((propF >> 2) + v * (256 / rippleWidth))), amp);
      SEGMENT.blendPixelColor(left + v,  col, mag);
      SEGMENT.blendPixelColor(right - v, col, mag);
    }
    r.state = (r.state + rippleDecay > 254) ? 0 : (uint8_t)(r.state + rippleDecay);
  }
}
static const char _data_FX_MODE_MIDI_PUDDLES[] PROGMEM =
  "MIDI Puddles@Ripple speed,Ripple width,Note low,Note high,Velocity floor,Trails;;!;1;sx=180,ix=100,c1=21,c2=108,c3=0";

/////////////////////////////////////////////////////////////////////////////
// MIDI Comet: every fresh note strike launches a short comet from that
// note's position. Direction is melodic, not positional: it's the
// *reverse* of the interval from the previous note played. Pitch went up
// from the last note -> comet flies down; pitch went down -> comet flies
// up; the very first note (no previous one yet) defaults to down. E.g.
// C3, D4, E5, D4 (up, up, down) fires down, down, down, up. Unlike
// Puddles' short-range splash, a comet needs to be able to cross the
// *entire* segment, so it tracks a continuous floating-point position
// advanced by real elapsed time each frame (the same style as the note
// decay model in midi_state.h) instead of the bounded 8-bit ripple-state
// trick, which only ever covers a small fixed radius regardless of tuning.
/////////////////////////////////////////////////////////////////////////////
static constexpr uint8_t MAX_MIDI_COMETS = 16;
static constexpr uint8_t MIDI_NO_PREV_NOTE = 0xFF;

struct MidiComet {
  bool     active = false;
  uint8_t  velocity = 0;
  float    pos = 0.0f;      // current position, advances every frame
  uint16_t spawnPx = 0;     // pixel it spawned at - fixes its color_from_palette(mapping=true) index for its whole life
  int8_t   dir = 1;
  uint32_t spawnMs = 0;
};

struct MidiCometData {
  uint8_t    prevHeld[16] = {0};
  uint8_t    lastTriggerNote = MIDI_NO_PREV_NOTE; // previous note played, for interval-reversed direction
  MidiComet  comets[MAX_MIDI_COMETS];
};

static void midiSpawnComet(MidiCometData &d, float pos, int8_t dir, uint8_t velocity, uint32_t nowMs) {
  int slot = 0;
  uint32_t oldestSpawn = 0xFFFFFFFF;
  for (int i = 0; i < MAX_MIDI_COMETS; i++) {
    if (!d.comets[i].active) { slot = i; oldestSpawn = 0; break; } // free slot always wins immediately
    if (d.comets[i].spawnMs < oldestSpawn) { oldestSpawn = d.comets[i].spawnMs; slot = i; }
  }
  MidiComet &c = d.comets[slot];
  c.active = true; c.pos = pos; c.dir = dir; c.spawnPx = (uint16_t)pos; c.velocity = velocity; c.spawnMs = nowMs;
}

static void mode_midi_comet(void) {
  if (SEGLEN < 1) { SEGMENT.fill(SEGCOLOR(0)); return; }
  MidiCometData *dp = midiGetEffectData<MidiCometData>();
  if (!dp) { SEGMENT.fill(SEGCOLOR(0)); return; }
  MidiCometData &d = *dp;

  const uint8_t  noteLow  = SEGMENT.custom1;
  const uint8_t  noteHigh = SEGMENT.custom2;
  const uint8_t  velocityFloor = SEGMENT.custom3 * 8;
  const uint32_t nowMs   = strip.now;
  const uint32_t deltaMs = midiFrameDeltaMs();

  SEGMENT.fadeToBlackBy(SEGMENT.check1 ? 10 : 60); // Long trails checkbox: subtler fade for longer streaks

  if (noteHigh > noteLow) {
    midiForEachNewTrigger(d.prevHeld, [&](uint8_t note) {
      MidiTriggerParams p;
      if (!midiTriggerParamsFor(note, noteLow, noteHigh, velocityFloor, p)) return;

      // Direction is the *reverse* of the melodic interval from the last
      // note played: pitch went up -> comet flies down, pitch went down ->
      // comet flies up. No previous note yet (or a repeated pitch) defaults
      // to down.
      int8_t dir = -1;
      if (d.lastTriggerNote != MIDI_NO_PREV_NOTE) {
        if (note > d.lastTriggerNote)      dir = -1;
        else if (note < d.lastTriggerNote) dir = 1;
      }
      d.lastTriggerNote = note;

      midiSpawnComet(d, p.pos, dir, p.velocity, nowMs);
    });
  }

  const float   speedPxPerMs = map(SEGMENT.speed, 0, 255, 5, 200) / 1000.0f; // 0.005 - 0.2 px/ms
  const uint32_t lifeMs      = map(SEGMENT.intensity, 0, 255, 400, 6000);
  const uint8_t  tailLen     = (uint8_t)constrain((int)map(SEGMENT.intensity, 0, 255, 3, 14), 3, 14);

  for (int i = 0; i < MAX_MIDI_COMETS; i++) {
    MidiComet &c = d.comets[i];
    if (!c.active) continue;

    const uint32_t ageMs = nowMs - c.spawnMs;
    if (ageMs >= lifeMs || c.pos < -(float)tailLen || c.pos > (float)(SEGLEN - 1 + tailLen)) { c.active = false; continue; }

    const uint8_t  envelope = 255 - (uint8_t)constrain((int32_t)(ageMs * 255 / lifeMs), 0, 255); // fades out over its lifetime
    const uint8_t  ampBase  = scale8(envelope, (c.velocity * 2u > 255u) ? 255 : c.velocity * 2u);
    const uint32_t col      = SEGMENT.color_from_palette(c.spawnPx, true, PALETTE_SOLID_WRAP, 0);
    const int      headPx   = (int)c.pos;

    for (int t = 0; t < tailLen; t++) {
      const int px = headPx - c.dir * t;
      if (px < 0 || px >= (int)SEGLEN) continue;
      const uint8_t mag = scale8(ampBase, (uint8_t)(255 - (t * 255 / tailLen)));
      SEGMENT.blendPixelColor(px, col, mag);
    }

    c.pos += c.dir * speedPxPerMs * deltaMs;
  }
}
static const char _data_FX_MODE_MIDI_COMET[] PROGMEM =
  "MIDI Comet@Comet speed,Tail length,Note low,Note high,Velocity floor,Long trails;;!;1;sx=140,ix=120,c1=21,c2=108,c3=0";
