/** \file
 * \brief Virtual EtherCAT slave (software ESC) implementation. See slave_sim.h.
 *
 * Emulates one or more identical Elmo-style CiA402 axes daisy-chained on the
 * bus. Each frame is passed through every slave in order, exactly like a real
 * EtherCAT segment, so auto-increment / fixed / broadcast / logical addressing
 * and the Working Counter all behave correctly for multi-slave setups.
 *
 * Per slave it emulates: the AL state machine, EEPROM (SII) reads, a CoE
 * mailbox SDO server backed by a small object dictionary, FMMU programming and
 * logical process-data exchange, plus a trivial CiA402 echo.
 */
#include "slave_sim.h"
#include <string.h>
#include <stdio.h>

/* ----------------------------------------------------------------------- */
/* Little-endian access helpers                                            */
/* ----------------------------------------------------------------------- */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
          ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v)
{
   p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
   p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* ----------------------------------------------------------------------- */
/* EtherCAT command codes (ec_cmdtype)                                     */
/* ----------------------------------------------------------------------- */
enum {
   CMD_NOP = 0, CMD_APRD, CMD_APWR, CMD_APRW,
   CMD_FPRD,    CMD_FPWR, CMD_FPRW,
   CMD_BRD,     CMD_BWR,  CMD_BRW,
   CMD_LRD,     CMD_LWR,  CMD_LRW,
   CMD_ARMW,    CMD_FRMW
};

/* ESC register offsets we react to */
#define REG_TYPE      0x0000
#define REG_ESCSUP    0x0008
#define REG_STADR     0x0010
#define REG_DLSTAT    0x0110
#define REG_ALCTL     0x0120
#define REG_ALSTAT    0x0130
#define REG_ALSTATCODE 0x0134
#define REG_PDICTL    0x0140
#define REG_EEPCFG    0x0500
#define REG_EEPCTL    0x0502  /* comm(2) + addr(2) + data(2) window */
#define REG_EEPDAT    0x0508
#define REG_FMMU0     0x0600
#define REG_SM0       0x0800
#define REG_SM0STAT   0x0805
#define REG_SM1STAT   0x080D
#define REG_DCSYSDIFF 0x092C  /* DC System Time Difference (sign + 31-bit ns) */

/* Mailbox protocol type (low nibble of mailbox header byte 5) */
#define MBXT_COE      0x03
#define MBXT_FOE      0x04

/* FoE opcodes (ETG.1000 FoE) */
#define FOE_OP_WRITE  0x02
#define FOE_OP_DATA   0x03
#define FOE_OP_ACK    0x04

/* Mailbox physical windows (must match the SII SM section below) */
#define MBX_OUT_ADDR  0x1000  /* master -> slave (SM0) */
#define MBX_IN_ADDR   0x1080  /* slave -> master (SM1) */
#define MBX_LEN       0x0080

/* Process data physical windows (must match the SII SM section below) */
#define PD_OUT_ADDR   0x1100  /* SM2 outputs (RxPDO) */
#define PD_IN_ADDR    0x1180  /* SM3 inputs  (TxPDO) */

/* ----------------------------------------------------------------------- */
/* Slave state                                                             */
/* ----------------------------------------------------------------------- */
#define SII_SIZE 512
#define OD_MAX   256
#define PD_SIZE  0x2000
#define FOE_CAP  4096            /* max bytes a virtual slave accepts via FoE */

typedef struct { uint16_t index; uint8_t sub; uint8_t size; uint32_t val; } od_entry_t;

typedef struct {
   uint32_t logstart;
   uint16_t loglen;
   uint16_t physstart;
   uint8_t  type;    /* 1 = inputs (slave->master), 2 = outputs (master->slave) */
   uint8_t  active;
} fmmu_t;

typedef struct {
   uint16_t station;              /* fixed station address (set via APWR STADR) */
   uint16_t al_state;             /* current AL status (low nibble = state)     */
   uint16_t eep_addr;             /* latched EEPROM word address                */
   uint8_t  sii[SII_SIZE];

   od_entry_t od[OD_MAX];
   int        od_count;

   uint8_t  mbx_resp[MBX_LEN];    /* pending CoE response served on SM1 read     */
   int      mbx_ready;

   fmmu_t   fmmu[16];

   uint8_t  freeze;               /* test fault: stop mirroring RxPDO->TxPDO      */
   uint8_t  wkc_drop;             /* test fault: withhold input-side WKC          */
   uint8_t  err_ctr[0x14];        /* ESC error counters 0x0300..0x0313            */
   uint8_t  dc_diff[4];           /* DC System Time Difference (reg 0x092C)       */

   uint8_t  foe_buf[FOE_CAP];     /* bytes received via FoE download              */
   int      foe_len;              /* number of FoE bytes received so far          */
   uint32_t foe_password;         /* password from the FoE write request          */

   uint8_t  pd[PD_SIZE];          /* physical process image                      */
} slave_t;

static slave_t g_slave[SLAVESIM_MAX_SLAVES];
static int     g_nslaves = 1;
static int     g_verbose = 0;
static int     g_configured = 0;   /* set once the test/nicdrv has built the bus */

/* ----------------------------------------------------------------------- */
/* SII (EEPROM) image                                                      */
/* ----------------------------------------------------------------------- */
static void sii_setword(slave_t *s, int word, uint16_t v)
{
   if ((word * 2 + 1) < SII_SIZE) wr16(&s->sii[word * 2], v);
}

static void build_sii(slave_t *s)
{
   memset(s->sii, 0, sizeof(s->sii));

   /* Configured station alias / vendor / product / revision */
   sii_setword(s, 0x08, 0x009A);   /* Vendor ID  (Elmo = 0x0000009A) lo */
   sii_setword(s, 0x09, 0x0000);   /*                               hi */
   sii_setword(s, 0x0a, 0x0924);   /* Product code lo */
   sii_setword(s, 0x0b, 0x0003);   /* Product code hi */
   sii_setword(s, 0x0c, 0x0420);   /* Revision lo */
   sii_setword(s, 0x0d, 0x0001);   /* Revision hi */

   /* Mailbox configuration (standard offsets used by config_init) */
   sii_setword(s, 0x18, MBX_OUT_ADDR); /* Receive  mailbox offset (SM0) */
   sii_setword(s, 0x19, MBX_LEN);      /* Receive  mailbox size         */
   sii_setword(s, 0x1a, MBX_IN_ADDR);  /* Transmit mailbox offset (SM1) */
   sii_setword(s, 0x1b, MBX_LEN);      /* Transmit mailbox size         */
   sii_setword(s, 0x1c, 0x000C);       /* Mailbox protocols: CoE + FoE   */

   /* Category area starts at word 0x40 (ECT_SII_START). Each category is
    * [id(word)][length in words(word)][data...]; list ends with 0xffff. */
   int w = 0x40;

   /* --- STRING category (id 10): one string "SimElmo" --- */
   {
      static const char *name = "SimElmo";
      int nlen = (int)strlen(name);
      int dbytes = 2 + nlen;
      int dwords = (dbytes + 1) / 2;
      sii_setword(s, w++, 10);
      sii_setword(s, w++, (uint16_t)dwords);
      int base = w * 2;
      s->sii[base + 0] = 1;           /* number of strings */
      s->sii[base + 1] = (uint8_t)nlen;
      memcpy(&s->sii[base + 2], name, nlen);
      w += dwords;
   }

   /* --- SyncManager category (id 41): 4 SM entries, 8 bytes each --- */
   {
      static const uint16_t sm[4][2] = {
         { MBX_OUT_ADDR, MBX_LEN },   /* SM0 mailbox out */
         { MBX_IN_ADDR,  MBX_LEN },   /* SM1 mailbox in  */
         { PD_OUT_ADDR,  0x0000  },   /* SM2 outputs     */
         { PD_IN_ADDR,   0x0000  },   /* SM3 inputs      */
      };
      static const uint8_t creg[4] = { 0x26, 0x22, 0x64, 0x20 };
      sii_setword(s, w++, 41);
      sii_setword(s, w++, 16);
      int base = w * 2;
      for (int i = 0; i < 4; i++) {
         uint8_t *e = &s->sii[base + i * 8];
         wr16(&e[0], sm[i][0]);      /* PhysStart */
         wr16(&e[2], sm[i][1]);      /* Length    */
         e[4] = creg[i];             /* control register */
         e[5] = 0x00;                /* status register  */
         e[6] = 0x01;                /* activate         */
         e[7] = 0x00;                /* PDI control      */
      }
      w += 16;
   }

   sii_setword(s, w, 0xffff);        /* end of categories */
}

/* ----------------------------------------------------------------------- */
/* Object dictionary (CoE)                                                 */
/* ----------------------------------------------------------------------- */
static od_entry_t *od_find(slave_t *s, uint16_t index, uint8_t sub)
{
   for (int i = 0; i < s->od_count; i++)
      if (s->od[i].index == index && s->od[i].sub == sub) return &s->od[i];
   return NULL;
}

static void od_set(slave_t *s, uint16_t index, uint8_t sub, uint8_t size, uint32_t val)
{
   od_entry_t *e = od_find(s, index, sub);
   if (!e) {
      if (s->od_count >= OD_MAX) return;
      e = &s->od[s->od_count++];
      e->index = index;
      e->sub   = sub;
   }
   e->size = size;
   e->val  = val;
}

static void seed_od(slave_t *s)
{
   s->od_count = 0;
   /* 0x1C00 Sync Manager Communication Type. sub0 = count of SM, then the
    * type of each SM: 1=mbxout 2=mbxin 3=outputs(RxPDO) 4=inputs(TxPDO). */
   od_set(s, 0x1C00, 0, 1, 4);
   od_set(s, 0x1C00, 1, 1, 1);
   od_set(s, 0x1C00, 2, 1, 2);
   od_set(s, 0x1C00, 3, 1, 3);   /* SM2 -> outputs */
   od_set(s, 0x1C00, 4, 1, 4);   /* SM3 -> inputs  */

   /* Echo the CiA402 mode of operation display from what the master sets. */
   od_set(s, 0x6061, 0, 1, 0);
}

/* ----------------------------------------------------------------------- */
/* CoE mailbox SDO server                                                  */
/* ----------------------------------------------------------------------- */
static void coe_handle(slave_t *s, const uint8_t *req){
   uint8_t  type  = req[5] & 0x0f;
   uint8_t  cmd   = req[8];
   uint16_t index = rd16(&req[9]);
   uint8_t  sub   = req[11];
   uint8_t  ccs   = (uint8_t)((cmd >> 5) & 0x07);
   uint8_t *r = s->mbx_resp;

   if (type != 0x03) {
      if (g_verbose) printf("[sim] mbx non-CoE type %u ignored\n", type);
      return;
   }

   memset(r, 0, MBX_LEN);
   r[0] = 0x0a; r[1] = 0x00;     /* CoE SDO payload length */
   r[5] = 0x03;                  /* mailbox type = CoE */
   wr16(&r[6], (uint16_t)(0x03 << 12)); /* service = SDO response (3) */
   wr16(&r[9], index);
   r[11] = sub;

   if (ccs == 1) {               /* download / SDO write */
      uint8_t  size = (cmd & 0x02) ? (uint8_t)(4 - ((cmd >> 2) & 0x03)) : 4;
      uint32_t val  = rd32(&req[12]);
      if (size < 4) val &= (0xFFFFFFFFu >> (8 * (4 - size)));
      od_set(s, index, sub, size, val);
      /* mode of operation (0x6060) is reflected in mode display (0x6061) */
      if (index == 0x6060 && sub == 0) od_set(s, 0x6061, 0, 1, val & 0xff);
      r[8] = 0x60;               /* download response */
      if (g_verbose)
         printf("[sim] SDO write 0x%04X:%02X = 0x%X (%u B)\n", index, sub, val, size);
   } else if (ccs == 2) {        /* upload / SDO read */
      od_entry_t *e = od_find(s, index, sub);
      uint8_t  size = e ? e->size : 1;
      uint32_t val  = e ? e->val  : 0;
      if (!e && g_verbose)
         printf("[sim] SDO read  0x%04X:%02X UNKNOWN -> 0\n", index, sub);
      if (size < 1) size = 1;
      if (size > 4) size = 4;
      r[8] = (uint8_t)(0x43 | ((4 - size) << 2)); /* expedited upload resp */
      wr32(&r[12], val);
      if (g_verbose)
         printf("[sim] SDO read  0x%04X:%02X = 0x%X (%u B)\n", index, sub, val, size);
   } else {
      if (g_verbose) printf("[sim] SDO unsupported ccs %u\n", ccs);
      return;
   }
   s->mbx_ready = 1;
}

/* ----------------------------------------------------------------------- */
/* FoE mailbox server (firmware / file download)                           */
/* ----------------------------------------------------------------------- */
/* Handles the master side of ecx_FOEwrite: a WRITE request starts a transfer
 * (ack packet 0), each DATA packet is appended to foe_buf and acked with the
 * same packet number, so ec_FOEwrite runs to completion against the sim. */
static void foe_handle(slave_t *s, const uint8_t *req)
{
   uint8_t  op  = req[6];
   uint32_t pkt = rd32(&req[8]);
   uint16_t mlen = rd16(&req[0]);          /* mailbox payload length */
   uint8_t *r = s->mbx_resp;

   if (op == FOE_OP_WRITE) {
      s->foe_len = 0;
      s->foe_password = pkt;               /* WRITE carries the password here */
   } else if (op == FOE_OP_DATA) {
      int dlen = (int)mlen - 6;            /* payload after opcode+reserved+pkt# */
      if (dlen > 0) {
         const uint8_t *d = &req[12];
         for (int i = 0; i < dlen && s->foe_len < FOE_CAP; i++)
            s->foe_buf[s->foe_len++] = d[i];
      }
   } else {
      if (g_verbose) printf("[sim] FoE unexpected opcode %u\n", op);
      return;
   }

   /* Acknowledge: FoE ACK echoing the packet number the master expects. */
   memset(r, 0, MBX_LEN);
   r[0] = 0x06; r[1] = 0x00;               /* FoE ack payload length = 6 */
   r[5] = MBXT_FOE;                        /* mailbox type = FoE          */
   r[6] = FOE_OP_ACK;
   r[7] = 0x00;
   wr32(&r[8], pkt);
   s->mbx_ready = 1;
   if (g_verbose)
      printf("[sim] FoE %s pkt %u -> ack (%d bytes total)\n",
             op == FOE_OP_WRITE ? "WRITE" : "DATA", pkt, s->foe_len);
}

/* ----------------------------------------------------------------------- */
/* Register read / write                                                   */
/* ----------------------------------------------------------------------- */
static void reg_read(slave_t *s, uint16_t ado, uint8_t *data, int len)
{
   memset(data, 0, len);

   if (ado == REG_TYPE) {
      if (len >= 1) data[0] = 0x02;
   } else if (ado == REG_ESCSUP) {
      /* 0 -> no distributed clock support -> master skips DC */
   } else if (ado == REG_STADR) {
      if (len >= 2) wr16(data, s->station);
   } else if (ado == REG_DLSTAT) {
      /* Report a line topology: every slave but the last has port0 (upstream)
       * and port1 (downstream) open; the last slave is end-of-line (port0 only).
       * Bits per port: 0x0200 = port0 open+comm, 0x0800 = port1 open+comm. */
      if (len >= 2) {
         int idx = (int)(s - g_slave);
         uint16_t dl = 0x0200;                    /* port0 open, end of line */
         if (idx >= 0 && idx < g_nslaves - 1) dl |= 0x0800; /* + port1 downstream */
         wr16(data, dl);
      }
   } else if (ado == REG_ALSTAT) {
      if (len >= 2) wr16(data, s->al_state);
   } else if (ado == REG_ALSTATCODE) {
      /* 0 -> no error */
   } else if (ado == REG_PDICTL) {
      if (len >= 2) wr16(data, 0x0008);
   } else if (ado == 0x0502) {
      /* EEPROM control/status: 0 -> not busy, no error, 4-byte read mode */
   } else if (ado == REG_EEPDAT) {
      int off = s->eep_addr * 2;
      for (int i = 0; i < len; i++)
         data[i] = ((off + i) < SII_SIZE) ? s->sii[off + i] : 0;
   } else if (ado == REG_SM0STAT) {
      if (len >= 1) data[0] = 0x00;              /* mailbox-out empty */
   } else if (ado == REG_SM1STAT) {
      if (len >= 1) data[0] = (uint8_t)(s->mbx_ready ? 0x08 : 0x00);
   } else if (ado >= 0x0300 && ado < 0x0314) {
      int off = ado - 0x0300;                    /* ESC error-counter block */
      for (int i = 0; i < len; i++)
         data[i] = ((off + i) < 0x14) ? s->err_ctr[off + i] : 0;
   } else if (ado == REG_DCSYSDIFF) {
      for (int i = 0; i < len && i < 4; i++)      /* DC System Time Difference */
         data[i] = s->dc_diff[i];
   } else if (ado >= MBX_IN_ADDR && ado < (MBX_IN_ADDR + MBX_LEN)) {
      int off = ado - MBX_IN_ADDR;
      for (int i = 0; i < len; i++)
         data[i] = ((off + i) < MBX_LEN) ? s->mbx_resp[off + i] : 0;
      s->mbx_ready = 0;
      /* One-shot: neutralize the CANOpen service so SOEM's mailbox-receive loop
       * (which re-reads until it sees a non-emergency frame) terminates instead
       * of re-parsing the same emergency over and over. */
      wr16(&s->mbx_resp[6], (uint16_t)(0x03 << 12));
   } else if (g_verbose) {
      printf("[sim] unhandled REG read 0x%04X len %d\n", ado, len);
   }
}

static void reg_write(slave_t *s, uint16_t ado, const uint8_t *data, int len)
{
   if (ado == REG_STADR) {
      if (len >= 2) s->station = rd16(data);
   } else if (ado == REG_ALCTL) {
      if (len >= 1) s->al_state = (uint16_t)(data[0] & 0x0f);
   } else if (ado == REG_EEPCFG) {
      /* ignore EEPROM assign/config */
   } else if (ado == REG_EEPCTL) {
      if (len >= 4) s->eep_addr = rd16(&data[2]);
   } else if (ado >= REG_FMMU0 && ado < (REG_FMMU0 + 16 * 16)) {
      int idx = (ado - REG_FMMU0) / 16;
      if (idx >= 0 && idx < 16 && len >= 13) {
         s->fmmu[idx].logstart  = rd32(&data[0]);
         s->fmmu[idx].loglen    = rd16(&data[4]);
         s->fmmu[idx].physstart = rd16(&data[8]);
         s->fmmu[idx].type      = data[11];
         s->fmmu[idx].active    = data[12];
         if (g_verbose)
            printf("[sim] st%u FMMU%d log 0x%X len %u phys 0x%X type %u act %u\n",
                   s->station, idx, s->fmmu[idx].logstart, s->fmmu[idx].loglen,
                   s->fmmu[idx].physstart, s->fmmu[idx].type, s->fmmu[idx].active);
      }
   } else if (ado >= REG_SM0 && ado < (REG_SM0 + 8 * 8)) {
      /* SyncManager configuration: accept, no action */
   } else if (ado >= MBX_OUT_ADDR && ado < (MBX_OUT_ADDR + MBX_LEN)) {
      uint8_t mbxtype = (len > 5) ? (uint8_t)(data[5] & 0x0f) : 0;
      if (mbxtype == MBXT_FOE) foe_handle(s, data);
      else                     coe_handle(s, data);
   } else if (g_verbose) {
      printf("[sim] unhandled REG write 0x%04X len %d\n", ado, len);
   }
}

/* ----------------------------------------------------------------------- */
/* Cyclic process data echo (CiA402-ish)                                   */
/* ----------------------------------------------------------------------- */
/* Both the RxPDO and TxPDO start with the (control|status)word, so mirror the
 * output payload after the controlword into the input payload after the
 * statusword. This yields lifelike feedback for any cyclic mode / mapping. */
static void update_process_data(slave_t *s)
{
   int oi = -1, ii = -1;
   for (int i = 0; i < 16; i++) {
      if (!s->fmmu[i].active || !s->fmmu[i].loglen) continue;
      if (s->fmmu[i].type == 2 && oi < 0) oi = i;
      if (s->fmmu[i].type == 1 && ii < 0) ii = i;
   }
   if (ii < 0) return;

   uint16_t istart = s->fmmu[ii].physstart;
   wr16(&s->pd[istart], 0x0637);                 /* "Operation enabled" */

   if (oi < 0) return;
   if (s->freeze) return;          /* feedback stalls: keep last mirrored values */
   uint16_t ostart = s->fmmu[oi].physstart;
   int olen = s->fmmu[oi].loglen;
   int ilen = s->fmmu[ii].loglen;
   int n = (olen < ilen ? olen : ilen) - 2;
   for (int i = 0; i < n; i++)
      s->pd[istart + 2 + i] = s->pd[ostart + 2 + i];
}

/* ----------------------------------------------------------------------- */
/* Logical (FMMU mapped) read / write                                      */
/* ----------------------------------------------------------------------- */
static int fmmu_copy(slave_t *s, int cmd, uint32_t la, uint8_t *data, int len)
{
   int wkc = 0;
   int do_out = (cmd == CMD_LRW || cmd == CMD_LWR);
   int do_in  = (cmd == CMD_LRW || cmd == CMD_LRD);

   if (do_out) {
      for (int i = 0; i < 16; i++) {
         fmmu_t *f = &s->fmmu[i];
         if (!f->active || f->type != 2 || !f->loglen) continue;
         uint32_t fs = f->logstart, fe = fs + f->loglen;
         uint32_t os = (la > fs) ? la : fs;
         uint32_t oe = ((la + len) < fe) ? (la + len) : fe;
         if (oe <= os) continue;
         memcpy(&s->pd[f->physstart + (os - fs)], data + (os - la), oe - os);
         wkc += 2;
      }
   }

   update_process_data(s);

   if (do_in) {
      for (int i = 0; i < 16; i++) {
         fmmu_t *f = &s->fmmu[i];
         if (!f->active || f->type != 1 || !f->loglen) continue;
         uint32_t fs = f->logstart, fe = fs + f->loglen;
         uint32_t os = (la > fs) ? la : fs;
         uint32_t oe = ((la + len) < fe) ? (la + len) : fe;
         if (oe <= os) continue;
         memcpy(data + (os - la), &s->pd[f->physstart + (os - fs)], oe - os);
         if (!s->wkc_drop) wkc += 1;
      }
   }
   return wkc;
}

/* ----------------------------------------------------------------------- */
/* One datagram applied to one slave (in place)                            */
/* ----------------------------------------------------------------------- */
static void dgram_on_slave(slave_t *s, uint8_t cmd, uint8_t *d, int dlen)
{
   uint16_t adp  = rd16(&d[2]);
   uint16_t ado  = rd16(&d[4]);
   uint8_t *data = &d[10];
   uint8_t *wkcp = &d[10 + dlen];
   int inc = 0;

   switch (cmd) {
   case CMD_NOP:
      break;
   case CMD_APRD: case CMD_ARMW:
      if (adp == 0) { reg_read(s, ado, data, dlen); inc = 1; }
      wr16(&d[2], (uint16_t)(adp + 1));
      break;
   case CMD_APWR:
      if (adp == 0) { reg_write(s, ado, data, dlen); inc = 1; }
      wr16(&d[2], (uint16_t)(adp + 1));
      break;
   case CMD_APRW:
      if (adp == 0) { reg_write(s, ado, data, dlen); reg_read(s, ado, data, dlen); inc = 3; }
      wr16(&d[2], (uint16_t)(adp + 1));
      break;
   case CMD_FPRD: case CMD_FRMW:
      if (adp == s->station) { reg_read(s, ado, data, dlen); inc = 1; }
      break;
   case CMD_FPWR:
      if (adp == s->station) { reg_write(s, ado, data, dlen); inc = 1; }
      break;
   case CMD_FPRW:
      if (adp == s->station) { reg_write(s, ado, data, dlen); reg_read(s, ado, data, dlen); inc = 3; }
      break;
   case CMD_BRD:
      reg_read(s, ado, data, dlen); inc = 1;
      wr16(&d[2], (uint16_t)(adp + 1));
      break;
   case CMD_BWR:
      reg_write(s, ado, data, dlen); inc = 1;
      wr16(&d[2], (uint16_t)(adp + 1));
      break;
   case CMD_BRW:
      reg_write(s, ado, data, dlen); reg_read(s, ado, data, dlen); inc = 3;
      wr16(&d[2], (uint16_t)(adp + 1));
      break;
   case CMD_LRD: case CMD_LWR: case CMD_LRW: {
      uint32_t la = (uint32_t)adp | ((uint32_t)ado << 16);
      inc = fmmu_copy(s, cmd, la, data, dlen);
      break;
   }
   default:
      if (g_verbose) printf("[sim] unhandled cmd %u\n", cmd);
      break;
   }

   if (inc) {
      uint16_t cur = rd16(wkcp);
      wr16(wkcp, (uint16_t)(cur + inc));
   }
}

/* ----------------------------------------------------------------------- */
/* Public API                                                              */
/* ----------------------------------------------------------------------- */
void slavesim_set_verbose(int on) { g_verbose = on; }

void slavesim_set_freeze(int pos, int on)
{
   if (pos <= 0) { for (int i = 0; i < g_nslaves; i++) g_slave[i].freeze = on ? 1 : 0; }
   else if (pos <= g_nslaves) g_slave[pos - 1].freeze = on ? 1 : 0;
}

void slavesim_set_wkc_drop(int pos, int on)
{
   if (pos <= 0) { for (int i = 0; i < g_nslaves; i++) g_slave[i].wkc_drop = on ? 1 : 0; }
   else if (pos <= g_nslaves) g_slave[pos - 1].wkc_drop = on ? 1 : 0;
}

void slavesim_set_rxcrc(int pos, int port, uint8_t count)
{
   int off;
   if (port < 0 || port > 3) return;
   off = 0x01 + port * 2;               /* 0x0301+2p = RX/CRC error byte */
   if (pos <= 0) { for (int i = 0; i < g_nslaves; i++) g_slave[i].err_ctr[off] = count; }
   else if (pos <= g_nslaves) g_slave[pos - 1].err_ctr[off] = count;
}

/* Build a CoE Emergency mailbox message in the slave's SM1 response buffer.
 * ecx_mbxreceive() parses this (CoE type 0x03, CANOpen service nibble 0x01)
 * and pushes an EC_ERR_TYPE_EMERGENCY onto SOEM's error list. */
static void queue_emergency(slave_t *s, uint16_t error_code, uint8_t error_reg)
{
   uint8_t *r = s->mbx_resp;
   memset(r, 0, MBX_LEN);
   r[0] = 0x0a; r[1] = 0x00;                  /* mailbox payload length = 10 */
   r[5] = 0x03;                               /* mailbox type = CoE          */
   wr16(&r[6], (uint16_t)(0x01 << 12));       /* CoE service = Emergency (1) */
   wr16(&r[8], error_code);                   /* emergency error code        */
   r[10] = error_reg;                         /* error register (0x1001)     */
   s->mbx_ready = 1;
}

void slavesim_queue_emergency(int pos, uint16_t error_code, uint8_t error_reg)
{
   if (pos <= 0) { for (int i = 0; i < g_nslaves; i++) queue_emergency(&g_slave[i], error_code, error_reg); }
   else if (pos <= g_nslaves) queue_emergency(&g_slave[pos - 1], error_code, error_reg);
}

/* Store a signed-magnitude DC deviation into the 0x092C register image. */
static void set_dcdiff(slave_t *s, uint32_t ns, int ahead)
{
   uint32_t raw = (ns & 0x7FFFFFFFu) | (ahead ? 0u : 0x80000000u);
   wr32(s->dc_diff, raw);
}

void slavesim_set_dcdiff(int pos, uint32_t ns, int ahead)
{
   if (pos <= 0) { for (int i = 0; i < g_nslaves; i++) set_dcdiff(&g_slave[i], ns, ahead); }
   else if (pos <= g_nslaves) set_dcdiff(&g_slave[pos - 1], ns, ahead);
}

int slavesim_foe_received(int pos, uint8_t *buf, int cap)
{
   slave_t *s;
   int n;
   if (pos < 1 || pos > g_nslaves) return 0;
   s = &g_slave[pos - 1];
   if (buf && cap > 0) {
      n = (s->foe_len < cap) ? s->foe_len : cap;
      memcpy(buf, s->foe_buf, n);
   }
   return s->foe_len;
}

void slavesim_init_n(int n)
{
   if (n < 1) n = 1;
   if (n > SLAVESIM_MAX_SLAVES) n = SLAVESIM_MAX_SLAVES;
   g_nslaves = n;
   memset(g_slave, 0, sizeof(g_slave));
   for (int i = 0; i < g_nslaves; i++) {
      g_slave[i].station  = 0;
      g_slave[i].al_state = 0x0001;   /* INIT */
      build_sii(&g_slave[i]);
      seed_od(&g_slave[i]);
   }
   g_configured = 1;
}

void slavesim_init(void) { slavesim_init_n(1); }

void slavesim_ensure_default(void)
{
   if (!g_configured) slavesim_init_n(1);
}

int slavesim_process(uint8_t *ecat, int len)
{
   if (len < 4) return 0;

   int pos = 2;
   int guard = 0;

   while (pos + 10 <= len && guard++ < 128) {
      uint8_t  cmd  = ecat[pos];
      uint16_t dfld = rd16(&ecat[pos + 6]);
      int      dlen = dfld & 0x07ff;
      int      more = dfld & 0x8000;

      if (pos + 10 + dlen + 2 > len) break;

      /* Pass this datagram through every slave in bus order. */
      for (int si = 0; si < g_nslaves; si++)
         dgram_on_slave(&g_slave[si], cmd, &ecat[pos], dlen);

      pos += 10 + dlen + 2;
      if (!more) break;
   }
   return 0;
}
