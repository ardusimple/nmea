#include "nmea.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Minimal test framework ───────────────────────────────────────────────── */
static int g_run = 0, g_fail = 0;
static const char *g_current_test = "";

#define TEST(name) static void test_##name(void)

#define RUN(name)                                                              \
    do                                                                         \
    {                                                                          \
        g_current_test = #name;                                                \
        printf("  %s\n", #name);                                               \
        test_##name();                                                         \
    } while (0)

#define ASSERT(cond)                                                           \
    do                                                                         \
    {                                                                          \
        g_run++;                                                               \
        if (!(cond))                                                           \
        {                                                                      \
            fprintf(stderr, "    FAIL (%s:%d): %s\n", __FILE__, __LINE__,      \
                    #cond);                                                    \
            g_fail++;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_STR(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_APPROX(a, b) ASSERT(fabs((double)(a) - (double)(b)) < 1e-5)

/* Fixtures  ────────────────────────────────────────────────────────────── */
static const uint8_t GLL[] =
    "$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*42\r\n";
static const uint8_t GGA[] = "$GPGGA,092750.000,5321.68020,N,00630.33720,W,1,8,"
                             "1.03,61.7,M,55.2,M,,*76\r\n";
static const uint8_t GST[] =
    "$GPGST,092750.000,0.5,0.3,0.2,45.0,0.3,0.2,0.1*5B\r\n";
static const uint8_t RMC[] = "$GPRMC,092750.000,A,5321.68020,N,00630.33720,W,0."
                             "02,31.66,280511,,,A*43\r\n";
static const uint8_t GSV[] =
    "$GPGSV,3,1,11,10,63,137,17,07,61,098,15,05,59,290,20,08,54,157,30*70\r\n";
static const uint8_t GSA[] =
    "$GPGSA,A,3,10,7,5,2,29,4,8,13,20,,,,1.72,1.03,1.38*38\r\n";
static const uint8_t GSA410[] =
    "$GPGSA,A,3,1,2,3,4,5,6,7,8,9,10,11,12,1.2,1.0,0.8,1*16\r\n";

static int slen(const uint8_t *s) { return (int)strlen((const char *)s); }

/* Tests ────────────────────────────────────────────────────────────────── */

TEST(checksum)
{
    ASSERT_EQ(nmea_checksum((const uint8_t *)"GPGGA", 5), 0x56);
    ASSERT_EQ(nmea_checksum((const uint8_t *)"", 0), 0);
}

TEST(validate_ok)
{
    ASSERT_EQ(nmea_validate(GLL, slen(GLL)), NMEA_OK);
    ASSERT_EQ(nmea_validate(GGA, slen(GGA)), NMEA_OK);
}

TEST(validate_missing_dollar)
{
    ASSERT_EQ(nmea_validate(GLL + 1, slen(GLL) - 1), NMEA_ERR_INVALID);
}

TEST(validate_missing_crlf)
{
    /* Remove last 2 bytes */
    uint8_t buf[128];
    int n = slen(GLL) - 2;
    memcpy(buf, GLL, (size_t)n);
    ASSERT_EQ(nmea_validate(buf, n), NMEA_ERR_INVALID);
}

TEST(validate_lf_only)
{
    uint8_t buf[128];
    int n = slen(GLL);
    memcpy(buf, GLL, (size_t)n);
    buf[n - 2] = '\n';
    buf[n - 1] = '\0';
    ASSERT_EQ(nmea_validate(buf, n - 1), NMEA_ERR_INVALID);
}

TEST(validate_missing_star)
{
    /* No checksum at all */
    static const uint8_t bad[] =
        "$GPGLL,4916.45,N,12311.12,W,225444.00,A,A\r\n";
    ASSERT_EQ(nmea_validate(bad, slen(bad)), NMEA_ERR_INVALID);
}

TEST(validate_wrong_checksum)
{
    static const uint8_t bad[] =
        "$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*FF\r\n";
    ASSERT_EQ(nmea_validate(bad, slen(bad)), NMEA_ERR_INVALID);
}

TEST(is_valid)
{
    /* Valid */
    ASSERT_EQ(nmea_validate(GLL, slen(GLL)), NMEA_OK);
    /* Garbage */
    static const uint8_t garbage[] = "garbage\r\n";
    ASSERT(nmea_validate(garbage, slen(garbage)) != NMEA_OK);
    /* Wrong checksum */
    static const uint8_t bad[] =
        "$GPGLL,4916.45,N,12311.12,W,225444.00,A,A*00\r\n";
    ASSERT(nmea_validate(bad, slen(bad)) != NMEA_OK);
}

TEST(parse_gga)
{
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(GGA, slen(GGA), &s, true), NMEA_OK);
    ASSERT_EQ(s.type, NMEA_MSG_GGA);
    ASSERT_EQ(s.talker, NMEA_TALKER_GPS);
    ASSERT_APPROX(s.gga.lat, 53.361337);
    ASSERT_APPROX(s.gga.lon, -6.505620);
    ASSERT_EQ(s.gga.fix, NMEA_FIX_GPS);
    ASSERT_EQ(s.gga.num_satellites, 8);
    ASSERT_APPROX(s.gga.hdop, 1.03);
    ASSERT_APPROX(s.gga.alt, 61.7);
    ASSERT_APPROX(s.gga.geoid_height, 55.2);
    ASSERT(isnan(s.gga.age));
    ASSERT_EQ(s.gga.station, 0);
}

TEST(parse_gll)
{
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(GLL, slen(GLL), &s, true), NMEA_OK);
    ASSERT_EQ(s.type, NMEA_MSG_GLL);
    ASSERT_EQ(s.talker, NMEA_TALKER_GPS);
    ASSERT_APPROX(s.gll.lat, 49.274167);
    ASSERT_APPROX(s.gll.lon, -123.185333);
    ASSERT_EQ(s.gll.status, 'A');
    ASSERT_EQ(s.gll.mode, 'A');
    ASSERT_EQ(s.gll.time.hour, 22);
    ASSERT_EQ(s.gll.time.min, 54);
    ASSERT_EQ(s.gll.time.sec, 44);
    ASSERT_EQ(s.gll.time.ms, 0);
}

TEST(parse_gst)
{
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(GST, slen(GST), &s, true), NMEA_OK);
    ASSERT_EQ(s.type, NMEA_MSG_GST);
    ASSERT_APPROX(s.gst.rms_range, 0.5);
    ASSERT_APPROX(s.gst.std_major, 0.3);
    ASSERT_APPROX(s.gst.std_minor, 0.2);
    ASSERT_APPROX(s.gst.angle_major, 45.0);
    ASSERT_APPROX(s.gst.std_lat, 0.3);
    ASSERT_APPROX(s.gst.std_lon, 0.2);
    ASSERT_APPROX(s.gst.std_alt, 0.1);
}

TEST(parse_rmc)
{
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(RMC, slen(RMC), &s, true), NMEA_OK);
    ASSERT_EQ(s.type, NMEA_MSG_RMC);
    ASSERT_EQ(s.talker, NMEA_TALKER_GPS);
    ASSERT_EQ(s.rmc.status, 'A');
    ASSERT_APPROX(s.rmc.lat, 53.361337);
    ASSERT_APPROX(s.rmc.lon, -6.505620);
    ASSERT_APPROX(s.rmc.speed, 0.02);
    ASSERT_APPROX(s.rmc.course, 31.66);
    ASSERT_STR(s.rmc.date, "280511");
    ASSERT(isnan(s.rmc.mag_variation));
    ASSERT_EQ(s.rmc.mag_dir, '\0');
    ASSERT_EQ(s.rmc.pos_mode, 'A');
}

TEST(parse_gsv)
{
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(GSV, slen(GSV), &s, true), NMEA_OK);
    ASSERT_EQ(s.type, NMEA_MSG_GSV);
    ASSERT_EQ(s.gsv.total_msg, 3);
    ASSERT_EQ(s.gsv.msg_num, 1);
    ASSERT_EQ(s.gsv.visible_satellites, 11);
    ASSERT_EQ(s.gsv.num_satellites, 4);
    ASSERT_EQ(s.gsv.satellites[0].prn, 10);
    ASSERT_EQ(s.gsv.satellites[0].azimuth, 137);
    ASSERT_EQ(s.gsv.satellites[3].prn, 8);
    ASSERT_EQ(s.gsv.satellites[3].snr, 30);
    ASSERT_EQ(s.gsv.signal_id[0], '\0'); /* no signal_id */
}

TEST(parse_gsv_with_signal_id)
{
    static const uint8_t gsv1[] = "$GPGSV,1,1,1,01,40,083,46,1*69\r\n";
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(gsv1, slen(gsv1), &s, true), NMEA_OK);
    ASSERT_STR(s.gsv.signal_id, "1");
}

TEST(parse_gsa)
{
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(GSA, slen(GSA), &s, true), NMEA_OK);
    ASSERT_EQ(s.type, NMEA_MSG_GSA);
    ASSERT_EQ(s.gsa.op_mode, 'A');
    ASSERT_EQ(s.gsa.nav_mode, NMEA_NAV_FIX_3D);
    ASSERT_EQ(s.gsa.num_prns, 9);
    ASSERT_EQ(s.gsa.prns[0], 10);
    ASSERT_EQ(s.gsa.prns[1], 7);
    ASSERT_EQ(s.gsa.prns[2], 5);
    ASSERT_APPROX(s.gsa.pdop, 1.72);
    ASSERT_APPROX(s.gsa.hdop, 1.03);
    ASSERT_APPROX(s.gsa.vdop, 1.38);
    ASSERT_EQ(s.gsa.system_id, NMEA_TALKER_UNKNOWN);
}

TEST(parse_gsa_410)
{
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(GSA410, slen(GSA410), &s, true), NMEA_OK);
    ASSERT_EQ(s.gsa.system_id, NMEA_TALKER_GPS);
    ASSERT_EQ(s.gsa.num_prns, 12);
    ASSERT_APPROX(s.gsa.pdop, 1.2);
    ASSERT_APPROX(s.gsa.hdop, 1.0);
    ASSERT_APPROX(s.gsa.vdop, 0.8);
}

TEST(parse_gsa_fewer_prns)
{
    static const uint8_t few[] = "$GPGSA,A,3,1,2,3,,,,,,,,,,1.2,1.0,0.8*08\r\n";
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(few, slen(few), &s, true), NMEA_OK);
    ASSERT_EQ(s.gsa.num_prns, 3);
    ASSERT_EQ(s.gsa.prns[0], 1);
    ASSERT_EQ(s.gsa.prns[2], 3);
}

TEST(parse_gga_no_fix)
{
    /* GPS with no fix: lat/lon/hdop/alt/geoid_height all absent */
    static const uint8_t no_fix[] =
        "$GPGGA,092750.000,,,,,0,0,,,M,,M,,*41\r\n";
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(no_fix, slen(no_fix), &s, true), NMEA_OK);
    ASSERT(isnan(s.gga.lat));
    ASSERT(isnan(s.gga.lon));
    ASSERT_EQ(s.gga.fix, NMEA_FIX_NOT_VALID);
    ASSERT_EQ(s.gga.num_satellites, 0);
    ASSERT(isnan(s.gga.hdop));
    ASSERT(isnan(s.gga.alt));
    ASSERT(isnan(s.gga.geoid_height));

    /* Must serialize back to the original sentence */
    uint8_t out[128] = {0};
    int n = nmea_serialize(&s, out, (int)sizeof out);
    ASSERT(n > 0);
    ASSERT_EQ(n, slen(no_fix));
    ASSERT(memcmp(out, no_fix, (size_t)n) == 0);
}

TEST(serialize_absent_fields_as_empty_commas)
{
    /* GGA: absent age and station must serialize as ",," not ",0," or ",nan," */
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(GGA, slen(GGA), &s, true), NMEA_OK);
    ASSERT(isnan(s.gga.age));
    ASSERT_EQ(s.gga.station, 0);

    uint8_t out[128] = {0};
    int n = nmea_serialize(&s, out, (int)sizeof out);
    ASSERT(n > 0);
    ASSERT(strstr((char *)out, ",,*") != NULL); /* two consecutive empty fields before '*' */

    /* RMC: absent mag_variation and mag_dir must serialize as ",," */
    ASSERT_EQ(nmea_parse(RMC, slen(RMC), &s, true), NMEA_OK);
    ASSERT(isnan(s.rmc.mag_variation));
    ASSERT_EQ(s.rmc.mag_dir, '\0');

    memset(out, 0, sizeof out);
    n = nmea_serialize(&s, out, (int)sizeof out);
    ASSERT(n > 0);
    ASSERT(strstr((char *)out, ",,,") != NULL); /* mag_variation + mag_dir both empty */
}

TEST(serialize_gll_no_mode)
{
    /* Pre-NMEA 2.3 GLL has no mode field; serializer must not embed a null byte */
    static const uint8_t gll_no_mode[] =
        "$GPGLL,4916.45000,N,12311.12000,W,225444.000,A*2F\r\n";
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(gll_no_mode, slen(gll_no_mode), &s, true), NMEA_OK);
    ASSERT_EQ(s.gll.mode, '\0');

    uint8_t out[128] = {0};
    int n = nmea_serialize(&s, out, (int)sizeof out);
    ASSERT(n > 0);
    /* n must equal strlen: no embedded null byte */
    ASSERT_EQ(n, (int)strlen((char *)out));
    ASSERT_EQ(nmea_validate(out, n), NMEA_OK);
    ASSERT_EQ(n, slen(gll_no_mode));
    ASSERT(memcmp(out, gll_no_mode, (size_t)n) == 0);
}

TEST(parse_skip_validation)
{
    static const uint8_t bad[] =
        "$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*FF\r\n";
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(bad, slen(bad), &s, false), NMEA_OK);
    ASSERT_EQ(s.type, NMEA_MSG_GLL);
}

TEST(parse_unknown_type)
{
    /* XYZ is not a supported sentence type */
    static const uint8_t unk[] = "$GPXYZ,foo*52\r\n";
    nmea_sentence_t s;
    /* Checksum doesn't match, but we skip validation */
    nmea_err_t err = nmea_parse(unk, slen(unk), &s, false);
    ASSERT_EQ(err, NMEA_ERR_UNKNOWN);
}

/* Serialization round-trips ────────────────────────────────────────────── */

static void check_roundtrip(const uint8_t *sentence)
{
    int orig_len = slen(sentence);
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(sentence, orig_len, &s, true), NMEA_OK);

    uint8_t out[128] = {0};
    int n = nmea_serialize(&s, out, (int)sizeof out);
    ASSERT(n > 0);
    ASSERT_EQ(n, orig_len);
    ASSERT(memcmp(out, sentence, (size_t)orig_len) == 0);
    if (memcmp(out, sentence, (size_t)orig_len) != 0)
    {
        fprintf(stderr, "    expected: %s", sentence);
        fprintf(stderr, "    got:      %s", out);
    }
}

TEST(serialize_roundtrip)
{
    check_roundtrip(GLL);
    check_roundtrip(GGA);
    check_roundtrip(GST);
    check_roundtrip(RMC);
    check_roundtrip(GSV);
    check_roundtrip(GSA);
    check_roundtrip(GSA410);
}

/* Framer ───────────────────────────────────────────────────────────────── */

static int g_frame_count = 0;
static uint8_t g_last_frame[128];
static int g_last_len = 0;

static void frame_cb(const uint8_t *data, int len, void *ctx)
{
    (void)ctx;
    g_frame_count++;
    if (len < (int)sizeof g_last_frame)
    {
        memcpy(g_last_frame, data, (size_t)len);
        g_last_len = len;
    }
}

static void reset_cb_state(void)
{
    g_frame_count = 0;
    g_last_len = 0;
    memset(g_last_frame, 0, sizeof g_last_frame);
}

TEST(framer_process)
{
    reset_cb_state();
    nmea_framer_t f;
    nmea_framer_init(&f, frame_cb, NULL);

    /* Random-ish prefix, then GLL sentence, then random suffix */
    static const uint8_t junk[] = {0x12, 0xAB, 0x00, 0xFF, 0x7E, 0x7F,
                                   0x80, 0x1F, 0x21, 0x23, 0x25, 0x5E};
    nmea_framer_feed(&f, junk, (int)sizeof junk);
    nmea_framer_feed(&f, GLL, slen(GLL));
    nmea_framer_feed(&f, junk, (int)sizeof junk);

    ASSERT_EQ(g_frame_count, 1);
    ASSERT_EQ(g_last_len, slen(GLL));
    ASSERT(memcmp(g_last_frame, GLL, (size_t)slen(GLL)) == 0);

    /* Verify the framed bytes parse correctly */
    nmea_sentence_t s;
    ASSERT_EQ(nmea_parse(g_last_frame, g_last_len, &s, true), NMEA_OK);
    ASSERT_EQ(s.type, NMEA_MSG_GLL);
    ASSERT_APPROX(s.gll.lat, 49.274167);
    ASSERT_APPROX(s.gll.lon, -123.185333);
}

TEST(framer_interrupted_sentence)
{
    reset_cb_state();
    nmea_framer_t f;
    nmea_framer_init(&f, frame_cb, NULL);

    /* Partial GGA (only header), then random bytes, then complete GLL */
    static const uint8_t junk[] = {0xAA, 0xBB, 0xCC};
    nmea_framer_feed(&f, junk, (int)sizeof junk);
    nmea_framer_feed(&f, GGA, 6); /* truncated */
    nmea_framer_feed(&f, junk, (int)sizeof junk);
    nmea_framer_feed(&f, GLL, slen(GLL));
    nmea_framer_feed(&f, junk, (int)sizeof junk);

    ASSERT_EQ(g_frame_count, 1);
    ASSERT(memcmp(g_last_frame + 3, "GLL", 3) == 0);
}

static int g_multi_cnt;
static uint8_t g_multi_frames[4][128];
static int g_multi_lengths[4];

static void multi_cb(const uint8_t *d, int l, void *ctx)
{
    (void)ctx;
    if (g_multi_cnt < 4)
    {
        memcpy(g_multi_frames[g_multi_cnt], d, (size_t)l);
        g_multi_lengths[g_multi_cnt] = l;
    }
    g_multi_cnt++;
}

TEST(framer_multiple_sentences)
{
    g_multi_cnt = 0;
    memset(g_multi_frames, 0, sizeof g_multi_frames);

    nmea_framer_t f;
    nmea_framer_init(&f, multi_cb, NULL);

    nmea_framer_feed(&f, GLL, slen(GLL));
    nmea_framer_feed(&f, GGA, slen(GGA));

    ASSERT_EQ(g_multi_cnt, 2);
    ASSERT(memcmp(g_multi_frames[0] + 3, "GLL", 3) == 0);
    ASSERT(memcmp(g_multi_frames[1] + 3, "GGA", 3) == 0);
}

TEST(framer_max_length)
{
    reset_cb_state();
    nmea_framer_t f;
    nmea_framer_init(&f, frame_cb, NULL);

    /* '$' followed by NMEA_MAX_LEN+1 bytes (exceeds limit) → reset */
    uint8_t overflow[NMEA_MAX_LEN + 3];
    overflow[0] = '$';
    memset(overflow + 1, 'x', NMEA_MAX_LEN + 1);
    overflow[NMEA_MAX_LEN + 2] = '\0';
    nmea_framer_feed(&f, overflow, NMEA_MAX_LEN + 2);

    /* After overflow, a valid GLL should still be detected */
    nmea_framer_feed(&f, GLL, slen(GLL));
    ASSERT_EQ(g_frame_count, 1);
    ASSERT(memcmp(g_last_frame + 3, "GLL", 3) == 0);
}

TEST(framer_bad_checksum_passes_through)
{
    reset_cb_state();
    nmea_framer_t f;
    nmea_framer_init(&f, frame_cb, NULL);

    static const uint8_t bad[] =
        "$GPGLL,4916.45000,N,12311.12000,W,225444.000,A,A*FF\r\n";
    nmea_framer_feed(&f, bad, slen(bad));

    /* Framer passes through regardless of checksum */
    ASSERT_EQ(g_frame_count, 1);
    ASSERT(memcmp(g_last_frame, bad, (size_t)slen(bad)) == 0);
}

TEST(framer_return_value)
{
    nmea_framer_t f;
    nmea_framer_init(&f, NULL, NULL);

    const uint8_t *frame = NULL;
    int len = 0;
    for (int i = 0; i < slen(GLL); i++)
    {
        frame = nmea_framer_process(&f, GLL[i], &len);
    }
    ASSERT(frame != NULL);
    ASSERT_EQ(len, slen(GLL));
    ASSERT(memcmp(frame, GLL, (size_t)slen(GLL)) == 0);
}

/* Main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("nmea C tests\n");
    printf("------------\n");

    RUN(checksum);
    RUN(validate_ok);
    RUN(validate_missing_dollar);
    RUN(validate_missing_crlf);
    RUN(validate_lf_only);
    RUN(validate_missing_star);
    RUN(validate_wrong_checksum);
    RUN(is_valid);
    RUN(parse_gga);
    RUN(parse_gll);
    RUN(parse_gst);
    RUN(parse_rmc);
    RUN(parse_gsv);
    RUN(parse_gsv_with_signal_id);
    RUN(parse_gsa);
    RUN(parse_gsa_410);
    RUN(parse_gsa_fewer_prns);
    RUN(parse_gga_no_fix);
    RUN(serialize_absent_fields_as_empty_commas);
    RUN(serialize_gll_no_mode);
    RUN(parse_skip_validation);
    RUN(parse_unknown_type);
    RUN(serialize_roundtrip);
    RUN(framer_process);
    RUN(framer_interrupted_sentence);
    RUN(framer_multiple_sentences);
    RUN(framer_max_length);
    RUN(framer_bad_checksum_passes_through);
    RUN(framer_return_value);

    printf("------------\n");
    printf("%d tests run, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
