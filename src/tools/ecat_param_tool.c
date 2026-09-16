/** \file
 * \brief ecat_param_tool - talk to drives on a real EtherCAT segment.
 *
 * The configuration GUI runs on an engineer's PC with a cable to the machine.
 * The GUI itself is Python and cannot open a raw socket in any sane way, so
 * every bus operation goes through this small C program, which links the same
 * SOEM master and the same src/ code as the production application.
 *
 * Output is JSON on stdout (one document per run) so the GUI can parse it;
 * human-readable progress goes to stderr. That split means the same binary is
 * pleasant to use by hand and trivial to drive from a web backend.
 *
 * Usage:
 *   ecat_param_tool scan     --iface eth0
 *   ecat_param_tool download --iface eth0 --params set.json [--config cfg.json]
 *   ecat_param_tool upload   --iface eth0 --params set.json --out live.json
 *   ecat_param_tool sendfile --iface eth0 --slave 1 --file fw.bin
 *                            [--name fw.bin] [--password 0x0] [--no-boot]
 *   ecat_param_tool files    --iface eth0 --params set.json
 *
 * Every mode needs raw-socket access: run as root, or grant the binary
 * CAP_NET_RAW once with
 *     sudo setcap cap_net_raw,cap_net_admin+eip build/host/ecat_param_tool
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"
#include "ethercatconfig.h"
#include "ethercatcoe.h"
#include "ethercatprint.h"

#include "config_loader.h"
#include "param_set.h"
#include "param_apply.h"
#include "drive_profile.h"
#include "ecat_coe.h"
#include "ecat_param.h"
#include "vendors.h"

static char IOmap[4096];

/* ------------------------------------------------------------------------ */
/* JSON output helpers - small and dependency-free on purpose                */
/* ------------------------------------------------------------------------ */
static void json_escape(const char *s)
{
   if (!s) { printf("null"); return; }
   putchar('"');
   for (; *s; s++)
   {
      switch (*s)
      {
         case '"':  fputs("\\\"", stdout); break;
         case '\\': fputs("\\\\", stdout); break;
         case '\n': fputs("\\n", stdout);  break;
         case '\r': fputs("\\r", stdout);  break;
         case '\t': fputs("\\t", stdout);  break;
         default:
            if ((unsigned char)*s < 0x20) printf("\\u%04x", *s);
            else putchar(*s);
      }
   }
   putchar('"');
}

static void emit_failure(const char *stage, const char *detail)
{
   printf("{\n  \"ok\": false,\n  \"stage\": ");
   json_escape(stage);
   printf(",\n  \"error\": ");
   json_escape(detail);
   printf("\n}\n");
}

/* ------------------------------------------------------------------------ */
/* Arguments                                                                 */
/* ------------------------------------------------------------------------ */
typedef struct {
   const char *mode;
   const char *iface;
   const char *params;
   const char *config;
   const char *out;
   const char *file;
   const char *name;
   uint32_t    password;
   int         slave;
   int         no_boot;
   int         no_verify;
} args_t;

static void usage(void)
{
   fprintf(stderr,
      "usage: ecat_param_tool <mode> --iface <nic> [options]\n"
      "\n"
      "modes:\n"
      "  scan       list the slaves present on the segment\n"
      "  download   write a parameter set to the drives\n"
      "  upload     read the parameter set's objects back from the drives\n"
      "  sendfile   push one file to one slave over FoE\n"
      "  files      push every file listed in a parameter set\n"
      "\n"
      "options:\n"
      "  --iface <nic>       network interface, e.g. eth0        (required)\n"
      "  --params <file>     parameter set JSON\n"
      "  --config <file>     bus configuration JSON (optional, enables\n"
      "                      per-slave drive profiles during the scan)\n"
      "  --out <file>        where 'upload' writes the captured set\n"
      "  --slave <n>         1-based bus position (sendfile)\n"
      "  --file <path>       local file to send (sendfile)\n"
      "  --name <str>        name used in the FoE transaction\n"
      "  --password <hex>    FoE password, default 0\n"
      "  --no-boot           do not switch the slave to BOOT before FoE\n"
      "  --no-verify         do not read parameters back after writing\n");
}

static int parse_args(int argc, char **argv, args_t *a)
{
   int i;

   memset(a, 0, sizeof(*a));
   a->slave = 1;

   if (argc < 2 || argv[1][0] == '-')
      return -1;
   a->mode = argv[1];

   for (i = 2; i < argc; i++)
   {
      const char *k = argv[i];
      const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

      if      (!strcmp(k, "--iface")    && v) { a->iface  = v; i++; }
      else if (!strcmp(k, "--params")   && v) { a->params = v; i++; }
      else if (!strcmp(k, "--config")   && v) { a->config = v; i++; }
      else if (!strcmp(k, "--out")      && v) { a->out    = v; i++; }
      else if (!strcmp(k, "--file")     && v) { a->file   = v; i++; }
      else if (!strcmp(k, "--name")     && v) { a->name   = v; i++; }
      else if (!strcmp(k, "--slave")    && v) { a->slave  = atoi(v); i++; }
      else if (!strcmp(k, "--password") && v) { a->password = (uint32_t)strtoul(v, NULL, 0); i++; }
      else if (!strcmp(k, "--no-boot"))       { a->no_boot   = 1; }
      else if (!strcmp(k, "--no-verify"))     { a->no_verify = 1; }
      else if (!strcmp(k, "-h") || !strcmp(k, "--help")) { return -1; }
      else
      {
         fprintf(stderr, "unknown option '%s'\n", k);
         return -1;
      }
   }

   return a->iface ? 0 : -1;
}

/* ------------------------------------------------------------------------ */
/* Bus bring-up                                                              */
/* ------------------------------------------------------------------------ */

/* Parameter work happens in PRE-OP: the mailbox is alive so CoE and FoE work,
 * but no process data is exchanged and nothing is energised. Deliberately not
 * OP - writing tuning parameters to a drive that is actively controlling a
 * motor is how people break machines. */
static int bus_open(const args_t *a, int want_preop)
{
   if (!ec_init(a->iface))
   {
      emit_failure("ec_init",
                   "cannot open the interface - check the name and that the "
                   "tool has raw-socket permission (run as root or use setcap)");
      return -1;
   }

   if (ec_config_init(FALSE) <= 0)
   {
      emit_failure("ec_config_init", "no slaves found on the segment");
      ec_close();
      return -1;
   }

   if (want_preop)
   {
      ec_slave[0].state = EC_STATE_PRE_OP;
      ec_writestate(0);
      ec_statecheck(0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE * 4);
   }

   fprintf(stderr, "%d slave(s) found on %s\n", ec_slavecount, a->iface);
   return 0;
}

static void bus_close(void)
{
   ec_slave[0].state = EC_STATE_INIT;
   ec_writestate(0);
   ec_close();
}

/* ------------------------------------------------------------------------ */
/* scan                                                                      */
/* ------------------------------------------------------------------------ */
static int cmd_scan(const args_t *a)
{
   int i;

   if (bus_open(a, 1) != 0)
      return 1;

   printf("{\n  \"ok\": true,\n  \"mode\": \"scan\",\n");
   printf("  \"interface\": ");
   json_escape(a->iface);
   printf(",\n  \"slave_count\": %d,\n  \"slaves\": [\n", ec_slavecount);

   for (i = 1; i <= ec_slavecount; i++)
   {
      const ecat_slave_config_t *sc =
         a->config ? ecat_config_find_slave(&g_ecat_config, i) : NULL;
      const drive_profile_t *p = sc ? drive_profile_for_slave(sc) : NULL;

      printf("    { \"position\": %d, \"name\": ", i);
      json_escape(ec_slave[i].name);
      printf(", \"vendor_id\": \"0x%08X\"", ec_slave[i].eep_man);
      printf(", \"product_code\": \"0x%08X\"", ec_slave[i].eep_id);
      printf(", \"revision\": \"0x%08X\"", ec_slave[i].eep_rev);
      printf(", \"state\": \"%s\"", ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
      printf(", \"al_state\": %d", ec_slave[i].state);
      printf(", \"output_bytes\": %d", ec_slave[i].Obytes);
      printf(", \"input_bytes\": %d", ec_slave[i].Ibytes);
      printf(", \"mailbox\": %d", ec_slave[i].mbx_l > 0 ? 1 : 0);
      printf(", \"supports_foe\": %d",
             (ec_slave[i].mbx_proto & ECT_MBXPROT_FOE) ? 1 : 0);
      printf(", \"supports_coe\": %d",
             (ec_slave[i].mbx_proto & ECT_MBXPROT_COE) ? 1 : 0);
      printf(", \"configured_profile\": ");
      json_escape(p ? p->id : NULL);
      printf(", \"configured_name\": ");
      json_escape(sc ? sc->name : NULL);
      printf(" }%s\n", (i < ec_slavecount) ? "," : "");
   }

   printf("  ]\n}\n");
   bus_close();
   return 0;
}

/* ------------------------------------------------------------------------ */
/* download / upload                                                         */
/* ------------------------------------------------------------------------ */
static void emit_report(const char *mode, const param_set_t *set,
                        const param_report_t *rep)
{
   char want[64], got[64];
   int i;

   printf("{\n  \"ok\": %s,\n  \"mode\": \"%s\",\n",
          (rep->failed == 0 && rep->mismatched == 0) ? "true" : "false", mode);
   printf("  \"summary\": { \"ok\": %d, \"mismatched\": %d, \"failed\": %d },\n",
          rep->ok, rep->mismatched, rep->failed);
   printf("  \"results\": [\n");

   for (i = 0; i < rep->count; i++)
   {
      const param_result_t *r = &rep->results[i];
      const param_entry_t *e = &set->entries[r->entry];

      param_format_value(want, sizeof(want), e->type, e->value);
      param_format_value(got, sizeof(got), e->type, r->readback);

      printf("    { \"slave\": %d, \"index\": \"0x%04X\", \"subindex\": %d",
             r->slave, e->index, e->subindex);
      printf(", \"name\": ");
      json_escape(e->name);
      printf(", \"type\": \"%s\"", param_type_name(e->type));
      printf(", \"wanted\": ");
      json_escape(want);
      printf(", \"readback\": ");
      if (r->status == PARAM_OK || r->status == PARAM_MISMATCH)
         json_escape(got);
      else
         printf("null");
      printf(", \"wkc\": %d, \"status\": \"%s\" }%s\n",
             r->wkc, param_status_name(r->status),
             (i < rep->count - 1) ? "," : "");
   }

   printf("  ]\n}\n");
}

static int load_params(const args_t *a, param_set_t *set)
{
   if (!a->params)
   {
      emit_failure("arguments", "--params is required for this mode");
      return -1;
   }
   if (param_set_load_file(a->params, set) != 0)
   {
      emit_failure("param_set_load", param_set_last_error());
      return -1;
   }
   fprintf(stderr, "loaded %d parameter(s) from %s\n",
           set->entry_count, a->params);
   return 0;
}

static int cmd_download(const args_t *a)
{
   param_set_t set;
   param_report_t rep;
   int i;

   if (load_params(a, &set) != 0)
      return 1;

   if (a->no_verify)
      for (i = 0; i < set.entry_count; i++)
         set.entries[i].verify = 0;

   if (bus_open(a, 1) != 0)
      return 1;

   param_download(&set, ecat_coe_ops(), ec_slavecount, &rep);
   emit_report("download", &set, &rep);

   bus_close();
   return (rep.failed || rep.mismatched) ? 2 : 0;
}

static int cmd_upload(const args_t *a)
{
   param_set_t set, live;
   param_report_t rep;

   if (load_params(a, &set) != 0)
      return 1;

   if (bus_open(a, 1) != 0)
      return 1;

   param_upload(&set, ecat_coe_ops(), ec_slavecount, &live, &rep);

   if (a->out)
   {
      if (param_set_save_file(a->out, &live) != 0)
         fprintf(stderr, "WARNING: cannot write %s: %s\n",
                 a->out, param_set_last_error());
      else
         fprintf(stderr, "captured live values to %s\n", a->out);
   }

   emit_report("upload", &live, &rep);
   bus_close();
   return rep.failed ? 2 : 0;
}

/* ------------------------------------------------------------------------ */
/* FoE                                                                       */
/* ------------------------------------------------------------------------ */
static int cmd_sendfile(const args_t *a)
{
   param_file_t f;
   int rc;

   if (!a->file)
   {
      emit_failure("arguments", "--file is required for sendfile");
      return 1;
   }

   memset(&f, 0, sizeof(f));
   f.slave = a->slave;
   snprintf(f.path, sizeof(f.path), "%s", a->file);
   if (a->name)
      snprintf(f.remote_name, sizeof(f.remote_name), "%s", a->name);
   else
   {
      const char *slash = strrchr(a->file, '/');
      snprintf(f.remote_name, sizeof(f.remote_name), "%s",
               slash ? slash + 1 : a->file);
   }
   f.password = a->password;
   f.use_boot_state = a->no_boot ? 0 : 1;

   if (bus_open(a, 0) != 0)
      return 1;

   rc = ecat_param_send_file(&f);

   printf("{\n  \"ok\": %s,\n  \"mode\": \"sendfile\",\n",
          rc == 0 ? "true" : "false");
   printf("  \"slave\": %d,\n  \"file\": ", f.slave);
   json_escape(f.path);
   printf(",\n  \"remote_name\": ");
   json_escape(f.remote_name);
   printf(",\n  \"used_boot_state\": %s,\n", f.use_boot_state ? "true" : "false");
   printf("  \"result\": %d\n}\n", rc);

   bus_close();
   return rc == 0 ? 0 : 2;
}

static int cmd_files(const args_t *a)
{
   param_set_t set;
   int failures, i;

   if (load_params(a, &set) != 0)
      return 1;

   if (set.file_count == 0)
   {
      emit_failure("param_set", "the parameter set lists no files");
      return 1;
   }

   if (bus_open(a, 0) != 0)
      return 1;

   failures = ecat_param_send_files(&set);

   printf("{\n  \"ok\": %s,\n  \"mode\": \"files\",\n",
          failures == 0 ? "true" : "false");
   printf("  \"file_count\": %d,\n  \"failures\": %d,\n  \"files\": [\n",
          set.file_count, failures);
   for (i = 0; i < set.file_count; i++)
   {
      printf("    { \"slave\": %d, \"path\": ", set.files[i].slave);
      json_escape(set.files[i].path);
      printf(", \"remote_name\": ");
      json_escape(set.files[i].remote_name);
      printf(" }%s\n", (i < set.file_count - 1) ? "," : "");
   }
   printf("  ]\n}\n");

   bus_close();
   return failures ? 2 : 0;
}

/* ------------------------------------------------------------------------ */
int main(int argc, char **argv)
{
   args_t a;

   if (parse_args(argc, argv, &a) != 0)
   {
      usage();
      return 1;
   }

   vendors_register_all();

   if (a.config)
   {
      if (ecat_config_load_file(a.config, &g_ecat_config) != 0)
      {
         emit_failure("config_load", ecat_config_last_error());
         return 1;
      }
      fprintf(stderr, "loaded bus config from %s (%d slave(s))\n",
              a.config, g_ecat_config.slave_count);
   }

   if (!strcmp(a.mode, "scan"))     return cmd_scan(&a);
   if (!strcmp(a.mode, "download")) return cmd_download(&a);
   if (!strcmp(a.mode, "upload"))   return cmd_upload(&a);
   if (!strcmp(a.mode, "sendfile")) return cmd_sendfile(&a);
   if (!strcmp(a.mode, "files"))    return cmd_files(&a);

   fprintf(stderr, "unknown mode '%s'\n\n", a.mode);
   usage();
   return 1;
}
