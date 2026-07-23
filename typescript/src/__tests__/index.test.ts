import NMEA, {
  StreamFramer,
  codecs,
  applyHemisphere,
  Talker,
  DataValidity,
  FixType,
  OperationMode,
  ArrayOf,
} from '../index.js';

describe('Constants', () => {
  it('should export Talker constants', () => {
    expect(Talker.GPS).toBe('GP');
    expect(Talker.GLONASS).toBe('GL');
    expect(Talker.GALILEO).toBe('GA');
  });

  it('should export DataValidity constants', () => {
    expect(DataValidity.VALID).toBe('A');
    expect(DataValidity.INVALID).toBe('V');
  });

  it('should export FixType constants', () => {
    expect(FixType.GPS_FIX).toBe(1);
    expect(FixType.RTK_FIX).toBe(4);
  });
});

describe('ArrayOf', () => {
  it('should work with plain functions', () => {
    const double = ArrayOf((x: number) => x * 2);
    expect(double(1, 2, 3)).toEqual([2, 4, 6]);
  });

  it('should work with class constructors', () => {
    class Box { constructor(public value: number) {} }
    const result = ArrayOf(Box)(1, 2, 3);
    expect(result).toHaveLength(3);
    expect(result[0]).toBeInstanceOf(Box);
    expect(result[0].value).toBe(1);
  });
});

describe('Codecs', () => {
  describe('TIME codec', () => {
    it('should parse time', () => {
      const time = codecs.TIME.parse('123519');
      expect(time.getUTCHours()).toBe(12);
      expect(time.getUTCMinutes()).toBe(35);
      expect(time.getUTCSeconds()).toBe(19);
    });

    it('should parse time with milliseconds', () => {
      const time = codecs.TIME.parse('123519.500');
      expect(time.getUTCMilliseconds()).toBe(500);
    });

    it('should serialize time', () => {
      const date = new Date();
      date.setUTCHours(12, 35, 19, 500);
      const serialized = codecs.TIME.serialize(date);
      expect(serialized).toMatch(/^123519\.500$/);
    });

    it('should reject invalid time format', () => {
      expect(() => codecs.TIME.parse('invalid')).toThrow();
    });
  });

  describe('LAT codec', () => {
    it('should parse latitude', () => {
      const lat = codecs.LAT.parse('4026.7667');
      expect(lat).toBeCloseTo(40.446111, 5);
    });

    it('should serialize latitude', () => {
      const serialized = codecs.LAT.serialize(53.361337);
      expect(serialized).toBe('5321.68022');
    });

    it('should validate latitude bounds', () => {
      expect(codecs.LAT.validate?.(90)).toBe(true);
      expect(codecs.LAT.validate?.(-90)).toBe(true);
      expect(codecs.LAT.validate?.(91)).toBe(false);
      expect(codecs.LAT.validate?.(-91)).toBe(false);
    });
  });

  describe('LON codec', () => {
    it('should parse longitude', () => {
      const lon = codecs.LON.parse('01131.000');
      expect(lon).toBeCloseTo(11.5167, 4);
    });

    it('should serialize longitude', () => {
      const serialized = codecs.LON.serialize(11.5167);
      expect(serialized).toMatch(/^01131\.\d+$/);
    });

    it('should validate longitude bounds', () => {
      expect(codecs.LON.validate?.(180)).toBe(true);
      expect(codecs.LON.validate?.(-180)).toBe(true);
      expect(codecs.LON.validate?.(181)).toBe(false);
      expect(codecs.LON.validate?.(-181)).toBe(false);
    });
  });
});

describe('NMEA', () => {
  describe('checksum', () => {
    it('should calculate correct checksum', () => {
      const payload = 'GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,';
      const checksum = NMEA.checksum(payload);
      expect(checksum).toBe('76');
    });

    it('should handle empty payload', () => {
      const checksum = NMEA.checksum('');
      expect(checksum).toBe('00');
    });
  });

  describe('validation', () => {
    it('should validate correct NMEA sentence', () => {
      const valid = NMEA.isValid(
        '$GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,*76\r\n',
      );
      expect(valid).toBe(true);
    });

    it('should reject sentence with invalid checksum', () => {
      const invalid = NMEA.isValid(
        '$GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,*42\r\n',
      );
      expect(invalid).toBe(false);
    });

    it('should reject sentence without delimiter', () => {
      const invalid = NMEA.isValid(
        'GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,*76\r\n',
      );
      expect(invalid).toBe(false);
    });

    it('should reject sentence without CRLF', () => {
      const invalid = NMEA.isValid(
        '$GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,*76',
      );
      expect(invalid).toBe(false);
    });
  });

  describe('hemispheric correction', () => {
    it('should apply South/West corrections', () => {
      const fields = { lat: 48.1173, lon: 11.5167, latDir: 'S', lonDir: 'W' };
      const corrected = applyHemisphere(fields);

      expect(corrected.lat).toBe(-48.1173);
      expect(corrected.lon).toBe(-11.5167);
    });

    it('should not change North/East coordinates', () => {
      const fields = { lat: 48.1173, lon: 11.5167, latDir: 'N', lonDir: 'E' };
      const corrected = applyHemisphere(fields);

      expect(corrected.lat).toBe(48.1173);
      expect(corrected.lon).toBe(11.5167);
    });
  });

  describe('parse invalid sentences', () => {
    it('should throw for invalid sentence', () => {
      expect(() => NMEA.parse('$INVALID,DATA*FF\r\n')).toThrow('Checksum failed');
    });

    it('should return null for unregistered message type', () => {
      const checksum = NMEA.checksum('GPXXX,DATA');
      expect(() => NMEA.parse(`$GPXXX,DATA*${checksum}\r\n`)).toThrow('NMEA not registered');
    });
  });

  describe('parsing GGA', () => {
    it('should parse valid GGA sentence', () => {
      const sentence =
        '$GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,*76\r\n';
      const nmea = NMEA.parse<'GGA'>(sentence);

      expect(nmea).not.toBeNull();
      expect(nmea!.msgId).toBe('GGA');
      expect(nmea!.talker).toBe(Talker.GPS);
      expect(nmea!.lat).toBeCloseTo(53.361337, 6);
      expect(nmea!.lon).toBeCloseTo(-6.505620, 6);
      expect(nmea!.numSatellites).toBe(8);
      expect(nmea!.hdop).toBe(1.03);
      expect(nmea!.alt).toBe(61.7);
    });
  });

  describe('parsing RMC', () => {
    it('should parse valid RMC sentence', () => {
      const sentence = '$GPRMC,092750.000,A,5321.68020,N,00630.33720,W,0.02,31.66,280511,,,A*43\r\n';
      const nmea = NMEA.parse<'RMC'>(sentence);

      expect(nmea).not.toBeNull();
      expect(nmea!.msgId).toBe('RMC');
      expect(nmea!.status).toBe(DataValidity.VALID);
      expect(nmea!.lat).toBeCloseTo(53.361337, 6);
      expect(nmea!.lon).toBeCloseTo(-6.505620, 6);
      expect(nmea!.speed).toBe(0.02);
      expect(nmea!.course).toBe(31.66);
    });
  });

  describe('parsing GLL', () => {
    it('should parse valid GLL sentence', () => {
      const sentence = '$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*42\r\n';
      const nmea = NMEA.parse<'GLL'>(sentence);

      expect(nmea).not.toBeNull();
      expect(nmea!.msgId).toBe('GLL');
      expect(nmea!.lat).toBeCloseTo(49.2741667, 6);
      expect(nmea!.lon).toBeCloseTo(-123.1853333, 6);
      expect(nmea!.status).toBe('A');
      expect(nmea!.mode).toBe(OperationMode.AUTOMATIC);
    });
  });

  describe('parsing GST', () => {
    it('should parse valid GST sentence', () => {
      const sentence = '$GPGST,092750.000,0.5,0.3,0.2,45,0.3,0.2,0.1*45\r\n';
      const nmea = NMEA.parse<'GST'>(sentence);

      expect(nmea).not.toBeNull();
      expect(nmea!.msgId).toBe('GST');
      expect(nmea!.rmsRange).toBe(0.5);
      expect(nmea!.stdMajor).toBe(0.3);
    });
  });

  describe('parsing GSV', () => {
    it('should parse valid GSV sentence', () => {
      const sentence = '$GPGSV,3,1,11,10,63,137,17,7,61,98,15,5,59,290,20,8,54,157,30,*5C\r\n';
      const nmea = NMEA.parse<'GSV'>(sentence);

      expect(nmea).not.toBeNull();
      expect(nmea!.msgId).toBe('GSV');
      expect(nmea!.total_msg).toBe(3);
      expect(nmea!.msg_num).toBe(1);
      expect(nmea!.visibleSatellites).toBe(11);
      const satellites = nmea!.satellites;
      expect(satellites).toHaveLength(4);
      expect(satellites[0].prn).toBe(10);
      expect(satellites[0].azimuth).toBe(137);
      expect(satellites[3].prn).toBe(8);
      expect(satellites[3].snr).toBe(30);
      expect(nmea!.signalId).toBeUndefined();
    });
  });

  describe('serialization', () => {
    it('should serialize NMEA sentences', () => {
      const sentences = [
        '$GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,*76\r\n',
        '$GPRMC,092750.000,A,5321.68020,N,00630.33720,W,0.02,31.66,280511,,,A*43\r\n',
        '$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*42\r\n',
        '$GPGST,092750.000,0.5,0.3,0.2,45,0.3,0.2,0.1*45\r\n',
        '$GPGSV,3,1,11,10,63,137,17,7,61,98,15,5,59,290,20,8,54,157,30,*5C\r\n',
      ];
      sentences.forEach(sentence => {
        const nmea = NMEA.parse(sentence);
        expect(nmea).not.toBeNull();
        const serialized = NMEA.serialize(nmea!);
        expect(serialized).toBe(sentence);
      })
    });

    it('should round-trip RMC sentence', () => {
      const sentence = '$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n';
      const nmea = NMEA.parse<'RMC'>(sentence);
      expect(nmea).not.toBeNull();
      expect(nmea!.msgId).toBe('RMC');

      const serialized = NMEA.serialize(nmea!);
      const reparsed = NMEA.parse<'RMC'>(serialized);
      expect(reparsed).not.toBeNull();

      expect(reparsed!.speed).toBeCloseTo(nmea!.speed || 0, 1);
      expect(reparsed!.course).toBeCloseTo(nmea!.course || 0, 1);
    });
  });

  it('should parse and serialize a custom message', () => {
    NMEA.register({
      name: 'VTG',
      fields: [
        { name: 'courseTrue', type: Number },
        { name: 'refTrue', type: String },
        { name: 'courseMag', type: Number },
        { name: 'refMag', type: String },
        { name: 'speedKnots', type: Number },
        { name: 'unitKnots', type: String },
        { name: 'speedKmh', type: Number },
        { name: 'unitKmh', type: String },
        { name: 'mode', type: OperationMode },
      ],
    });

    const sentence = '$GPVTG,54.7,T,34.4,M,5.5,N,10.2,K,A*15\r\n';
    const nmea = NMEA.parse(sentence);

    expect(nmea).not.toBeNull();
    expect(nmea!.msgId).toBe('VTG');
    expect(nmea!.talker).toBe('GP');

    const serialized = NMEA.serialize(nmea!);
    expect(serialized).toBe(sentence);
  });

  it('should throw for unregistered message', () => {
    const checksum = NMEA.checksum('GPZZZ,1,2,3');
    expect(() => NMEA.parse(`$GPZZZ,1,2,3*${checksum}\r\n`)).toThrow('NMEA not registered');
  });
});

describe('StreamFramer', () => {
  it('should parse a complete frame', () => {
    const framer = new StreamFramer();
    const sentence = '$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n';

    const frames = Array.from(framer.feed(sentence));

    expect(frames).toHaveLength(1);
    const decoded = new TextDecoder().decode(frames[0]);
    expect(decoded).toContain('GPGGA');
  });

  it('should handle multiple frames', () => {
    const framer = new StreamFramer();
    const sentence1 = '$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n';
    const sentence2 = '$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n';

    const frames = Array.from(framer.feed(sentence1 + 'garbage' + sentence2));

    expect(frames).toHaveLength(2);
  });

  it('should handle byte input', () => {
    const framer = new StreamFramer();
    const sentence = '$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n';
    const bytes = new TextEncoder().encode(sentence);

    const frames = Array.from(framer.feed(bytes));

    expect(frames).toHaveLength(1);
  });

  it('should discard incomplete frames', () => {
    const framer = new StreamFramer();
    const incomplete = '$GPGGA,123519,4807.038,N,01131.000,E';

    const frames = Array.from(framer.feed(incomplete));

    expect(frames).toHaveLength(0);
  });
});
