/* eslint-disable @typescript-eslint/no-explicit-any */

// Type inference from field definitions
type Prettify<T> = { [k in keyof T]: T[k] } & {};
type Fn<T> = (...args: any[]) => T;
type Constructor<T> = new (...args: any[]) => T;

// Maps field type to its inferred TS type:
type FieldType<T> =
  T extends DateConstructor ? Date :
  T extends (...args: any[]) => infer R ? R :
  T extends Record<string, infer V> ? V : unknown;

type InferFields<F extends readonly any[]> = {
  [K in F[number] as K['name']]: K extends { required: false }
    ? FieldType<K['type']> | undefined
    : FieldType<K['type']>
};

export type FieldCodec<T> = {
  parse: (v: string) => T;
  serialize: (v: T) => string;
  validate?: (v: T) => boolean;
};

export type Field = {
  name: string;
  type: ((...args: any[]) => any) | Record<string, any>;
  codec?: FieldCodec<any>;
  required?: boolean;
};

export type Message = {
  name: string;
  fields: readonly Field[];
  parse?: (data: string[]) => Record<string, any>;
  refine?: (fields: Record<string, any>) => Record<string, any>;
  validate?: (fields: Record<string, any>) => boolean;
  serialize?: (msg: Record<string, any>) => string[];
};

// GNSS system talker IDs identifying the source of NMEA sentences
export const Talker = {
  ALL: 'GN', // Multiple
  QZSS: 'GQ', // Japan
  GPS: 'GP', // USA
  GLONASS: 'GL', // Russia
  GALILEO: 'GA', // Europe
  BEIDOU: 'BD', // China
  NAVIC: 'GI', // India
} as const;

// Data validity indicators for NMEA sentences
export const DataValidity = {
  VALID: 'A',
  INVALID: 'V',
} as const;

// Position fix type indicators describing the quality of the GPS fix
export const FixType = {
  NOT_VALID: 0,
  GPS_FIX: 1,
  DIFFERENTIAL_GPS_FIX: 2,
  PPS: 3,
  RTK_FIX: 4,
  RTK_FLOAT: 5,
  ESTIMATED: 6,
  MANUAL: 7,
  SIMULATION: 8,
} as const;

// GNSS operation modes (manual or automatic)
export const OperationMode = {
  MANUAL: 'M',
  AUTOMATIC: 'A',
} as const;

// Navigation modes describing the dimensionality of the position fix
export const NavigationMode = {
  NOT_AVAILABLE: 1,
  FIX_2D: 2,
  FIX_3D: 3,
} as const;

export type Talker = (typeof Talker)[keyof typeof Talker];
export type DataValidity = (typeof DataValidity)[keyof typeof DataValidity];
export type FixType = (typeof FixType)[keyof typeof FixType];
export type OperationMode = (typeof OperationMode)[keyof typeof OperationMode];
export type NavigationMode = (typeof NavigationMode)[keyof typeof NavigationMode];

// Assertion for callable to narrow a constructor
function isConstructor<T>(fn: Fn<T> | Constructor<T>): fn is new (...args: any[]) => T {
  return fn.toString().startsWith('class ');
}

// Wraps a function or class into a variadic factory that returns T[]
export const ArrayOf = <T>(fn: Fn<T> | Constructor<T>): Fn<T[]> => {
  return isConstructor(fn)
    ? (...args) => args.map(a => new fn(a))
    : (...args) => args.map(a => fn(a));
};


const TimeCodec: FieldCodec<Date> = {
  parse: value => {
    const match = value.match(/(\d{2})(\d{2})(\d{2})\.?(\d{3})?/);
    if (!match) throw new Error(`Invalid time format: ${value}`);
    const [, hour, min, sec, msec = 0] = match;
    const date = new Date();
    date.setUTCHours(+hour, +min, +sec, +msec);
    return date;
  },
  serialize: date => {
    const pad = (n: number, w = 2) => String(n).padStart(w, '0');
    const hour = pad(date.getUTCHours());
    const min = pad(date.getUTCMinutes());
    const sec = pad(date.getUTCSeconds());
    const msec = pad(date.getUTCMilliseconds(), 3);
    return `${hour}${min}${sec}.${msec}`;
  },
};

const CoordinateCodec = (kind: 'lat' | 'lon'): FieldCodec<number> => {
  const bound = kind === 'lat' ? 90 : 180;
  const width = kind === 'lat' ? 10 : 11;
  return {
    parse: value => {
      const f = parseFloat(value);
      const degs = Math.floor(f / 100);
      const mins = (f % 100) / 60;
      return Math.round((degs + mins) * 1e7) / 1e7;
    },
    serialize: coordinate => {
      const abs = Math.abs(coordinate);
      const degs = Math.floor(abs);
      const mins = (abs - degs) * 60;
      const combined = degs * 100 + mins;
      return combined.toFixed(5).padStart(width, '0');
    },
    validate: v => -bound <= v && v <= bound,
  };
};

// Standard field codecs for parsing/serialization
export const codecs = Object.freeze({
  TIME: TimeCodec,
  LAT: CoordinateCodec('lat'),
  LON: CoordinateCodec('lon'),
});

// Applies hemispheric correction to latitude and longitude values (negative for South/West)
export const applyHemisphere = (fields: Record<string, any>) => {
  const { lat, lon, latDir = 'N', lonDir = 'E' } = fields;
  return {
    lat: latDir === 'S' ? -lat : lat,
    lon: lonDir === 'W' ? -lon : lon,
  };
};

// Represents a single satellite used in position calculation (GSV messages)
export class Satellite {
  constructor(
    public prn: number,
    public elevation: number,
    public azimuth: number,
    public snr: number,
  ) {}

  // Coerce to string when serialize
  [Symbol.toPrimitive](hint: 'string' | 'number' | 'default'): string | number {
    if (hint === 'string') return [this.prn, this.elevation, this.azimuth, this.snr].join(',');
    throw new Error(`Satellite cannot be coerced to ${hint}`);
  }
}

const MESSAGES = {
  GGA: {
    name: 'GGA',
    fields: [
      { name: 'time', type: Date, codec: codecs.TIME },
      { name: 'lat', type: Number, codec: codecs.LAT },
      { name: 'latDir', type: String },
      { name: 'lon', type: Number, codec: codecs.LON },
      { name: 'lonDir', type: String },
      { name: 'fix', type: FixType },
      { name: 'numSatellites', type: Number },
      { name: 'hdop', type: Number },
      { name: 'alt', type: Number },
      { name: 'altUnit', type: String },
      { name: 'geoidHeight', type: Number },
      { name: 'geoidHeightUnit', type: String },
      { name: 'age', type: Number, required: false as const },
      { name: 'station', type: String, required: false as const },
    ] as const,
    refine: (fields: Record<string, any>) => ({ ...fields, ...applyHemisphere(fields) }),
  },

  GLL: {
    name: 'GLL',
    fields: [
      { name: 'lat', type: Number, codec: codecs.LAT },
      { name: 'latDir', type: String },
      { name: 'lon', type: Number, codec: codecs.LON },
      { name: 'lonDir', type: String },
      { name: 'time', type: Date, codec: codecs.TIME },
      { name: 'status', type: DataValidity },
      { name: 'mode', type: OperationMode },
    ] as const,
    refine: (fields: Record<string, any>) => ({ ...fields, ...applyHemisphere(fields) }),
  },

  GST: {
    name: 'GST',
    fields: [
      { name: 'time', type: Date, codec: codecs.TIME },
      { name: 'rmsRange', type: Number },
      { name: 'stdMajor', type: Number },
      { name: 'stdMinor', type: Number },
      { name: 'angleMajor', type: Number },
      { name: 'stdLat', type: Number },
      { name: 'stdLon', type: Number },
      { name: 'stdAlt', type: Number },
    ] as const,
  },

  RMC: {
    name: 'RMC',
    fields: [
      { name: 'time', type: Date, codec: codecs.TIME },
      { name: 'status', type: DataValidity },
      { name: 'lat', type: Number, codec: codecs.LAT },
      { name: 'latDir', type: String },
      { name: 'lon', type: Number, codec: codecs.LON },
      { name: 'lonDir', type: String },
      { name: 'speed', type: Number },
      { name: 'course', type: Number },
      { name: 'date', type: String },
      { name: 'magVariation', type: Number },
      { name: 'magDir', type: String },
      { name: 'posMode', type: OperationMode },
    ] as const,
    refine: (fields: Record<string, any>) => ({ ...fields, ...applyHemisphere(fields) }),
  },

  GSV: {
    name: 'GSV',
    fields: [
      { name: 'total_msg', type: Number },
      { name: 'msg_num', type: Number },
      { name: 'visibleSatellites', type: Number },
      { name: 'satellites', type: ArrayOf(Satellite) },
      { name: 'signalId', type: String, required: false },
    ] as const,
    parse: (data: string[]) => {
      const satellites: Satellite[] = Array.from(
        { length: Math.floor((data.length - 3) / 4) },
        (_, i) => {
          const pos = 3 + i * 4;
          const [prn = 0, ele = 0, azimuth = 0, snr = 0] = data.slice(pos, pos + 4);
          return new Satellite(+prn, +ele, +azimuth, +snr);
        },
      );
      return {
        total_msg: +data[0],
        msg_num: +data[1],
        visibleSatellites: +data[2],
        satellites,
        signalId: ((data.length - 3) % 4 && data[data.length - 1]) || undefined,
      };
    },
  },

  GSA: {
    name: 'GSA',
    fields: [
      { name: 'opMode', type: OperationMode },
      { name: 'navMode', type: NavigationMode },
      { name: 'prns', type: Array },
      { name: 'pdop', type: Number },
      { name: 'hdop', type: Number },
      { name: 'vdop', type: Number },
      { name: 'systemId', type: String, required: false },
    ] as const,
    parse: (data: string[]) => {
      const offset = data[data.length - 1].match(/^\d+$/) ? 1 : 0;
      const prns = data
        .slice(2, -3 - offset)
        .filter(p => p)
        .map(p => parseInt(p, 10));

      let systemId: Talker | undefined;
      if (offset) {
        const systemIdx = parseInt(data[data.length - 1], 10);
        const talkers = Object.values(Talker);
        if (systemIdx + 1 < talkers.length) {
          systemId = talkers[systemIdx + 1] as Talker;
        }
      }
      return {
        opMode: data[0] as OperationMode,
        navMode: parseInt(data[1], 10) as NavigationMode,
        prns,
        pdop: parseFloat(data[data.length - 3 - offset]),
        hdop: parseFloat(data[data.length - 2 - offset]),
        vdop: parseFloat(data[data.length - 1 - offset]),
        systemId,
      };
    },
  },
} as const;

// Derive NMEA sentence types from MESSAGES
type MessageId = keyof typeof MESSAGES;

export type NMEA<T extends string = string> = T extends MessageId
  ? Prettify<{ talker: string; msgId: T } & InferFields<(typeof MESSAGES)[T]['fields']>>
  : Prettify<{ talker: string; msgId: string } & Record<string, any>>;

// Core NMEA sentence parser and serializer
export default Object.freeze({
  register(msg: Message): void {
    (MESSAGES as Record<string, Message>)[msg.name] = msg;
  },

  checksum: (payload: string): string => {
    return [...payload]
      .reduce((acc, char) => acc ^ char.charCodeAt(0), 0)
      .toString(16)
      .toUpperCase()
      .padStart(2, '0');
  },

  validate(msg: string) {
    // Delimiters
    if (msg.at(0) !== '$' || msg.at(-5) !== '*' || msg.slice(-2) !== '\r\n')
      throw new Error('Invalid NMEA format');

    const payload = msg.slice(1, -5);
    if (this.checksum(payload) !== msg.slice(-4, -2)) throw new Error('Checksum failed');
  },

  isValid(msg: string): boolean {
    try {
      this.validate(msg);
      return true;
    } catch {
      return false;
    }
  },

  parse<T extends string = string>(data: string): NMEA<T> | null {
    this.validate(data);

    const payload = data.slice(1, -5);
    const [header, ...parts] = payload.split(',');

    const talker = header.substring(0, 2);
    const msgId = header.substring(2);

    const msg = (MESSAGES as Record<string, Message>)[msgId];
    if (!msg) throw new Error('NMEA not registered');

    const fields =
      msg.parse?.(parts) ??
      msg.fields.reduce(
        (acc, field, i) => {
          const value = parts[i];
          // codec → custom parse, function → call as coercer, const object → auto-coerce to number or string
          acc[field.name] = value === '' ? undefined
            : field.codec ? field.codec.parse(value)
            : typeof field.type === 'function' ? field.type(value)
            : isNaN(+value) ? value : +value;
          return acc;
        },
        {} as Record<string, unknown>,
      );

    const refined = { ...fields, ...msg.refine?.(fields) };

    if (msg.validate && !msg.validate(refined)) throw new Error('Invalid nmea');

    return { msgId, talker, ...refined } as NMEA<T>;
  },

  serialize(nmea: NMEA): string {
    const msg = (MESSAGES as Record<string, Message>)[nmea.msgId];
    if (!msg) throw new Error('NMEA not registered');

    const parts = msg.serialize?.(nmea) ?? msg.fields.map(field => {
      const value = nmea[field.name];
      if (value == null) return '';
      if (field.codec) return field.codec.serialize(value);
      if (Array.isArray(value)) return value.join(',');
      return String(value);
    });

    const payload = `${nmea.talker}${nmea.msgId},${parts.join(',')}`;
    return `$${payload}*${this.checksum(payload)}\r\n`;
  },
});

// Stateful stream parser
const DOLLAR = 36;
const ASTERISK = 42;
const CR = 13;
const LF = 10;
const MESSAGE_MAX_LENGTH = 82;

enum ParserState {
  IDLE,
  TRACKING,
}

export class StreamFramer {
  private readonly encoder = new TextEncoder();

  private state = ParserState.IDLE;
  private data: Uint8Array;
  private pos = 0;

  constructor(
    private readonly onFrame?: (frame: Uint8Array) => void,
    private readonly messageMaxLength = MESSAGE_MAX_LENGTH,
  ) {
    this.data = new Uint8Array(messageMaxLength);
  }

  reset(): void {
    this.state = ParserState.IDLE;
    this.pos = 0;
  }

  process(byte: number): Uint8Array | null {
    if (byte === DOLLAR) {
      this.reset();
      this.state = ParserState.TRACKING;
    }

    if (this.state === ParserState.TRACKING) {
      if (this.pos >= this.messageMaxLength) {
        this.reset();
        return null;
      }

      this.data[this.pos++] = byte;

      if (
        this.pos > 6 &&
        this.data[this.pos - 2] === CR &&
        this.data[this.pos - 1] === LF &&
        this.data[this.pos - 5] === ASTERISK
      ) {
        const frame = this.data.slice(0, this.pos);
        this.reset();
        this.onFrame?.(frame);
        return frame;
      }
    }

    return null;
  }

  *feed(data: Uint8Array | string): Generator<Uint8Array> {
    const bytes = typeof data === 'string' ? this.encoder.encode(data) : data;
    for (const byte of bytes) {
      const frame = this.process(byte);
      if (frame) yield frame;
    }
  }
}
