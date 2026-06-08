# NMEA

Implementations of the NMEA protocol — parsing, validation, serialization, and streaming — across multiple languages.

## Implementations

| Language | Path |
| --- | --- |
| C | [c](/c/README.md) |

## What is NMEA?

NMEA 0183 is a serial communication standard used by GPS receivers and other marine navigation equipment. Each message is a comma-delimited ASCII sentence with a talker prefix, message type, payload fields, and a checksum.

```txt
$GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,1.03,61.7,M,55.2,M,,*76\r\n
 └─┬─┘ └──────────────────────────────┬─────────────────────────────┘ └┬┘
   │                                  │                                │
talker + message type              payload                          checksum
```
