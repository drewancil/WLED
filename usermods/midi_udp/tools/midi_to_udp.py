#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = [
#     "python-rtmidi",
# ]
# ///
"""
Bridges a local MIDI input device to the midi_udp WLED usermod.

Forwards raw 3-byte MIDI messages (note on/off, control change - sustain
pedal included) as-is over UDP, one message per packet, with no batching or
queuing, to keep latency minimal.

Run with: uv run midi_to_udp.py --list
"""
import argparse
import socket
import sys
import threading

import rtmidi


def list_ports():
    midi_in = rtmidi.MidiIn()
    ports = midi_in.get_ports()
    if not ports:
        print("No MIDI input ports found.")
        return
    for i, name in enumerate(ports):
        print(f"[{i}] {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="list MIDI input ports and exit")
    parser.add_argument("--port-index", type=int, help="MIDI input port index (see --list)")
    parser.add_argument("--wled-ip", help="IP address of the WLED device")
    parser.add_argument("--udp-port", type=int, default=6969, help="UDP port configured in the usermod (default 6969)")
    args = parser.parse_args()

    if args.list:
        list_ports()
        return

    if args.port_index is None or not args.wled_ip:
        parser.error("--port-index and --wled-ip are required (use --list to find the port index)")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.wled_ip, args.udp_port)

    midi_in = rtmidi.MidiIn()
    ports = midi_in.get_ports()
    if args.port_index >= len(ports):
        sys.exit(f"Port index {args.port_index} out of range (found {len(ports)} ports, use --list)")
    midi_in.open_port(args.port_index)
    midi_in.ignore_types(sysex=True, timing=True, active_sense=True)

    print(f"Forwarding '{ports[args.port_index]}' -> {args.wled_ip}:{args.udp_port} (Ctrl+C to stop)")

    def on_message(event, _data=None):
        message, _deltatime = event
        if len(message) != 3:
            return  # usermod expects exactly 3-byte messages; drop anything else (e.g. sysex, program change)
        sock.sendto(bytes(message), dest)

    midi_in.set_callback(on_message)

    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        midi_in.close_port()


if __name__ == "__main__":
    main()
