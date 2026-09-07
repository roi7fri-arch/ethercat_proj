/** \file
 * \brief Simulated NIC driver: a drop-in replacement for SOEM 1.3.1
 *        oshw/linux/nicdrv.c that loops every transmitted frame through the
 *        in-process virtual slave (slave_sim.c) instead of a real socket.
 *
 * It exposes exactly the same public symbols as the real nicdrv.c (both the
 * ecx_* context API and the EC_VER1 ec_* wrappers, plus priMAC/secMAC), so the
 * rest of SOEM links and runs unchanged. Build this file INSTEAD of nicdrv.c.
 */
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "oshw.h"
#include "osal.h"
#include "slave_sim.h"

/** Redundancy modes (match nicdrv.c) */
enum { ECT_RED_NONE, ECT_RED_DOUBLE };

/* Fake MAC addresses; EtherCAT ignores them but SOEM references the symbols. */
const uint16 priMAC[3] = { 0x0101, 0x0101, 0x0101 };
const uint16 secMAC[3] = { 0x0404, 0x0404, 0x0404 };

static void ecx_clear_rxbufstat(int *rxbufstat)
{
   int i;
   for (i = 0; i < EC_MAXBUF; i++) rxbufstat[i] = EC_BUF_EMPTY;
}

/** Fill buffer with a (dummy) ethernet header. */
void ec_setupheader(void *p)
{
   ec_etherheadert *bp = p;
   bp->da0 = htons(0xffff);
   bp->da1 = htons(0xffff);
   bp->da2 = htons(0xffff);
   bp->sa0 = htons(priMAC[0]);
   bp->sa1 = htons(priMAC[1]);
   bp->sa2 = htons(priMAC[2]);
   bp->etype = htons(ETH_P_ECAT);
}

int ecx_setupnic(ecx_portt *port, const char *ifname, int secondary)
{
   int i;
   (void)ifname;

   if (secondary)
   {
      /* redundant mode is not emulated */
      return 0;
   }

   pthread_mutex_init(&(port->getindex_mutex), NULL);
   pthread_mutex_init(&(port->tx_mutex), NULL);
   pthread_mutex_init(&(port->rx_mutex), NULL);
   port->sockhandle = -1;
   port->lastidx    = 0;
   port->redstate   = ECT_RED_NONE;
   port->stack.sock        = &(port->sockhandle);
   port->stack.txbuf       = &(port->txbuf);
   port->stack.txbuflength = &(port->txbuflength);
   port->stack.tempbuf     = &(port->tempinbuf);
   port->stack.rxbuf       = &(port->rxbuf);
   port->stack.rxbufstat   = &(port->rxbufstat);
   port->stack.rxsa        = &(port->rxsa);
   ecx_clear_rxbufstat(&(port->rxbufstat[0]));

   for (i = 0; i < EC_MAXBUF; i++)
   {
      ec_setupheader(&(port->txbuf[i]));
      port->rxbufstat[i] = EC_BUF_EMPTY;
   }
   ec_setupheader(&(port->txbuf2));

   /* power-on the virtual slave(s) unless the test already built the bus */
   slavesim_ensure_default();

   return 1;
}

int ecx_closenic(ecx_portt *port)
{
   (void)port;
   return 0;
}

/** Get new frame identifier index and mark its rx buffer as allocated. */
int ecx_getindex(ecx_portt *port)
{
   int idx, cnt;

   pthread_mutex_lock(&(port->getindex_mutex));

   idx = port->lastidx + 1;
   if (idx >= EC_MAXBUF) idx = 0;
   cnt = 0;
   while ((port->rxbufstat[idx] != EC_BUF_EMPTY) && (cnt < EC_MAXBUF))
   {
      idx++;
      cnt++;
      if (idx >= EC_MAXBUF) idx = 0;
   }
   port->rxbufstat[idx] = EC_BUF_ALLOC;
   port->lastidx = idx;

   pthread_mutex_unlock(&(port->getindex_mutex));
   return idx;
}

void ecx_setbufstat(ecx_portt *port, int idx, int bufstat)
{
   port->rxbufstat[idx] = bufstat;
}

/** "Transmit": hand the frame to the virtual slave and stash the processed
 * result (WKC filled in) in the rx buffer for ecx_inframe to pick up. */
int ecx_outframe(ecx_portt *port, int idx, int stacknumber)
{
   int lp, paylen;
   (void)stacknumber;

   lp = port->txbuflength[idx];
   paylen = lp - ETH_HEADERSIZE;
   if (paylen < 0) paylen = 0;

   memcpy(port->rxbuf[idx], &(port->txbuf[idx][ETH_HEADERSIZE]), paylen);
   slavesim_process(port->rxbuf[idx], paylen);
   port->rxbufstat[idx] = EC_BUF_RCVD;

   return lp;
}

int ecx_outframe_red(ecx_portt *port, int idx)
{
   return ecx_outframe(port, idx, 0);
}

/** "Receive": the processed frame is already waiting in the rx buffer. */
int ecx_inframe(ecx_portt *port, int idx, int stacknumber)
{
   uint16 l;
   int rval = EC_NOFRAME;
   uint8 *rxbuf;
   (void)stacknumber;

   if ((idx < EC_MAXBUF) && (port->rxbufstat[idx] == EC_BUF_RCVD))
   {
      rxbuf = &(port->rxbuf[idx][0]);
      l = (uint16)(rxbuf[0] + ((uint16)(rxbuf[1] & 0x0f) << 8));
      rval = (int)(rxbuf[l] + ((uint16)rxbuf[l + 1] << 8));
      port->rxbufstat[idx] = EC_BUF_COMPLETE;
   }
   return rval;
}

int ecx_waitinframe(ecx_portt *port, int idx, int timeout)
{
   int wkc;
   (void)timeout;
   wkc = ecx_inframe(port, idx, 0);
   if (wkc <= EC_NOFRAME) ecx_setbufstat(port, idx, EC_BUF_EMPTY);
   return wkc;
}

int ecx_srconfirm(ecx_portt *port, int idx, int timeout)
{
   int wkc;
   (void)timeout;
   ecx_outframe_red(port, idx);
   wkc = ecx_inframe(port, idx, 0);
   if (wkc <= EC_NOFRAME) ecx_setbufstat(port, idx, EC_BUF_EMPTY);
   return wkc;
}

#ifdef EC_VER1
int ec_setupnic(const char *ifname, int secondary) { return ecx_setupnic(&ecx_port, ifname, secondary); }
int ec_closenic(void)                              { return ecx_closenic(&ecx_port); }
int ec_getindex(void)                              { return ecx_getindex(&ecx_port); }
void ec_setbufstat(int idx, int bufstat)           { ecx_setbufstat(&ecx_port, idx, bufstat); }
int ec_outframe(int idx, int stacknumber)          { return ecx_outframe(&ecx_port, idx, stacknumber); }
int ec_outframe_red(int idx)                       { return ecx_outframe_red(&ecx_port, idx); }
int ec_inframe(int idx, int stacknumber)           { return ecx_inframe(&ecx_port, idx, stacknumber); }
int ec_waitinframe(int idx, int timeout)           { return ecx_waitinframe(&ecx_port, idx, timeout); }
int ec_srconfirm(int idx, int timeout)             { return ecx_srconfirm(&ecx_port, idx, timeout); }
#endif
