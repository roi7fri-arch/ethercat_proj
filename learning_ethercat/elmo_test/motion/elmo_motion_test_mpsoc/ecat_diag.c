#include <stdio.h>
#include <string.h>

#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"
#include "ethercatprint.h"
#include "ecat_diag.h"

/* One contiguous read of the ESC error-counter block 0x0300..0x0313 (20 bytes):
 *   0x0300 RX error (per port, 2 B: lo=invalid frame, hi=RX/CRC error)
 *   0x0308 forwarded RX error (per port, 1 B)
 *   0x030C EPU processing error, 0x030D PDI error
 *   0x0310 lost-link counter (per port, 1 B)                                   */
#define DIAG_BLK_ADR   ECT_REG_RXERR        /* 0x0300 */
#define DIAG_BLK_LEN   0x14                 /* covers up to 0x0313             */

int ecat_diag_read(int position, ecat_diag_slave_t *out)
{
    uint8 blk[DIAG_BLK_LEN];
    int wkc, p;

    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    out->position = position;
    out->configadr = ec_slave[position].configadr;
    out->al_state = ec_slave[position].state;
    out->al_status_code = ec_slave[position].ALstatuscode;

    memset(blk, 0, sizeof(blk));
    wkc = ec_FPRD(out->configadr, DIAG_BLK_ADR, sizeof(blk), blk, EC_TIMEOUTRET);
    if (wkc <= 0)
    {
        out->read_ok = 0;
        return -1;
    }
    out->read_ok = 1;

    for (p = 0; p < ECAT_DIAG_MAX_PORTS; p++)
    {
        out->rx_invalid[p] = blk[(0x0300 - DIAG_BLK_ADR) + p * 2];
        out->rx_error[p]   = blk[(0x0301 - DIAG_BLK_ADR) + p * 2];
        out->fwd_error[p]  = blk[(0x0308 - DIAG_BLK_ADR) + p];
        out->lost_link[p]  = blk[(0x0310 - DIAG_BLK_ADR) + p];
    }
    out->proc_error = blk[0x030C - DIAG_BLK_ADR];
    out->pdi_error  = blk[0x030D - DIAG_BLK_ADR];
    return 0;
}

int ecat_diag_scan(ecat_diag_slave_t *out, int cap)
{
    int i, n = 0;

    if (!out || cap <= 0)
        return 0;
    /* Refresh AL state/code for all slaves before reading counters. */
    ec_readstate();
    for (i = 1; i <= ec_slavecount && n < cap; i++)
    {
        ecat_diag_read(i, &out[n]);
        n++;
    }
    return n;
}

static int diag_sum(const uint8_t *a)
{
    int p, s = 0;
    for (p = 0; p < ECAT_DIAG_MAX_PORTS; p++)
        s += a[p];
    return s;
}

int ecat_diag_has_errors(const ecat_diag_slave_t *d, int n)
{
    int i;

    if (!d)
        return 0;
    for (i = 0; i < n; i++)
    {
        if (d[i].al_status_code != 0)
            return 1;
        if ((d[i].al_state & EC_STATE_ERROR) != 0)
            return 1;
        if (diag_sum(d[i].rx_invalid) || diag_sum(d[i].rx_error) ||
            diag_sum(d[i].fwd_error) || diag_sum(d[i].lost_link) ||
            d[i].proc_error || d[i].pdi_error)
            return 1;
    }
    return 0;
}

void ecat_diag_print(const char *title, const ecat_diag_slave_t *d, int n)
{
    int i;

    if (title)
        printf("\n=== EtherCAT diagnostics: %s ===\n", title);
    printf("pos state code                      rxCRC lost fwd  epu pdi\n");
    for (i = 0; i < n; i++)
    {
        if (!d[i].read_ok)
        {
            printf("%3d  0x%2.2x  (register read failed)\n",
                   d[i].position, d[i].al_state);
            continue;
        }
        printf("%3d  0x%2.2x  %-24.24s  %4d %4d %4d  %3d %3d\n",
               d[i].position, d[i].al_state,
               ec_ALstatuscode2string(d[i].al_status_code),
               diag_sum(d[i].rx_error), diag_sum(d[i].lost_link),
               diag_sum(d[i].fwd_error), d[i].proc_error, d[i].pdi_error);
    }
}

int ecat_diag_drain_errors(void)
{
    int n = 0;

    while (ec_iserror())
    {
        printf("  [EtherCAT error] %s", ec_elist2string());
        n++;
    }
    return n;
}

int ecat_diag_verify_identity(const ecat_config_t *cfg)
{
    int i, mismatches = 0;

    if (!cfg)
        return 0;
    for (i = 0; i < cfg->slave_count; i++)
    {
        const ecat_slave_config_t *s = &cfg->slaves[i];
        int pos = s->position;
        uint32 man, id, rev;

        if (!s->expected_vendor_id && !s->expected_product_code && !s->expected_revision)
            continue;                               /* nothing to check for this slave */

        if (pos < 1 || pos > ec_slavecount)
        {
            printf("  [identity] slave %d '%s' NOT PRESENT on the bus (have %d slaves)\n",
                   pos, s->name, ec_slavecount);
            mismatches++;
            continue;
        }

        man = ec_slave[pos].eep_man;
        id  = ec_slave[pos].eep_id;
        rev = ec_slave[pos].eep_rev;

        if (s->expected_vendor_id && man != s->expected_vendor_id)
        {
            printf("  [identity] slave %d '%s' vendor 0x%08X != expected 0x%08X\n",
                   pos, s->name, man, s->expected_vendor_id);
            mismatches++;
        }
        if (s->expected_product_code && id != s->expected_product_code)
        {
            printf("  [identity] slave %d '%s' product 0x%08X != expected 0x%08X\n",
                   pos, s->name, id, s->expected_product_code);
            mismatches++;
        }
        if (s->expected_revision && rev != s->expected_revision)
        {
            printf("  [identity] slave %d '%s' revision 0x%08X != expected 0x%08X\n",
                   pos, s->name, rev, s->expected_revision);
            mismatches++;
        }
    }
    if (mismatches == 0)
        printf("  [identity] all configured slaves match expected vendor/product/revision\n");
    return mismatches;
}

/* ------------------------------------------------------------------------- */
/* Topology / link-state                                                     */
/* ------------------------------------------------------------------------- */
int ecat_diag_topology_scan(ecat_topo_t *out, int cap)
{
    int i, n = 0;

    if (!out || cap <= 0)
        return 0;
    for (i = 1; i <= ec_slavecount && n < cap; i++)
    {
        out[n].position    = i;
        out[n].configadr   = ec_slave[i].configadr;
        out[n].topology    = ec_slave[i].topology;
        out[n].activeports = ec_slave[i].activeports;
        out[n].parent      = ec_slave[i].parent;
        out[n].parentport  = ec_slave[i].parentport;
        out[n].has_dc      = ec_slave[i].hasdc ? 1 : 0;
        n++;
    }
    return n;
}

void ecat_diag_topology_print(const char *title, const ecat_topo_t *t, int n)
{
    int i, p;

    if (title)
        printf("\n=== EtherCAT topology: %s ===\n", title);
    printf("pos parent:port  links  ports[3210]  dc\n");
    for (i = 0; i < n; i++)
    {
        char ports[5];
        for (p = 0; p < 4; p++)
            ports[3 - p] = (t[i].activeports & (1 << p)) ? '1' : '0';
        ports[4] = '\0';
        printf("%3d   %4d:%-4d  %4d       %s     %s\n",
               t[i].position, t[i].parent, t[i].parentport,
               t[i].topology, ports, t[i].has_dc ? "yes" : "no");
    }
}

/* ------------------------------------------------------------------------- */
/* Distributed-clock sync-window monitoring (ESC reg 0x092C)                 */
/* ------------------------------------------------------------------------- */
#define DIAG_DC_SYSDIFF_ADR  0x092C   /* System Time Difference register */

int ecat_diag_dc_read(int position, ecat_dc_diff_t *out)
{
    uint8 buf[4];
    uint32 raw;
    int wkc;

    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    out->position  = position;
    out->configadr = ec_slave[position].configadr;
    out->has_dc    = ec_slave[position].hasdc ? 1 : 0;

    memset(buf, 0, sizeof(buf));
    wkc = ec_FPRD(out->configadr, DIAG_DC_SYSDIFF_ADR, sizeof(buf), buf, EC_TIMEOUTRET);
    if (wkc <= 0)
    {
        out->read_ok = 0;
        return -1;
    }
    raw = (uint32)buf[0] | ((uint32)buf[1] << 8) |
          ((uint32)buf[2] << 16) | ((uint32)buf[3] << 24);
    /* Bit 31 is the sign, bits 0..30 the mean deviation magnitude in ns. */
    out->diff_ns = raw & 0x7FFFFFFFu;
    out->ahead   = (raw & 0x80000000u) ? 0 : 1;
    out->read_ok = 1;
    return 0;
}

int ecat_diag_dc_scan(ecat_dc_diff_t *out, int cap)
{
    int i, n = 0;

    if (!out || cap <= 0)
        return 0;
    for (i = 1; i <= ec_slavecount && n < cap; i++)
    {
        ecat_diag_dc_read(i, &out[n]);
        n++;
    }
    return n;
}

void ecat_diag_dc_print(const char *title, const ecat_dc_diff_t *d, int n,
                        uint32_t window_ns)
{
    int i;

    if (title)
        printf("\n=== EtherCAT DC sync (window %u ns): %s ===\n", window_ns, title);
    printf("pos    deviation  dir     status\n");
    for (i = 0; i < n; i++)
    {
        if (!d[i].read_ok)
        {
            printf("%3d    (register read failed)\n", d[i].position);
            continue;
        }
        printf("%3d  %8u ns  %-6s  %s\n",
               d[i].position, d[i].diff_ns,
               d[i].ahead ? "ahead" : "behind",
               d[i].diff_ns <= window_ns ? "in-sync" : "OUT-OF-WINDOW");
    }
}

int ecat_diag_dc_out_of_window(const ecat_dc_diff_t *d, int n, uint32_t window_ns)
{
    int i, bad = 0;

    if (!d)
        return 0;
    for (i = 0; i < n; i++)
    {
        if (!d[i].read_ok)
            continue;
        if (d[i].diff_ns > window_ns)
            bad++;
    }
    return bad;
}

