#include "config_loader.h"
#include "cJSON.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_err[256] = "";

const char *ecat_config_last_error(void)
{
    return g_err;
}

static int fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
    va_end(ap);
    return -1;
}

uint32_t ecat_pdo_map_value(const ecat_pdo_entry_t *e)
{
    return ((uint32_t)e->index << 16) | ((uint32_t)e->subindex << 8) | (uint32_t)e->bitlen;
}

const ecat_slave_config_t *ecat_config_find_slave(const ecat_config_t *cfg, int position)
{
    int i;
    for (i = 0; i < cfg->slave_count; i++)
        if (cfg->slaves[i].position == position)
            return &cfg->slaves[i];
    return NULL;
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/* Parse a CoE index that may be "0x6040", "6040" or a JSON number. */
static int parse_index(const cJSON *item, uint16_t *out){
    long v;
    if (cJSON_IsNumber(item))
    {
        v = (long)item->valuedouble;
    }
    else if (cJSON_IsString(item) && item->valuestring)
    {
        char *end = NULL;
        v = strtol(item->valuestring, &end, 16); /* base 16 handles optional 0x */
        if (end == item->valuestring)
            return fail("index '%s' is not a hex number", item->valuestring);
    }
    else
    {
        return fail("PDO entry missing 'index'");
    }
    if (v < 0 || v > 0xFFFF)
        return fail("index 0x%lX out of range 0x0000-0xFFFF", v);
    *out = (uint16_t)v;
    return 0;
}

/* Optional 32-bit id ("0x00000021", "33" or a JSON number). Absent -> 0. */
static uint32_t parse_u32_opt(const cJSON *item)
{
    if (cJSON_IsNumber(item))
        return (uint32_t)item->valuedouble;
    if (cJSON_IsString(item) && item->valuestring)
        return (uint32_t)strtoul(item->valuestring, NULL, 0); /* 0 = auto base */
    return 0;
}

/* Optional 16-bit CoE index ("0x1600", "1600" hex, or a JSON number).
 * Absent/invalid -> def. */
static uint16_t parse_index_opt(const cJSON *item, uint16_t def)
{
    if (cJSON_IsNumber(item))
        return (uint16_t)item->valuedouble;
    if (cJSON_IsString(item) && item->valuestring && item->valuestring[0])
        return (uint16_t)strtoul(item->valuestring, NULL, 16);
    return def;
}

/* Optional per-slave list of PRE-OP init SDO writes (vendor start-up params). */
static int parse_startup_sdo(const cJSON *arr, ecat_slave_config_t *s)
{
    int n = 0;
    const cJSON *item;

    s->startup_sdo_count = 0;
    if (arr == NULL)
        return 0;
    if (!cJSON_IsArray(arr))
        return fail("'startup_sdo' must be an array");

    cJSON_ArrayForEach(item, arr)
    {
        ecat_sdo_cmd_t *c;
        const cJSON *jsub, *jsize, *jval, *jcomment;
        long sub, size;

        if (n >= ECAT_CFG_MAX_SDO_CMDS)
            return fail("'startup_sdo' has more than %d entries", ECAT_CFG_MAX_SDO_CMDS);
        c = &s->startup_sdo[n];

        if (parse_index(cJSON_GetObjectItemCaseSensitive(item, "index"), &c->index) != 0)
            return -1;

        jsub     = cJSON_GetObjectItemCaseSensitive(item, "subindex");
        jsize    = cJSON_GetObjectItemCaseSensitive(item, "size");
        jval     = cJSON_GetObjectItemCaseSensitive(item, "value");
        jcomment = cJSON_GetObjectItemCaseSensitive(item, "comment");

        sub  = cJSON_IsNumber(jsub)  ? (long)jsub->valuedouble  : 0;
        size = cJSON_IsNumber(jsize) ? (long)jsize->valuedouble : 4;
        if (sub < 0 || sub > 255)
            return fail("startup_sdo entry %d: subindex %ld out of range 0-255", n, sub);
        if (size != 1 && size != 2 && size != 4)
            return fail("startup_sdo entry %d (0x%04X): size %ld must be 1, 2 or 4",
                        n, c->index, size);

        c->subindex = (uint8_t)sub;
        c->size     = (uint8_t)size;
        c->value    = parse_u32_opt(jval);
        copy_str(c->comment, sizeof(c->comment),
                 cJSON_IsString(jcomment) ? jcomment->valuestring : "");
        n++;
    }
    s->startup_sdo_count = n;
    return 0;
}

static int parse_pdo_list(const cJSON *arr, const char *which,
                          ecat_pdo_entry_t *out, int *out_count)
{
    int n = 0;
    const cJSON *item;

    if (arr == NULL)
    {
        *out_count = 0;
        return 0;
    }
    if (!cJSON_IsArray(arr))
        return fail("'%s' must be an array", which);

    cJSON_ArrayForEach(item, arr)
    {
        const cJSON *jsub, *jbits, *jname;
        long sub, bits;

        if (n >= ECAT_CFG_MAX_PDO_ENTRIES)
            return fail("'%s' has more than %d entries", which, ECAT_CFG_MAX_PDO_ENTRIES);

        if (parse_index(cJSON_GetObjectItemCaseSensitive(item, "index"), &out[n].index) != 0)
            return -1;

        jsub  = cJSON_GetObjectItemCaseSensitive(item, "subindex");
        jbits = cJSON_GetObjectItemCaseSensitive(item, "bitlen");
        jname = cJSON_GetObjectItemCaseSensitive(item, "name");

        sub  = cJSON_IsNumber(jsub)  ? (long)jsub->valuedouble  : 0;
        bits = cJSON_IsNumber(jbits) ? (long)jbits->valuedouble : -1;

        if (sub < 0 || sub > 255)
            return fail("'%s' entry %d: subindex %ld out of range 0-255", which, n, sub);
        if (bits < 1 || bits > 64)
            return fail("'%s' entry %d (0x%04X): bitlen %ld out of range 1-64",
                        which, n, out[n].index, bits);

        out[n].subindex = (uint8_t)sub;
        out[n].bitlen   = (uint8_t)bits;
        copy_str(out[n].name, sizeof(out[n].name),
                 cJSON_IsString(jname) ? jname->valuestring : "");
        n++;
    }
    *out_count = n;
    return 0;
}

static int parse_network(const cJSON *net, ecat_network_config_t *out)
{
    const cJSON *j;

    if (!cJSON_IsObject(net))
        return fail("missing 'network' object");

    j = cJSON_GetObjectItemCaseSensitive(net, "interface");
    copy_str(out->interface, sizeof(out->interface),
             cJSON_IsString(j) ? j->valuestring : "eth0");

    /* Optional second NIC for cable redundancy (ec_init_redundant). "" = off. */
    j = cJSON_GetObjectItemCaseSensitive(net, "redundant_interface");
    copy_str(out->redundant_interface, sizeof(out->redundant_interface),
             cJSON_IsString(j) ? j->valuestring : "");

    j = cJSON_GetObjectItemCaseSensitive(net, "cycle_time_us");
    out->cycle_time_us = cJSON_IsNumber(j) ? (int)j->valuedouble : 0;
    if (out->cycle_time_us <= 0)
        return fail("network.cycle_time_us must be > 0");

    j = cJSON_GetObjectItemCaseSensitive(net, "number_of_cycles");
    out->number_of_cycles = cJSON_IsNumber(j) ? (int)j->valuedouble : 0;

    j = cJSON_GetObjectItemCaseSensitive(net, "distributed_clock");
    out->distributed_clock = cJSON_IsBool(j) ? cJSON_IsTrue(j) : 1;

    j = cJSON_GetObjectItemCaseSensitive(net, "sync0_shift_us");
    out->sync0_shift_us = cJSON_IsNumber(j) ? (int)j->valuedouble : 0;

    /* DC phase-lock PI divisors: optional, default to the classic SOEM gains. */
    j = cJSON_GetObjectItemCaseSensitive(net, "sync_kp_div");
    out->sync_kp_div = cJSON_IsNumber(j) ? (int)j->valuedouble : 100;
    if (out->sync_kp_div <= 0)
        out->sync_kp_div = 100;

    j = cJSON_GetObjectItemCaseSensitive(net, "sync_ki_div");
    out->sync_ki_div = cJSON_IsNumber(j) ? (int)j->valuedouble : 20;
    if (out->sync_ki_div <= 0)
        out->sync_ki_div = 20;

    /* Auto-recovery of lost/errored slaves: optional, on by default. */
    j = cJSON_GetObjectItemCaseSensitive(net, "auto_recovery");
    out->auto_recovery = cJSON_IsBool(j) ? cJSON_IsTrue(j) : 1;

    j = cJSON_GetObjectItemCaseSensitive(net, "auto_recovery_timeout_us");
    out->auto_recovery_timeout_us = cJSON_IsNumber(j) ? (int)j->valuedouble : 500;
    if (out->auto_recovery_timeout_us <= 0)
        out->auto_recovery_timeout_us = 500;

    /* Slave identity verification against config: optional, on by default. */
    j = cJSON_GetObjectItemCaseSensitive(net, "verify_identity");
    out->verify_identity = cJSON_IsBool(j) ? cJSON_IsTrue(j) : 1;

    return 0;
}

int ecat_config_load_string(const char *json, ecat_config_t *cfg)
{
    cJSON *root, *jslaves, *jslave, *j;
    int idx = 0;

    memset(cfg, 0, sizeof(*cfg));
    g_err[0] = '\0';

    root = cJSON_Parse(json);
    if (root == NULL)
    {
        const char *ep = cJSON_GetErrorPtr();
        return fail("JSON parse error near: %.40s", ep ? ep : "(unknown)");
    }

    j = cJSON_GetObjectItemCaseSensitive(root, "version");
    cfg->version = cJSON_IsNumber(j) ? (int)j->valuedouble : 1;

    if (parse_network(cJSON_GetObjectItemCaseSensitive(root, "network"), &cfg->network) != 0)
    {
        cJSON_Delete(root);
        return -1;
    }

    jslaves = cJSON_GetObjectItemCaseSensitive(root, "slaves");
    if (!cJSON_IsArray(jslaves))
    {
        cJSON_Delete(root);
        return fail("missing 'slaves' array");
    }

    cJSON_ArrayForEach(jslave, jslaves)
    {
        ecat_slave_config_t *s;

        if (idx >= ECAT_CFG_MAX_SLAVES)
        {
            cJSON_Delete(root);
            return fail("more than %d slaves configured", ECAT_CFG_MAX_SLAVES);
        }
        s = &cfg->slaves[idx];

        j = cJSON_GetObjectItemCaseSensitive(jslave, "position");
        s->position = cJSON_IsNumber(j) ? (int)j->valuedouble : (idx + 1);

        j = cJSON_GetObjectItemCaseSensitive(jslave, "name");
        copy_str(s->name, sizeof(s->name), cJSON_IsString(j) ? j->valuestring : "");

        j = cJSON_GetObjectItemCaseSensitive(jslave, "profile");
        copy_str(s->profile, sizeof(s->profile), cJSON_IsString(j) ? j->valuestring : "");

        j = cJSON_GetObjectItemCaseSensitive(jslave, "mode_of_operation");
        if (!cJSON_IsNumber(j))
        {
            cJSON_Delete(root);
            return fail("slave %d: missing 'mode_of_operation'", s->position);
        }
        s->mode_of_operation = (int)j->valuedouble;

        /* Optional identity check (0 = skip). Verified against SII after config. */
        s->expected_vendor_id =
            parse_u32_opt(cJSON_GetObjectItemCaseSensitive(jslave, "expected_vendor_id"));
        s->expected_product_code =
            parse_u32_opt(cJSON_GetObjectItemCaseSensitive(jslave, "expected_product_code"));
        s->expected_revision =
            parse_u32_opt(cJSON_GetObjectItemCaseSensitive(jslave, "expected_revision"));

        /* Optional PDO mapping / SM-assign object overrides (defaults are the
         * standard CiA402 ones; a differing family can change them as data). */
        s->rxpdo_map_base = parse_index_opt(
            cJSON_GetObjectItemCaseSensitive(jslave, "rxpdo_map_base"), ECAT_CFG_DEF_RXMAP_BASE);
        s->txpdo_map_base = parse_index_opt(
            cJSON_GetObjectItemCaseSensitive(jslave, "txpdo_map_base"), ECAT_CFG_DEF_TXMAP_BASE);
        s->sm2_assign = parse_index_opt(
            cJSON_GetObjectItemCaseSensitive(jslave, "sm2_assign"), ECAT_CFG_DEF_SM2_ASSIGN);
        s->sm3_assign = parse_index_opt(
            cJSON_GetObjectItemCaseSensitive(jslave, "sm3_assign"), ECAT_CFG_DEF_SM3_ASSIGN);
        j = cJSON_GetObjectItemCaseSensitive(jslave, "map_entries_per_obj");
        s->map_entries_per_obj = cJSON_IsNumber(j) ? (int)j->valuedouble : ECAT_CFG_DEF_MAP_PER_OBJ;
        if (s->map_entries_per_obj <= 0)
            s->map_entries_per_obj = ECAT_CFG_DEF_MAP_PER_OBJ;

        if (parse_pdo_list(cJSON_GetObjectItemCaseSensitive(jslave, "rxpdo"),
                           "rxpdo", s->rxpdo, &s->rxpdo_count) != 0 ||
            parse_pdo_list(cJSON_GetObjectItemCaseSensitive(jslave, "txpdo"),
                           "txpdo", s->txpdo, &s->txpdo_count) != 0)
        {
            cJSON_Delete(root);
            return -1;
        }

        if (parse_startup_sdo(cJSON_GetObjectItemCaseSensitive(jslave, "startup_sdo"), s) != 0)
        {
            cJSON_Delete(root);
            return -1;
        }
        idx++;
    }
    cfg->slave_count = idx;

    cJSON_Delete(root);
    return 0;
}

int ecat_config_load_file(const char *path, ecat_config_t *cfg)
{
    FILE *fp;
    long size;
    char *buf;
    int rc;

    fp = fopen(path, "rb");
    if (fp == NULL)
        return fail("cannot open config file '%s'", path);

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0)
    {
        fclose(fp);
        return fail("config file '%s' is empty", path);
    }

    buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL)
    {
        fclose(fp);
        return fail("out of memory reading '%s'", path);
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size)
    {
        free(buf);
        fclose(fp);
        return fail("failed to read '%s'", path);
    }
    buf[size] = '\0';
    fclose(fp);

    rc = ecat_config_load_string(buf, cfg);
    free(buf);
    return rc;
}

void ecat_config_print(const ecat_config_t *cfg)
{
    int i, k;
    printf("EtherCAT config v%d\n", cfg->version);
    printf("  interface        : %s\n", cfg->network.interface);
    if (cfg->network.redundant_interface[0])
        printf("  redundant_iface  : %s (cable redundancy)\n", cfg->network.redundant_interface);
    printf("  cycle_time_us    : %d\n", cfg->network.cycle_time_us);
    printf("  number_of_cycles : %d\n", cfg->network.number_of_cycles);
    printf("  distributed_clock: %s\n", cfg->network.distributed_clock ? "on" : "off");
    printf("  sync0_shift_us   : %d\n", cfg->network.sync0_shift_us);
    printf("  auto_recovery    : %s (timeout %d us)\n",
           cfg->network.auto_recovery ? "on" : "off",
           cfg->network.auto_recovery_timeout_us);
    printf("  verify_identity  : %s\n", cfg->network.verify_identity ? "on" : "off");
    printf("  sync_kp_div      : %d\n", cfg->network.sync_kp_div);
    printf("  sync_ki_div      : %d\n", cfg->network.sync_ki_div);
    for (i = 0; i < cfg->slave_count; i++)
    {
        const ecat_slave_config_t *s = &cfg->slaves[i];
        printf("  slave %d '%s' mode=%d  rx=%d tx=%d\n",
               s->position, s->name, s->mode_of_operation, s->rxpdo_count, s->txpdo_count);
        if (s->expected_vendor_id || s->expected_product_code || s->expected_revision)
            printf("    expect id: vendor 0x%08X product 0x%08X rev 0x%08X\n",
                   s->expected_vendor_id, s->expected_product_code, s->expected_revision);
        if (s->rxpdo_map_base != ECAT_CFG_DEF_RXMAP_BASE ||
            s->txpdo_map_base != ECAT_CFG_DEF_TXMAP_BASE ||
            s->sm2_assign != ECAT_CFG_DEF_SM2_ASSIGN ||
            s->sm3_assign != ECAT_CFG_DEF_SM3_ASSIGN)
            printf("    map objs : rx 0x%04X (SM2 0x%04X) / tx 0x%04X (SM3 0x%04X), <=%d/obj\n",
                   s->rxpdo_map_base, s->sm2_assign, s->txpdo_map_base, s->sm3_assign,
                   s->map_entries_per_obj);
        for (k = 0; k < s->startup_sdo_count; k++)
            printf("    initSDO 0x%04X:%02X = 0x%X (%uB)  %s\n",
                   s->startup_sdo[k].index, s->startup_sdo[k].subindex,
                   s->startup_sdo[k].value, s->startup_sdo[k].size,
                   s->startup_sdo[k].comment);
        for (k = 0; k < s->rxpdo_count; k++)
            printf("    RxPDO 0x%04X:%02X %2ub -> map 0x%08X  %s\n",
                   s->rxpdo[k].index, s->rxpdo[k].subindex, s->rxpdo[k].bitlen,
                   ecat_pdo_map_value(&s->rxpdo[k]), s->rxpdo[k].name);
        for (k = 0; k < s->txpdo_count; k++)
            printf("    TxPDO 0x%04X:%02X %2ub -> map 0x%08X  %s\n",
                   s->txpdo[k].index, s->txpdo[k].subindex, s->txpdo[k].bitlen,
                   ecat_pdo_map_value(&s->txpdo[k]), s->txpdo[k].name);
    }
}
