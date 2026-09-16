/** \file
 * \brief Semantic binding and access for the EtherCAT process image.
 *
 * See pdo_bind.h. Vendor-neutral: the only knowledge encoded here is the
 * standard CiA 402 object index for each role, and even that is just the
 * default - a drive family that maps a role to a manufacturer object overrides
 * it through its profile.
 */

#include "pdo_bind.h"
#include "cia402.h"

#include <stdio.h>
#include <string.h>

static char g_err[256];

const char *pdo_bind_last_error(void) { return g_err; }

/* ------------------------------------------------------------------------ */
/* Role table                                                                */
/* ------------------------------------------------------------------------ */
typedef struct {
   pdo_signal_t sig;
   pdo_dir_t    dir;
   uint16_t     index;     /* default CiA 402 object index */
   const char  *name;
} sig_info_t;

static const sig_info_t g_sig[PDO_SIG__COUNT] = {
   { PDO_SIG_CONTROLWORD,       PDO_DIR_RX, CIA402_OD_CONTROLWORD,         "controlword"        },
   { PDO_SIG_MODE_OF_OPERATION, PDO_DIR_RX, CIA402_OD_MODES_OF_OPERATION,  "mode_of_operation"  },
   { PDO_SIG_TARGET_POSITION,   PDO_DIR_RX, CIA402_OD_TARGET_POSITION,     "target_position"    },
   { PDO_SIG_TARGET_VELOCITY,   PDO_DIR_RX, CIA402_OD_TARGET_VELOCITY,     "target_velocity"    },
   { PDO_SIG_TARGET_TORQUE,     PDO_DIR_RX, CIA402_OD_TARGET_TORQUE,       "target_torque"      },
   { PDO_SIG_POSITION_OFFSET,   PDO_DIR_RX, CIA402_OD_POSITION_OFFSET,     "position_offset"    },
   { PDO_SIG_VELOCITY_OFFSET,   PDO_DIR_RX, CIA402_OD_VELOCITY_OFFSET,     "velocity_offset"    },
   { PDO_SIG_TORQUE_OFFSET,     PDO_DIR_RX, CIA402_OD_TORQUE_OFFSET,       "torque_offset"      },
   { PDO_SIG_MAX_TORQUE,        PDO_DIR_RX, CIA402_OD_MAX_TORQUE,          "max_torque"         },

   { PDO_SIG_STATUSWORD,        PDO_DIR_TX, CIA402_OD_STATUSWORD,          "statusword"         },
   { PDO_SIG_MODE_DISPLAY,      PDO_DIR_TX, CIA402_OD_MODES_OF_OP_DISPLAY, "mode_display"       },
   { PDO_SIG_ERROR_CODE,        PDO_DIR_TX, CIA402_OD_ERROR_CODE,          "error_code"         },
   { PDO_SIG_POSITION_DEMAND,   PDO_DIR_TX, CIA402_OD_POSITION_DEMAND,     "position_demand"    },
   { PDO_SIG_POSITION_ACTUAL,   PDO_DIR_TX, CIA402_OD_POSITION_ACTUAL,     "position_actual"    },
   { PDO_SIG_VELOCITY_DEMAND,   PDO_DIR_TX, CIA402_OD_VELOCITY_DEMAND,     "velocity_demand"    },
   { PDO_SIG_VELOCITY_ACTUAL,   PDO_DIR_TX, CIA402_OD_VELOCITY_ACTUAL,     "velocity_actual"    },
   { PDO_SIG_TORQUE_DEMAND,     PDO_DIR_TX, CIA402_OD_TORQUE_DEMAND,       "torque_demand"      },
   { PDO_SIG_TORQUE_ACTUAL,     PDO_DIR_TX, CIA402_OD_TORQUE_ACTUAL,       "torque_actual"      }
};

const char *pdo_signal_name(pdo_signal_t sig)
{
   if (sig < 0 || sig >= PDO_SIG__COUNT)
      return "?";
   return g_sig[sig].name;
}

pdo_dir_t pdo_signal_dir(pdo_signal_t sig)
{
   if (sig < 0 || sig >= PDO_SIG__COUNT)
      return PDO_DIR_TX;
   return g_sig[sig].dir;
}

uint16_t pdo_signal_default_index(pdo_signal_t sig)
{
   if (sig < 0 || sig >= PDO_SIG__COUNT)
      return 0;
   return g_sig[sig].index;
}

/* ------------------------------------------------------------------------ */
/* Binding                                                                   */
/* ------------------------------------------------------------------------ */

/* Locate the `occurrence`-th entry with the given object index and return its
 * bit offset, or -1. Entries are laid out in the image in map order, tightly
 * packed, which is how the CoE PDO mapping is defined. */
static int32_t find_entry(const ecat_pdo_entry_t *list, int count,
                          uint16_t index, int occurrence, uint8_t *bit_len_out)
{
   int32_t bit = 0;
   int seen = 0;
   int i;

   for (i = 0; i < count; i++)
   {
      if (list[i].index == index)
      {
         if (seen == occurrence)
         {
            *bit_len_out = list[i].bitlen;
            return bit;
         }
         seen++;
      }
      bit += list[i].bitlen;
   }
   return -1;
}

int pdo_axis_count(const ecat_slave_config_t *sc)
{
   int n = 0;
   int i;

   if (!sc)
      return 0;

   for (i = 0; i < sc->rxpdo_count; i++)
      if (sc->rxpdo[i].index == CIA402_OD_CONTROLWORD)
         n++;

   return n;
}

int pdo_bind(pdo_io_t *io, const ecat_slave_config_t *sc,
             int position, int axis_index,
             void *out_img, int out_bytes,
             const void *in_img, int in_bytes)
{
   int s;

   if (!io || !sc || axis_index < 0 || out_bytes < 0 || in_bytes < 0)
   {
      snprintf(g_err, sizeof(g_err), "pdo_bind: bad arguments");
      return -1;
   }

   memset(io, 0, sizeof(*io));
   io->position   = position;
   io->axis_index = axis_index;
   io->out_img    = (uint8_t *)out_img;
   io->out_bytes  = out_bytes;
   io->in_img     = (const uint8_t *)in_img;
   io->in_bytes   = in_bytes;

   for (s = 0; s < PDO_SIG__COUNT; s++)
   {
      const sig_info_t *info = &g_sig[s];
      const ecat_pdo_entry_t *list;
      int count;
      uint8_t len = 0;
      int32_t off;

      if (info->dir == PDO_DIR_RX) { list = sc->rxpdo; count = sc->rxpdo_count; }
      else                         { list = sc->txpdo; count = sc->txpdo_count; }

      off = find_entry(list, count, info->index, axis_index, &len);

      io->sig[s].bit_off = off;
      io->sig[s].bit_len = (off >= 0) ? len : 0;
      io->sig[s].present = (off >= 0) ? 1u : 0u;
   }

   g_err[0] = '\0';
   return 0;
}

int pdo_has(const pdo_io_t *io, pdo_signal_t sig)
{
   if (!io || sig < 0 || sig >= PDO_SIG__COUNT)
      return 0;
   return io->sig[sig].present != 0;
}

int pdo_require(const pdo_io_t *io, const pdo_signal_t *required, int count)
{
   int problems = 0;
   int i;

   if (!io || !required)
      return -1;

   for (i = 0; i < count; i++)
   {
      pdo_signal_t sig = required[i];
      const pdo_bind_t *b;
      int img_bytes;

      if (sig < 0 || sig >= PDO_SIG__COUNT)
         continue;

      b = &io->sig[sig];
      img_bytes = (g_sig[sig].dir == PDO_DIR_RX) ? io->out_bytes : io->in_bytes;

      if (!b->present)
      {
         fprintf(stderr,
                 "slave %d axis %d: required signal '%s' (0x%04X) is not in the "
                 "configured %s map\n",
                 io->position, io->axis_index, g_sig[sig].name, g_sig[sig].index,
                 (g_sig[sig].dir == PDO_DIR_RX) ? "RxPDO" : "TxPDO");
         problems++;
         continue;
      }

      if ((b->bit_off + b->bit_len + 7) / 8 > img_bytes)
      {
         fprintf(stderr,
                 "slave %d axis %d: signal '%s' ends at bit %d but the %s image "
                 "is only %d bytes - PDO map and process image disagree\n",
                 io->position, io->axis_index, g_sig[sig].name,
                 b->bit_off + b->bit_len,
                 (g_sig[sig].dir == PDO_DIR_RX) ? "output" : "input", img_bytes);
         problems++;
      }
   }

   return problems;
}

/* Minimum set of signals each CiA 402 mode needs. Controlword and statusword
 * are mandatory everywhere - without them the drive cannot even be enabled. */
static const pdo_signal_t g_req_common[] = {
   PDO_SIG_CONTROLWORD, PDO_SIG_STATUSWORD
};
static const pdo_signal_t g_req_csp[] = {
   PDO_SIG_CONTROLWORD, PDO_SIG_STATUSWORD, PDO_SIG_POSITION_ACTUAL
};
static const pdo_signal_t g_req_csv[] = {
   PDO_SIG_CONTROLWORD, PDO_SIG_STATUSWORD, PDO_SIG_VELOCITY_ACTUAL
};
static const pdo_signal_t g_req_cst[] = {
   PDO_SIG_CONTROLWORD, PDO_SIG_STATUSWORD, PDO_SIG_TORQUE_ACTUAL
};

int pdo_required_for_mode(int mode, const pdo_signal_t **out)
{
   switch (mode)
   {
      case CIA402_MODE_CSP:
         *out = g_req_csp; return (int)(sizeof(g_req_csp) / sizeof(g_req_csp[0]));
      case CIA402_MODE_CSV:
         *out = g_req_csv; return (int)(sizeof(g_req_csv) / sizeof(g_req_csv[0]));
      case CIA402_MODE_CST:
         *out = g_req_cst; return (int)(sizeof(g_req_cst) / sizeof(g_req_cst[0]));
      default:
         *out = g_req_common;
         return (int)(sizeof(g_req_common) / sizeof(g_req_common[0]));
   }
}

void pdo_bind_print(const pdo_io_t *io)
{
   int s;

   if (!io)
      return;

   printf("PDO binding: slave %d axis %d (out %d B, in %d B)\n",
          io->position, io->axis_index, io->out_bytes, io->in_bytes);

   for (s = 0; s < PDO_SIG__COUNT; s++)
   {
      if (!io->sig[s].present)
         continue;
      printf("   %-18s 0x%04X  %-3s  bit %4d  len %2d\n",
             g_sig[s].name, g_sig[s].index,
             (g_sig[s].dir == PDO_DIR_RX) ? "rx" : "tx",
             io->sig[s].bit_off, io->sig[s].bit_len);
   }
}

/* ------------------------------------------------------------------------ */
/* Accessors                                                                 */
/* ------------------------------------------------------------------------ */

/* Extract `bit_len` bits (little-endian bit order, as CoE packs them) starting
 * at `bit_off` and sign-extend to 64 bits. */
static int64_t extract_le_signed(const uint8_t *img, int img_bytes,
                                 int32_t bit_off, uint8_t bit_len)
{
   uint64_t v = 0;
   int i;

   if (!img || bit_len == 0 || bit_len > 64)
      return 0;
   if ((bit_off + bit_len + 7) / 8 > img_bytes)
      return 0;   /* would read past the image */

   for (i = 0; i < bit_len; i++)
   {
      int b = bit_off + i;
      if ((img[b >> 3] >> (b & 7)) & 1u)
         v |= (uint64_t)1 << i;
   }
   if (bit_len < 64 && (v & ((uint64_t)1 << (bit_len - 1))))
      v |= ~(((uint64_t)1 << bit_len) - 1);

   return (int64_t)v;
}

static void insert_le(uint8_t *img, int img_bytes,
                      int32_t bit_off, uint8_t bit_len, uint64_t value)
{
   int i;

   if (!img || bit_len == 0 || bit_len > 64)
      return;
   if ((bit_off + bit_len + 7) / 8 > img_bytes)
      return;   /* would write past the image */

   for (i = 0; i < bit_len; i++)
   {
      int b = bit_off + i;
      uint8_t mask = (uint8_t)(1u << (b & 7));
      if ((value >> i) & 1u)
         img[b >> 3] |= mask;
      else
         img[b >> 3] &= (uint8_t)~mask;
   }
}

int64_t pdo_get_i64(const pdo_io_t *io, pdo_signal_t sig)
{
   const pdo_bind_t *b;

   if (!io || sig < 0 || sig >= PDO_SIG__COUNT)
      return 0;

   b = &io->sig[sig];
   if (!b->present)
      return 0;

   if (g_sig[sig].dir == PDO_DIR_RX)
      return extract_le_signed(io->out_img, io->out_bytes, b->bit_off, b->bit_len);
   return extract_le_signed(io->in_img, io->in_bytes, b->bit_off, b->bit_len);
}

int32_t  pdo_get_i32(const pdo_io_t *io, pdo_signal_t sig) { return (int32_t)pdo_get_i64(io, sig); }
int16_t  pdo_get_i16(const pdo_io_t *io, pdo_signal_t sig) { return (int16_t)pdo_get_i64(io, sig); }
uint16_t pdo_get_u16(const pdo_io_t *io, pdo_signal_t sig) { return (uint16_t)pdo_get_i64(io, sig); }
int8_t   pdo_get_i8 (const pdo_io_t *io, pdo_signal_t sig) { return (int8_t) pdo_get_i64(io, sig); }

void pdo_set_i64(pdo_io_t *io, pdo_signal_t sig, int64_t value)
{
   const pdo_bind_t *b;

   if (!io || sig < 0 || sig >= PDO_SIG__COUNT)
      return;

   b = &io->sig[sig];
   if (!b->present)
      return;

   /* Feedback objects live in the input image, which the slave owns. */
   if (g_sig[sig].dir != PDO_DIR_RX)
      return;

   insert_le(io->out_img, io->out_bytes, b->bit_off, b->bit_len, (uint64_t)value);
}

void pdo_set_i32(pdo_io_t *io, pdo_signal_t sig, int32_t value)  { pdo_set_i64(io, sig, (int64_t)value); }
void pdo_set_i16(pdo_io_t *io, pdo_signal_t sig, int16_t value)  { pdo_set_i64(io, sig, (int64_t)value); }
void pdo_set_u16(pdo_io_t *io, pdo_signal_t sig, uint16_t value) { pdo_set_i64(io, sig, (int64_t)value); }
void pdo_set_i8 (pdo_io_t *io, pdo_signal_t sig, int8_t value)   { pdo_set_i64(io, sig, (int64_t)value); }
