/** \file
 * \brief Drive-family registry and the standard CiA 402 PRE-OP sequence.
 *
 * The PDO-mapping code here is the former apply_pdo_direction() from
 * elmo_config_setup.c, unchanged in behaviour but with CoE access routed
 * through drive_coe_ops_t so it no longer depends on SOEM.
 */

#include "drive_profile.h"
#include "cia402.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Registry                                                                  */
/* ------------------------------------------------------------------------ */
static const drive_profile_t *g_profiles[DRIVE_PROFILE_MAX];
static int g_profile_count;

int drive_profile_register(const drive_profile_t *profile)
{
   int i;

   if (!profile || !profile->id)
      return -1;

   for (i = 0; i < g_profile_count; i++)
      if (strcmp(g_profiles[i]->id, profile->id) == 0)
      {
         fprintf(stderr, "drive profile '%s' registered twice\n", profile->id);
         return -1;
      }

   if (g_profile_count >= DRIVE_PROFILE_MAX)
   {
      fprintf(stderr, "drive profile table full (max %d)\n", DRIVE_PROFILE_MAX);
      return -1;
   }

   g_profiles[g_profile_count++] = profile;
   return 0;
}

const drive_profile_t *drive_profile_find(const char *id)
{
   int i;

   if (!id || !id[0])
      return NULL;

   for (i = 0; i < g_profile_count; i++)
      if (strcmp(g_profiles[i]->id, id) == 0)
         return g_profiles[i];

   return NULL;
}

int drive_profile_count(void) { return g_profile_count; }

const drive_profile_t *drive_profile_at(int i)
{
   if (i < 0 || i >= g_profile_count)
      return NULL;
   return g_profiles[i];
}

void drive_profile_reset(void)
{
   g_profile_count = 0;
   memset(g_profiles, 0, sizeof(g_profiles));
}

void drive_profile_list(void)
{
   int i, k;

   printf("registered drive profiles (%d):\n", g_profile_count);
   for (i = 0; i < g_profile_count; i++)
   {
      const drive_profile_t *p = g_profiles[i];
      printf("   %-20s vendor 0x%08X  %s\n", p->id, p->vendor_id,
             p->description ? p->description : "");
      for (k = 0; k < p->n_product_codes; k++)
         printf("      product 0x%08X\n", p->product_codes[k]);
   }
}

/* ------------------------------------------------------------------------ */
/* Generic CiA 402 setup                                                     */
/* ------------------------------------------------------------------------ */

int drive_apply_pdo_map(const drive_ctx_t *ctx,
                        uint16_t map_base, uint16_t sm_assign, int per_obj,
                        const ecat_pdo_entry_t *entries, int count)
{
   int retval = 0;
   uint8_t zero = 0;
   int n_objs, k, j;

   if (!ctx || !ctx->coe || !ctx->coe->sdo_write)
      return 0;

   if (per_obj < 1)
      per_obj = 8;

   n_objs = (count + per_obj - 1) / per_obj;
   if (n_objs < 1)
      n_objs = 1;   /* still clear and assign one (empty) object */

   /* Disable the SM assignment before rewriting the mapping objects. */
   retval += ctx->coe->sdo_write(ctx->coe->user, ctx->slave, sm_assign, 0x00, 0,
                                 (int)sizeof(zero), &zero);

   for (k = 0; k < n_objs; k++)
   {
      uint16_t map_idx = (uint16_t)(map_base + k);
      int start = k * per_obj;
      int n_in = count - start;
      uint8_t c;

      if (n_in > per_obj) n_in = per_obj;
      if (n_in < 0)       n_in = 0;

      /* Clear the entry count before (re)writing the sub-entries. */
      retval += ctx->coe->sdo_write(ctx->coe->user, ctx->slave, map_idx, 0x00, 0,
                                    (int)sizeof(zero), &zero);

      for (j = 0; j < n_in; j++)
      {
         uint32_t val = ecat_pdo_map_value(&entries[start + j]);
         retval += ctx->coe->sdo_write(ctx->coe->user, ctx->slave, map_idx,
                                       (uint8_t)(j + 1), 0,
                                       (int)sizeof(val), &val);
      }

      c = (uint8_t)n_in;
      retval += ctx->coe->sdo_write(ctx->coe->user, ctx->slave, map_idx, 0x00, 0,
                                    (int)sizeof(c), &c);
   }

   /* Assign the (possibly multiple) mapping objects to the sync manager. */
   for (k = 0; k < n_objs; k++)
   {
      uint16_t map_idx = (uint16_t)(map_base + k);
      retval += ctx->coe->sdo_write(ctx->coe->user, ctx->slave, sm_assign,
                                    (uint8_t)(k + 1), 0,
                                    (int)sizeof(map_idx), &map_idx);
   }
   {
      uint8_t assign_count = (uint8_t)n_objs;
      retval += ctx->coe->sdo_write(ctx->coe->user, ctx->slave, sm_assign, 0x00, 0,
                                    (int)sizeof(assign_count), &assign_count);
   }

   return retval;
}

int drive_generic_setup(const drive_ctx_t *ctx)
{
   const ecat_slave_config_t *sc;
   int retval = 0;
   uint8_t mode;
   int l, i;

   if (!ctx || !ctx->cfg || !ctx->coe)
      return 0;

   sc = ctx->cfg;

   /* Family-specific PRE-OP parameters, carried as data in the JSON so a new
    * drive usually needs no code at all. */
   for (i = 0; i < sc->startup_sdo_count; i++)
   {
      const ecat_sdo_cmd_t *c = &sc->startup_sdo[i];
      uint32_t v = c->value;   /* little-endian; low bytes used for 1/2 B */
      retval += ctx->coe->sdo_write(ctx->coe->user, ctx->slave,
                                    c->index, c->subindex, 0, (int)c->size, &v);
      printf("slave %d init SDO 0x%04X:%02X = 0x%X (%uB) %s\n",
             ctx->slave, c->index, c->subindex, c->value, c->size, c->comment);
   }

   /* Mode of operation (0x6060), read back via 0x6061. */
   mode = (uint8_t)sc->mode_of_operation;
   retval += ctx->coe->sdo_write(ctx->coe->user, ctx->slave,
                                 CIA402_OD_MODES_OF_OPERATION, 0x00, 0,
                                 (int)sizeof(mode), &mode);

   mode = 0;
   l = (int)sizeof(mode);
   if (ctx->coe->sdo_read)
      retval += ctx->coe->sdo_read(ctx->coe->user, ctx->slave,
                                   CIA402_OD_MODES_OF_OP_DISPLAY, 0x00, 0,
                                   &l, &mode);
   printf("slave %d '%s': mode of operation requested %d (%s), read back %d\n",
          ctx->slave, sc->name, sc->mode_of_operation,
          cia402_mode_name(sc->mode_of_operation), mode);

   /* RxPDO -> SM2, TxPDO -> SM3. Bases and limits come from the config, so a
    * family whose mapping objects live elsewhere works without code. */
   retval += drive_apply_pdo_map(ctx, sc->rxpdo_map_base, sc->sm2_assign,
                                 sc->map_entries_per_obj,
                                 sc->rxpdo, sc->rxpdo_count);
   retval += drive_apply_pdo_map(ctx, sc->txpdo_map_base, sc->sm3_assign,
                                 sc->map_entries_per_obj,
                                 sc->txpdo, sc->txpdo_count);

   printf("slave %d configured from JSON (rx=%d, tx=%d), SDO wkc sum = %d\n",
          ctx->slave, sc->rxpdo_count, sc->txpdo_count, retval);
   return retval;
}

int drive_profile_run_setup(const drive_profile_t *profile,
                            const drive_ctx_t *ctx)
{
   if (!ctx || !ctx->cfg)
      return 0;

   if (!profile)
      profile = drive_profile_generic();

   if (profile->pre_setup && profile->pre_setup(ctx) < 0)
   {
      fprintf(stderr, "slave %d: profile '%s' pre_setup failed\n",
              ctx->slave, profile->id);
      return 0;
   }

   if (profile->setup)
      (void)profile->setup(ctx);
   else
      (void)drive_generic_setup(ctx);

   if (profile->post_setup && profile->post_setup(ctx) < 0)
   {
      fprintf(stderr, "slave %d: profile '%s' post_setup failed\n",
              ctx->slave, profile->id);
      return 0;
   }

   return 1;
}

/* ------------------------------------------------------------------------ */
/* Fault text                                                                */
/* ------------------------------------------------------------------------ */

/* The CiA 402 / DS-301 error-code classes. The class is the top nibble, except
 * 0xFFxx which is reserved for device-specific codes. Enough to tell an
 * operator what kind of thing went wrong without a vendor manual. */
static const char *cia402_error_class(uint16_t code)
{
   if ((code & 0xFF00u) == 0xFF00u)
      return "device specific";

   switch (code & 0xF000u)
   {
      case 0x0000: return (code == 0) ? "no error" : "generic error";
      case 0x1000: return "generic error";
      case 0x2000: return "current";
      case 0x3000: return "voltage";
      case 0x4000: return "temperature";
      case 0x5000: return "device hardware";
      case 0x6000: return "device software";
      case 0x7000: return "additional modules";
      case 0x8000: return "monitoring (communication / following error)";
      case 0x9000: return "external error";
      case 0xF000: return "additional functions";
      default:     return "unknown error class";
   }
}

const char *drive_fault_string(const drive_profile_t *profile, uint16_t code)
{
   if (profile && profile->fault_string)
   {
      const char *s = profile->fault_string(code);
      if (s)
         return s;
   }
   return cia402_error_class(code);
}

/* ------------------------------------------------------------------------ */
/* Built-in generic profile                                                  */
/* ------------------------------------------------------------------------ */
static const drive_profile_t g_generic = {
   "cia402_generic",
   "Standard CiA 402 drive - no vendor extensions",
   0, NULL, 0,
   NULL, NULL, NULL, NULL, NULL
};

const drive_profile_t *drive_profile_generic(void) { return &g_generic; }

const drive_profile_t *drive_profile_for_slave(const ecat_slave_config_t *cfg)
{
   const drive_profile_t *p;

   if (!cfg)
      return &g_generic;

   p = drive_profile_find(cfg->profile);
   if (p)
      return p;

   if (cfg->profile[0])
      printf("slave %d '%s': profile '%s' is not registered, "
             "falling back to %s\n",
             cfg->position, cfg->name, cfg->profile, g_generic.id);

   return &g_generic;
}
