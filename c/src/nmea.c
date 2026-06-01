#include "nmea.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Internal constants ───────────────────────────────────────────────────── */
#define FRAMER_IDLE 0
#define FRAMER_TRACKING 1
#define MAX_FIELDS 32

/* Helpers: string → value ──────────────────────────────────────────────── */

static double parse_double(const char *s)
{
    return (s && *s) ? strtod(s, NULL) : NAN;
}

static int parse_int(const char *s)
{
    return (s && *s) ? (int)strtol(s, NULL, 10) : 0;
}

/* Parse NMEA ddmm.mmmmm / dddmm.mmmmm coordinate to decimal degrees. */
static double parse_coord(const char *s)
{
    if (!s || !*s) return NAN;
    double v = strtod(s, NULL);
    int deg = (int)(v / 100.0);
    double min = v - deg * 100.0;
    return deg + min / 60.0;
}

/* Parse HHMMSS[.mmm] into nmea_time_t. */
static nmea_time_t parse_time(const char *s)
{
    nmea_time_t t = {0, 0, 0, 0};
    if (!s || (int)strlen(s) < 6) return t;
    t.hour = (uint8_t)((s[0] - '0') * 10 + (s[1] - '0'));
    t.min = (uint8_t)((s[2] - '0') * 10 + (s[3] - '0'));
    t.sec = (uint8_t)((s[4] - '0') * 10 + (s[5] - '0'));
    if (s[6] == '.')
    {
        char ms[4] = "000";
        for (int i = 0; i < 3 && s[7 + i]; i++)
            ms[i] = s[7 + i];
        t.ms = (uint16_t)strtol(ms, NULL, 10);
    }
    return t;
}

/* Split comma-separated payload into fields[]; returns field count.
   buf is modified in-place (commas replaced with '\0'). */
static int split_fields(char *buf, char *fields[], int max)
{
    int n = 0;
    if (n < max) fields[n++] = buf;
    for (char *p = buf; *p; p++)
    {
        if (*p == ',' && n < max)
        {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

/* Talker / message-type lookup ─────────────────────────────────────────── */

static const char *const TALKER_STR[] = {"GN", "GQ", "GP", "GL",
                                         "GA", "BD", "GI", "??"};

static nmea_talker_t parse_talker(const char *s)
{
    for (int i = 0; i < 7; i++)
    {
        if (s[0] == TALKER_STR[i][0] && s[1] == TALKER_STR[i][1])
            return (nmea_talker_t)i;
    }
    return NMEA_TALKER_UNKNOWN;
}

static nmea_msg_t parse_msg_type(const char *s)
{
    if (memcmp(s, "GGA", 3) == 0) return NMEA_MSG_GGA;
    if (memcmp(s, "GLL", 3) == 0) return NMEA_MSG_GLL;
    if (memcmp(s, "GST", 3) == 0) return NMEA_MSG_GST;
    if (memcmp(s, "RMC", 3) == 0) return NMEA_MSG_RMC;
    if (memcmp(s, "GSV", 3) == 0) return NMEA_MSG_GSV;
    if (memcmp(s, "GSA", 3) == 0) return NMEA_MSG_GSA;
    return NMEA_MSG_UNKNOWN;
}

/* Per-message parsers (fields[0] = talker+msgid, fields[1..] = data) ───── */

static nmea_err_t parse_gga(char *f[], int n, nmea_gga_t *out)
{
    if (n < 15) return NMEA_ERR_FIELDS;
    out->time = parse_time(f[1]);
    out->lat = parse_coord(f[2]);
    if (f[3][0] == 'S') out->lat = -out->lat;
    out->lon = parse_coord(f[4]);
    if (f[5][0] == 'W') out->lon = -out->lon;
    out->fix = (nmea_fix_t)parse_int(f[6]);
    out->num_satellites = parse_int(f[7]);
    out->hdop = parse_double(f[8]);
    out->alt = parse_double(f[9]);
    /* f[10] = alt unit (M) */
    out->geoid_height = parse_double(f[11]);
    /* f[12] = geoid unit (M) */
    out->age = parse_double(f[13]);
    out->station = parse_int(f[14]);
    return NMEA_OK;
}

static nmea_err_t parse_gll(char *f[], int n, nmea_gll_t *out)
{
    if (n < 7) return NMEA_ERR_FIELDS;
    out->lat = parse_coord(f[1]);
    if (f[2][0] == 'S') out->lat = -out->lat;
    out->lon = parse_coord(f[3]);
    if (f[4][0] == 'W') out->lon = -out->lon;
    out->time = parse_time(f[5]);
    out->status = f[6][0];
    out->mode = (n >= 8 && f[7][0]) ? f[7][0] : '\0';
    return NMEA_OK;
}

static nmea_err_t parse_gst(char *f[], int n, nmea_gst_t *out)
{
    if (n < 9) return NMEA_ERR_FIELDS;
    out->time = parse_time(f[1]);
    out->rms_range = parse_double(f[2]);
    out->std_major = parse_double(f[3]);
    out->std_minor = parse_double(f[4]);
    out->angle_major = parse_double(f[5]);
    out->std_lat = parse_double(f[6]);
    out->std_lon = parse_double(f[7]);
    out->std_alt = parse_double(f[8]);
    return NMEA_OK;
}

static nmea_err_t parse_rmc(char *f[], int n, nmea_rmc_t *out)
{
    if (n < 12) return NMEA_ERR_FIELDS;
    out->time = parse_time(f[1]);
    out->status = f[2][0];
    out->lat = parse_coord(f[3]);
    if (f[4][0] == 'S') out->lat = -out->lat;
    out->lon = parse_coord(f[5]);
    if (f[6][0] == 'W') out->lon = -out->lon;
    out->speed = parse_double(f[7]);
    out->course = parse_double(f[8]);
    strncpy(out->date, f[9], 6);
    out->date[6] = '\0';
    out->mag_variation = parse_double(f[10]);
    out->mag_dir = (f[11] && *f[11]) ? f[11][0] : '\0';
    out->pos_mode = (n >= 13 && f[12] && *f[12]) ? f[12][0] : '\0';
    return NMEA_OK;
}

static nmea_err_t parse_gsv(char *f[], int n, nmea_gsv_t *out)
{
    if (n < 4) return NMEA_ERR_FIELDS;
    out->total_msg = parse_int(f[1]);
    out->msg_num = parse_int(f[2]);
    out->visible_satellites = parse_int(f[3]);
    out->num_satellites = 0;
    out->signal_id[0] = '\0';

    /* Satellite groups start at field 4, 4 fields each */
    int i = 4;
    while (i + 3 < n && out->num_satellites < NMEA_MAX_SATELLITES)
    {
        /* If 4 fields remain after this group they might be another group;
           if only 1 field remains it is the optional signal_id */
        if (!(f[i] && *f[i])) break; /* empty PRN – stop */
        nmea_satellite_t *s = &out->satellites[out->num_satellites++];
        s->prn = (uint8_t)parse_int(f[i]);
        s->elevation = (int8_t)parse_int(f[i + 1]);
        s->azimuth = (uint16_t)parse_int(f[i + 2]);
        s->snr = (uint8_t)parse_int(f[i + 3]);
        i += 4;
    }

    /* Remaining fields after all satellite groups: optional signal_id */
    int data_fields = n - 1;           /* exclude header field */
    int sat_slots = (data_fields - 3); /* fields after total/num/vis */
    if (sat_slots % 4 != 0 && i < n)
    {
        strncpy(out->signal_id, f[i], 3);
        out->signal_id[3] = '\0';
    }

    return NMEA_OK;
}

static nmea_err_t parse_gsa(char *f[], int n, nmea_gsa_t *out)
{
    if (n < 6) return NMEA_ERR_FIELDS;
    out->op_mode = f[1][0];
    out->nav_mode = (nmea_nav_mode_t)parse_int(f[2]);

    /* NMEA 4.10 adds a system ID field: standard GSA has 18 fields (header +
       op + nav + 12 PRN slots + pdop + hdop + vdop), 19 means sysid present. */
    bool has_sysid = (n > 18);
    int offset = has_sysid ? 1 : 0;

    out->vdop = parse_double(f[n - 1 - offset]);
    out->hdop = parse_double(f[n - 2 - offset]);
    out->pdop = parse_double(f[n - 3 - offset]);
    out->system_id = has_sysid ? (nmea_talker_t)(parse_int(f[n - 1]) + 1)
                               : NMEA_TALKER_UNKNOWN;

    out->num_prns = 0;
    for (int i = 3; i <= n - 4 - offset; i++)
    {
        if (f[i] && *f[i] && out->num_prns < NMEA_MAX_PRNS)
            out->prns[out->num_prns++] = (uint8_t)parse_int(f[i]);
    }

    return NMEA_OK;
}

/* Public: checksum ─────────────────────────────────────────────────────── */

uint8_t nmea_checksum(const uint8_t *data, int len)
{
    uint8_t cs = 0;
    for (int i = 0; i < len; i++)
        cs ^= data[i];
    return cs;
}

/* Public: validate ─────────────────────────────────────────────────────── */

nmea_err_t nmea_validate(const uint8_t *data, int len)
{
    if (len < 7) return NMEA_ERR_INVALID;
    if (data[0] != '$') return NMEA_ERR_INVALID;
    if (data[len - 2] != '\r') return NMEA_ERR_INVALID;
    if (data[len - 1] != '\n') return NMEA_ERR_INVALID;
    if (data[len - 5] != '*') return NMEA_ERR_INVALID;

    char hex[3] = {(char)data[len - 4], (char)data[len - 3], '\0'};
    uint8_t expected = (uint8_t)strtol(hex, NULL, 16);
    uint8_t actual = nmea_checksum(data + 1, len - 6);

    return (actual == expected) ? NMEA_OK : NMEA_ERR_INVALID;
}

/* Public: parse ────────────────────────────────────────────────────────── */

nmea_err_t nmea_parse(const uint8_t *data, int len, nmea_sentence_t *out,
                      bool validate)
{
    if (validate)
    {
        nmea_err_t err = nmea_validate(data, len);
        if (err != NMEA_OK) return err;
    }
    if (len < 7) return NMEA_ERR_INVALID;

    /* Copy payload (between '$' and '*XX\r\n') into a scratch buffer */
    int payload_len = len - 6; /* skip '$' (1) and '*XX\r\n' (5) */
    if (payload_len <= 0 || payload_len > NMEA_MAX_LEN) return NMEA_ERR_INVALID;

    char buf[NMEA_MAX_LEN + 1];
    memcpy(buf, data + 1, (size_t)payload_len);
    buf[payload_len] = '\0';

    char *fields[MAX_FIELDS];
    int nfields = split_fields(buf, fields, MAX_FIELDS);
    if (nfields < 1) return NMEA_ERR_INVALID;

    out->talker = parse_talker(fields[0]);
    out->type = parse_msg_type(fields[0] + 2);

    switch (out->type)
    {
    case NMEA_MSG_GGA:
        return parse_gga(fields, nfields, &out->gga);
    case NMEA_MSG_GLL:
        return parse_gll(fields, nfields, &out->gll);
    case NMEA_MSG_GST:
        return parse_gst(fields, nfields, &out->gst);
    case NMEA_MSG_RMC:
        return parse_rmc(fields, nfields, &out->rmc);
    case NMEA_MSG_GSV:
        return parse_gsv(fields, nfields, &out->gsv);
    case NMEA_MSG_GSA:
        return parse_gsa(fields, nfields, &out->gsa);
    default:
        return NMEA_ERR_UNKNOWN;
    }
}

/* Helpers: value → string ──────────────────────────────────────────────── */

/* Format time as HHMMSS.mmm */
static void fmt_time(char *buf, int size, const nmea_time_t *t)
{
    snprintf(buf, (size_t)size, "%02d%02d%02d.%03d", t->hour, t->min, t->sec,
             t->ms);
}

/* Format latitude as DDmm.mmmmm + hemisphere char; empty strings when NAN. */
static void fmt_lat(char *val, int vsz, char *hemi, double v)
{
    if (isnan(v))
    {
        val[0] = '\0';
        hemi[0] = '\0';
        return;
    }
    double a = fabs(v);
    int deg = (int)a;
    snprintf(val, (size_t)vsz, "%02d%08.5f", deg, (a - deg) * 60.0);
    hemi[0] = v >= 0 ? 'N' : 'S';
    hemi[1] = '\0';
}

/* Format longitude as DDDmm.mmmmm + hemisphere char; empty strings when NAN. */
static void fmt_lon(char *val, int vsz, char *hemi, double v)
{
    if (isnan(v))
    {
        val[0] = '\0';
        hemi[0] = '\0';
        return;
    }
    double a = fabs(v);
    int deg = (int)a;
    snprintf(val, (size_t)vsz, "%03d%08.5f", deg, (a - deg) * 60.0);
    hemi[0] = v >= 0 ? 'E' : 'W';
    hemi[1] = '\0';
}

/* Format double like Python's str(float): shortest, always has decimal point */
static void fmt_double(char *buf, int size, double v)
{
    snprintf(buf, (size_t)size, "%g", v);
    if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E') &&
        buf[0] != 'n' && buf[0] != 'N')
    {
        int n = (int)strlen(buf);
        if (n + 2 < size)
        {
            buf[n] = '.';
            buf[n + 1] = '0';
            buf[n + 2] = '\0';
        }
    }
}

/* Format optional double: empty string if NAN */
static void fmt_double_opt(char *buf, int size, double v)
{
    if (isnan(v))
    {
        buf[0] = '\0';
        return;
    }
    fmt_double(buf, size, v);
}

/* Per-message serializers → payload string (no $ or checksum) ──────────── */

static int ser_gga(const nmea_sentence_t *s, char *buf, int size)
{
    const nmea_gga_t *g = &s->gga;
    char tm[16], la[16] = {0}, ns[2] = {0}, lo[16] = {0}, ew[2] = {0};
    char hd[16], al[16], ge[16], ag[16], st[8];

    fmt_time(tm, sizeof tm, &g->time);
    fmt_lat(la, sizeof la, ns, g->lat);
    fmt_lon(lo, sizeof lo, ew, g->lon);
    fmt_double_opt(hd, sizeof hd, g->hdop);
    fmt_double_opt(al, sizeof al, g->alt);
    fmt_double_opt(ge, sizeof ge, g->geoid_height);
    fmt_double_opt(ag, sizeof ag, g->age);

    if (g->station > 0)
        snprintf(st, sizeof st, "%d", g->station);
    else
        st[0] = '\0';

    return snprintf(buf, (size_t)size,
                    "%sGGA,%s,%s,%s,%s,%s,%d,%d,%s,%s,M,%s,M,%s,%s",
                    TALKER_STR[s->talker], tm, la, ns, lo, ew, (int)g->fix,
                    g->num_satellites, hd, al, ge, ag, st);
}

static int ser_gll(const nmea_sentence_t *s, char *buf, int size)
{
    const nmea_gll_t *g = &s->gll;
    char tm[16], la[16] = {0}, ns[2] = {0}, lo[16] = {0}, ew[2] = {0};

    fmt_time(tm, sizeof tm, &g->time);
    fmt_lat(la, sizeof la, ns, g->lat);
    fmt_lon(lo, sizeof lo, ew, g->lon);

    int n = snprintf(buf, (size_t)size, "%sGLL,%s,%s,%s,%s,%s,%c",
                     TALKER_STR[s->talker], la, ns, lo, ew, tm, g->status);
    if (g->mode && n < size)
        n += snprintf(buf + n, (size_t)(size - n), ",%c", g->mode);
    return n;
}

static int ser_gst(const nmea_sentence_t *s, char *buf, int size)
{
    const nmea_gst_t *g = &s->gst;
    char tm[16], rm[16], sma[16], smi[16], an[16], sl[16], so[16], sa[16];

    fmt_time(tm, sizeof tm, &g->time);
    fmt_double(rm, sizeof rm, g->rms_range);
    fmt_double(sma, sizeof sma, g->std_major);
    fmt_double(smi, sizeof smi, g->std_minor);
    fmt_double(an, sizeof an, g->angle_major);
    fmt_double(sl, sizeof sl, g->std_lat);
    fmt_double(so, sizeof so, g->std_lon);
    fmt_double(sa, sizeof sa, g->std_alt);

    return snprintf(buf, (size_t)size, "%sGST,%s,%s,%s,%s,%s,%s,%s,%s",
                    TALKER_STR[s->talker], tm, rm, sma, smi, an, sl, so, sa);
}

static int ser_rmc(const nmea_sentence_t *s, char *buf, int size)
{
    const nmea_rmc_t *g = &s->rmc;
    char tm[16], la[16] = {0}, ns[2] = {0}, lo[16] = {0}, ew[2] = {0};
    char sp[16], co[16], mv[16];

    fmt_time(tm, sizeof tm, &g->time);
    fmt_lat(la, sizeof la, ns, g->lat);
    fmt_lon(lo, sizeof lo, ew, g->lon);
    fmt_double_opt(sp, sizeof sp, g->speed);
    fmt_double_opt(co, sizeof co, g->course);
    fmt_double_opt(mv, sizeof mv, g->mag_variation);
    char mag_dir[2] = {g->mag_dir, '\0'};
    char pos_mode[2] = {g->pos_mode, '\0'};

    return snprintf(buf, (size_t)size,
                    "%sRMC,%s,%c,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s",
                    TALKER_STR[s->talker], tm, g->status, la, ns, lo, ew, sp,
                    co, g->date, mv, mag_dir, pos_mode);
}

static int ser_gsv(const nmea_sentence_t *s, char *buf, int size)
{
    const nmea_gsv_t *g = &s->gsv;
    int n = snprintf(buf, (size_t)size, "%sGSV,%d,%d,%d", TALKER_STR[s->talker],
                     g->total_msg, g->msg_num, g->visible_satellites);

    for (int i = 0; i < g->num_satellites && n < size; i++)
    {
        const nmea_satellite_t *sat = &g->satellites[i];
        n += snprintf(buf + n, (size_t)(size - n), ",%02d,%02d,%03d,%02d",
                      sat->prn, sat->elevation, sat->azimuth, sat->snr);
    }

    if (g->signal_id[0] && n < size)
        n += snprintf(buf + n, (size_t)(size - n), ",%s", g->signal_id);

    return n;
}

static int ser_gsa(const nmea_sentence_t *s, char *buf, int size)
{
    const nmea_gsa_t *g = &s->gsa;
    char pd[16], hd[16], vd[16];
    fmt_double(pd, sizeof pd, g->pdop);
    fmt_double(hd, sizeof hd, g->hdop);
    fmt_double(vd, sizeof vd, g->vdop);

    int n = snprintf(buf, (size_t)size, "%sGSA,%c,%d", TALKER_STR[s->talker],
                     g->op_mode, (int)g->nav_mode);

    /* Always emit 12 PRN slots */
    for (int i = 0; i < 12 && n < size; i++)
    {
        if (i < g->num_prns)
            n += snprintf(buf + n, (size_t)(size - n), ",%d", g->prns[i]);
        else
            n += snprintf(buf + n, (size_t)(size - n), ",");
    }

    n += snprintf(buf + n, (size_t)(size - n), ",%s,%s,%s", pd, hd, vd);

    if (g->system_id != NMEA_TALKER_UNKNOWN && n < size)
        n +=
            snprintf(buf + n, (size_t)(size - n), ",%d", (int)g->system_id - 1);

    return n;
}

/* Public: serialize ────────────────────────────────────────────────────── */

int nmea_serialize(const nmea_sentence_t *s, uint8_t *buf, int buf_len)
{
    if (s->talker >= NMEA_TALKER_UNKNOWN) return -1;

    char payload[NMEA_MAX_LEN + 1];
    int n;

    switch (s->type)
    {
    case NMEA_MSG_GGA:
        n = ser_gga(s, payload, sizeof payload);
        break;
    case NMEA_MSG_GLL:
        n = ser_gll(s, payload, sizeof payload);
        break;
    case NMEA_MSG_GST:
        n = ser_gst(s, payload, sizeof payload);
        break;
    case NMEA_MSG_RMC:
        n = ser_rmc(s, payload, sizeof payload);
        break;
    case NMEA_MSG_GSV:
        n = ser_gsv(s, payload, sizeof payload);
        break;
    case NMEA_MSG_GSA:
        n = ser_gsa(s, payload, sizeof payload);
        break;
    default:
        return -1;
    }

    if (n < 0 || n >= (int)sizeof payload) return -1;

    uint8_t cs = nmea_checksum((const uint8_t *)payload, n);
    int total =
        snprintf((char *)buf, (size_t)buf_len, "$%s*%02X\r\n", payload, cs);

    return total;
}

/* Stream framer ────────────────────────────────────────────────────────── */

void nmea_framer_init(nmea_framer_t *f, nmea_frame_cb_t cb, void *ctx)
{
    f->on_frame = cb;
    f->ctx = ctx;
    nmea_framer_reset(f);
}

void nmea_framer_reset(nmea_framer_t *f)
{
    f->state = FRAMER_IDLE;
    f->len = 0;
}

const uint8_t *nmea_framer_process(nmea_framer_t *f, uint8_t byte, int *out_len)
{
    if (byte == '$')
    {
        f->len = 0;
        f->state = FRAMER_TRACKING;
    }

    if (f->state != FRAMER_TRACKING) return NULL;

    if (f->len >= NMEA_MAX_LEN)
    {
        nmea_framer_reset(f);
        return NULL;
    }

    f->buf[f->len++] = byte;

    if (f->len > 6 && f->buf[f->len - 2] == '\r' &&
        f->buf[f->len - 1] == '\n' && f->buf[f->len - 5] == '*')
    {

        int frame_len = f->len;
        if (out_len) *out_len = frame_len;
        /* Reset state but leave buffer contents intact for the caller */
        f->state = FRAMER_IDLE;
        f->len = 0;
        if (f->on_frame) f->on_frame(f->buf, frame_len, f->ctx);
        return f->buf;
    }

    return NULL;
}

void nmea_framer_feed(nmea_framer_t *f, const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++)
        nmea_framer_process(f, data[i], NULL);
}
