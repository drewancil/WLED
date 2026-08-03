#include "wled.h"
#include <WiFiUdp.h>
#include "midi_state.h"
#include "midi_effects.h"

MidiNoteState midiState;

class MidiUdpUsermod : public Usermod {
  private:
    WiFiUDP udp;
    bool     enabled = true;
    uint16_t udpPort = 6969;
    bool     udpBegun = false;

    static const char _name[];
    static const char _enabled[];
    static const char _udpPort[];

    void noteOn(uint8_t note, uint8_t velocity) {
      MidiNote &n = midiState.notes[note];
      const float target = min(velocity * 2.0f, 255.0f);
      if (target >= n.level) { // a softer retrigger doesn't dim an already-brighter held/fading note
        n.velocity = velocity;
        n.level    = target;
      }
      n.held = true; // key is down either way, so decay stops here regardless of which value won
      midiState.lastNote       = note;
      midiState.lastVelocity   = velocity;
      midiState.lastNoteOnTime = millis();
      midiState.noteOnCount++;
    }

    void noteOff(uint8_t note) {
      MidiNote &n = midiState.notes[note];
      if (!n.held) return; // stray/duplicate note-off; leave any decay already in progress alone
      n.held = false; // level is untouched here - that's what makes release seamless
    }

  public:
    void setup() override {
      strip.addEffect(255, &mode_midi_keys,  _data_FX_MODE_MIDI_KEYS);
      strip.addEffect(255, &mode_midi_flash, _data_FX_MODE_MIDI_FLASH);
    }

    void connected() override {
      if (!enabled) return;
      udp.begin(udpPort);
      udpBegun = true;
    }

    void loop() override {
      if (!enabled || !udpBegun) return;

      int packetSize;
      while ((packetSize = udp.parsePacket()) > 0) {
        if (packetSize != 3) continue; // drop malformed packets; next parsePacket() moves past it

        uint8_t buf[3];
        udp.read(buf, 3);

        const uint8_t status = buf[0] & 0xF0;
        const uint8_t data1  = buf[1] & 0x7F;
        const uint8_t data2  = buf[2] & 0x7F;

        switch (status) {
          case 0x90: // note on (velocity 0 is a note-off per MIDI convention)
            if (data2 == 0) noteOff(data1);
            else            noteOn(data1, data2);
            break;
          case 0x80: // note off
            noteOff(data1);
            break;
          case 0xB0: // control change
            if (data1 == MIDI_CC_SUSTAIN) midiState.sustain = (data2 >= 64);
            break;
          default:
            break; // ignore other MIDI message types
        }
      }
    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)]  = enabled;
      top[FPSTR(_udpPort)]  = udpPort;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      bool configComplete = !top.isNull();
      configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled, true);
      configComplete &= getJsonValue(top[FPSTR(_udpPort)], udpPort, 6969);
      return configComplete;
    }
};

const char MidiUdpUsermod::_name[]    PROGMEM = "MIDI UDP";
const char MidiUdpUsermod::_enabled[] PROGMEM = "enabled";
const char MidiUdpUsermod::_udpPort[] PROGMEM = "udpPort";

static MidiUdpUsermod midi_udp_usermod;
REGISTER_USERMOD(midi_udp_usermod);
