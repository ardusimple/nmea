"""NMEA

This module provides an implementation for working with NMEA messages,
including parsing, validation, and serialization. It exposes the NMEA message
class and a stateful streaming framer.
"""

from enum import Enum, StrEnum
from datetime import datetime, time as Time
from typing import Callable, Dict, Literal, NamedTuple, List, Type, Generator, Any

# States
_PARSER_IDLE = 0
_PARSER_TRACKING = 1

_MESSAGE_MAX_LENGTH = 82


class Talker(StrEnum):
    """GNSS talker"""

    ALL = "GN"  # Multiple
    QZSS = "GQ"  # Japan
    GPS = "GP"  # USA
    GLONASS = "GL"  # Russia
    GALILEO = "GA"  # Europe
    BEIDOU = "BD"  # China
    NAVIC = "GI"  # India


class DataValidity(StrEnum):
    """Data validity"""

    VALID = "A"
    INVALID = "V"


class FixType(StrEnum):
    """Fix of GNSS"""

    NOT_VALID = "0"
    GPS_FIX = "1"
    DIFFERENTIAL_GPS_FIX = "2"
    PPS = "3"
    RTK_FIX = "4"
    RTK_FLOAT = "5"
    ESTIMATED = "6"
    MANUAL = "7"
    SIMULATION = "8"


class OperationMode(StrEnum):
    """Operation mode"""

    MANUAL = "M"
    AUTOMATIC = "A"


class NavigationMode(Enum):
    """Navigation mode"""

    NOT_AVAILABLE = 1
    FIX_2D = 2
    FIX_3D = 3


class Satellite(NamedTuple):
    """A single satellite entry from a GSV message."""

    prn: int
    elevation: float
    azimuth: float
    snr: float

    def __str__(self) -> str:
        return f"{self.prn:02d},{self.elevation:02.0f},{self.azimuth:03.0f},{self.snr:02.0f}"


class FieldCodec(NamedTuple):
    """A pair of functions to parse and serialize a specific field type."""

    parse: Callable[[str], Any]
    serialize: Callable[[Any], str]
    validate: Callable[[Any], bool] | None = None

    def __call__(self, value: str) -> Any:
        result = self.parse(value)
        if self.validate and not self.validate(result):  # pylint: disable=not-callable
            raise ValueError(f"value {result} is invalid")
        return result


class Field(NamedTuple):
    """Descriptor for a single field within an NMEA message."""

    name: str
    type: Type
    codec: FieldCodec | None = None
    required: bool = True


class Message(NamedTuple):
    """Descriptor for a supported NMEA message type."""

    name: str
    fields: List[Field]
    parse: Callable[[List[str]], Dict[str, Any]] | None = None
    refine: Callable[[Dict[str, Any]], Dict[str, Any]] | None = None
    validate: Callable[[Dict[str, Any]], bool] | None = None
    serialize: Callable[[NMEA], List[str]] | None = None


def apply_hemisphere(fields: Dict[str, Any]) -> Dict[str, Any]:
    """Apply hemisphere sign to parsed lat/lon fields"""
    return {
        "lat": -fields["lat"] if fields.get("lat_dir") == "S" else fields["lat"],
        "lon": -fields["lon"] if fields.get("lon_dir") == "W" else fields["lon"],
    }


def create_coordinate_codec(kind: Literal["lat", "lon"]) -> FieldCodec:
    """Create a coordinate FieldCodec for latitude or longitude"""
    bound = 90 if kind == "lat" else 180
    width = 10 if kind == "lat" else 11
    return FieldCodec(
        parse=lambda v: round(int(float(v) // 100) + float(v) % 100 / 60, 7),
        serialize=lambda v, w=width: f"{int(abs(v)) * 100 + (abs(v) - int(abs(v))) * 60:0{w}.5f}",
        validate=lambda v, b=bound: -b <= v <= b,
    )


latitude = create_coordinate_codec("lat")
longitude = create_coordinate_codec("lon")

time_codec = FieldCodec(
    parse=lambda v: datetime.strptime(v, "%H%M%S.%f" if "." in v else "%H%M%S").time(),
    serialize=lambda t: t.strftime("%H%M%S.") + f"{t.microsecond // 1000:03d}",
)


MESSAGES = {
    "GGA": Message(
        name="GGA",
        fields=[
            Field(name="time", type=Time, codec=time_codec),  # UTC time
            Field(name="lat", type=float, codec=latitude),  # latitude
            Field(name="lat_dir", type=str),  # latitude direction
            Field(name="lon", type=float, codec=longitude),  # longitude
            Field(name="lon_dir", type=str),  # longitude direction
            Field(name="fix", type=FixType),  # Fix type
            Field(name="num_satellites", type=int),  # Number of satellites
            Field(name="hdop", type=float),  # Horizontal dilution of precision
            Field(name="alt", type=float),  # Altitude
            Field(name="alt_unit", type=str),  # Altitude unit
            Field(name="geoid_height", type=float),  # Height above sea level
            Field(name="geoid_height_unit", type=str),  # Height above sea level unit
            Field(name="age", type=float),  # Age of differential
            Field(name="station", type=str),  # Station ID
        ],
        refine=lambda fields: fields | apply_hemisphere(fields),
    ),
    "GLL": Message(
        name="GLL",
        fields=[
            Field(name="lat", type=float, codec=latitude),  # latitude
            Field(name="lat_dir", type=str),  # latitude direction
            Field(name="lon", type=float, codec=longitude),  # longitude
            Field(name="lon_dir", type=str),  # longitude direction
            Field(name="time", type=Time, codec=time_codec),  # UTC time
            Field(name="status", type=str),  # Status
            Field(name="mode", type=str),  # Mode
        ],
        refine=lambda fields: fields | apply_hemisphere(fields),
    ),
    "GST": Message(
        name="GST",
        fields=[
            Field(name="time", type=Time, codec=time_codec),  # UTC time
            Field(name="rms_range", type=float),  # RMS range
            Field(name="std_major", type=float),  # Standard deviation of major axis
            Field(name="std_minor", type=float),  # Standard deviation of minor axis
            Field(name="angle_major", type=float),  # Orientation of major axis
            Field(name="std_lat", type=float),  # Standard deviation of latitude
            Field(name="std_lon", type=float),  # Standard deviation of longitude
            Field(name="std_alt", type=float),  # Standard deviation of altitude
        ],
    ),
    "RMC": Message(
        name="RMC",
        fields=[
            Field(name="time", type=Time, codec=time_codec),  # UTC time
            Field(name="status", type=DataValidity),  # Data status A=valid V=invalid
            Field(name="lat", type=float, codec=latitude),  # latitude
            Field(name="lat_dir", type=str),  # latitude direction
            Field(name="lon", type=float, codec=longitude),  # longitude
            Field(name="lon_dir", type=str),  # longitude direction
            Field(name="speed", type=float),  # Speed over ground
            Field(name="course", type=float),  # Course over ground
            Field(name="date", type=str),  # Date
            Field(name="mag_variation", type=float),  # Magnetic variation
            Field(name="mag_variation_dir", type=str),  # Magnetic variation direction
            Field(name="pos_mode", type=str),  # Positioning mode indicator
        ],
        refine=lambda fields: fields | apply_hemisphere(fields),
    ),
    "GSV": Message(
        name="GSV",
        fields=[
            Field(name="total_msg", type=int),  # Total number of messages
            Field(name="msg_num", type=int),  # Message number
            Field(name="visible_satellites", type=int),  # Number of visible satellites
            Field(name="satellites", type=list),  # Satellites
            Field(name="signal_id", type=str, required=False),  # Signal ID
        ],
        parse=lambda data: {
            "total_msg": int(data[0]),  # Total number of messages
            "msg_num": int(data[1]),  # Message number
            "visible_satellites": int(data[2]),  # Number of visible satellites
            "satellites": [  # Satellites
                Satellite(*[int(x or 0) for x in part])
                for i in range(3, len(data), 4)
                if len(part := data[i : i + 4]) == 4
            ],
            "signal_id": data[-1] or None if len(data[3:]) % 4 else None,
        },
    ),
    "GSA": Message(
        name="GSA",
        fields=[
            Field(name="op_mode", type=OperationMode),  # Operation mode
            Field(name="nav_mode", type=NavigationMode),  # Navigation mode
            Field(  # PRNs
                name="prns",
                type=list,
                codec=FieldCodec(
                    parse=lambda _: _,  # Handled by Message parser
                    # Fulfill prns array to 12
                    serialize=lambda v: ",".join(map(str, v + [""] * (12 - len(v)))),
                ),
            ),
            Field(name="pdop", type=float),  # Position dilution of precision
            Field(name="hdop", type=float),  # Horizontal dilution of precision
            Field(name="vdop", type=float),  # Vertical dilution of precision
            Field(  # NMEA 4.10 system ID
                name="system_id",
                type=Talker,
                codec=FieldCodec(
                    parse=lambda _: _,  # Handled by Message parser
                    serialize=lambda v: str(list(Talker).index(v) - 1),
                ),
                required=False,
            ),
        ],
        parse=lambda data: {
            "system_id": (
                Talker(list(Talker)[int(data[-1]) + offset])
                if (offset := 1 if data[-1].isdigit() else 0)
                else None
            ),
            "op_mode": OperationMode(data[0]),  # Operation mode
            "nav_mode": NavigationMode(int(data[1])),  # Navigation mode
            "pdop": float(data[-3 - offset]),  # Position dilution of precision
            "hdop": float(data[-2 - offset]),  # Horizontal dilution of precision
            "vdop": float(data[-1 - offset]),  # Vertical dilution of precision
            "prns": [int(prn) for prn in data[2 : -3 - offset] if prn],  # PRNs
        },
    ),
}


class NMEA:
    """NMEA message.

    This class provides high-level methods to validate, parse and handle NMEA messages.
    It supports both standard and custom NMEA message types.
    """

    msg_id: str
    talker: Talker

    def __init__(self, msg_id: str, talker: str, **kwargs):
        """Initialize an NMEA message.

        Args:
            msg_id: Message identifier (e.g., "GGA" for position data).
            talker: GNSS system identifier (e.g., "GP" for GPS).
            **kwargs: Message-specific field values.

        Raises:
            ValueError: If the message name is not supported.
            TypeError: If a field value does not match the expected type.
        """
        self.msg_id = msg_id
        self.talker = Talker(talker)

        message = MESSAGES.get(msg_id, None)

        if not message:
            raise ValueError(f"{msg_id} is not a supported NMEA message")

        for name, kind, *_ in message.fields:
            value = kwargs.get(name, None)

            if value is not None and not isinstance(value, kind):
                raise TypeError(f"{name} must be {kind}")

            # Assign value, or override if new one is not None
            if not hasattr(self, name) or value is not None:
                setattr(self, name, value)

    def __getattr__(self, name: str) -> Any:
        """Declare dynamic attribute access for type checkers.

        NMEA message fields (lat, lon, fix, etc.) are set dynamically via setattr
        based on the message descriptor in MESSAGES. This method's return type of
        Any allows type checkers to accept arbitrary attribute access on NMEA
        instances without errors, while still raising AttributeError at runtime
        for genuinely missing attributes.
        """
        raise AttributeError(name)

    def serialize(self) -> bytes:
        """Serialize an NMEA object to a raw sentence.

        Returns:
            The complete NMEA sentence as bytes, including the leading '$',
            checksum, and trailing CRLF.
        """
        message = MESSAGES.get(self.msg_id)
        if not message:
            raise TypeError("This message should not exist")

        if message.serialize:
            parts = message.serialize(self)
        else:
            parts = [
                (
                    field.codec.serialize(getattr(self, field.name))
                    if field.codec
                    else (
                        ",".join(map(str, v))
                        if isinstance(v := getattr(self, field.name), list)
                        else str(v.value if isinstance(v, Enum) else v or "")
                    )
                )
                for field in message.fields
                if field.required or getattr(self, field.name)
            ]

        payload = ",".join([self.talker + self.msg_id] + parts)
        checksum = NMEA.checksum(payload.encode("utf-8"))
        return ("$" + payload + "*" + f"{checksum:02X}" + "\r\n").encode("utf-8")

    def __str__(self) -> str:
        skip = ("talker", "msg_id")
        attrs = [
            (
                f"{key}={type(value).__name__}.{value.name}"
                if isinstance(value, Enum)
                else f"{key}={value}"
            )
            for key, value in self.__dict__.items()
            if not key.startswith("_") and key not in skip
        ]
        return f"{self.__class__.__name__}<{self.msg_id}>({", ".join(attrs)})"

    def __repr__(self) -> str:
        return self.__str__()

    @classmethod
    def checksum(cls, data: bytes) -> int:
        """Calculate the NMEA checksum for the given data.

        Args:
            data: The byte sequence over which to calculate the checksum.

        Returns:
            The integer XOR checksum of the provided data.
        """
        result = 0
        for byte in data:
            result ^= byte
        return result

    @classmethod
    def validate(cls, data: bytes, name: str | None = None) -> None:
        """Validate a raw NMEA message.

        Args:
            data: A raw NMEA message as bytes.
            name: The expected NMEA message type.

        Raises:
            AssertionError: If the message is invalid.
        """
        assert data[0] == 36, "Must start with $"
        assert data[-2] == 13 and data[-1] == 10, "Must end with CRLF"
        assert data[-5] == 42, "Must include checksum"

        if name:
            assert name == data[3:6].decode("utf-8"), "Unexpected message type"

        checksum = int(chr(data[-4]) + chr(data[-3]), 16)
        assert cls.checksum(data[1:-5]) == checksum, "Invalid checksum"

    @classmethod
    def is_valid(cls, data: bytes, msg: str | None = None) -> bool:
        """Check if data is a valid NMEA message.

        Args:
            data: A raw NMEA message as bytes.
            msg: The expected NMEA message type.

        Returns:
            True if the message is valid, False otherwise.
        """
        try:
            cls.validate(data, msg)
            return True
        except AssertionError:
            return False

    @classmethod
    def parse(cls, data: bytes, validate: bool | None = True) -> NMEA:
        """Parse a raw NMEA message into a structured object.

        Args:
            data: A raw NMEA message as bytes.
            validate: Whether to validate the message before parsing.

        Returns:
            A structured NMEA object.
        """
        try:
            if validate:
                NMEA.validate(data)

            payload, _ = data.decode("utf-8").split("*")
            header, *parts = payload.split(",")
            msg_id = header[3:]

            message = MESSAGES.get(msg_id, None)

            if message is None:
                raise ValueError(f"{msg_id} is not a supported NMEA message")

            required = [field.name for field in message.fields if field.required]
            if message.parse is None and len(parts) < len(required):
                raise ValueError(f"{msg_id} has fewer than required fields")

            fields = (
                message.parse(parts)
                if message.parse
                else {
                    field.name: (field.codec(value) if field.codec else field.type(value))
                    for field, value in zip(message.fields, parts)
                    if value
                }
            )

            if message.refine:
                fields = message.refine(fields)

            if message.validate:
                assert message.validate(fields)

            talker = fields.get("talker", Talker(header[1:3]))

            return NMEA(msg_id=msg_id, talker=talker, **fields)

        except (ValueError, TypeError, AssertionError) as error:
            raise ValueError(f"NMEA message is invalid: {error}") from error


class StreamFramer:
    """Stateful framer for streaming NMEA data.

    Implements a state machine to extract complete NMEA sentences from an incoming
    byte stream, invoking a callback for each structurally complete frame. Checksum
    validation and parsing are left to the caller — use NMEA.validate() or NMEA.parse().
    """

    _on_frame: Callable[[bytes], None] | None
    _state: int
    _data: bytearray
    _max_length: int

    def __init__(
        self,
        on_frame: Callable[[bytes], None] | None = None,
        max_length: int = _MESSAGE_MAX_LENGTH,
    ):
        """Initialize the framer.

        Args:
            on_frame: Called with the raw sentence bytes for each structurally complete frame.
            max_length: Maximum allowed message length in bytes before the buffer is reset.
        """
        self._on_frame = on_frame
        self._max_length = max_length
        self.reset()

    def reset(self) -> None:
        """Reset the framer state."""
        self._state = _PARSER_IDLE
        self._data = bytearray()

    def process(self, data: int) -> bytes | None:
        """Process an incoming byte and accumulate it into a complete NMEA sentence.

        Resets the buffer on each '$' start delimiter and when the maximum length is
        exceeded. Emits via on_frame when a structurally complete frame is detected
        ($...*XX\\r\\n). No checksum validation is performed.

        Args:
            data: The incoming data byte to be processed.

        Returns:
            The complete NMEA sentence as bytes if a frame is detected, otherwise None.
        """
        if data == 36:
            self.reset()
            self._state = _PARSER_TRACKING

        if self._state == _PARSER_TRACKING:
            self._data.append(data)

            if len(self._data) > self._max_length:
                self.reset()
                return None

            if (
                len(self._data) > 6
                and self._data[-2] == 13
                and self._data[-1] == 10
                and self._data[-5] == 42
            ):
                frame = bytes(self._data)
                self.reset()
                if self._on_frame:
                    self._on_frame(frame)
                return frame

        return None

    def feed(self, data: bytes) -> Generator[bytes]:
        """Process a chunk of bytes and yield each complete NMEA frame.

        Args:
            data: A sequence of bytes to process.

        Yields:
            Each structurally complete NMEA sentence as bytes.
        """
        for byte in data:
            if frame := self.process(byte):
                yield frame
