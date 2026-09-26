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
MSG_NAMES = {MSG_SENSOR: "SENSOR", MSG_STATUS: "STATUS", 0x10: "COMMAND"}

SENSOR_FORMAT = struct.Struct("<hHI")
STATUS_FORMAT = struct.Struct("<IIHHHHI")


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
    return f"{head} payload={frame.payload.hex()}"


def serial_chunks(port: str, baud: int) -> Iterator[bytes]:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        sys.exit("pyserial is required for --port: python -m pip install pyserial")
    with serial.Serial(port, baud, timeout=0.2) as link:
        while True:
            chunk = link.read(256)
            if chunk:
                yield chunk


def file_chunks(stream: BinaryIO) -> Iterator[bytes]:
    while chunk := stream.read(4096):
        yield chunk


def run(chunks: Iterator[bytes], save: BinaryIO | None, emit: Callable[[str], None]) -> Decoder:
    decoder = Decoder()
    start = time.monotonic()
    last_seq: int | None = None
    gaps = 0
    try:
        for chunk in chunks:
            if save:
                save.write(chunk)
            for item in decoder.feed(chunk):
                rx_ms = int((time.monotonic() - start) * 1000)
                if isinstance(item, str):
                    emit(f"rx_ms={rx_ms} event={item}")
                    continue
                if last_seq is not None and item.sequence != (last_seq + 1) & 0xFFFF:
                    gaps += 1
                last_seq = item.sequence
                emit(f"rx_ms={rx_ms} event=frame {describe(item)}")
    except KeyboardInterrupt:
        pass
    s = decoder.stats
    emit(f"summary frames_ok={s.frames_ok} bad_crc={s.bad_crc} bad_length={s.bad_length} "
         f"bad_version={s.bad_version} bytes_discarded={s.bytes_discarded} seq_gaps={gaps}")
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
