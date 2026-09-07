/* Host smoke test for the telemetry logger (no SOEM).
 * Fabricates a command/feedback process image where the feedback position
 * lags the commanded position by LAG cycles, samples it, writes the CSV, and
 * prints it so you can eyeball that fb_* trails cmd_* by LAG rows.
 *
 *   make -f Makefile.loader_test telemetry_test && ./telemetry_test
 */
#include "telemetry.h"
#include "config_loader.h"
#include <stdio.h>
#include <string.h>

static const char *CFG =
"{ \"version\":1,"
"  \"network\":{\"interface\":\"eth0\",\"cycle_time_us\":250,"
"               \"number_of_cycles\":8,\"distributed_clock\":true,\"sync0_shift_us\":0},"
"  \"slaves\":[{\"position\":1,\"name\":\"Elmo\",\"mode_of_operation\":8,"
"    \"rxpdo\":[{\"index\":\"0x6040\",\"subindex\":0,\"bitlen\":16,\"name\":\"Controlword\"},"
"              {\"index\":\"0x60B0\",\"subindex\":0,\"bitlen\":32,\"name\":\"Position offset\"}],"
"    \"txpdo\":[{\"index\":\"0x6041\",\"subindex\":0,\"bitlen\":16,\"name\":\"Statusword\"},"
"              {\"index\":\"0x6062\",\"subindex\":0,\"bitlen\":32,\"name\":\"Position demand value\"},"
"              {\"index\":\"0x6064\",\"subindex\":0,\"bitlen\":32,\"name\":\"Position actual value\"}] }] }";

#define LAG 3
#define NCYC 8

static void put_u16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put_i32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = u & 0xFF; p[1] = (u >> 8) & 0xFF; p[2] = (u >> 16) & 0xFF; p[3] = u >> 24;
}

int main(void)
{
    ecat_config_t cfg;
    telemetry_t tlm;
    uint8_t out_img[6];  /* CW(2) + PosOff(4)            */
    uint8_t in_img[10];  /* SW(2) + PosDemand(4) + PosAct(4) */
    int i;

    if (ecat_config_load_string(CFG, &cfg) != 0)
    {
        fprintf(stderr, "config: %s\n", ecat_config_last_error());
        return 1;
    }
    if (telemetry_init(&tlm, &cfg, NCYC) != 0)
    {
        fprintf(stderr, "telemetry: %s\n", telemetry_last_error());
        return 1;
    }
    telemetry_add_slave(&tlm, 1, out_img, sizeof(out_img), in_img, sizeof(in_img));

    for (i = 0; i < NCYC; i++)
    {
        int32_t cmd_pos = i * 100;
        int32_t fb_pos = (i - LAG) * 100; /* feedback trails by LAG cycles */

        put_u16(out_img, 0x000F);      /* controlword */
        put_i32(out_img + 2, cmd_pos); /* position offset */
        put_u16(in_img, 0x1237);       /* statusword */
        put_i32(in_img + 2, cmd_pos);  /* position demand (tracks command) */
        put_i32(in_img + 6, fb_pos);   /* position actual (lags command) */

        telemetry_sample(&tlm, i,
                         (int64_t)i * 250000, /* t_ns @ 250us */
                         3,                   /* wkc */
                         1200 + i,            /* latency_ns */
                         40000 + i,           /* exec_ns */
                         (int64_t)i * 250000);/* dc_time */
    }

    if (telemetry_write(&tlm, "/tmp/tlm_test") != 0)
    {
        fprintf(stderr, "write: %s\n", telemetry_last_error());
        return 1;
    }
    telemetry_free(&tlm);

    printf("---- /tmp/tlm_test_slave1.csv ----\n");
    {
        FILE *fp = fopen("/tmp/tlm_test_slave1.csv", "r");
        char line[512];
        if (fp)
        {
            while (fgets(line, sizeof(line), fp))
                fputs(line, stdout);
            fclose(fp);
        }
    }
    return 0;
}
