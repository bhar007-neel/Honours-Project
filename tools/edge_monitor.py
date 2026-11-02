#!/usr/bin/env python3
"""Decode edge-protocol frames on a development PC.

An independent Python implementation of shared/protocol/edge_protocol.h, used
to inspect the STM32's binary output (APP_OUTPUT_TEXT = 0) before the Pi is
connected, and to cross-check the C decoder.

Examples:
    python tools/edge_monitor.py --port COM5               # live, needs pyserial
    python tools/edge_monitor.py --port COM5 --save run.bin
    python tools/edge_monitor.py --file run.bin
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import BinaryIO, Callable, Iterator

SOF = b"\xA5\x5A"
VERSION = 1
HEADER_LEN = 11
CRC_LEN = 2
MAX_PAYLOAD = 64

MSG_SENSOR = 0x01
MSG_STATUS = 0x02
MSG_COMMAND = 0x10
CMD_SET_PERIOD = 0x01
SILENCE_MS = 3000
MSG_NAMES = {MSG_SENSOR: "SENSOR", MSG_STATUS: "STATUS", MSG_COMMAND: "COMMAND"}

SENSOR_FORMAT = struct.Struct("<hHI")
STATUS_FORMAT = struct.Struct("<IIHHHHI")
COMMAND_FORMAT = struct.Struct("<BH")


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


@dataclass
class Frame:
    msg_type: int
    sequence: int
    timestamp_ms: int
    payload: bytes


@dataclass
class DecoderStats:
    frames_ok: int = 0
    bad_version: int = 0
    bad_length: int = 0
    bad_crc: int = 0
    bytes_discarded: int = 0


@dataclass
class Decoder:
    """Streaming decoder with the same resynchronization rules as the C version."""

    buf: bytearray = field(default_factory=bytearray)
    stats: DecoderStats = field(default_factory=DecoderStats)

    def feed(self, data: bytes) -> Iterator[Frame | str]:
        """Yields a Frame for each valid frame and an error name for each rejection."""
        self.buf.extend(data)
        while self.buf:
            if self.buf[0] != SOF[0] or (len(self.buf) >= 2 and self.buf[1] != SOF[1]):
                self._drop()
                continue
            if len(self.buf) < HEADER_LEN:
                return
            if self.buf[2] != VERSION:
                self.stats.bad_version += 1
                self._drop()
                yield "bad_version"
                continue
            payload_len = self.buf[10]
            if payload_len > MAX_PAYLOAD:
                self.stats.bad_length += 1
                self._drop()
                yield "bad_length"
                continue
            total = HEADER_LEN + payload_len + CRC_LEN
            if len(self.buf) < total:
                return
            body = bytes(self.buf[2 : HEADER_LEN + payload_len])
            (received_crc,) = struct.unpack_from("<H", self.buf, HEADER_LEN + payload_len)
            if crc16_ccitt_false(body) != received_crc:
                self.stats.bad_crc += 1
                self._drop()
                yield "bad_crc"
                continue
            msg_type, sequence, timestamp_ms = struct.unpack_from("<BHI", self.buf, 3)
            payload = bytes(self.buf[HEADER_LEN : HEADER_LEN + payload_len])
            del self.buf[:total]
            self.stats.frames_ok += 1
            yield Frame(msg_type, sequence, timestamp_ms, payload)

    def _drop(self) -> None:
        del self.buf[0]
        self.stats.bytes_discarded += 1


def centi(value: int) -> str:
    sign = "-" if value < 0 else ""
    return f"{sign}{abs(value) // 100}.{abs(value) % 100:02d}"


def describe(frame: Frame) -> str:
    name = MSG_NAMES.get(frame.msg_type, f"0x{frame.msg_type:02X}")
    head = f"type={name} seq={frame.sequence} src_ms={frame.timestamp_ms}"
    if frame.msg_type == MSG_SENSOR and len(frame.payload) == SENSOR_FORMAT.size:
        temp, hum, press = SENSOR_FORMAT.unpack(frame.payload)
        return f"{head} temp_c={centi(temp)} hum_pct={centi(hum)} press_pa={press}"
    if frame.msg_type == MSG_STATUS and len(frame.payload) == STATUS_FORMAT.size:
        sent, dropped, period, jitter, overruns, stack, heap = STATUS_FORMAT.unpack(frame.payload)
        return (f"{head} sent={sent} dropped={dropped} period_ms={period} "
                f"max_jitter_us={jitter} overruns={overruns} stack_min_words={stack} "
                f"heap_free={heap}")
    if frame.msg_type == MSG_COMMAND and len(frame.payload) == COMMAND_FORMAT.size:
        command_id, period = COMMAND_FORMAT.unpack(frame.payload)
        command = "set_period" if command_id == CMD_SET_PERIOD else f"0x{command_id:02X}"
        return f"{head} command={command} period_ms={period}"
    return f"{head} payload={frame.payload.hex()}"


def classify_sequence(last: int | None, sequence: int) -> tuple[str, int | None, int]:
    """Same four outcomes as shared/common/link_health.c.

    Returns the event name, the new cursor (unchanged on duplicate or reorder),
    and how many sequence numbers a gap skipped.
    """
    if last is None:
        return "first", sequence, 0
    forward = (sequence - last) & 0xFFFF
    if forward == 0:
        return "duplicate", last, 0
    if forward >= 0x8000:
        return "reorder", last, 0
    if forward == 1:
        return "in_order", sequence, 0
    return "gap", sequence, forward - 1


def serial_chunks(port: str, baud: int) -> Iterator[bytes]:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        sys.exit("pyserial is required for --port: python -m pip install pyserial")
    with serial.Serial(port, baud, timeout=0.2) as link:
        while True:
            # An empty read is a timeout. The caller uses it to notice silence.
            yield link.read(256)


def file_chunks(stream: BinaryIO) -> Iterator[bytes]:
    while chunk := stream.read(4096):
        yield chunk


def run(chunks: Iterator[bytes], save: BinaryIO | None, emit: Callable[[str], None]) -> Decoder:
    decoder = Decoder()
    start = time.monotonic()
    last_seq: int | None = None
    counts = {"in_order": 0, "gap": 0, "duplicate": 0, "reorder": 0, "missing": 0}
    heard = False
    silent = False
    last_heard = 0
    silence_events = 0
    try:
        for chunk in chunks:
            rx_ms = int((time.monotonic() - start) * 1000)
            if not chunk:
                if heard and not silent and rx_ms - last_heard >= SILENCE_MS:
                    silent = True
                    silence_events += 1
                    emit(f"rx_ms={rx_ms} event=silence")
                continue
            if save:
                save.write(chunk)
            heard = True
            silent = False
            last_heard = rx_ms
            for item in decoder.feed(chunk):
                rx_ms = int((time.monotonic() - start) * 1000)
                if isinstance(item, str):
                    emit(f"rx_ms={rx_ms} event={item}")
                    continue
                event, last_seq, missing = classify_sequence(last_seq, item.sequence)
                if event in counts:
                    counts[event] += 1
                counts["missing"] += missing
                emit(f"rx_ms={rx_ms} event=frame seq_event={event} {describe(item)}")
    except KeyboardInterrupt:
        pass
    s = decoder.stats
    emit(f"summary frames_ok={s.frames_ok} bad_crc={s.bad_crc} bad_length={s.bad_length} "
         f"bad_version={s.bad_version} bytes_discarded={s.bytes_discarded} "
         f"seq_in_order={counts['in_order']} seq_gaps={counts['gap']} "
         f"seq_missing={counts['missing']} seq_duplicates={counts['duplicate']} "
         f"seq_reorders={counts['reorder']} silence={silence_events}")
    return decoder


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    source.add_argument("--file", type=argparse.FileType("rb"), help="raw capture, or - for stdin")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--save", type=argparse.FileType("wb"), help="also record raw bytes")
    args = parser.parse_args()

    chunks = serial_chunks(args.port, args.baud) if args.port else file_chunks(args.file)
    run(chunks, args.save, lambda line: print(line, flush=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
