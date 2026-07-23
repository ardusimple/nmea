# TypeScript NMEA Parser

Lightweight NMEA 0183 parser and serializer with full type inference.

## Setup

```bash
npm install
```

### VSCode

Install recommended extensions when prompted:

- [ESLint](https://marketplace.visualstudio.com/items?itemName=dbaeumer.vscode-eslint)
- [Prettier](https://marketplace.visualstudio.com/items?itemName=esbenp.prettier-vscode)

Auto-linting and formatting on save enabled.

## Usage

### Parsing

Parse NMEA sentences into typed objects. Use a string literal generic to get full field autocompletion and type narrowing.

```typescript
import NMEA from 'nmea';

const sentence = '$GPGGA,123519.000,4807.03800,N,01131.00000,E,1,08,0.9,545,M,47,M,,*4B\r\n';

// Parse with type narrowing via string literal
const gga = NMEA.parse<'GGA'>(sentence);
gga.fix;           // 0 | 1 | 2 | ... | 8 (FixType)
gga.lat;           // number
gga.time;          // Date
gga.numSatellites; // number

// Parse without narrowing (generic NMEA)
const msg = NMEA.parse(sentence);
```

### Serializing

Build an NMEA object and serialize it back to a sentence string with automatic checksum calculation.

```typescript
const output = NMEA.serialize({
  talker: 'GP',
  msgId: 'GGA',
  time: new Date(),
  lat: 48.1173,
  latDir: 'N',
  lon: 11.5167,
  lonDir: 'E',
  fix: 1,
  numSatellites: 8,
  hdop: 0.9,
  alt: 545,
  altUnit: 'M',
  geoidHeight: 47,
  geoidHeightUnit: 'M',
});
// "$GPGGA,123519.000,4807.03800,N,01131.00000,E,1,8,0.9,545,M,47,M,,*XX\r\n"
```

### Validation

Check sentence format, delimiters, and checksum integrity.

```typescript
NMEA.isValid('$GPGGA,...*4B\r\n'); // true
NMEA.validate('$GPGGA,...*00\r\n'); // throws on bad checksum or format
```

### Custom messages

Register additional NMEA message types at runtime. Field types can be constructors (`Number`, `String`, `Date`) or const objects (`FixType`, `OperationMode`) for automatic coercion.

```typescript
import NMEA from 'nmea';

NMEA.register({
  name: 'VTG',
  fields: [
    { name: 'course', type: Number },
    { name: 'ref', type: String },
    { name: 'courseM', type: Number },
    { name: 'refM', type: String },
    { name: 'speedKnots', type: Number },
    { name: 'unitKnots', type: String },
    { name: 'speedKmh', type: Number },
    { name: 'unitKmh', type: String },
  ],
});

const vtg = NMEA.parse('$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48\r\n');
vtg.speedKnots; // 5.5
```

### Stream framing

Extract NMEA sentences from a raw byte stream (serial port, TCP socket, etc.). Handles partial reads and sentence boundaries automatically.

```typescript
import { StreamFramer } from 'nmea';

// Callback style
const decoder = new TextDecoder();
const framer = new StreamFramer(frame => {
  const text = decoder.decode(frame);
  const msg = NMEA.parse(text);
});

// Generator style
const framer = new StreamFramer();
for (const frame of framer.feed(chunk)) {
  const msg = NMEA.parse(decoder.decode(frame));
}
```

### Constants

Exported const objects for talker IDs, fix types, and other NMEA enumerations. Usable both as runtime values and as TypeScript types.

```typescript
import { Talker, FixType, DataValidity, OperationMode, NavigationMode } from 'nmea';

Talker.GPS        // 'GP'
FixType.RTK_FIX   // 4
DataValidity.VALID // 'A'
```

## Supported messages

| Message | Description |
|---------|-------------|
| GGA | GPS fix data (position, fix quality, satellites, altitude) |
| GLL | Geographic position (lat/lon with time and status) |
| GST | GPS pseudorange noise statistics |
| RMC | Recommended minimum (position, velocity, time) |
| GSV | Satellites in view (satellite details per message) |
| GSA | DOP and active satellites |

## Development

```bash
npm run build        # Compile TypeScript
npm test             # Run tests
npm test -- --watch  # Watch mode
npm run lint         # ESLint
npm run format       # Prettier
```
