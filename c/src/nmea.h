#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Maximum NMEA sentence length (characters, including $, *XX, CRLF) */
#define NMEA_MAX_LEN 82
#define NMEA_MAX_SATELLITES 16
#define NMEA_MAX_PRNS 12

/* Return codes ─────────────────────────────────────────────────────────── */
typedef enum
{
    NMEA_OK = 0,
    NMEA_ERR_INVALID = -1, /* bad structure or checksum          */
    NMEA_ERR_UNKNOWN = -2, /* unsupported sentence type          */
    NMEA_ERR_FIELDS = -3,  /* too few fields for message type    */
} nmea_err_t;

/* Talker IDs ───────────────────────────────────────────────────────────── */
typedef enum
{
    NMEA_TALKER_ALL = 0,     /* GN – combined GNSS   */
    NMEA_TALKER_QZSS = 1,    /* GQ – Japan           */
    NMEA_TALKER_GPS = 2,     /* GP – USA             */
    NMEA_TALKER_GLONASS = 3, /* GL – Russia          */
    NMEA_TALKER_GALILEO = 4, /* GA – Europe          */
    NMEA_TALKER_BEIDOU = 5,  /* BD – China           */
    NMEA_TALKER_NAVIC = 6,   /* GI – India           */
    NMEA_TALKER_UNKNOWN = 7,
} nmea_talker_t;

/* Message types ────────────────────────────────────────────────────────── */
typedef enum
{
    NMEA_MSG_GGA = 0,
    NMEA_MSG_GLL,
    NMEA_MSG_GST,
    NMEA_MSG_RMC,
    NMEA_MSG_GSV,
    NMEA_MSG_GSA,
    NMEA_MSG_UNKNOWN,
} nmea_msg_t;

/* Fix / navigation enumerations ────────────────────────────────────────── */
typedef enum
{
    NMEA_FIX_NOT_VALID = 0,
    NMEA_FIX_GPS = 1,
    NMEA_FIX_DGPS = 2,
    NMEA_FIX_PPS = 3,
    NMEA_FIX_RTK = 4,
    NMEA_FIX_RTK_FLOAT = 5,
    NMEA_FIX_ESTIMATED = 6,
    NMEA_FIX_MANUAL = 7,
    NMEA_FIX_SIMULATION = 8,
} nmea_fix_t;

typedef enum
{
    NMEA_NAV_NOT_AVAILABLE = 1,
    NMEA_NAV_FIX_2D = 2,
    NMEA_NAV_FIX_3D = 3,
} nmea_nav_mode_t;

/* Time ─────────────────────────────────────────────────────────────────── */
typedef struct
{
    uint8_t hour, min, sec;
    uint16_t ms;
} nmea_time_t;

/* Satellite entry (GSV) ────────────────────────────────────────────────── */
typedef struct
{
    uint8_t prn;
    int8_t elevation;
    uint16_t azimuth;
    uint8_t snr;
} nmea_satellite_t;

/* Message payloads ─────────────────────────────────────────────────────── */

/* GGA – Global positioning system fix data */
typedef struct
{
    nmea_time_t time;           /* UTC time (HH:MM:SS.mmm) */
    double lat, lon;            /* decimal degrees; negative = S/W */
    nmea_fix_t fix;             /* 0=invalid, 1=GPS, 2=DGPS, 3=PPS, 4=RTK, 5=RTK float, 6=est, 7=manual, 8=sim */
    int num_satellites;         /* satellites in solution */
    double hdop, alt;           /* horizontal DOP, altitude (m) */
    double geoid_height;        /* height above ellipsoid (m) */
    double age;                 /* differential GPS age (sec), NAN if absent */
    int station;                /* station ID, 0 if absent */
} nmea_gga_t;

/* GLL – Geographic position, Latitude, Longitude */
typedef struct
{
    double lat, lon;            /* decimal degrees; negative = S/W */
    nmea_time_t time;           /* UTC time (HH:MM:SS.mmm) */
    char status;                /* 'A'=valid, 'V'=invalid */
    char mode;                  /* 'A'=autonomous, 'D'=differential; '\0' if absent */
} nmea_gll_t;

/* GST – GNSS pseudorange error statistics */
typedef struct
{
    nmea_time_t time;           /* UTC time (HH:MM:SS.mmm) */
    double rms_range;           /* RMS range error (m) */
    double std_major, std_minor; /* standard deviation of error ellipse axes (m) */
    double angle_major;         /* orientation of major axis (degrees true) */
    double std_lat, std_lon;    /* standard deviation of latitude, longitude (m) */
    double std_alt;             /* standard deviation of altitude (m) */
} nmea_gst_t;

/* RMC – Recommended Minimum Navigation Information */
typedef struct
{
    nmea_time_t time;           /* UTC time (HH:MM:SS.mmm) */
    char status;                /* 'A'=valid, 'V'=invalid */
    double lat, lon;            /* decimal degrees; negative = S/W */
    double speed, course;       /* speed over ground (knots), course true (degrees) */
    char date[7];               /* "DDMMYY\0" format */
    double mag_variation;       /* magnetic variation (degrees), NAN if absent */
    char mag_dir;               /* 'E' or 'W'; '\0' if absent */
    char pos_mode;              /* positioning mode; '\0' if absent */
} nmea_rmc_t;

/* GSV – GNSS Satellites in View */
typedef struct
{
    int total_msg, msg_num;     /* total sentences in sequence, current sentence number */
    int visible_satellites;     /* total number of visible satellites */
    int num_satellites;         /* number of satellite entries in this sentence (0-4) */
    nmea_satellite_t satellites[NMEA_MAX_SATELLITES]; /* satellite data (prn, elevation, azimuth, snr) */
    char signal_id[4];          /* signal ID (e.g., "L1"); empty string if absent */
} nmea_gsv_t;

/* GSA – GNSS DOP and Active Satellites */
typedef struct
{
    char op_mode;               /* 'M'=manual, 'A'=automatic */
    nmea_nav_mode_t nav_mode;   /* 1=not available, 2=2D fix, 3=3D fix */
    int num_prns;               /* number of PRNs in solution (0-12) */
    uint8_t prns[NMEA_MAX_PRNS]; /* PRNs of satellites used in solution */
    double pdop, hdop, vdop;    /* position, horizontal, vertical dilution of precision */
    nmea_talker_t system_id;    /* GNSS system identifier (NMEA 4.1+); NMEA_TALKER_UNKNOWN if absent */
} nmea_gsa_t;

/* Top-level sentence ───────────────────────────────────────────────────── */
typedef struct
{
    nmea_talker_t talker;
    nmea_msg_t type;
    union
    {
        nmea_gga_t gga;
        nmea_gll_t gll;
        nmea_gst_t gst;
        nmea_rmc_t rmc;
        nmea_gsv_t gsv;
        nmea_gsa_t gsa;
    };
} nmea_sentence_t;

/* Core API ─────────────────────────────────────────────────────────────── */

/* XOR checksum of data[0..len) */
uint8_t nmea_checksum(const uint8_t *data, int len);

/* Validate structure + checksum.  Returns NMEA_OK or NMEA_ERR_INVALID. */
nmea_err_t nmea_validate(const uint8_t *data, int len);

/* Parse raw sentence into *out.  If validate=true, runs nmea_validate first.
   Returns NMEA_OK on success, negative on error. */
nmea_err_t nmea_parse(const uint8_t *data, int len, nmea_sentence_t *out,
                      bool validate);

/* Serialize *s into buf (must be >= NMEA_MAX_LEN+1 bytes).
   Returns bytes written excluding '\0' (i.e. strlen of result), or <0 on error.
 */
int nmea_serialize(const nmea_sentence_t *s, uint8_t *buf, int buf_len);

/* Stream framer ────────────────────────────────────────────────────────── */
typedef void (*nmea_frame_cb_t)(const uint8_t *data, int len, void *ctx);

typedef struct
{
    int state;
    int len;
    uint8_t buf[NMEA_MAX_LEN + 1];
    nmea_frame_cb_t on_frame;
    void *ctx;
} nmea_framer_t;

void nmea_framer_init(nmea_framer_t *f, nmea_frame_cb_t cb, void *ctx);
void nmea_framer_reset(nmea_framer_t *f);

/* Process one byte. Returns pointer to complete frame in f->buf (valid until
   the next call that starts a new sentence), or NULL. *out_len is set to the
   frame length when a frame is returned. */
const uint8_t *nmea_framer_process(nmea_framer_t *f, uint8_t byte,
                                   int *out_len);

/* Process a chunk; calls on_frame for each complete sentence. */
void nmea_framer_feed(nmea_framer_t *f, const uint8_t *data, int len);
