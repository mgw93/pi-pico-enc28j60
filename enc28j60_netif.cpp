/**
 * @addtogroup enc28j60 ENC28J60 driver
 * @{
 * @addtogroup enc28j60-impl ENC28J60 driver implementation
 * @{
 *
 * .
 *
 * Documentation references in here are relative to the ENC28J60 Data Sheet
 * DS39662D
 * */

#include <cstdint>
#include <cstdio>

#include "enc28j60_netif.hpp"
#include "lwip/etharp.h"
#if  LWIP_IPV6
#include "lwip/ethip6.h"
#endif


//#define DEBUG(...) printf(__VA_ARGS__)

#define DEBUG(...) LWIP_DEBUGF(NETIF_DEBUG, (__VA_ARGS__))

#ifndef DEBUG
#error "Please provide a DEBUG(...) macro that behaves like a printf."
#endif



/** Like enc_transmit, but read from a pbuf. This is not a trivial wrapper
 * around enc_transmit as the pbuf is not guaranteed to have a contiguous
 * memory region to be transmitted. */
void enc28j60_netif::enc_transmit_pbuf(const struct pbuf *buf)
{
	uint16_t length = buf->tot_len;

	/** @todo check buffer size */
	transmit_start();
	while(1) {
		transmit_partial(static_cast<uint8_t*>(buf->payload), buf->len);
		if (buf->len == buf->tot_len)
			break;
		buf = buf->next;
	}
	transmit_end(length);
}



/** Like enc_read_received, but allocate a pbuf buf. Returns 0 on success, or
 * unspecified non-zero values on errors. */
int enc28j60_netif::enc_read_received_pbuf(struct pbuf **buf)
{
	uint8_t header[6];
	uint16_t length;

	if (*buf != NULL)
		return 1;

	receive_start(header, &length);
	if (length < 4) {
		/* This could be indicative of a crashed (brown-outed?) ENC28J60 controller */
		DEBUG("Empty frame (length %u)\n", length);
		goto end;
	}
	length -= 4; /* Drop the 4 byte CRC from length */

	/* workaround for https://savannah.nongnu.org/bugs/index.php?50040 */
	if (length > 32000) {
		DEBUG("Huge frame received or underflow (framelength %u)\n", length);
		goto end;
	}

	*buf = pbuf_alloc(PBUF_RAW, length, PBUF_RAM);

	if (*buf == NULL) {
		DEBUG("failed to allocate buf of length %u, discarding\n", length);
		goto end;
	}

	enc_RBM(static_cast<uint8_t*>((*buf)->payload), ENC_READLOCATION_ANY, length);

end:
	receive_end(header);

	return (*buf == NULL) ? 2 : 0;
}

err_t enc28j60_netif::netif_init(struct netif *netif){
   enc28j60 &dev=*static_cast<enc28j60*>(netif->state);
   int result;
   LWIP_DEBUGF(NETIF_DEBUG, ("Starting mchdrv_init.\n"));
   result=dev.enc_setup_basic();
   if (result != 0) {
      LWIP_DEBUGF(NETIF_DEBUG, ("Error %d in enc_setup, interface setup aborted.\n", result));
      return ERR_IF;
   }
   result = dev.enc_bist_manual();
   if(result!=0){
      LWIP_DEBUGF(NETIF_DEBUG, ("Error %d in enc_bist_manual, interface setup aborted.\n", result));
      return ERR_IF;
   }
   dev.enc_ethernet_setup(4*1024,netif->hwaddr);
   dev.enc_set_multicast_reception(true);
   netif->output=etharp_output;
   #if LWIP_IPV6
   netif->output_ip6 = &ethip6_output;
   #endif
   netif->linkoutput=&linkoutput;
   netif->mtu = 1500;
   netif->flags|=NETIF_FLAG_ETHARP | NETIF_FLAG_BROADCAST;
   LWIP_DEBUGF(NETIF_DEBUG, ("Driver initialized.\n"));
   return ERR_OK;
}

err_t enc28j60_netif::linkoutput(struct netif *netif, struct pbuf *p){
   enc28j60_netif &dev=*static_cast<enc28j60_netif*>(netif->state);
   dev.enc_transmit_pbuf(p);
   LWIP_DEBUGF(NETIF_DEBUG, ("sent %d bytes.\n", p->tot_len));
   // TODO: Evaluate result
   return ERR_OK;
}

void enc28j60_netif::poll(){
   err_t result;
   struct pbuf *buf = NULL;

   uint8_t epktcnt;
   bool linkstate=this->linkstate();
   //DEBUG("Linkstate: %hhd\n",linkstate);

   //if (linkstate) netif_set_link_up(netif);
   //else netif_set_link_down(netif);

   epktcnt = enc_RCR(ENC_EPKTCNT);

   if (epktcnt) {
      if (enc_read_received_pbuf(&buf) == 0)
      {
         LWIP_DEBUGF(NETIF_DEBUG, ("incoming: %d packages, first read into %x\n", epktcnt, (unsigned int)(buf)));
         result = this->input(buf, this);
         LWIP_DEBUGF(NETIF_DEBUG, ("received with result %d\n", result));
      } else {
         /* FIXME: error reporting */
         LWIP_DEBUGF(NETIF_DEBUG, ("didn't receive.\n"));
      }
   }

}


