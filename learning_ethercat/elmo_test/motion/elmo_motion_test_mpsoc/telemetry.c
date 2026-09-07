#include "telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_err[256];

const char *telemetry_last_error(void) { return g_err; }

static int fail(const char *msg)
{
    snprintf(g_err, sizeof(g_err), "%s", msg);
    return -1;
}

int telemetry_init(telemetry_t *t, const ecat_config_t *cfg, int capacity)
{
    if (!t || !cfg || capacity <= 0)
        return fail("telemetry_init: bad arguments");

    memset(t, 0, sizeof(*t));
    t->cfg = cfg;
    t->capacity = capacity;

    t->cycle      = calloc((size_t)capacity, sizeof(*t->cycle));
    t->t_ns       = calloc((size_t)capacity, sizeof(*t->t_ns));
    t->wkc        = calloc((size_t)capacity, sizeof(*t->wkc));
    t->latency_ns = calloc((size_t)capacity, sizeof(*t->latency_ns));
    t->exec_ns    = calloc((size_t)capacity, sizeof(*t->exec_ns));
    t->dc_time    = calloc((size_t)capacity, sizeof(*t->dc_time));

    if (!t->cycle || !t->t_ns || !t->wkc || !t->latency_ns ||
        !t->exec_ns || !t->dc_time)
    {
        telemetry_free(t);
        return fail("telemetry_init: out of memory (timing arrays)");
    }

    /* Touch every page now so the RT loop never faults (mlockall keeps them). */
    memset(t->cycle, 0, (size_t)capacity * sizeof(*t->cycle));
    memset(t->t_ns, 0, (size_t)capacity * sizeof(*t->t_ns));
    memset(t->wkc, 0, (size_t)capacity * sizeof(*t->wkc));
    memset(t->latency_ns, 0, (size_t)capacity * sizeof(*t->latency_ns));
    memset(t->exec_ns, 0, (size_t)capacity * sizeof(*t->exec_ns));
    memset(t->dc_time, 0, (size_t)capacity * sizeof(*t->dc_time));
    return 0;
}

int telemetry_add_slave(telemetry_t *t, int position,
                        const void *out_img, int out_bytes,
                        const void *in_img, int in_bytes)
{
    telemetry_slave_t *s;
    const ecat_slave_config_t *sc;

    if (!t)
        return fail("telemetry_add_slave: null telemetry");
    if (t->slave_count >= ECAT_CFG_MAX_SLAVES)
        return fail("telemetry_add_slave: too many slaves");
    if (out_bytes < 0 || in_bytes < 0)
        return fail("telemetry_add_slave: negative image size");

    sc = ecat_config_find_slave(t->cfg, position);
    if (!sc)
        return fail("telemetry_add_slave: no config for that position");

    s = &t->slaves[t->slave_count];
    memset(s, 0, sizeof(*s));
    s->position = position;
    s->out_img = (const uint8_t *)out_img;
    s->in_img = (const uint8_t *)in_img;
    s->out_bytes = out_bytes;
    s->in_bytes = in_bytes;
    s->sc = sc;

    if (out_bytes > 0)
    {
        s->out_buf = calloc((size_t)t->capacity, (size_t)out_bytes);
        if (!s->out_buf)
            return fail("telemetry_add_slave: out of memory (out_buf)");
        memset(s->out_buf, 0, (size_t)t->capacity * (size_t)out_bytes);
    }
    if (in_bytes > 0)
    {
        s->in_buf = calloc((size_t)t->capacity, (size_t)in_bytes);
        if (!s->in_buf)
            return fail("telemetry_add_slave: out of memory (in_buf)");
        memset(s->in_buf, 0, (size_t)t->capacity * (size_t)in_bytes);
    }

    /* Locate position demand (0x6062) and actual (0x6064) in the TxPDO so we
     * can emit a master-computed following error column. */
    {
        int bit = 0, i, seen = 0;
        for (i = 0; i < sc->txpdo_count; i++)
        {
            const ecat_pdo_entry_t *e = &sc->txpdo[i];
            if (e->index == 0x6062)
            {
                s->fe_demand_bit = bit;
                s->fe_demand_len = e->bitlen;
                seen |= 1;
            }
            else if (e->index == 0x6064)
            {
                s->fe_actual_bit = bit;
                s->fe_actual_len = e->bitlen;
                seen |= 2;
            }
            bit += e->bitlen;
        }
        s->fe_present = (seen == 3);
    }

    t->slave_count++;
    return 0;
}

void telemetry_sample(telemetry_t *t, int32_t cycle, int64_t t_ns, int32_t wkc,
                      int64_t latency_ns, int64_t exec_ns, int64_t dc_time)
{
    int r, k;

    if (!t || t->count >= t->capacity)
        return;

    r = t->count;
    t->cycle[r] = cycle;
    t->t_ns[r] = t_ns;
    t->wkc[r] = wkc;
    t->latency_ns[r] = latency_ns;
    t->exec_ns[r] = exec_ns;
    t->dc_time[r] = dc_time;

    for (k = 0; k < t->slave_count; k++)
    {
        telemetry_slave_t *s = &t->slaves[k];
        if (s->out_buf && s->out_img)
            memcpy(s->out_buf + (size_t)r * s->out_bytes, s->out_img,
                   (size_t)s->out_bytes);
        if (s->in_buf && s->in_img)
            memcpy(s->in_buf + (size_t)r * s->in_bytes, s->in_img,
                   (size_t)s->in_bytes);
    }

    t->count++;
}

/* Extract `bitlen` bits (little-endian) starting at `bit_off` and sign-extend
 * to 64 bits (CiA402 process values are signed two's complement). */
static int64_t extract_le_signed(const uint8_t *img, int img_bytes,
                                 int bit_off, int bitlen)
{
    uint64_t v = 0;
    int i;

    if (bitlen <= 0 || bitlen > 64)
        return 0;
    if ((bit_off + bitlen + 7) / 8 > img_bytes)
        return 0; /* would read past the image */

    for (i = 0; i < bitlen; i++)
    {
        int b = bit_off + i;
        if ((img[b >> 3] >> (b & 7)) & 1u)
            v |= (uint64_t)1 << i;
    }
    if (bitlen < 64 && (v & ((uint64_t)1 << (bitlen - 1))))
        v |= ~(((uint64_t)1 << bitlen) - 1);
    return (int64_t)v;
}

/* Build a CSV-safe column name: "<prefix><sanitized name>_<INDEX hex>". */
static void column_name(char *dst, size_t cap, const char *prefix,
                        const ecat_pdo_entry_t *e)
{
    size_t n = 0;
    const char *p;

    for (p = prefix; *p && n + 1 < cap; p++)
        dst[n++] = *p;

    for (p = e->name; *p && n + 1 < cap; p++)
    {
        char c = *p;
        dst[n++] = ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9'))
                       ? c
                       : '_';
    }
    if (n + 6 < cap)
        n += (size_t)snprintf(dst + n, cap - n, "_%04X", e->index);
    dst[n < cap ? n : cap - 1] = '\0';
}

static void write_header(FILE *fp, const telemetry_slave_t *s)
{
    const ecat_slave_config_t *sc = s->sc;
    char col[128];
    int i;

    fprintf(fp, "cycle,t_ns,wkc,latency_ns,exec_ns,dc_time");
    for (i = 0; i < sc->rxpdo_count; i++)
    {
        column_name(col, sizeof(col), "cmd_", &sc->rxpdo[i]);
        fprintf(fp, ",%s", col);
    }
    for (i = 0; i < sc->txpdo_count; i++)
    {
        column_name(col, sizeof(col), "fb_", &sc->txpdo[i]);
        fprintf(fp, ",%s", col);
    }
    if (s->fe_present)
        fprintf(fp, ",following_error_pos");
    fprintf(fp, "\n");
}

static void write_row(FILE *fp, const telemetry_t *t,
                      const telemetry_slave_t *s, int r)
{
    int i, bit;
    const uint8_t *out_row = s->out_buf + (size_t)r * s->out_bytes;
    const uint8_t *in_row = s->in_buf + (size_t)r * s->in_bytes;
    int64_t t_rel = t->t_ns[r] - t->t_ns[0];

    fprintf(fp, "%d,%lld,%d,%lld,%lld,%lld",
            (int)t->cycle[r], (long long)t_rel, (int)t->wkc[r],
            (long long)t->latency_ns[r], (long long)t->exec_ns[r],
            (long long)t->dc_time[r]);

    bit = 0;
    for (i = 0; i < s->sc->rxpdo_count; i++)
    {
        const ecat_pdo_entry_t *e = &s->sc->rxpdo[i];
        fprintf(fp, ",%lld",
                (long long)extract_le_signed(out_row, s->out_bytes, bit,
                                             e->bitlen));
        bit += e->bitlen;
    }
    bit = 0;
    for (i = 0; i < s->sc->txpdo_count; i++)
    {
        const ecat_pdo_entry_t *e = &s->sc->txpdo[i];
        fprintf(fp, ",%lld",
                (long long)extract_le_signed(in_row, s->in_bytes, bit,
                                             e->bitlen));
        bit += e->bitlen;
    }
    if (s->fe_present)
    {
        int64_t demand = extract_le_signed(in_row, s->in_bytes,
                                           s->fe_demand_bit, s->fe_demand_len);
        int64_t actual = extract_le_signed(in_row, s->in_bytes,
                                           s->fe_actual_bit, s->fe_actual_len);
        fprintf(fp, ",%lld", (long long)(demand - actual));
    }
    fprintf(fp, "\n");
}

int telemetry_write(const telemetry_t *t, const char *prefix)
{
    int k, r;

    if (!t || !prefix)
        return fail("telemetry_write: bad arguments");
    if (t->count == 0)
        return fail("telemetry_write: no samples captured");

    for (k = 0; k < t->slave_count; k++)
    {
        const telemetry_slave_t *s = &t->slaves[k];
        char path[256];
        FILE *fp;

        snprintf(path, sizeof(path), "%s_slave%d.csv", prefix, s->position);
        fp = fopen(path, "w");
        if (!fp)
        {
            snprintf(g_err, sizeof(g_err),
                     "telemetry_write: cannot open output file for slave %d",
                     s->position);
            return -1;
        }

        write_header(fp, s);
        for (r = 0; r < t->count; r++)
            write_row(fp, t, s, r);

        fclose(fp);
        printf("telemetry: wrote %d rows to %s\n", t->count, path);
    }
    return 0;
}

void telemetry_free(telemetry_t *t)
{
    int k;
    if (!t)
        return;

    free(t->cycle);
    free(t->t_ns);
    free(t->wkc);
    free(t->latency_ns);
    free(t->exec_ns);
    free(t->dc_time);
    t->cycle = NULL;
    t->t_ns = NULL;
    t->wkc = NULL;
    t->latency_ns = NULL;
    t->exec_ns = NULL;
    t->dc_time = NULL;

    for (k = 0; k < t->slave_count; k++)
    {
        free(t->slaves[k].out_buf);
        free(t->slaves[k].in_buf);
        t->slaves[k].out_buf = NULL;
        t->slaves[k].in_buf = NULL;
    }
    t->slave_count = 0;
    t->count = 0;
    t->capacity = 0;
}
