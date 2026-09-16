#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"
#include "ethercatfoe.h"
#include "ecat_foe.h"

int ecat_foe_download_buffer(int slave, const char *filename, uint32_t password,
                             const void *data, int size, int timeout_us)
{
    int wkc;

    if (slave < 1 || slave > ec_slavecount)
    {
        printf("  [FoE] slave %d out of range (have %d)\n", slave, ec_slavecount);
        return -1;
    }
    if (!filename || !data || size <= 0)
        return -1;

    printf("  [FoE] downloading '%s' (%d bytes) to slave %d - DO NOT power off\n",
           filename, size, slave);

    /* ec_FOEwrite takes a non-const buffer; SOEM only reads from it. */
    wkc = ec_FOEwrite(slave, (char *)filename, password, size,
                      (void *)data, timeout_us > 0 ? timeout_us : EC_TIMEOUTRXM);
    if (wkc <= 0)
    {
        printf("  [FoE] FAILED (wkc/err %d) - drive image may be incomplete\n", wkc);
        return wkc < 0 ? wkc : -1;
    }
    printf("  [FoE] '%s' transferred OK (wkc %d)\n", filename, wkc);
    return 0;
}

int ecat_foe_download_file(int slave, const char *filename, uint32_t password,
                           const char *localpath, int timeout_us)
{
    FILE *f;
    long size;
    void *buf;
    int rc;

    f = fopen(localpath, "rb");
    if (!f)
    {
        printf("  [FoE] cannot open '%s'\n", localpath);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0)
    {
        fclose(f);
        printf("  [FoE] '%s' is empty\n", localpath);
        return -1;
    }
    buf = malloc((size_t)size);
    if (!buf)
    {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size)
    {
        fclose(f);
        free(buf);
        printf("  [FoE] short read on '%s'\n", localpath);
        return -1;
    }
    fclose(f);

    rc = ecat_foe_download_buffer(slave, filename, password, buf, (int)size, timeout_us);
    free(buf);
    return rc;
}
