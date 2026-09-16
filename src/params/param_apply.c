/** \file
 * \brief Download / upload a parameter set over CoE. No SOEM, no EtherCAT.
 */

#include "param_apply.h"

#include <stdio.h>
#include <string.h>

const char *param_status_name(param_status_t s)
{
   switch (s)
   {
      case PARAM_OK:           return "ok";
      case PARAM_SKIPPED:      return "skipped";
      case PARAM_WRITE_FAILED: return "write failed";
      case PARAM_READ_FAILED:  return "read failed";
      case PARAM_MISMATCH:     return "mismatch";
   }
   return "?";
}

static void report_add(param_report_t *r, int entry, int slave,
                       param_status_t st, int wkc, uint32_t readback)
{
   param_result_t *res;

   if (!r || r->count >= PARAM_MAX)
      return;

   res = &r->results[r->count++];
   res->entry    = entry;
   res->slave    = slave;
   res->status   = st;
   res->wkc      = wkc;
   res->readback = readback;

   if (st == PARAM_OK)            r->ok++;
   else if (st == PARAM_MISMATCH) r->mismatched++;
   else if (st != PARAM_SKIPPED)  r->failed++;
}

/* A parameter may target one slave or, with slave == 0, every slave on the
 * bus - which is what a machine with several identical axes wants. */
static int slave_range(const param_entry_t *e, int slave_count,
                       int *first, int *last)
{
   if (e->slave == 0)
   {
      *first = 1;
      *last  = slave_count;
      return slave_count > 0;
   }
   if (e->slave < 1 || e->slave > slave_count)
      return 0;
   *first = *last = e->slave;
   return 1;
}

static int write_one(const drive_coe_ops_t *coe, int slave,
                     const param_entry_t *e, int *wkc_out)
{
   uint32_t v = e->value;
   int size = param_type_size(e->type);

   *wkc_out = coe->sdo_write(coe->user, (uint16_t)slave, e->index, e->subindex,
                             0, size, &v);
   return (*wkc_out > 0) ? 0 : -1;
}

static int read_one(const drive_coe_ops_t *coe, int slave,
                    const param_entry_t *e, uint32_t *out, int *wkc_out)
{
   uint32_t v = 0;
   int size = param_type_size(e->type);

   if (!coe->sdo_read)
   {
      *wkc_out = 0;
      return -1;
   }

   *wkc_out = coe->sdo_read(coe->user, (uint16_t)slave, e->index, e->subindex,
                            0, &size, &v);
   if (*wkc_out <= 0)
      return -1;

   *out = v;
   return 0;
}

int param_download(const param_set_t *set, const drive_coe_ops_t *coe,
                   int slave_count, param_report_t *report)
{
   param_report_t local;
   int i, s, first, last;

   if (!report)
      report = &local;
   memset(report, 0, sizeof(*report));

   if (!set || !coe || !coe->sdo_write)
      return -1;

   for (i = 0; i < set->entry_count; i++)
   {
      const param_entry_t *e = &set->entries[i];

      if (!slave_range(e, slave_count, &first, &last))
      {
         report_add(report, i, e->slave, PARAM_SKIPPED, 0, 0);
         continue;
      }

      for (s = first; s <= last; s++)
      {
         uint32_t back = 0;
         int wkc = 0;

         if (write_one(coe, s, e, &wkc) != 0)
         {
            report_add(report, i, s, PARAM_WRITE_FAILED, wkc, 0);
            continue;
         }

         if (!e->verify)
         {
            report_add(report, i, s, PARAM_OK, wkc, e->value);
            continue;
         }

         if (read_one(coe, s, e, &back, &wkc) != 0)
         {
            report_add(report, i, s, PARAM_READ_FAILED, wkc, 0);
            continue;
         }

         report_add(report, i, s,
                    param_values_equal(e->type, back, e->value)
                       ? PARAM_OK : PARAM_MISMATCH,
                    wkc, back);
      }
   }

   return report->failed + report->mismatched;
}

int param_upload(const param_set_t *set, const drive_coe_ops_t *coe,
                 int slave_count, param_set_t *out, param_report_t *report)
{
   param_report_t local;
   int i, first, last;

   if (!report)
      report = &local;
   memset(report, 0, sizeof(*report));

   if (!set || !coe || !out)
      return -1;

   *out = *set;

   for (i = 0; i < set->entry_count; i++)
   {
      const param_entry_t *e = &set->entries[i];
      uint32_t v = 0;
      int wkc = 0, s;

      if (!slave_range(e, slave_count, &first, &last))
      {
         report_add(report, i, e->slave, PARAM_SKIPPED, 0, 0);
         continue;
      }

      /* For a broadcast parameter the first slave is what gets captured; a
       * fleet is only worth uploading if the axes agree anyway. */
      s = first;
      if (read_one(coe, s, e, &v, &wkc) != 0)
      {
         report_add(report, i, s, PARAM_READ_FAILED, wkc, 0);
         continue;
      }

      out->entries[i].value = v;
      report_add(report, i, s, PARAM_OK, wkc, v);
   }

   return report->failed;
}

void param_report_print(const param_set_t *set, const param_report_t *report)
{
   char want[64], got[64];
   int i;

   if (!set || !report)
      return;

   printf("\n%-5s %-11s %-6s %-14s %-14s %s\n",
          "slave", "object", "type", "wanted", "read back", "result");

   for (i = 0; i < report->count; i++)
   {
      const param_result_t *r = &report->results[i];
      const param_entry_t *e = &set->entries[r->entry];

      param_format_value(want, sizeof(want), e->type, e->value);
      if (r->status == PARAM_OK || r->status == PARAM_MISMATCH)
         param_format_value(got, sizeof(got), e->type, r->readback);
      else
         snprintf(got, sizeof(got), "-");

      printf("%-5d 0x%04X:%02X %-6s %-14s %-14s %s%s%s\n",
             r->slave, e->index, e->subindex, param_type_name(e->type),
             want, got, param_status_name(r->status),
             e->name[0] ? "  " : "", e->name);
   }

   printf("\n%d ok, %d mismatched, %d failed\n",
          report->ok, report->mismatched, report->failed);
}
