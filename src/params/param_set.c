/** \file
 * \brief Parameter-set document: parse, serialise, format.
 *
 * No SOEM, no EtherCAT - see param_set.h. Applying a set to a live bus is
 * src/ecat/ecat_param.c.
 */

#include "param_set.h"
#include "cJSON.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_err[256] = "";

const char *param_set_last_error(void) { return g_err; }

static int fail(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(g_err, sizeof(g_err), fmt, ap);
   va_end(ap);
   return -1;
}

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */
static const struct { const char *name; int size; } g_types[PARAM_TYPE__COUNT] = {
   { "u8",  1 }, { "i8",  1 },
   { "u16", 2 }, { "i16", 2 },
   { "u32", 4 }, { "i32", 4 },
   { "f32", 4 }
};

int param_type_size(param_type_t t)
{
   if (t < 0 || t >= PARAM_TYPE__COUNT)
      return 4;
   return g_types[t].size;
}

const char *param_type_name(param_type_t t)
{
   if (t < 0 || t >= PARAM_TYPE__COUNT)
      return "u32";
   return g_types[t].name;
}

int param_type_from_name(const char *name, param_type_t *out)
{
   int i;

   if (!name || !out)
      return -1;

   for (i = 0; i < PARAM_TYPE__COUNT; i++)
      if (strcmp(g_types[i].name, name) == 0)
      {
         *out = (param_type_t)i;
         return 0;
      }
   return -1;
}

/* ------------------------------------------------------------------------ */
/* Value formatting and parsing                                              */
/* ------------------------------------------------------------------------ */
static uint32_t mask_for(param_type_t type)
{
   switch (param_type_size(type))
   {
      case 1:  return 0x000000FFu;
      case 2:  return 0x0000FFFFu;
      default: return 0xFFFFFFFFu;
   }
}

static int32_t sign_extend(param_type_t type, uint32_t raw)
{
   switch (param_type_size(type))
   {
      case 1:  return (int32_t)(int8_t)(raw & 0xFFu);
      case 2:  return (int32_t)(int16_t)(raw & 0xFFFFu);
      default: return (int32_t)raw;
   }
}

void param_format_value(char *dst, int cap, param_type_t type, uint32_t raw)
{
   if (!dst || cap <= 0)
      return;

   switch (type)
   {
      case PARAM_TYPE_I8:
      case PARAM_TYPE_I16:
      case PARAM_TYPE_I32:
         snprintf(dst, (size_t)cap, "%d", sign_extend(type, raw));
         break;
      case PARAM_TYPE_F32:
      {
         float f;
         memcpy(&f, &raw, sizeof(f));
         snprintf(dst, (size_t)cap, "%g", (double)f);
         break;
      }
      default:
         snprintf(dst, (size_t)cap, "%u", (unsigned)(raw & mask_for(type)));
         break;
   }
}

int param_parse_value(const char *text, param_type_t type, uint32_t *out)
{
   char *end = NULL;

   if (!text || !out)
      return -1;

   while (*text == ' ' || *text == '\t')
      text++;

   if (type == PARAM_TYPE_F32)
   {
      /* Hex is still accepted for a float, as a raw bit pattern - that is how
       * a drive manual usually prints IEEE-754 defaults. */
      if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
      {
         unsigned long bits = strtoul(text, &end, 16);
         if (end == text || *end != '\0')
            return -1;
         *out = (uint32_t)bits;
         return 0;
      }
      {
         float f = strtof(text, &end);
         if (end == text || *end != '\0')
            return -1;
         memcpy(out, &f, sizeof(*out));
         return 0;
      }
   }

   if (text[0] == '-')
   {
      long v = strtol(text, &end, 0);
      if (end == text || *end != '\0')
         return -1;
      *out = ((uint32_t)(int32_t)v) & mask_for(type);
      return 0;
   }

   {
      unsigned long v = strtoul(text, &end, 0);
      if (end == text || *end != '\0')
         return -1;
      *out = (uint32_t)v & mask_for(type);
      return 0;
   }
}

int param_values_equal(param_type_t type, uint32_t a, uint32_t b)
{
   uint32_t m = mask_for(type);
   return (a & m) == (b & m);
}

/* ------------------------------------------------------------------------ */
/* Parsing                                                                   */
/* ------------------------------------------------------------------------ */
static void copy_str(char *dst, int cap, const cJSON *node, const char *dflt)
{
   const char *s = (node && cJSON_IsString(node)) ? node->valuestring : dflt;
   if (!s)
      s = "";
   snprintf(dst, (size_t)cap, "%s", s);
}

/* Accepts "0x6040", "24640" or a JSON number. */
static int parse_index(const cJSON *node, uint32_t *out)
{
   if (!node)
      return -1;
   if (cJSON_IsNumber(node))
   {
      *out = (uint32_t)node->valuedouble;
      return 0;
   }
   if (cJSON_IsString(node))
   {
      char *end = NULL;
      unsigned long v = strtoul(node->valuestring, &end, 0);
      if (end == node->valuestring || *end != '\0')
         return -1;
      *out = (uint32_t)v;
      return 0;
   }
   return -1;
}

static int parse_entry(const cJSON *j, param_entry_t *e, int n)
{
   const cJSON *node;
   uint32_t u;

   memset(e, 0, sizeof(*e));

   node = cJSON_GetObjectItemCaseSensitive(j, "slave");
   e->slave = (node && cJSON_IsNumber(node)) ? (int)node->valuedouble : 1;
   if (e->slave < 0)
      return fail("parameter %d: slave must be >= 0 (0 means all)", n);

   if (parse_index(cJSON_GetObjectItemCaseSensitive(j, "index"), &u) != 0)
      return fail("parameter %d: missing or malformed 'index'", n);
   if (u > 0xFFFFu)
      return fail("parameter %d: index 0x%X out of range", n, u);
   e->index = (uint16_t)u;

   node = cJSON_GetObjectItemCaseSensitive(j, "subindex");
   e->subindex = (node && cJSON_IsNumber(node)) ? (uint8_t)node->valuedouble : 0;

   node = cJSON_GetObjectItemCaseSensitive(j, "type");
   if (node && cJSON_IsString(node))
   {
      if (param_type_from_name(node->valuestring, &e->type) != 0)
         return fail("parameter %d: unknown type '%s'", n, node->valuestring);
   }
   else
   {
      e->type = PARAM_TYPE_U32;
   }

   node = cJSON_GetObjectItemCaseSensitive(j, "value");
   if (node && cJSON_IsString(node))
   {
      if (param_parse_value(node->valuestring, e->type, &e->value) != 0)
         return fail("parameter %d (0x%04X:%02X): cannot parse value '%s' as %s",
                     n, e->index, e->subindex, node->valuestring,
                     param_type_name(e->type));
   }
   else if (node && cJSON_IsNumber(node))
   {
      if (e->type == PARAM_TYPE_F32)
      {
         float f = (float)node->valuedouble;
         memcpy(&e->value, &f, sizeof(e->value));
      }
      else
      {
         e->value = (uint32_t)(int64_t)node->valuedouble & mask_for(e->type);
      }
   }
   else
   {
      return fail("parameter %d (0x%04X:%02X): missing 'value'",
                  n, e->index, e->subindex);
   }

   node = cJSON_GetObjectItemCaseSensitive(j, "verify");
   e->verify = node ? cJSON_IsTrue(node) : 1;   /* verify by default */

   copy_str(e->name,  sizeof(e->name),  cJSON_GetObjectItemCaseSensitive(j, "name"),  "");
   copy_str(e->group, sizeof(e->group), cJSON_GetObjectItemCaseSensitive(j, "group"), "General");
   copy_str(e->unit,  sizeof(e->unit),  cJSON_GetObjectItemCaseSensitive(j, "unit"),  "");
   return 0;
}

static int parse_file(const cJSON *j, param_file_t *f, int n)
{
   const cJSON *node;
   uint32_t u = 0;

   memset(f, 0, sizeof(*f));

   node = cJSON_GetObjectItemCaseSensitive(j, "slave");
   f->slave = (node && cJSON_IsNumber(node)) ? (int)node->valuedouble : 1;
   if (f->slave < 1)
      return fail("file %d: slave must be >= 1", n);

   node = cJSON_GetObjectItemCaseSensitive(j, "path");
   if (!node || !cJSON_IsString(node) || !node->valuestring[0])
      return fail("file %d: missing 'path'", n);
   copy_str(f->path, sizeof(f->path), node, "");

   /* Default the FoE name to the file's own basename, which is what a drive
    * almost always expects. */
   node = cJSON_GetObjectItemCaseSensitive(j, "remote_name");
   if (node && cJSON_IsString(node) && node->valuestring[0])
   {
      copy_str(f->remote_name, sizeof(f->remote_name), node, "");
   }
   else
   {
      const char *slash = strrchr(f->path, '/');
      snprintf(f->remote_name, sizeof(f->remote_name), "%s",
               slash ? slash + 1 : f->path);
   }

   node = cJSON_GetObjectItemCaseSensitive(j, "password");
   if (node && parse_index(node, &u) == 0)
      f->password = u;

   node = cJSON_GetObjectItemCaseSensitive(j, "use_boot_state");
   f->use_boot_state = node ? cJSON_IsTrue(node) : 1;  /* firmware needs BOOT */
   return 0;
}

int param_set_load_string(const char *json, param_set_t *set)
{
   cJSON *root;
   const cJSON *arr, *item;
   int n;

   if (!json || !set)
      return fail("param_set_load_string: bad arguments");

   memset(set, 0, sizeof(*set));
   g_err[0] = '\0';

   root = cJSON_Parse(json);
   if (!root)
   {
      const char *e = cJSON_GetErrorPtr();
      return fail("JSON parse error near: %.40s", e ? e : "(start)");
   }

   item = cJSON_GetObjectItemCaseSensitive(root, "version");
   set->version = (item && cJSON_IsNumber(item)) ? (int)item->valuedouble : 1;

   copy_str(set->name, sizeof(set->name),
            cJSON_GetObjectItemCaseSensitive(root, "name"), "parameter set");
   copy_str(set->description, sizeof(set->description),
            cJSON_GetObjectItemCaseSensitive(root, "description"), "");

   arr = cJSON_GetObjectItemCaseSensitive(root, "parameters");
   if (arr && cJSON_IsArray(arr))
   {
      n = 0;
      cJSON_ArrayForEach(item, arr)
      {
         if (set->entry_count >= PARAM_MAX)
         {
            cJSON_Delete(root);
            return fail("too many parameters (max %d)", PARAM_MAX);
         }
         if (parse_entry(item, &set->entries[set->entry_count], n) != 0)
         {
            cJSON_Delete(root);
            return -1;
         }
         set->entry_count++;
         n++;
      }
   }

   arr = cJSON_GetObjectItemCaseSensitive(root, "files");
   if (arr && cJSON_IsArray(arr))
   {
      n = 0;
      cJSON_ArrayForEach(item, arr)
      {
         if (set->file_count >= PARAM_FILES_MAX)
         {
            cJSON_Delete(root);
            return fail("too many files (max %d)", PARAM_FILES_MAX);
         }
         if (parse_file(item, &set->files[set->file_count], n) != 0)
         {
            cJSON_Delete(root);
            return -1;
         }
         set->file_count++;
         n++;
      }
   }

   cJSON_Delete(root);
   return 0;
}

int param_set_load_file(const char *path, param_set_t *set)
{
   FILE *fp;
   long size;
   char *buf;
   int rc;

   if (!path || !set)
      return fail("param_set_load_file: bad arguments");

   fp = fopen(path, "rb");
   if (!fp)
      return fail("cannot open '%s'", path);

   fseek(fp, 0, SEEK_END);
   size = ftell(fp);
   fseek(fp, 0, SEEK_SET);
   if (size < 0 || size > (long)(8 * 1024 * 1024))
   {
      fclose(fp);
      return fail("'%s': implausible size %ld", path, size);
   }

   buf = (char *)malloc((size_t)size + 1);
   if (!buf)
   {
      fclose(fp);
      return fail("out of memory reading '%s'", path);
   }
   if (fread(buf, 1, (size_t)size, fp) != (size_t)size)
   {
      free(buf);
      fclose(fp);
      return fail("short read on '%s'", path);
   }
   buf[size] = '\0';
   fclose(fp);

   rc = param_set_load_string(buf, set);
   free(buf);
   return rc;
}

/* ------------------------------------------------------------------------ */
/* Serialising                                                               */
/* ------------------------------------------------------------------------ */
char *param_set_to_json(const param_set_t *set)
{
   cJSON *root, *arr, *item;
   char *out;
   char buf[64];
   int i;

   if (!set)
      return NULL;

   root = cJSON_CreateObject();
   if (!root)
      return NULL;

   cJSON_AddNumberToObject(root, "version", set->version ? set->version : 1);
   cJSON_AddStringToObject(root, "name", set->name);
   cJSON_AddStringToObject(root, "description", set->description);

   arr = cJSON_AddArrayToObject(root, "parameters");
   for (i = 0; i < set->entry_count; i++)
   {
      const param_entry_t *e = &set->entries[i];

      item = cJSON_CreateObject();
      cJSON_AddNumberToObject(item, "slave", e->slave);
      snprintf(buf, sizeof(buf), "0x%04X", e->index);
      cJSON_AddStringToObject(item, "index", buf);
      cJSON_AddNumberToObject(item, "subindex", e->subindex);
      cJSON_AddStringToObject(item, "type", param_type_name(e->type));
      param_format_value(buf, sizeof(buf), e->type, e->value);
      cJSON_AddStringToObject(item, "value", buf);
      cJSON_AddBoolToObject(item, "verify", e->verify ? 1 : 0);
      cJSON_AddStringToObject(item, "name", e->name);
      cJSON_AddStringToObject(item, "group", e->group);
      cJSON_AddStringToObject(item, "unit", e->unit);
      cJSON_AddItemToArray(arr, item);
   }

   arr = cJSON_AddArrayToObject(root, "files");
   for (i = 0; i < set->file_count; i++)
   {
      const param_file_t *f = &set->files[i];

      item = cJSON_CreateObject();
      cJSON_AddNumberToObject(item, "slave", f->slave);
      cJSON_AddStringToObject(item, "path", f->path);
      cJSON_AddStringToObject(item, "remote_name", f->remote_name);
      snprintf(buf, sizeof(buf), "0x%08X", f->password);
      cJSON_AddStringToObject(item, "password", buf);
      cJSON_AddBoolToObject(item, "use_boot_state", f->use_boot_state ? 1 : 0);
      cJSON_AddItemToArray(arr, item);
   }

   out = cJSON_Print(root);
   cJSON_Delete(root);
   return out;
}

int param_set_save_file(const char *path, const param_set_t *set)
{
   char *json;
   FILE *fp;
   size_t len;

   if (!path || !set)
      return fail("param_set_save_file: bad arguments");

   json = param_set_to_json(set);
   if (!json)
      return fail("cannot serialise parameter set");

   fp = fopen(path, "wb");
   if (!fp)
   {
      free(json);
      return fail("cannot write '%s'", path);
   }

   len = strlen(json);
   if (fwrite(json, 1, len, fp) != len)
   {
      free(json);
      fclose(fp);
      return fail("short write on '%s'", path);
   }
   fputc('\n', fp);
   fclose(fp);
   free(json);
   return 0;
}

void param_set_print(const param_set_t *set)
{
   char buf[64];
   int i;

   if (!set)
      return;

   printf("parameter set '%s' v%d - %d parameter(s), %d file(s)\n",
          set->name, set->version, set->entry_count, set->file_count);
   if (set->description[0])
      printf("  %s\n", set->description);

   for (i = 0; i < set->entry_count; i++)
   {
      const param_entry_t *e = &set->entries[i];
      param_format_value(buf, sizeof(buf), e->type, e->value);
      printf("  slave %-2d 0x%04X:%02X %-4s = %-12s %-8s %s%s%s\n",
             e->slave, e->index, e->subindex, param_type_name(e->type),
             buf, e->unit, e->name,
             e->verify ? "" : "  (no verify)",
             e->group[0] ? "" : "");
   }

   for (i = 0; i < set->file_count; i++)
   {
      const param_file_t *f = &set->files[i];
      printf("  slave %-2d FoE '%s' -> '%s' password 0x%08X%s\n",
             f->slave, f->path, f->remote_name, f->password,
             f->use_boot_state ? " (via BOOT)" : "");
   }
}
