/* Host-side smoke test for the config loader (no SOEM needed).
 *   make -f Makefile.loader_test && ./config_loader_test path/to/config.json
 * With no argument it parses a small built-in sample.
 */
#include "config_loader.h"
#include <stdio.h>

static const char *SAMPLE =
"{\n"
"  \"version\": 1,\n"
"  \"network\": { \"interface\": \"eth0\", \"cycle_time_us\": 250,\n"
"                 \"number_of_cycles\": 12000, \"distributed_clock\": true,\n"
"                 \"sync0_shift_us\": 0 },\n"
"  \"slaves\": [ { \"position\": 1, \"name\": \"Elmo Platinum\", \"mode_of_operation\": 8,\n"
"    \"rxpdo\": [ {\"index\":\"0x6040\",\"subindex\":0,\"bitlen\":16,\"name\":\"Controlword\"},\n"
"               {\"index\":\"0x60B0\",\"subindex\":0,\"bitlen\":32,\"name\":\"Position offset\"} ],\n"
"    \"txpdo\": [ {\"index\":\"0x6041\",\"subindex\":0,\"bitlen\":16,\"name\":\"Statusword\"} ] } ]\n"
"}\n";

int main(int argc, char **argv)
{
    ecat_config_t cfg;
    int rc;

    if (argc > 1)
        rc = ecat_config_load_file(argv[1], &cfg);
    else
        rc = ecat_config_load_string(SAMPLE, &cfg);

    if (rc != 0)
    {
        fprintf(stderr, "load failed: %s\n", ecat_config_last_error());
        return 1;
    }
    ecat_config_print(&cfg);
    return 0;
}
