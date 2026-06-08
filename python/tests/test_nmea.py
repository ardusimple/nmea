"""Test"""

from os import urandom
from datetime import time
from pytest import raises, approx
from src.nmea import (
    NMEA,
    StreamFramer,
    MESSAGES,
    Talker,
    FixType,
    DataValidity,
    NavigationMode,
    OperationMode,
    create_coordinate_codec,
    time_codec,
    apply_hemisphere,
)

GLL = b"$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*42\r\n"
GGA = b"$GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,*76\r\n"
GST = b"$GPGST,092750.000,0.5,0.3,0.2,45.0,0.3,0.2,0.1*5B\r\n"
RMC = b"$GPRMC,092750.000,A,5321.68020,N,00630.33720,W,0.02,31.66,280511,,,A*43\r\n"
GSV = b"$GPGSV,3,1,11,10,63,137,17,07,61,098,15,05,59,290,20,08,54,157,30*70\r\n"
GSA = b"$GPGSA,A,3,10,7,5,2,29,4,8,13,20,,,,1.72,1.03,1.38*38\r\n"
GSA_410 = b"$GPGSA,A,3,1,2,3,4,5,6,7,8,9,10,11,12,1.2,1.0,0.8,1*16\r\n"


def test_talker_order():
    """should keep the Talker enum order"""
    talkers = list(Talker)
    assert talkers[0] == Talker.ALL
    assert talkers[1] == Talker.QZSS
    assert talkers[2] == Talker.GPS
    assert talkers[3] == Talker.GLONASS
    assert talkers[4] == Talker.GALILEO
    assert talkers[5] == Talker.BEIDOU
    assert talkers[6] == Talker.NAVIC


def test_coordinate_parser_parse():
    """should parse NMEA ddmm.mmmm coordinates to decimal degrees and reject out-of-range values"""
    parser = create_coordinate_codec("lat")
    latitudes = [
        ("4026.7667", 40.446111),
        ("0000.0000", 0),
        ("9000.0000", 90),
    ]
    for given, expected in latitudes:
        assert parser(given) == approx(expected, abs=1e-6)

    # out of bounds for latitude
    with raises(ValueError):
        parser("9100.0000")

    parser = create_coordinate_codec("lon")
    longitudes = [
        ("4026.7667", 40.446111),
        ("0000.0000", 0),
        ("18000.0000", 180),
    ]
    for given, expected in longitudes:
        assert parser(given) == approx(expected, abs=1e-6)

    # out of bounds for longitude
    with raises(ValueError):
        parser("18100.0000")


def test_coordinate_parser_serialize():
    """should serialize decimal degrees to DDMM.MMMMM with leading zeros"""
    parser = create_coordinate_codec("lat")
    assert parser.serialize(53.361337) == "5321.68022"

    parser = create_coordinate_codec("lon")
    assert parser.serialize(-6.505620) == "00630.33720"
    assert parser.serialize(9.5) == "00930.00000"


def test_time_codec_parse():
    """should parse NMEA hhmmss.ss time strings into time objects"""
    times = [
        ("202530.00", time(20, 25, 30)),
        ("000000.00", time(0, 0, 0)),
        ("235959.99", time(23, 59, 59, 990000)),
        ("092750", time(9, 27, 50)),  # no fractional seconds
    ]
    for given, expected in times:
        assert time_codec(given) == expected


def test_time_codec_serialize():
    """should serialize a time object to HHMMSS.mmm"""
    assert time_codec.serialize(time(9, 27, 50)) == "092750.000"
    assert time_codec.serialize(time(0, 0, 0)) == "000000.000"
    assert time_codec.serialize(time(23, 59, 59, 990000)) == "235959.990"


def test_apply_hemisphere():
    """should negate lat for S and lon for W, leaving N and E unchanged"""
    fields = {"lat": 40.0, "lon": 6.0}
    assert apply_hemisphere(fields) == {"lat": 40.0, "lon": 6.0}

    fields = fields | {"lat_dir": "N", "lon_dir": "E"}
    assert apply_hemisphere(fields) == {"lat": 40.0, "lon": 6.0}

    fields = fields | {"lat_dir": "S", "lon_dir": "W"}
    assert apply_hemisphere(fields) == {"lat": -40.0, "lon": -6.0}


def test_nmea_checksum():
    """should compute XOR checksum over payload bytes"""
    assert NMEA.checksum(b"GPGGA") == 0x56
    assert NMEA.checksum(b"") == 0


def test_nmea_validate():
    """should accept well-formed sentences and reject malformed"""
    NMEA.validate(GLL)

    # missing $
    with raises(AssertionError):
        NMEA.validate(GLL[1:])

    # missing CRLF
    with raises(AssertionError):
        NMEA.validate(GLL[:-2])

    # LF only, no CR
    with raises(AssertionError):
        NMEA.validate(GLL[:-2] + b"\n")

    # missing *checksum
    with raises(AssertionError):
        NMEA.validate(b"$GPGLL,4916.45,N,12311.12,W,225444.00,A,A\r\n")

    # wrong message type
    with raises(AssertionError):
        NMEA.validate(GLL, name="GGA")


def test_nmea_validate_wrong_checksum():
    """should raise AssertionError when checksum does not match"""
    with raises(AssertionError):
        NMEA.validate(b"$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*FF\r\n")


def test_nmea_is_valid():
    """should return True for valid sentences and False for malformed ones"""
    assert NMEA.is_valid(GLL)
    assert not NMEA.is_valid(b"garbage")
    assert not NMEA.is_valid(b"$GPGLL,4916.45,N,12311.12,W,225444.00,A,A*00\r\n")


def test_parser_parse_gga():
    """should parse a GGA sentence into a structured NMEA object with correct field values"""
    nmea = NMEA.parse(GGA)

    assert nmea.msg_id == "GGA"
    assert nmea.talker == Talker.GPS
    assert nmea.lat == approx(53.361337, abs=1e-6)
    assert nmea.lon == approx(-6.505620, abs=1e-6)
    assert nmea.fix == FixType.GPS_FIX
    assert nmea.num_satellites == 8


def test_parser_parse_gst():
    """should parse a GST sentence with position error statistics"""
    nmea = NMEA.parse(GST)

    assert nmea.msg_id == "GST"
    assert nmea.rms_range == 0.5
    assert nmea.std_major == 0.3
    assert nmea.std_minor == 0.2
    assert nmea.angle_major == 45.0
    assert nmea.std_lat == 0.3
    assert nmea.std_lon == 0.2
    assert nmea.std_alt == 0.1


def test_parser_parse_rmc():
    """should parse an RMC sentence with hemisphere-corrected coordinates"""
    nmea = NMEA.parse(RMC)

    assert nmea.msg_id == "RMC"
    assert nmea.talker == Talker.GPS
    assert nmea.status == DataValidity.VALID
    assert nmea.lat == approx(53.361337, abs=1e-6)
    assert nmea.lon == approx(-6.505620, abs=1e-6)
    assert nmea.speed == 0.02
    assert nmea.course == 31.66
    assert nmea.date == "280511"


def test_parser_parse_gsv():
    """should parse a GSV sentence with a list of visible satellites"""
    nmea = NMEA.parse(GSV)

    assert nmea.msg_id == "GSV"
    assert nmea.total_msg == 3
    assert nmea.msg_num == 1
    assert nmea.visible_satellites == 11
    assert len(nmea.satellites) == 4
    assert nmea.satellites[0].prn == 10
    assert nmea.satellites[0].azimuth == 137
    assert nmea.satellites[3].prn == 8
    assert nmea.satellites[3].snr == 30
    assert nmea.signal_id is None


def test_parser_parse_gsv_with_signal_id():
    """should parse GSV signal_id when present"""
    nmea = NMEA.parse(b"$GPGSV,1,1,1,01,40,083,46,1*69\r\n")
    assert nmea.signal_id == "1"


def test_parser_parse_gsa():
    """should parse a GSA sentence with DOP values and active satellite PRNs"""
    nmea = NMEA.parse(GSA)

    assert nmea.msg_id == "GSA"
    assert nmea.op_mode == OperationMode.AUTOMATIC
    assert nmea.nav_mode == NavigationMode.FIX_3D
    assert nmea.prns == [10, 7, 5, 2, 29, 4, 8, 13, 20]
    assert nmea.pdop == 1.72
    assert nmea.hdop == 1.03
    assert nmea.vdop == 1.38
    assert not nmea.system_id

    nmea = NMEA.parse(GSA_410)
    assert nmea.system_id == Talker.GPS


def test_parser_parse_gsa_fewer_prns():
    """should parse GSA with fewer than 12 active PRNs"""
    nmea = NMEA.parse(b"$GPGSA,A,3,1,2,3,,,,,,,,,,1.2,1.0,0.8*08\r\n")
    assert nmea.prns == [1, 2, 3]


def test_parser_parse_gsa_system_ids():
    """should map every GSA NMEA 4.10 system ID to the correct Talker"""
    # op_mode, nav_mode, 12 PRN slots, pdop, hdop, vdop
    base = ["A", "3"] + ["1"] * 15
    cases = [
        ("0", Talker.QZSS),
        ("1", Talker.GPS),
        ("2", Talker.GLONASS),
        ("3", Talker.GALILEO),
        ("4", Talker.BEIDOU),
    ]
    parse = MESSAGES["GSA"].parse
    assert parse is not None
    for system_id, expected in cases:
        assert parse(base + [system_id])["system_id"] == expected


def test_parser_parse_skip_validation():
    """should parse a structurally valid sentence even with a wrong checksum when validate=False"""
    bad = b"$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*FF\r\n"
    nmea = NMEA.parse(bad, validate=False)
    assert nmea.msg_id == "GLL"


def test_parser_serialize():
    """should round-trip every supported message type through parse and serialize"""
    for sentence in [GLL, GGA, GST, RMC, GSV, GSA, GSA_410]:
        nmea = NMEA.parse(sentence)
        assert nmea.serialize() == sentence


def test_framer_process():
    """should emit raw sentence bytes when fed a byte stream"""

    def _assert(raw):
        nmea = NMEA.parse(raw)
        assert nmea.msg_id == "GLL"
        assert nmea.lat == approx(49.274167, abs=1e-6)
        assert nmea.lon == approx(-123.185333, abs=1e-6)

    framer = StreamFramer(on_frame=_assert)
    for byte in urandom(32) + GLL + urandom(32):
        framer.process(byte)


def test_framer_process_interrupted():
    """should discard a partial sentence and still frame the subsequent complete one"""

    def _assert(raw):
        assert raw[3:6] == b"GLL"

    framer = StreamFramer(on_frame=_assert)

    # GGA[:6] = header only, never completes
    for byte in urandom(32) + GGA[:6] + urandom(32) + GLL + urandom(32):
        framer.process(byte)


def test_framer_process_multiple_sentences():
    """should emit one raw frame per complete sentence in the stream"""
    received = []
    framer = StreamFramer(on_frame=received.append)

    for byte in GLL + GGA:
        framer.process(byte)

    assert len(received) == 2
    assert received[0][3:6] == b"GLL"
    assert received[1][3:6] == b"GGA"


def test_framer_process_max_length():
    """should reset and recover when a sentence exceeds max_length"""

    def _assert(raw):
        assert raw[3:6] == b"GLL"

    framer = StreamFramer(on_frame=_assert, max_length=82)

    # 83 bytes after $ exceeds max_length of 82, forcing a reset
    for byte in b"$" + b"x" * 83 + GLL:
        framer.process(byte)


def test_framer_passes_bad_checksum():
    """should emit structurally complete frames regardless of checksum"""
    received = []
    framer = StreamFramer(on_frame=received.append)

    bad = b"$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*FF\r\n"
    for byte in bad:
        framer.process(byte)

    assert len(received) == 1
    assert received[0] == bad


def test_framer_feed():
    """should yield complete frames from a byte chunk without requiring a callback"""
    framer = StreamFramer()
    chunk = urandom(32) + GLL + urandom(32)
    for frame in framer.feed(chunk):
        nmea = NMEA.parse(frame)
        assert nmea.msg_id == "GLL"
