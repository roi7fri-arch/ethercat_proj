/** \file
 * \brief Elmo drive families (Platinum, Gold).
 *
 * Almost everything these drives need is plain CiA 402 and is handled by
 * drive_generic_setup(); what remains is identity, a few manufacturer fault
 * codes, and the Platinum's habit of leaving stale entries in the mapping
 * objects of the second axis. That is the whole vendor-specific surface.
 *
 * No SOEM here: CoE goes through ctx->coe, so this file is unit-testable on
 * the host against an in-memory object dictionary.
 */

#include "drive_profile.h"
#include "cia402.h"

#include <stdio.h>

/* ------------------------------------------------------------------------ */
/* Identity                                                                  */
/* ------------------------------------------------------------------------ */
#define ELMO_VENDOR_ID            0x0000009Au

#define ELMO_PRODUCT_PLATINUM     0x00030924u
#define ELMO_PRODUCT_GOLD         0x00030924u  /* same family id, see below */
#define ELMO_REVISION_PLATINUM    0x00010420u

static const uint32_t g_platinum_products[] = { ELMO_PRODUCT_PLATINUM };
static const uint32_t g_gold_products[]     = { ELMO_PRODUCT_GOLD };

/* ------------------------------------------------------------------------ */
/* Manufacturer-specific objects                                             */
/* ------------------------------------------------------------------------ */
#define ELMO_OD_DRIVE_TEMPERATURE 0x3610u
#define ELMO_OD_SAMPLE_VQ         0x3640u

/* ------------------------------------------------------------------------ */
/* Fault decoding                                                            */
/* ------------------------------------------------------------------------ */
static const char *elmo_fault_string(uint16_t code)
{
   switch (code)
   {
      case 0x0000: return "no error";
      case 0x2310: return "continuous current limit exceeded";
      case 0x3210: return "DC bus over-voltage";
      case 0x3220: return "DC bus under-voltage";
      case 0x4310: return "drive over-temperature";
      case 0x5530: return "non-volatile memory fault";
      case 0x7300: return "feedback / sensor error";
      case 0x7500: return "communication error";
      case 0x8611: return "following error too large";
      case 0x8130: return "heartbeat / life guard error";
      case 0xFF01: return "motor stuck (Elmo specific)";
      case 0xFF02: return "speed tracking error (Elmo specific)";
      case 0xFF10: return "cannot start motor (Elmo specific)";
      default:     return NULL;   /* fall back to the standard error class */
   }
}

/* ------------------------------------------------------------------------ */
/* Platinum: two axes behind one node                                        */
/* ------------------------------------------------------------------------ */

/* A Platinum keeps the mapping objects of both axis groups populated across a
 * power cycle. If the new configuration uses fewer entries than the old one,
 * the leftovers stay mapped and the process image comes out the wrong size -
 * which used to show up much later as a baffling working-counter mismatch.
 * Clearing the whole range before the generic setup writes it costs a handful
 * of SDOs in PRE-OP and makes the bring-up deterministic. */
static int elmo_platinum_pre_setup(const drive_ctx_t *ctx)
{
   static const uint16_t bases[] = { 0x1600, 0x1610, 0x1A00, 0x1A10 };
   uint8_t zero = 0;
   unsigned b;
   int k;

   if (!ctx->coe || !ctx->coe->sdo_write)
      return 0;

   for (b = 0; b < sizeof(bases) / sizeof(bases[0]); b++)
      for (k = 0; k < 4; k++)
         (void)ctx->coe->sdo_write(ctx->coe->user, ctx->slave,
                                   (uint16_t)(bases[b] + k), 0x00, 0,
                                   (int)sizeof(zero), &zero);

   printf("slave %d: Elmo Platinum - cleared stale PDO mapping objects\n",
          ctx->slave);
   return 0;
}

static int elmo_platinum_sim_identity(drive_identity_t *out)
{
   out->vendor_id    = ELMO_VENDOR_ID;
   out->product_code = ELMO_PRODUCT_PLATINUM;
   out->revision     = ELMO_REVISION_PLATINUM;
   return 0;
}

static int elmo_gold_sim_identity(drive_identity_t *out)
{
   out->vendor_id    = ELMO_VENDOR_ID;
   out->product_code = ELMO_PRODUCT_GOLD;
   out->revision     = ELMO_REVISION_PLATINUM;
   return 0;
}

/* ------------------------------------------------------------------------ */
/* Profiles                                                                  */
/* ------------------------------------------------------------------------ */
static const drive_profile_t g_elmo_platinum = {
   "elmo_platinum",
   "Elmo Platinum servo drive (single or dual axis)",
   ELMO_VENDOR_ID,
   g_platinum_products,
   (int)(sizeof(g_platinum_products) / sizeof(g_platinum_products[0])),
   elmo_platinum_pre_setup,      /* pre_setup   */
   NULL,                         /* setup: generic CiA 402 is enough */
   NULL,                         /* post_setup  */
   elmo_fault_string,
   elmo_platinum_sim_identity
};

static const drive_profile_t g_elmo_gold = {
   "elmo_gold",
   "Elmo Gold servo drive",
   ELMO_VENDOR_ID,
   g_gold_products,
   (int)(sizeof(g_gold_products) / sizeof(g_gold_products[0])),
   NULL,                         /* nothing special at all */
   NULL,
   NULL,
   elmo_fault_string,
   elmo_gold_sim_identity
};

int elmo_register_profiles(void)
{
   int rc = 0;

   rc |= drive_profile_register(&g_elmo_platinum);
   rc |= drive_profile_register(&g_elmo_gold);
   return rc;
}
