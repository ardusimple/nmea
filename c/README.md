# nmea — C parser

A small, fast NMEA 0183 parser written in C11. No heap allocation, no external
dependencies beyond the C standard library.

## Features

- Parse and serialize **GGA, GLL, GST, RMC, GSV, GSA** sentences
- XOR **checksum** calculation and validation
- **Stream framer** — feed raw bytes one at a time and receive complete frames
- Full **round-trip** fidelity (parse → serialize reproduces the original sentence)

## Build

```sh
make        # compile and run tests
make clean  # remove the test binary
```

Requires a C11 compiler (`cc`) and `libm`.

## Usage

### Parse a sentence

```c
#include "nmea.h"

const uint8_t raw[] = "$GPGGA,092750.000,5321.68020,N,00630.33720,W,"
                      "1,8,1.03,61.7,M,55.2,M,,*76\r\n";

nmea_sentence_t nmea;
nmea_err_t err = nmea_parse(raw, sizeof raw - 1, &nmea, /*validate=*/true);

if (err == NMEA_OK && nmea.type == NMEA_MSG_GGA) {
    printf("lat=%.7f  lon=%.7f  fix=%d  sats=%d\n",
           nmea.gga.lat, nmea.gga.lon, nmea.gga.fix, nmea.gga.num_satellites);
}
```

### Serialize a sentence

```c
uint8_t buf[NMEA_MAX_LEN + 1];
int n = nmea_serialize(&nmea, buf, sizeof buf);
// buf now contains the complete sentence including $, *XX and CRLF
```

### Stream framer

The framer extracts complete NMEA sentences from a raw byte stream (e.g. a serial port). It handles noise, partial sentences, and back-to-back sentences
correctly.

```c
void on_frame(const uint8_t *data, int len, void *ctx) {
    nmea_sentence_t nmea;
    if (nmea_parse(data, len, &nmea, true) == NMEA_OK) {
        /* process nmea */
    }
}

nmea_framer_t framer;
nmea_framer_init(&framer, on_frame, NULL);

// Feed incoming bytes one at a time or in chunks
nmea_framer_feed(&framer, buf, bytes_read);
```

`nmea_framer_process()` can also be used byte-by-byte; it returns a pointer to
the completed frame inside the framer's internal buffer (valid until the next
`$` byte is processed).

## API reference

### Core

| Function | Description |
| --- | --- |
| `nmea_checksum(data, len)` | XOR checksum over `data[0..len)` |
| `nmea_validate(data, len)` | Validate structure and checksum |
| `nmea_parse(data, len, out, validate)` | Parse raw bytes into `nmea_sentence_t` |
| `nmea_serialize(s, buf, buf_len)` | Serialize back to raw bytes |

`nmea_checksum`, `nmea_validate`, and `nmea_parse` return `nmea_err_t` (`NMEA_OK (0)` on success, negative on error). `nmea_serialize` returns the number of bytes written (excluding `'\0'`), or `<0` on error.

### Framer

| Function | Description |
| --- | --- |
| `nmea_framer_init(f, cb, ctx)` | Initialise with optional frame callback |
| `nmea_framer_reset(f)` | Discard any in-progress frame |
| `nmea_framer_process(f, byte, out_len)` | Feed one byte; returns frame pointer or `NULL` |
| `nmea_framer_feed(f, data, len)` | Feed a buffer; fires callback per frame |

### Key types

```c
nmea_sentence_t   // top-level: .talker, .type, union { .gga, .gll, … }
nmea_gga_t        // time, lat, lon, fix, num_satellites, hdop, alt, …
nmea_gll_t        // lat, lon, time, status, mode
nmea_gst_t        // time, rms_range, std_major/minor/lat/lon/alt
nmea_rmc_t        // time, status, lat, lon, speed, course, date, …
nmea_gsv_t        // total_msg, msg_num, visible_satellites, satellites[]
nmea_gsa_t        // op_mode, nav_mode, prns[], pdop, hdop, vdop, system_id
nmea_framer_t     // opaque state machine; stack-allocate, init before use
```

Absent optional fields use `NAN` (doubles) or `'\0'` (chars) as sentinels.

### Adding a new message type

1. Add a payload struct (`nmea_xxx_t`) and a `NMEA_MSG_XXX` enum value to [include/nmea.h](include/nmea.h).
2. Add `parse_xxx()` and `ser_xxx()` static functions in [src/nmea.c](src/nmea.c).
3. Wire them into the `switch` statements in `nmea_parse()` and `nmea_serialize()`.
4. Add at least one parse test and one round-trip test in [tests/test_nmea.c](tests/test_nmea.c).

## Development

### Prerequisites

| Tool | Minimum version | Notes |
| --- | --- | --- |
| C compiler | C11 | `cc` resolves to clang on macOS, gcc on Linux |
| make | any | GNU Make 3.81+ or BSD make |
| libm | — | ships with the OS; link with `-lm` (handled by the Makefile) |

On macOS the compiler ships with Xcode Command Line Tools:

```sh
xcode-select --install
```

On Debian/Ubuntu:

```sh
sudo apt install build-essential
```

### Running the tests

```sh
cd c
make  # compiles src/nmea.c + tests/test_nmea.c and runs the binary
```

Expected output:

```text
nmea C tests
------------
  checksum
  validate_ok
  ...
  framer_return_value
------------
169 tests run, 0 failed
```

A non-zero exit code means at least one assertion failed — the failing line is
printed to stderr with the file, line number, and condition that did not hold.

### Adding a test

Tests live in [tests/test_nmea.c](tests/test_nmea.c). Each test is a static
function declared with `TEST(name)` and registered in `main()` with `RUN(name)`:

```c
TEST(my_new_test) {
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(MY_RAW, sizeof MY_RAW - 1, &s, true), NMEA_OK);
    ASSERT_APPROX(s.gga.lat, 53.361337);  /* abs tolerance 1e-5 */
}

// in main():
RUN(my_new_test);
```

Available assertion macros:

| Macro | Description |
| --- | --- |
| `ASSERT(cond)` | Generic boolean check |
| `ASSERT_EQ(a, b)` | Integer / pointer equality (`==`) |
| `ASSERT_STR(a, b)` | String equality (`strcmp`) |
| `ASSERT_APPROX(a, b)` | Floating-point equality within `1e-5` |
