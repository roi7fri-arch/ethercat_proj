/** \file
 * \brief Unit tests for parameter sets (src/params) and the download/upload
 *        engine (src/ecat/ecat_param.c).
 *
 * Pure host test. CoE is bound to an in-memory object dictionary that can be
 * told to refuse writes, refuse reads, or clamp a value the way a real drive
 * does - which is the case that matters most, because a clamped parameter
 * writes successfully and then reads back different.
 */
#include <stdio.h>
#include <string.h>

#include "param_set.h"
#include "param_apply.h"
#include "cia402.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

/* ------------------------------------------------------------------------ */
/* Fake drive                                                                */
/* ------------------------------------------------------------------------ */
#define OD_MAX 128

typedef struct { int slave; uint16_t index; uint8_t sub; uint32_t value; } od_e_t;

static struct {
   od_e_t e[OD_MAX];
   int    count;
   int    refuse_write_index;   /* 0 = none */
   int    refuse_read_index;
   int    clamp_index;          /* writes to this object are clamped */
   uint32_t clamp_to;
} g_od;

static od_e_t *slot(int slave, uint16_t index, uint8_t sub)
{
   int i;
   for (i = 0; i < g_od.count; i++)
      if (g_od.e[i].slave == slave && g_od.e[i].index == index &&
          g_od.e[i].sub == sub)
         return &g_od.e[i];
   if (g_od.count >= OD_MAX)
      return NULL;
   g_od.e[g_od.count].slave = slave;
   g_od.e[g_od.count].index = index;
   g_od.e[g_od.count].sub   = sub;
   g_od.e[g_od.count].value = 0;
   return &g_od.e[g_od.count++];
}

static int od_write(void *user, uint16_t slave, uint16_t index, uint8_t sub,
                    int ca, int size, const void *data)
{
   od_e_t *s;
   uint32_t v = 0;

   (void)user; (void)ca;
   if (g_od.refuse_write_index && index == (uint16_t)g_od.refuse_write_index)
      return 0;   /* working counter 0 = the drive refused */

   if (size > 0 && size <= 4)
      memcpy(&v, data, (size_t)size);

   s = slot((int)slave, index, sub);
   if (!s)
      return 0;

   if (g_od.clamp_index && index == (uint16_t)g_od.clamp_index)
      v = g_od.clamp_to;

   s->value = v;
   return 1;
}

static int od_read(void *user, uint16_t slave, uint16_t index, uint8_t sub,
                   int ca, int *size, void *data)
{
   od_e_t *s;

   (void)user; (void)ca;
   if (g_od.refuse_read_index && index == (uint16_t)g_od.refuse_read_index)
      return 0;

   s = slot((int)slave, index, sub);
   if (!s || !size || *size <= 0 || *size > 4)
      return 0;

   memcpy(data, &s->value, (size_t)*size);
   return 1;
}

static const drive_coe_ops_t g_ops = { od_write, od_read, NULL };

static void od_reset(void) { memset(&g_od, 0, sizeof(g_od)); }
static uint32_t od_get(int slave, uint16_t index, uint8_t sub)
{
   return slot(slave, index, sub)->value;
}

/* ------------------------------------------------------------------------ */
/* Documents                                                                 */
/* ------------------------------------------------------------------------ */
static const char SET_JSON[] =
"{ \"version\": 1, \"name\": \"Axis tuning\","
"  \"description\": \"commissioning set\","
"  \"parameters\": ["
"    { \"slave\": 1, \"index\": \"0x6072\", \"subindex\": 0, \"type\": \"u16\","
"      \"value\": \"1000\", \"name\": \"Max torque\", \"group\": \"Limits\","
"      \"unit\": \"per mille\" },"
"    { \"slave\": 1, \"index\": \"0x6081\", \"subindex\": 0, \"type\": \"u32\","
"      \"value\": \"0x000186A0\", \"name\": \"Profile velocity\" },"
"    { \"slave\": 1, \"index\": \"0x607D\", \"subindex\": 1, \"type\": \"i32\","
"      \"value\": \"-500000\", \"name\": \"Min position limit\" },"
"    { \"slave\": 2, \"index\": \"0x6072\", \"subindex\": 0, \"type\": \"u16\","
"      \"value\": \"800\", \"name\": \"Max torque\" }"
"  ],"
"  \"files\": ["
"    { \"slave\": 1, \"path\": \"/tmp/fw.bin\", \"password\": \"0x12345678\" }"
"  ] }";

static void test_parsing(void)
{
   param_set_t set;

   printf("TEST parameter set parsing\n");

   CHECK(param_set_load_string(SET_JSON, &set) == 0,
         "parsed (%s)", param_set_last_error());
   CHECK(set.entry_count == 4, "4 parameters (got %d)", set.entry_count);
   CHECK(set.file_count == 1,  "1 file (got %d)", set.file_count);
   CHECK(strcmp(set.name, "Axis tuning") == 0, "name kept");

   CHECK(set.entries[0].index == 0x6072 && set.entries[0].type == PARAM_TYPE_U16,
         "first entry is 0x6072 as u16");
   CHECK(set.entries[0].value == 1000, "decimal value parsed (got %u)",
         set.entries[0].value);
   CHECK(set.entries[0].verify == 1, "verify defaults to on");
   CHECK(strcmp(set.entries[0].group, "Limits") == 0, "group kept");
   CHECK(strcmp(set.entries[0].unit, "per mille") == 0, "unit kept");

   CHECK(set.entries[1].value == 0x000186A0u, "hex value parsed");
   CHECK((int32_t)set.entries[2].value == -500000,
         "negative i32 parsed (got %d)", (int32_t)set.entries[2].value);
   CHECK(set.entries[3].slave == 2, "second slave targeted");

   CHECK(set.files[0].password == 0x12345678u, "FoE password parsed");
   CHECK(strcmp(set.files[0].remote_name, "fw.bin") == 0,
         "remote name defaults to the basename (got '%s')",
         set.files[0].remote_name);
   CHECK(set.files[0].use_boot_state == 1, "BOOT is the default for files");
}

static void test_types_and_values(void)
{
   uint32_t raw = 0;
   char buf[64];
   param_type_t t;

   printf("TEST value types\n");

   CHECK(param_type_size(PARAM_TYPE_U8) == 1 &&
         param_type_size(PARAM_TYPE_I16) == 2 &&
         param_type_size(PARAM_TYPE_F32) == 4, "sizes per type");

   CHECK(param_type_from_name("i32", &t) == 0 && t == PARAM_TYPE_I32,
         "type looked up by name");
   CHECK(param_type_from_name("nope", &t) != 0, "unknown type rejected");

   CHECK(param_parse_value("-1", PARAM_TYPE_I16, &raw) == 0 && raw == 0xFFFFu,
         "-1 as i16 is 0xFFFF (got 0x%X)", raw);
   param_format_value(buf, sizeof(buf), PARAM_TYPE_I16, raw);
   CHECK(strcmp(buf, "-1") == 0, "and formats back to -1 (got %s)", buf);

   CHECK(param_parse_value("0x3E8", PARAM_TYPE_U16, &raw) == 0 && raw == 1000,
         "hex accepted");
   CHECK(param_parse_value("1.5", PARAM_TYPE_F32, &raw) == 0, "float parsed");
   param_format_value(buf, sizeof(buf), PARAM_TYPE_F32, raw);
   CHECK(strcmp(buf, "1.5") == 0, "float formats back (got %s)", buf);

   CHECK(param_parse_value("banana", PARAM_TYPE_U16, &raw) != 0,
         "garbage rejected");
   CHECK(param_parse_value("12x", PARAM_TYPE_U16, &raw) != 0,
         "trailing junk rejected");

   /* A 16-bit object only carries 16 bits, so the comparison must ignore the
    * rest - otherwise every verify of a small object would report a mismatch. */
   CHECK(param_values_equal(PARAM_TYPE_U16, 0xDEAD03E8u, 0x000003E8u),
         "compare masks off bytes the object does not carry");
   CHECK(!param_values_equal(PARAM_TYPE_U16, 0x03E9u, 0x03E8u),
         "a real difference is still a difference");
}

static void test_round_trip(void)
{
   param_set_t a, b;
   char *json;

   printf("TEST save / load round trip\n");

   param_set_load_string(SET_JSON, &a);
   json = param_set_to_json(&a);
   CHECK(json != NULL, "serialised");
   CHECK(param_set_load_string(json, &b) == 0, "re-parsed");

   CHECK(b.entry_count == a.entry_count, "same parameter count");
   CHECK(b.file_count == a.file_count, "same file count");
   CHECK(b.entries[2].value == a.entries[2].value,
         "negative value survived the round trip (%d vs %d)",
         (int32_t)b.entries[2].value, (int32_t)a.entries[2].value);
   CHECK(b.entries[1].type == a.entries[1].type, "types survived");
   CHECK(b.files[0].password == a.files[0].password, "password survived");
   free(json);
}

static void test_bad_documents(void)
{
   param_set_t set;

   printf("TEST malformed documents are rejected with a reason\n");

   CHECK(param_set_load_string("{ not json", &set) != 0, "garbage rejected");
   CHECK(param_set_load_string(
            "{\"parameters\":[{\"subindex\":0,\"value\":\"1\"}]}", &set) != 0,
         "missing index rejected");
   CHECK(param_set_load_string(
            "{\"parameters\":[{\"index\":\"0x6072\"}]}", &set) != 0,
         "missing value rejected");
   CHECK(param_set_load_string(
            "{\"parameters\":[{\"index\":\"0x6072\",\"type\":\"u9\",\"value\":\"1\"}]}",
            &set) != 0, "bad type rejected");
   CHECK(param_set_load_string(
            "{\"files\":[{\"slave\":1}]}", &set) != 0,
         "file without a path rejected");
   CHECK(strlen(param_set_last_error()) > 0, "an error message is available");
}

static void test_download(void)
{
   param_set_t set;
   param_report_t rep;
   int problems;

   printf("TEST download writes and verifies\n");

   od_reset();
   param_set_load_string(SET_JSON, &set);

   problems = param_download(&set, &g_ops, 2, &rep);
   CHECK(problems == 0, "no problems (got %d)", problems);
   CHECK(rep.ok == 4, "4 parameters applied (got %d)", rep.ok);

   CHECK(od_get(1, 0x6072, 0) == 1000, "slave 1 max torque written");
   CHECK(od_get(2, 0x6072, 0) == 800, "slave 2 got its own value");
   CHECK(od_get(1, 0x6081, 0) == 0x000186A0u, "profile velocity written");
   CHECK((int32_t)od_get(1, 0x607D, 1) == -500000,
         "negative limit written (got %d)", (int32_t)od_get(1, 0x607D, 1));
}

static void test_slave_not_present(void)
{
   param_set_t set;
   param_report_t rep;

   printf("TEST parameters for a missing slave are skipped, not failed\n");

   od_reset();
   param_set_load_string(SET_JSON, &set);

   /* Only one slave on the bus; the entry for slave 2 has nowhere to go. */
   param_download(&set, &g_ops, 1, &rep);
   CHECK(rep.ok == 3, "3 applied (got %d)", rep.ok);
   CHECK(rep.failed == 0, "nothing reported as failed (got %d)", rep.failed);
   {
      int i, skipped = 0;
      for (i = 0; i < rep.count; i++)
         if (rep.results[i].status == PARAM_SKIPPED)
            skipped++;
      CHECK(skipped == 1, "one entry skipped (got %d)", skipped);
   }
}

/* The case that justifies verifying at all. */
static void test_clamped_value_is_reported(void)
{
   param_set_t set;
   param_report_t rep;
   int problems;

   printf("TEST a drive that clamps the value is caught\n");

   od_reset();
   g_od.clamp_index = 0x6072;
   g_od.clamp_to = 500;          /* drive refuses more than 500 */
   param_set_load_string(SET_JSON, &set);

   problems = param_download(&set, &g_ops, 2, &rep);
   CHECK(problems == 2, "both clamped parameters reported (got %d)", problems);
   CHECK(rep.mismatched == 2, "reported as mismatches, not write failures");
   CHECK(rep.failed == 0, "the writes themselves succeeded");
   {
      int i, found = 0;
      for (i = 0; i < rep.count; i++)
         if (rep.results[i].status == PARAM_MISMATCH &&
             rep.results[i].readback == 500)
            found++;
      CHECK(found == 2, "the value the drive actually took is reported");
   }
   param_report_print(&set, &rep);
}

static void test_write_and_read_failures(void)
{
   param_set_t set;
   param_report_t rep;

   printf("TEST refused writes and reads are distinguished\n");

   od_reset();
   g_od.refuse_write_index = 0x6081;
   param_set_load_string(SET_JSON, &set);
   param_download(&set, &g_ops, 2, &rep);
   {
      int i, w = 0;
      for (i = 0; i < rep.count; i++)
         if (rep.results[i].status == PARAM_WRITE_FAILED) w++;
      CHECK(w == 1, "one write failure reported (got %d)", w);
   }

   od_reset();
   g_od.refuse_read_index = 0x6081;
   param_download(&set, &g_ops, 2, &rep);
   {
      int i, r = 0;
      for (i = 0; i < rep.count; i++)
         if (rep.results[i].status == PARAM_READ_FAILED) r++;
      CHECK(r == 1, "one read failure reported (got %d)", r);
   }
   CHECK(strcmp(param_status_name(PARAM_MISMATCH), "mismatch") == 0,
         "status names available for the GUI");
}

static void test_verify_can_be_turned_off(void)
{
   param_set_t set;
   param_report_t rep;
   int i;

   printf("TEST verify can be disabled per parameter\n");

   od_reset();
   g_od.clamp_index = 0x6072;
   g_od.clamp_to = 500;
   param_set_load_string(SET_JSON, &set);
   for (i = 0; i < set.entry_count; i++)
      set.entries[i].verify = 0;

   CHECK(param_download(&set, &g_ops, 2, &rep) == 0,
         "clamping goes unnoticed when verification is off");
   CHECK(rep.ok == 4, "everything reported ok");
}

static void test_upload(void)
{
   param_set_t set, live;
   param_report_t rep;

   printf("TEST upload captures the drive's live values\n");

   od_reset();
   param_set_load_string(SET_JSON, &set);
   param_download(&set, &g_ops, 2, &rep);

   /* Someone re-tunes the drive behind our back. */
   slot(1, 0x6072, 0)->value = 777;

   CHECK(param_upload(&set, &g_ops, 2, &live, &rep) == 0, "upload ok");
   CHECK(live.entries[0].value == 777,
         "captured the changed value (got %u)", live.entries[0].value);
   CHECK(live.entries[1].value == set.entries[1].value,
         "unchanged parameters match");
   CHECK(live.entry_count == set.entry_count, "shape preserved");
   CHECK(strcmp(live.name, set.name) == 0, "metadata preserved");
}

/* slave 0 means "every drive on the bus", for a machine with identical axes. */
static void test_broadcast_slave_zero(void)
{
   static const char JSON[] =
   "{ \"parameters\": [ { \"slave\": 0, \"index\": \"0x6072\", \"type\": \"u16\","
   "                      \"value\": \"1234\", \"name\": \"Max torque\" } ] }";
   param_set_t set;
   param_report_t rep;

   printf("TEST slave 0 applies to every drive\n");

   od_reset();
   CHECK(param_set_load_string(JSON, &set) == 0, "parsed");
   CHECK(param_download(&set, &g_ops, 3, &rep) == 0, "applied");
   CHECK(rep.ok == 3, "written to all 3 slaves (got %d)", rep.ok);
   CHECK(od_get(1, 0x6072, 0) == 1234 &&
         od_get(2, 0x6072, 0) == 1234 &&
         od_get(3, 0x6072, 0) == 1234, "every drive got the value");
}

int main(void)
{
   printf("=== parameter set / download engine unit tests ===\n");

   test_parsing();
   test_types_and_values();
   test_round_trip();
   test_bad_documents();
   test_download();
   test_slave_not_present();
   test_clamped_value_is_reported();
   test_write_and_read_failures();
   test_verify_can_be_turned_off();
   test_upload();
   test_broadcast_slave_zero();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
