# Microstar DLMS reader helper

This folder contains a lightweight Python script that sends the HDLC/DLMS frames
required to query a Microstar meter over TCP/IP and extract instantaneous
register values such as the phase voltage. The implementation follows the frame
examples published in the _Microstar DLMS Protocol Guide_ (see the PDF in this
folder) and the sample capture `TRAMA DLMS CURRENT.txt`.

## Files

- `dlms_reader.py` – executable script that opens a TCP connection, performs the
  SNRM/AARQ handshake, sends DLMS GET requests, parses the responses (including
  scaler/unit structures) and prints the resolved value.
- `tests/test_dlms_reader.py` – quick unit tests that validate the frame builder
  and basic DLMS data parsing helpers.

## Usage

```bash
python3 dlms_reader.py --host 192.168.1.50 --port 4059 --measurement voltage_l1 --verbose
```

Key options:

- `--measurement` (default `voltage_l1`) determines the OBIS code to read.
  A `current_l1` mapping is provided as a second example – extend the
  `MEASUREMENTS` dictionary for additional attributes.
- `--server-logical` / `--server-physical` control the HDLC addressing. By
  default the script targets logical `1` and physical `1`; change the physical
  part to the last four digits of the meter serial number if the factory default
  is still active (per the Microstar guide). For direct sessions that use the
  short address `0x01` you can pass `--server-logical 0 --server-physical 1`.
- `--client-sap` defaults to `0x10` (public client). When replaying the captured
  frames, set `--client-sap 1`.
- `--password` supplies the low-level security password (1‒16 ASCII chars).
- `--max-info-length` defines the negotiated HDLC information field length. If
  omitted the script sends the minimal SNRM frame (as seen in the latest capture).

Run `python3 dlms_reader.py --help` for the full list of arguments.

## Output

A successful execution prints the resolved value, the interpreted unit (when
known) and the raw integer returned by the register, for example:

```
Phase A instantaneous voltage (1-1:32.7.0): 228.54 V  [raw=22854, scaler unit code=32]
```

Add the `--verbose` switch to inspect each HDLC frame (hex encoded) as it is
sent or received.

## Tests

The helper functions can be validated locally:

```bash
python3 -m unittest discover -s tests -v
```

The tests only rely on the captured frames and do not require a live meter.

## Notes

- The script assumes **no security** (public client). For password-protected
  meters adjust the AARQ APDU construction accordingly.
- DLMS is highly configurable; some meters might expect different window sizes
  or HDLC parameters. Update `_build_snrm_info` or the negotiated limits to
  match your installation if needed.
- Only the subset of DLMS data types needed for register/scaler retrieval is
  implemented. Extend `_parse_data` if you plan to parse more complex objects.
