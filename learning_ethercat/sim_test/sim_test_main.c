/** \file
 * \brief Host bring-up test for the virtual EtherCAT slave.
 *
 * Drives the SOEM 1.3.1 master (EC_VER1 global API) through the standard
 * discovery -> PRE_OP -> SAFE_OP -> OP sequence against the in-process
 * virtual Elmo axis, then runs a few cyclic process-data exchanges and
 * checks the Working Counter. No NIC, no root, no real hardware.
 */
#include <stdio.h>
#include <string.h>

#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"
#include "ethercatconfig.h"
#include "ethercatcoe.h"
#include "ethercatprint.h"
#include "slave_sim.h"

static char IOmap[256];

/* CiA402 objects used by the mapping below */
#define OBJ_CONTROLWORD  0x6040  /* RxPDO, 16 bit */
#define OBJ_TARGET_POS   0x607A  /* RxPDO, 32 bit */
#define OBJ_STATUSWORD   0x6041  /* TxPDO, 16 bit */
#define OBJ_ACTUAL_POS   0x6064  /* TxPDO, 32 bit */
#define OBJ_MODE_OP      0x6060  /* mode of operation, 8 bit */

/* PDO mapping value = (index << 16) | (subindex << 8) | bitlength */
#define MAP(idx, sub, bits)  (((uint32)(idx) << 16) | ((uint32)(sub) << 8) | (bits))

/** PO2SOconfig hook: program the RxPDO/TxPDO mapping over CoE (PRE_OP->SAFE_OP). */
static int elmo_setup(uint16 slave)
{
   uint8  u8;
   uint16 u16;
   uint32 u32;

   /* --- RxPDO 0x1600: controlword (16b) + target position (32b) --- */
   u8 = 0;               ec_SDOwrite(slave, 0x1600, 0x00, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
   u32 = MAP(OBJ_CONTROLWORD, 0, 16); ec_SDOwrite(slave, 0x1600, 0x01, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
   u32 = MAP(OBJ_TARGET_POS,  0, 32); ec_SDOwrite(slave, 0x1600, 0x02, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
   u8 = 2;               ec_SDOwrite(slave, 0x1600, 0x00, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);

   /* --- Assign RxPDO 0x1600 to SM2 (0x1C12) --- */
   u8 = 0;               ec_SDOwrite(slave, 0x1C12, 0x00, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
   u16 = 0x1600;         ec_SDOwrite(slave, 0x1C12, 0x01, FALSE, sizeof(u16), &u16, EC_TIMEOUTRXM);
   u8 = 1;               ec_SDOwrite(slave, 0x1C12, 0x00, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);

   /* --- TxPDO 0x1A00: statusword (16b) + actual position (32b) --- */
   u8 = 0;               ec_SDOwrite(slave, 0x1A00, 0x00, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
   u32 = MAP(OBJ_STATUSWORD, 0, 16); ec_SDOwrite(slave, 0x1A00, 0x01, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
   u32 = MAP(OBJ_ACTUAL_POS, 0, 32); ec_SDOwrite(slave, 0x1A00, 0x02, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
   u8 = 2;               ec_SDOwrite(slave, 0x1A00, 0x00, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);

   /* --- Assign TxPDO 0x1A00 to SM3 (0x1C13) --- */
   u8 = 0;               ec_SDOwrite(slave, 0x1C13, 0x00, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
   u16 = 0x1A00;         ec_SDOwrite(slave, 0x1C13, 0x01, FALSE, sizeof(u16), &u16, EC_TIMEOUTRXM);
   u8 = 1;               ec_SDOwrite(slave, 0x1C13, 0x00, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);

   /* --- Cyclic Synchronous Position mode --- */
   u8 = 8;               ec_SDOwrite(slave, OBJ_MODE_OP, 0x00, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);

   printf("[test] PO2SO mapping written for slave %u\n", slave);
   return 1;
}

int main(int argc, char *argv[])
{
   int i, chk, expectedWKC, wkc;

   if (argc > 1 && strcmp(argv[1], "-v") == 0) slavesim_set_verbose(1);

   printf("Virtual EtherCAT slave test (SOEM 1.3.1)\n");

   if (!ec_init("sim"))
   {
      printf("ec_init failed\n");
      return 1;
   }

   if (ec_config_init(FALSE) <= 0)
   {
      printf("no slaves found\n");
      ec_close();
      return 1;
   }

   printf("%d slave(s) found\n", ec_slavecount);
   printf("  slave 1: name='%s' Obits=%d Ibits=%d\n",
          ec_slave[1].name, ec_slave[1].Obits, ec_slave[1].Ibits);

   /* register mapping hook, then map process data (PRE_OP -> SAFE_OP) */
   ec_slave[1].PO2SOconfig = &elmo_setup;
   ec_config_map(&IOmap);

   ec_statecheck(0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);
   printf("state after map: 0x%02x, Obytes=%d Ibytes=%d\n",
          ec_slave[0].state, ec_slave[0].Obytes, ec_slave[0].Ibytes);

   expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC;
   printf("expected WKC = %d\n", expectedWKC);

   /* request OP */
   ec_slave[0].state = EC_STATE_OPERATIONAL;
   ec_send_processdata();
   ec_receive_processdata(EC_TIMEOUTRET);
   ec_writestate(0);

   chk = 40;
   do {
      ec_send_processdata();
      ec_receive_processdata(EC_TIMEOUTRET);
      ec_statecheck(0, EC_STATE_OPERATIONAL, 50000);
   } while (chk-- && (ec_slave[0].state != EC_STATE_OPERATIONAL));

   if (ec_slave[0].state != EC_STATE_OPERATIONAL)
   {
      printf("FAILED to reach OP (state 0x%02x)\n", ec_slave[0].state);
      ec_close();
      return 1;
   }

   printf("*** All slaves reached OPERATIONAL ***\n");

   /* cyclic exchange: command a moving target and observe echoed feedback */
   for (i = 0; i < 5; i++)
   {
      int32 target = 1000 * (i + 1);
      uint16 sw;
      int32 apos;

      /* outputs: controlword @0, target position @2 */
      ec_slave[1].outputs[0] = 0x0F;                 /* controlword low byte */
      ec_slave[1].outputs[1] = 0x00;
      memcpy(&ec_slave[1].outputs[2], &target, 4);

      wkc = ec_send_processdata();
      wkc = ec_receive_processdata(EC_TIMEOUTRET);

      /* inputs: statusword @0, actual position @2 */
      memcpy(&sw,   &ec_slave[1].inputs[0], 2);
      memcpy(&apos, &ec_slave[1].inputs[2], 4);

      printf("cycle %d: wkc=%d (exp %d) target=%d -> status=0x%04x actual=%d %s\n",
             i, wkc, expectedWKC, target, sw, apos,
             (wkc == expectedWKC) ? "OK" : "WKC MISMATCH");
   }

   ec_slave[0].state = EC_STATE_INIT;
   ec_writestate(0);
   ec_close();
   printf("done\n");
   return 0;
}
