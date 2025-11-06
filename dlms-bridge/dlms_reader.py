#!/usr/bin/env python3
"""Minimal DLMS/COSEM client for Microstar meters over TCP/IP.

This script sends a fixed HDLC/DLMS handshake (SNRM + AARQ), reads one or more
COSEM register attributes and prints the resolved value applying the scaler/unit
information provided by the meter. The implementation is intentionally lean so
that the individual HDLC frames correspond to the examples documented in the
"9.2. Microstar DLMS Protocol Guide" and in the sample capture located in the
repository.

The code only depends on Python's standard library. It is **not** intended to be
feature complete, but to serve as an inspectable reference implementation that
can be extended as needed.
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
import time
from dataclasses import dataclass
from decimal import Decimal, getcontext
from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple

# Increase decimal precision to avoid rounding issues when applying scaler.
getcontext().prec = 12


# ---------------------------------------------------------------------------
# Utility helpers
# ---------------------------------------------------------------------------


def _crc16_hdlc(data: bytes) -> int:
    """Compute the HDLC CRC16 (x^16 + x^12 + x^5 + 1, reflected polynomial).

    The algorithm matches IEC 62056-46 requirements and the values used in the
    Microstar examples. The returned integer is the CRC with the least
    significant byte transmitted first.
    """

    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return (~crc) & 0xFFFF


def _encode_hdlc_address(value: int) -> bytes:
    """Encode a HDLC address value according to IEC 62056-46."""

    if value < 0:
        raise ValueError("HDLC addresses cannot be negative")
    if value < 0x80:
        return bytes([(value << 1) | 0x01])
    if value < 0x4000:
        hi = ((value >> 7) & 0x7F) << 1
        lo = (value & 0x7F) << 1 | 0x01
        return bytes([hi, lo])
    if value < 0x200000:
        b1 = ((value >> 14) & 0x7F) << 1
        b2 = ((value >> 7) & 0x7F) << 1
        b3 = (value & 0x7F) << 1 | 0x01
        return bytes([b1, b2, b3])
    raise ValueError("HDLC address too large for this implementation")


def _decode_hdlc_address(data: bytes, start: int = 0) -> Tuple[int, int]:
    """Decode a HDLC address starting at *start* and return (value, consumed)."""

    value = 0
    for offset in range(start, len(data)):
        byte = data[offset]
        value = (value << 7) | (byte >> 1)
        if byte & 0x01:
            consumed = offset - start + 1
            return value, consumed
    raise ValueError("unterminated HDLC address")


def _combine_server_address(logical: int, physical: int) -> int:
    """Combine logical + physical server addresses into single HDLC value."""

    if logical < 0 or logical > 0x7F:
        raise ValueError("Server logical address must be in range 0..127")
    if physical < 0 or physical > 0x7FFF:
        raise ValueError("Server physical address must be in range 0..32767")
    return (logical << 7) | physical


# ---------------------------------------------------------------------------
# Frame representation & building
# ---------------------------------------------------------------------------


@dataclass
class ParsedFrame:
    """Structured representation of a decoded HDLC frame."""

    format_field: int
    destination: int
    source: int
    control: int
    frame_type: str
    send_sequence: Optional[int]
    receive_sequence: Optional[int]
    poll_final: int
    info: bytes
    hcs_valid: bool
    fcs_valid: bool

    @property
    def is_valid(self) -> bool:
        return self.hcs_valid and self.fcs_valid


def _build_frame(control: int, dest: int, src: int, info: bytes) -> bytes:
    """Construct an HDLC frame with automatic length, HCS, and FCS."""

    dest_bytes = _encode_hdlc_address(dest)
    src_bytes = _encode_hdlc_address(src)
    include_hcs = len(info) > 0
    body_len = (
        2  # format field itself
        + len(dest_bytes)
        + len(src_bytes)
        + 1  # control
        + (2 if include_hcs else 0)
        + len(info)
        + 2  # FCS
    )
    format_field = 0xA000 | body_len
    format_bytes = format_field.to_bytes(2, "big")
    header = format_bytes + dest_bytes + src_bytes + bytes([control])

    hcs_bytes = b""
    if include_hcs:
        hcs = _crc16_hdlc(header)
        hcs_bytes = hcs.to_bytes(2, "little")

    fcs_input = header + hcs_bytes + info
    fcs = _crc16_hdlc(fcs_input)
    fcs_bytes = fcs.to_bytes(2, "little")

    return b"".join((b"\x7E", header, hcs_bytes, info, fcs_bytes, b"\x7E"))


def _build_snrm_info(max_info_tx: int, max_info_rx: int) -> bytes:
    """Construct the SNRM negotiation information block."""

    return bytes(
        [
            0x81,
            0x80,
            0x12,  # parameter identifier for HDLC parameters
            0x05,
            0x02,
            (max_info_tx >> 8) & 0xFF,
            max_info_tx & 0xFF,
            0x06,
            0x02,
            (max_info_rx >> 8) & 0xFF,
            max_info_rx & 0xFF,
            0x07,
            0x04,
            0x00,
            0x00,
            0x00,
            0x01,  # window size transmit
            0x08,
            0x04,
            0x00,
            0x00,
            0x00,
            0x01,  # window size receive
        ]
    )


def _build_aarq_apdu(password: bytes) -> bytes:
    """Return the ACSE AARQ APDU with the given authentication value."""

    if not (1 <= len(password) <= 16):
        raise ValueError("Password must be 1..16 bytes")

    prefix = bytes.fromhex(
        "6036"
        "A109060760857405080101"  # Application context name
        "8A020780"  # ACSE requirements
        "8B0760857405080201"  # Mechanism name (low level security 0/1)
    )
    auth_len = len(password)
    auth_field = b"".join(
        (
            b"\xAC",
            bytes([auth_len + 2]),
            b"\x80",
            bytes([auth_len]),
            password,
        )
    )
    suffix = bytes.fromhex(
        "BE10040E01000000065F1F0400007E1F04B0"  # Proposed quality & conformance
    )
    return b"\xE6\xE6\x00" + prefix + auth_field + suffix


def _build_get_apdu(
    invoke_id: int,
    class_id: int,
    logical_name: bytes,
    attribute_id: int,
) -> bytes:
    """Build the DLMS GET.request normal APDU."""

    if len(logical_name) != 6:
        raise ValueError("Logical name must consist of 6 bytes")
    return b"".join(
        (
            b"\xE6\xE6\x00",  # LLC for requests
            b"\xC0\x01",  # GET.request normal tag
            bytes([invoke_id & 0xFF]),
            class_id.to_bytes(2, "big"),
            logical_name,
            bytes([attribute_id & 0xFF]),
            b"\x00",  # No selective access
        )
    )


def _extract_get_response_payload(info: bytes, expected_invoke_id: int) -> bytes:
    """Validate a GET.response APDU and return the data payload."""

    if not info.startswith(b"\xE6\xE7\x00"):
        raise RuntimeError("Malformed GET response (missing LLC header)")
    if len(info) < 7:
        raise RuntimeError("Malformed GET response (too short)")
    if info[3] != 0xC4:
        raise RuntimeError("Unexpected GET response tag")
    if info[4] != 0x01:
        raise RuntimeError("Unsupported GET response type")

    invoke_field = info[5]
    if invoke_field != (expected_invoke_id & 0xFF):
        raise RuntimeError("Invoke-ID mismatch in GET response")

    result = info[6]
    if result != 0x00:
        raise RuntimeError(f"GET response returned error code 0x{result:02X}")

    return info[7:]


# ---------------------------------------------------------------------------
# Parsing helpers
# ---------------------------------------------------------------------------


def _determine_frame_type(control: int) -> Tuple[str, Optional[int], Optional[int], int]:
    if (control & 0x01) == 0:
        ns = (control >> 1) & 0x07
        nr = (control >> 5) & 0x07
        pf = (control >> 4) & 0x01
        return "I", ns, nr, pf
    if (control & 0x03) == 0x01:
        nr = (control >> 5) & 0x07
        pf = (control >> 4) & 0x01
        return "S", None, nr, pf
    pf = (control >> 4) & 0x01
    return "U", None, None, pf


def _parse_frame(raw: bytes) -> ParsedFrame:
    if len(raw) < 5 or raw[0] != 0x7E or raw[-1] != 0x7E:
        raise ValueError("Invalid HDLC frame boundary")

    body = raw[1:-1]
    format_field = int.from_bytes(body[:2], "big")
    idx = 2
    dest, consumed = _decode_hdlc_address(body, idx)
    idx += consumed
    src, consumed = _decode_hdlc_address(body, idx)
    idx += consumed
    control = body[idx]
    idx += 1

    payload = body[idx:]
    if len(payload) < 2:
        raise ValueError("Incomplete HDLC frame (missing FCS)")
    fcs_bytes = payload[-2:]
    payload = payload[:-2]

    if payload:
        if len(payload) < 2:
            raise ValueError("Invalid payload length for frame with information")
        hcs_bytes = payload[:2]
        info = payload[2:]
    else:
        hcs_bytes = b""
        info = b""

    header = body[: idx]
    hcs_valid = True
    if hcs_bytes:
        hcs_calc = _crc16_hdlc(header)
        hcs_valid = hcs_calc == int.from_bytes(hcs_bytes, "little")

    fcs_calc = _crc16_hdlc(header + hcs_bytes + info)
    fcs_valid = fcs_calc == int.from_bytes(fcs_bytes, "little")

    frame_type, ns, nr, pf = _determine_frame_type(control)
    return ParsedFrame(
        format_field=format_field,
        destination=dest,
        source=src,
        control=control,
        frame_type=frame_type,
        send_sequence=ns,
        receive_sequence=nr,
        poll_final=pf,
        info=info,
        hcs_valid=hcs_valid,
        fcs_valid=fcs_valid,
    )


# ---------------------------------------------------------------------------
# DLMS data parsing
# ---------------------------------------------------------------------------


class DlmsDataError(ValueError):
    pass


def _parse_data(buffer: bytes) -> Tuple[Any, bytes]:
    if not buffer:
        raise DlmsDataError("Unexpected end of data")
    tag = buffer[0]
    if tag == 0x00:  # null-data
        return None, buffer[1:]
    if tag == 0x02:  # structure
        if len(buffer) < 2:
            raise DlmsDataError("Malformed structure")
        count = buffer[1]
        remaining = buffer[2:]
        items = []
        for _ in range(count):
            value, remaining = _parse_data(remaining)
            items.append(value)
        return items, remaining
    if tag == 0x05:  # double-long (signed 32)
        if len(buffer) < 5:
            raise DlmsDataError("Malformed double-long")
        return int.from_bytes(buffer[1:5], "big", signed=True), buffer[5:]
    if tag == 0x06:  # double-long-unsigned (unsigned 32)
        if len(buffer) < 5:
            raise DlmsDataError("Malformed double-long-unsigned")
        return int.from_bytes(buffer[1:5], "big", signed=False), buffer[5:]
    if tag == 0x09:  # octet-string
        if len(buffer) < 2:
            raise DlmsDataError("Malformed octet-string")
        length = buffer[1]
        if len(buffer) < 2 + length:
            raise DlmsDataError("Incomplete octet-string")
        return buffer[2 : 2 + length], buffer[2 + length :]
    if tag == 0x0A:  # visible-string
        if len(buffer) < 2:
            raise DlmsDataError("Malformed visible-string")
        length = buffer[1]
        if len(buffer) < 2 + length:
            raise DlmsDataError("Incomplete visible-string")
        raw = buffer[2 : 2 + length]
        try:
            decoded = raw.decode("ascii")
        except UnicodeDecodeError:
            decoded = raw.decode("latin-1", errors="ignore")
        return decoded, buffer[2 + length :]
    if tag == 0x0F:  # integer (8-bit signed)
        if len(buffer) < 2:
            raise DlmsDataError("Malformed integer8")
        return struct.unpack("!b", buffer[1:2])[0], buffer[2:]
    if tag == 0x10:  # long (16-bit signed)
        if len(buffer) < 3:
            raise DlmsDataError("Malformed long")
        return int.from_bytes(buffer[1:3], "big", signed=True), buffer[3:]
    if tag == 0x11:  # unsigned (8-bit)
        if len(buffer) < 2:
            raise DlmsDataError("Malformed unsigned8")
        return buffer[1], buffer[2:]
    if tag == 0x12:  # long-unsigned (16-bit)
        if len(buffer) < 3:
            raise DlmsDataError("Malformed long-unsigned")
        return int.from_bytes(buffer[1:3], "big", signed=False), buffer[3:]
    if tag == 0x14:  # long64-unsigned (64-bit unsigned)
        if len(buffer) < 9:
            raise DlmsDataError("Malformed long64-unsigned")
        return int.from_bytes(buffer[1:9], "big", signed=False), buffer[9:]
    if tag == 0x16:  # enum
        if len(buffer) < 2:
            raise DlmsDataError("Malformed enum")
        return buffer[1], buffer[2:]
    raise DlmsDataError(f"Unsupported DLMS data type 0x{tag:02X}")


UNIT_LABELS: Dict[int, str] = {
    0: "",
    1: "year",
    5: "h",
    6: "min",
    7: "s",
    8: "°",
    9: "K",
    20: "Wh",
    25: "varh",
    27: "VAh",
    29: "W",
    30: "VA",
    31: "var",
    32: "V",
    33: "A",
    35: "Hz",
    37: "°C",
    38: "%",
}


def _resolve_unit_label(unit_code: int, preferred: Optional[str] = None) -> Tuple[str, Optional[str]]:
    reported = UNIT_LABELS.get(unit_code, f"code {unit_code}")
    if preferred and reported != preferred:
        return preferred, reported
    return reported, None


# ---------------------------------------------------------------------------
# DLMS client implementation
# ---------------------------------------------------------------------------


class DLMSClient:
    def __init__(
        self,
        host: str,
        port: int,
        server_logical: int,
        server_physical: int,
        client_sap: int,
        max_info_length: Optional[int],
        password: bytes,
        verbose: bool = False,
        timeout: float = 5.0,
    ) -> None:
        self.host = host
        self.port = port
        self.server_address = _combine_server_address(server_logical, server_physical)
        self.client_address = client_sap
        self.max_info_length = max_info_length
        self.password = password
        self.verbose = verbose
        self.timeout = timeout

        self._sock: Optional[socket.socket] = None
        self._send_seq = 0
        self._recv_seq = 0
        self._invoke_id = 1

    # ---- logging helpers -------------------------------------------------
    def _log(self, message: str) -> None:
        if self.verbose:
            now = time.strftime("%H:%M:%S")
            print(f"[{now}] {message}")

    def _log_frame(self, label: str, frame: bytes) -> None:
        if self.verbose:
            hex_repr = " ".join(f"{byte:02X}" for byte in frame)
            self._log(f"{label} {hex_repr}")

    # ---- socket helpers --------------------------------------------------
    def _send_frame(self, frame: bytes) -> None:
        if not self._sock:
            raise RuntimeError("Not connected")
        self._log_frame("TX", frame)
        self._sock.sendall(frame)

    def _read_frame(self, timeout: Optional[float] = None) -> bytes:
        if not self._sock:
            raise RuntimeError("Not connected")
        self._sock.settimeout(timeout if timeout is not None else self.timeout)
        buffer = bytearray()
        while True:
            chunk = self._sock.recv(1)
            if not chunk:
                raise ConnectionError("Socket closed while waiting for frame")
            byte = chunk[0]
            if not buffer:
                if byte != 0x7E:
                    continue
                buffer.append(byte)
            else:
                buffer.append(byte)
                if byte == 0x7E:
                    if len(buffer) == 1:  # consecutive flags
                        buffer.clear()
                        buffer.append(byte)
                        continue
                    frame = bytes(buffer)
                    self._log_frame("RX", frame)
                    return frame

    def _drain_initial_frames(self) -> None:
        if not self._sock:
            return
        self._sock.settimeout(0.2)
        try:
            while True:
                frame = self._read_frame(timeout=0.2)
                parsed = _parse_frame(frame)
                self._log(f"Discarding unsolicited frame type {parsed.frame_type}")
        except (socket.timeout, ConnectionError):
            pass
        finally:
            self._sock.settimeout(self.timeout)

    # ---- HDLC control ----------------------------------------------------
    def _build_i_control(self, poll: bool = True) -> int:
        pf = 1 if poll else 0
        return (
            ((self._recv_seq & 0x07) << 5)
            | ((pf & 0x01) << 4)
            | ((self._send_seq & 0x07) << 1)
        )

    def _increment_send_seq(self) -> None:
        self._send_seq = (self._send_seq + 1) % 8

    def _expect_i_response(self, frame: bytes, description: str) -> ParsedFrame:
        parsed = _parse_frame(frame)
        if parsed.frame_type != "I":
            raise RuntimeError(f"Expected I-frame for {description}, got {parsed.frame_type}")
        if not parsed.is_valid:
            raise RuntimeError(f"Checksum mismatch on {description} response")
        if parsed.receive_sequence is None:
            raise RuntimeError("Missing receive sequence number in response")
        expected_nr = self._send_seq % 8
        if parsed.receive_sequence != expected_nr:
            raise RuntimeError(
                f"Unexpected receive sequence. Expected {expected_nr}, got {parsed.receive_sequence}"
            )
        self._recv_seq = parsed.send_sequence if parsed.send_sequence is not None else 0
        return parsed

    # ---- connectivity ----------------------------------------------------
    def connect(self) -> None:
        if self._sock:
            return
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._log(f"Connected to {self.host}:{self.port}")
        self._drain_initial_frames()

        # SNRM
        if self.max_info_length is not None:
            snrm_info = _build_snrm_info(self.max_info_length, self.max_info_length)
        else:
            snrm_info = b""
        snrm_frame = _build_frame(0x93, self.server_address, self.client_address, snrm_info)
        self._send_frame(snrm_frame)
        ua_frame = self._read_frame()
        ua = _parse_frame(ua_frame)
        if ua.frame_type != "U" or ua.control not in (0x73, 0x63):
            raise RuntimeError("Unexpected response to SNRM")
        if not ua.is_valid:
            raise RuntimeError("UA frame failed CRC validation")
        self._send_seq = 0
        self._recv_seq = 0
        self._log("HDLC link established")

        # AARQ
        aarq_info = _build_aarq_apdu(self.password)
        aarq_frame = _build_frame(self._build_i_control(), self.server_address, self.client_address, aarq_info)
        self._send_frame(aarq_frame)
        self._increment_send_seq()
        aare_frame = self._read_frame()
        aare = self._expect_i_response(aare_frame, "AARQ")
        if not aare.info.startswith(b"\xE6\xE7\x00\x61"):
            raise RuntimeError("Unexpected AARE payload")
        result = None
        info = aare.info[3:]
        for idx in range(len(info) - 4):
            if (
                info[idx] == 0xA2
                and info[idx + 1] == 0x03
                and info[idx + 2] == 0x02
                and info[idx + 3] == 0x01
            ):
                result = info[idx + 4]
                break
        if result is None:
            raise RuntimeError("AARE payload missing association result")
        if result != 0x00:
            raise RuntimeError(f"Association rejected with result code 0x{result:02X}")
        self._log("Application association established")

    def close(self) -> None:
        if not self._sock:
            return
        try:
            disc_frame = _build_frame(0x53, self.server_address, self.client_address, b"")
            self._send_frame(disc_frame)
            try:
                ua_frame = self._read_frame(timeout=2.0)
                ua = _parse_frame(ua_frame)
                if ua.control not in (0x73, 0x63):
                    self._log("Unexpected DISC response; ignoring")
            except (socket.timeout, ConnectionError):
                self._log("No UA response to DISC")
        finally:
            try:
                self._sock.close()
            finally:
                self._sock = None
                self._log("Connection closed")

    # ---- DLMS GET helper -------------------------------------------------
    def _next_invoke_id(self) -> int:
        value = self._invoke_id & 0xFF
        self._invoke_id = (self._invoke_id + 1) % 0x100
        if self._invoke_id == 0:
            self._invoke_id = 1
        return value

    def _send_get_request(self, class_id: int, ln: bytes, attribute_id: int) -> bytes:
        invoke_id = self._next_invoke_id()
        apdu = _build_get_apdu(invoke_id, class_id, ln, attribute_id)
        frame = _build_frame(self._build_i_control(), self.server_address, self.client_address, apdu)
        self._send_frame(frame)
        self._increment_send_seq()
        response_frame = self._read_frame()
        parsed = self._expect_i_response(response_frame, f"GET attribute {attribute_id}")
        return _extract_get_response_payload(parsed.info, invoke_id)

    # ---- Public API ------------------------------------------------------
    def read_register(self, obis: str, attribute: int = 2, scaler_attribute: int = 3) -> Tuple[Decimal, int, Any]:
        logical_name = obis_to_bytes(obis)
        class_id = 3  # Register class

        scaler_payload = self._send_get_request(class_id, logical_name, scaler_attribute)
        scaler_structure, remaining = _parse_data(scaler_payload)
        if remaining:
            self._log("Warning: unused bytes after scaler/unit structure")
        if not isinstance(scaler_structure, list) or len(scaler_structure) != 2:
            raise RuntimeError("Unexpected scaler/unit structure")
        scaler = scaler_structure[0]
        unit_code = scaler_structure[1]
        if not isinstance(scaler, int) or not isinstance(unit_code, int):
            raise RuntimeError("Malformed scaler/unit contents")

        value_payload = self._send_get_request(class_id, logical_name, attribute)
        value_raw, remaining = _parse_data(value_payload)
        if remaining:
            self._log("Warning: unused bytes after value payload")

        if isinstance(value_raw, (bytes, str)):
            raise RuntimeError("Received non-numeric register value")
        value = Decimal(value_raw) * (Decimal(10) ** scaler)
        return value, unit_code, value_raw


# ---------------------------------------------------------------------------
# OBIS helpers
# ---------------------------------------------------------------------------


def obis_to_bytes(obis: str) -> bytes:
    try:
        first, rest = obis.split(":", 1)
    except ValueError as exc:
        raise ValueError(f"Invalid OBIS code '{obis}'") from exc

    try:
        a_str, b_str = first.split("-")
        cde_part, *f_part = rest.split("*")
        c_str, d_str, e_str = cde_part.split(".")
        f_str = f_part[0] if f_part else "255"
        a = int(a_str)
        b = int(b_str)
        c = int(c_str)
        d = int(d_str)
        e = int(e_str)
        f = int(f_str)
    except (ValueError, IndexError) as exc:
        raise ValueError(f"Invalid OBIS code '{obis}'") from exc

    for label, value in zip(("A", "B", "C", "D", "E", "F"), (a, b, c, d, e, f)):
        if not 0 <= value <= 255:
            raise ValueError(f"OBIS field {label} out of range: {value}")
    return bytes([a, b, c, d, e, f])


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


MEASUREMENTS: Dict[str, Dict[str, str]] = {
    "voltage_l1": {
        "obis": "1-1:32.7.0",
        "description": "Phase A instantaneous voltage",
        "preferred_unit": "V",
    },
    "current_l1": {
        "obis": "1-1:31.7.0",
        "description": "Phase A instantaneous current",
        "preferred_unit": "A",
    },
    "frequency": {
        "obis": "1-1:14.7.0",
        "description": "Supply frequency",
        "preferred_unit": "Hz",
    },
    "active_power": {
        "obis": "1-1:1.7.0",
        "description": "Sum active power+ (QI+QIV)",
        "preferred_unit": "W",
    },
    "active_energy": {
        "obis": "1-1:1.8.0",
        "description": "Active energy+ (import)",
        "preferred_unit": "Wh",
    },
}


def _parse_args(argv: Optional[Iterable[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Read Microstar DLMS/COSEM register values over TCP/IP",
    )
    parser.add_argument("--host", required=True, help="Meter IP address or hostname")
    parser.add_argument("--port", type=int, default=4059, help="Meter TCP port (default: 4059)")
    parser.add_argument(
        "--measurement",
        dest="measurements",
        choices=sorted(MEASUREMENTS.keys()),
        nargs="+",
        default=["voltage_l1"],
        help="One or more named measurements to request (default: voltage_l1)",
    )
    parser.add_argument(
        "--server-logical",
        type=int,
        default=1,
        help="Server logical SAP (default: 1)",
    )
    parser.add_argument(
        "--server-physical",
        type=lambda x: int(x, 0),
        default=1,
        help="Server physical address (default: 1). Use last 4 serial digits per Microstar guide.",
    )
    parser.add_argument(
        "--client-sap",
        type=lambda x: int(x, 0),
        default=16,
        help="Client SAP (default: 0x10 – public client). Use 1 for captured demo frames.",
    )
    parser.add_argument(
        "--password",
        default="22222222",
        help="Authentication password (1-16 ASCII chars, default: 22222222)",
    )
    parser.add_argument(
        "--max-info-length",
        type=lambda x: int(x, 0),
        default=None,
        help="Maximum HDLC information field length. Omit to send a minimal SNRM",
    )
    parser.add_argument("--timeout", type=float, default=5.0, help="Socket timeout seconds")
    parser.add_argument("--verbose", action="store_true", help="Print frame-level logs")
    return parser.parse_args(argv)


def main(argv: Optional[Iterable[str]] = None) -> int:
    args = _parse_args(argv)

    password_bytes = args.password.encode("ascii", errors="ignore")
    if not password_bytes:
        print("Error: password cannot be empty", file=sys.stderr)
        return 1
    if len(password_bytes) > 16:
        password_bytes = password_bytes[:16]

    client = DLMSClient(
        host=args.host,
        port=args.port,
        server_logical=args.server_logical,
        server_physical=args.server_physical,
        client_sap=args.client_sap,
        max_info_length=args.max_info_length,
        password=password_bytes,
        verbose=args.verbose,
        timeout=args.timeout,
    )

    try:
        client.connect()
        for key in args.measurements:
            measurement = MEASUREMENTS[key]
            preferred_unit = measurement.get("preferred_unit")
            obis = measurement["obis"]
            value, unit_code, raw_value = client.read_register(obis)
            display_unit, reported_unit = _resolve_unit_label(unit_code, preferred_unit)
            if reported_unit is not None:
                warning = (
                    f"Warning: meter reported unit '{reported_unit}' (code {unit_code}); "
                    f"overriding to '{display_unit}'"
                )
                print(warning, file=sys.stderr)
            description = measurement["description"]
            print(
                f"{description} ({obis}): {value} {display_unit}"
                f"  [raw={raw_value}, scaler unit code={unit_code}]"
            )
    except Exception as exc:  # broad catch to ensure clean disconnect
        print(f"Error: {exc}", file=sys.stderr)
        client.close()
        return 1

    client.close()
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
