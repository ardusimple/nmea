# NMEA

A Python library for parsing, validating, and serializing NMEA messages.

## Features

- Parse raw NMEA sentences into structured objects
- Serialize NMEA objects back to bytes with correct checksum
- Stateful streaming framer for byte-by-byte input
- Support for GGA, GLL, GST, RMC, GSV, and GSA message types

## Usage

### Parsing a sentence

```python
from nmea import NMEA

raw = b"$GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,*76\r\n"
nmea = NMEA.parse(raw)

print(nmea.lat)   # 53.361337
print(nmea.time)  # datetime.time(9, 27, 50)
```

### Serializing back to bytes

```python
nmea.serialize() == raw  # True
```

### Streaming

Feed individual bytes as they arrive; `on_frame` is invoked with the raw sentence bytes for each structurally complete frame (`$...*XX\r\n`). No checksum validation is performed — call `NMEA.validate` or `NMEA.parse` (which validates by default) inside the callback when needed.

```python
from nmea import NMEA, StreamFramer

def on_frame(raw: bytes):
    nmea = NMEA.parse(raw)  # validates and parses
    print(nmea)

framer = StreamFramer(on_frame=on_frame)

for byte in stream:
    framer.process(byte)
```

When data arrives in chunks, use `feed()` instead. It yields each complete frame as a generator, composing naturally with async I/O loops

```python
async def forward(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    framer = StreamFramer()
    while data := await reader.read(256):
        for frame in framer.feed(data):
            writer.write(frame)
```

## Supported messages

| ID | Description |
| --- | --- |
| GGA | Global positioning system fix data |
| GLL | Geographic position |
| GST | GNSS pseudorange error statistics |
| RMC | Recommended minimum specific data |
| GSV | GNSS satellites in view |
| GSA | GNSS DOP and active satellites |

### Extending with custom messages

New message types can be registered by adding entries to the `MESSAGES` dict.

Define a `Message` with a list of `Field` entries. Each field needs a name, an expected Python type, and an optional `FieldCodec` for complex values.

```python
from src.nmea import MESSAGES, Message, Field, time_codec, create_coordinate_codec

latitude = create_coordinate_codec("lat")
longitude = create_coordinate_codec("lon")

MESSAGES["GNS"] = Message(
    name="GNS",
    fields=[
        Field(name="time", type=Time, codec=time_codec),
        Field(name="lat", type=float, codec=latitude),
        Field(name="lat_dir", type=str),
        Field(name="lon", type=float, codec=longitude),
        Field(name="lon_dir", type=str),
        Field(name="mode", type=str),
        Field(name="num_satellites", type=int),
        Field(name="hdop", type=float),
        Field(name="alt", type=float),
    ],
)
```

For messages with a variable number of fields or non-trivial structure, supply a `parse` callable. It receives the raw comma-separated parts (after the talker+id header) as a list of strings and must return a `dict` of field values.

```python
# Example: a message with a fixed header and a variable number of trailing items.
# The default field-by-field parsing can't handle this, so `parse` is used instead.
MESSAGES["FOO"] = Message(
    name="FOO",
    fields=[
        Field(name="count", type=int),
        Field(name="items", type=list),
    ],
    parse=lambda parts: {
        "count": int(parts[0]),
        "items": parts[1:],
    },
)
```

#### Built-in field codecs

| Codec | Input format | Output type |
| --- | --- | --- |
| `create_coordinate_codec("lat")` | `DDMM.MMMMM` | `float` |
| `create_coordinate_codec("lon")` | `DDDMM.MMMMM` | `float` |
| `time_codec` | `HHMMSS[.mmm]` | `datetime.time` |

To handle a new field format, create a `FieldCodec` with matching `parse` and `serialize` callables:

```python
from src.nmea import FieldCodec
from datetime import datetime

date_codec = FieldCodec(
    parse=lambda v: datetime.strptime(v, "%d%m%y").date(),
    serialize=lambda d: d.strftime("%d%m%y"),
    validate=lambda d: d.year >= 2000,
)
```

## Development

```bash
python -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"
```

Run the tests:

```bash
pytest -v
```
