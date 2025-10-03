import unittest

from dlms_reader import (
    _build_frame,
    _build_snrm_info,
    _build_aarq_apdu,
    _crc16_hdlc,
    _encode_hdlc_address,
    _parse_data,
    _extract_get_response_payload,
    _resolve_unit_label,
    obis_to_bytes,
        _parse_args,
)


class HdlcHelpersTest(unittest.TestCase):
    def test_crc_against_capture(self) -> None:
        payload = bytes.fromhex(
            "A019030398BCC2E6E600C001C1000301011F0700FF0200"
        )
        crc = _crc16_hdlc(payload)
        self.assertEqual(crc, 0xE609)

    def test_encode_address_single_byte(self) -> None:
        self.assertEqual(_encode_hdlc_address(1), b"\x03")
        self.assertEqual(_encode_hdlc_address(16), b"\x21")

    def test_snrm_minimal_matches_capture(self) -> None:
        frame = _build_frame(0x93, 1, 1, b"")
        expected = bytes.fromhex(
            "7EA0070303938C117E"
        )
        self.assertEqual(frame, expected)

    def test_snrm_info_block(self) -> None:
        info = _build_snrm_info(0x01D2, 0x01D2)
        self.assertEqual(
            info,
            bytes.fromhex(
                "818012050201D2060201D2070400000001080400000001"
            ),
        )

    def test_aarq_matches_capture(self) -> None:
        apdu = _build_aarq_apdu(b"2222222222")
        frame = _build_frame(0x10, 1, 1, apdu)
        expected = bytes.fromhex(
            "7EA04603031013ADE6E6006036A1090607608574050801018A0207808B0760857405080201AC0C800A32323232323232323232BE10040E01000000065F1F0400007E1F04B090B67E"
        )
        self.assertEqual(frame, expected)


class DlmsDataParsingTest(unittest.TestCase):
    def test_extract_get_response_payload(self) -> None:
        info = bytes.fromhex("E6E700C401010002020FFD16214DC0")
        payload = _extract_get_response_payload(info, 1)
        self.assertEqual(payload, bytes.fromhex("02020FFD16214DC0"))

    def test_resolve_unit_label_with_override(self) -> None:
        display, reported = _resolve_unit_label(35, "V")
        self.assertEqual(display, "V")
        self.assertEqual(reported, "Hz")
        display, reported = _resolve_unit_label(33, "A")
        self.assertEqual(display, "A")
        self.assertIsNone(reported)

    class CliParsingTest(unittest.TestCase):
        def test_measurement_defaults_to_single_voltage(self) -> None:
            args = _parse_args(["--host", "127.0.0.1"])
            self.assertEqual(args.measurements, ["voltage_l1"])

        def test_measurement_accepts_multiple_values(self) -> None:
            args = _parse_args([
                "--host",
                "127.0.0.1",
                "--measurement",
                "voltage_l1",
                "current_l1",
            ])
            self.assertEqual(args.measurements, ["voltage_l1", "current_l1"])

    def test_obis_to_bytes(self) -> None:
        self.assertEqual(
            obis_to_bytes("1-1:32.7.0"),
            bytes([1, 1, 32, 7, 0, 255]),
        )
        with self.assertRaises(ValueError):
            obis_to_bytes("bad-format")

    def test_parse_structure_scaler(self) -> None:
        data = bytes.fromhex("02020FFF1620")
        values, remaining = _parse_data(data)
        self.assertEqual(values, [-1, 0x20])
        self.assertFalse(remaining)

    def test_parse_double_long(self) -> None:
        data = bytes.fromhex("05000004C3")
        value, remaining = _parse_data(data)
        self.assertEqual(value, 0x000004C3)
        self.assertFalse(remaining)


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
