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

#include "enc28j60.hpp"
#include "enchw.h"
#include "lwip/etharp.h"
#if  LWIP_IPV6
#include "lwip/ethip6.h"
#endif

//#define DEBUG(...) printf(__VA_ARGS__)

#define DEBUG(...) LWIP_DEBUGF(NETIF_DEBUG, (__VA_ARGS__))

#ifndef DEBUG
#error "Please provide a DEBUG(...) macro that behaves like a printf."
#endif


/** Initialize an ENC28J60 device. Returns 0 on success, or an unspecified
 * error code if something goes wrong.
 *
 * This function needs to be called first whenever the MCU or the network
 * device is powered up. It will not configure transmission or reception; use
 * @ref enc_ethernet_setup for that, possibly after having run self tests.
 * */
int enc28j60::enc_setup_basic()
{
        hwdev.setup();

	uint8_t revid = enc_RCR(ENC_EREVID);
        DEBUG("Revision: %hhd\n",revid);
	if (enc_wait()){
           DEBUG("ENC Timeout");
           return 1;
        }

	this->last_used_register = ENC_BANK_INDETERMINATE;
	this->rxbufsize = ~0;

	//uint8_t revid = enc_RCR(ENC_EREVID);
	if (revid != ENC_EREVID_B1 && revid != ENC_EREVID_B4 &&
			revid != ENC_EREVID_B5 && revid != ENC_EREVID_B7)
		return 1;

	enc_BFS(ENC_ECON2, ENC_ECON2_AUTOINC);

	return 0;
}

void enc28j60::set_erxnd(uint16_t erxnd)
{
	if (erxnd != this->rxbufsize) {
		this->rxbufsize = erxnd;
		enc_WCR16(ENC_ERXNDL, erxnd);
	}
}

/** Run the built-in diagnostics. Returns 0 on success or an unspecified
 * error code.
 *
 * @todo doesn't produce correct results (waits indefinitely on DMA, doesn't
 * alter pattern, addressfill produces correct checksum but wrong data (0xff
 * everywhere)
 * */
uint8_t enc28j60::enc_bist()
{
	/* according to 15.1 */
	/* 1. */
	enc_WCR16(ENC_EDMASTL, 0);
	/* 2. */
	enc_WCR16(ENC_EDMANDL, 0x1fff);
	set_erxnd(0x1fff);
	/* 3. */
	enc_BFS(ENC_ECON1, ENC_ECON1_CSUMEN);
	/* 4. */
	enc_WCR(ENC_EBSTSD, 0x0c);

	/* 5. */
	enc_WCR(ENC_EBSTCON, ENC_EBSTCON_PATTERNSHIFTFILL | (1 << 5) | ENC_EBSTCON_TME);
//	enc_WCR(ENC_EBSTCON, ENC_EBSTCON_ADDRESSFILL | ENC_EBSTCON_PSEL | ENC_EBSTCON_TME);
	/* 6. */
	enc_BFS(ENC_EBSTCON, ENC_EBSTCON_BISTST);
	/* wait a second -- never took any time yet */
	while(enc_RCR(ENC_EBSTCON) & ENC_EBSTCON_BISTST)
		DEBUG("(%02x)", enc_RCR(ENC_EBSTCON));
	/* 7. */
	enc_BFS(ENC_ECON1, ENC_ECON1_DMAST);
	/* 8. */
	while(enc_RCR(ENC_ECON1) & ENC_ECON1_DMAST)
		DEBUG("[%02x]", enc_RCR(ENC_ECON1));

	/* 9.: @todo pull this in */

	return 0;
}

/* Similar check to enc_bist, but doesn't rely on the BIST of the chip but
 * doesn some own reading and writing */
uint8_t enc28j60::enc_bist_manual()
{
	uint16_t address;
	uint8_t buffer[256];
	int i;

	set_erxnd(ENC_RAMSIZE-1);

	for (address = 0; address < ENC_RAMSIZE; address += 256)
	{
		for (i = 0; i < 256; ++i)
			buffer[i] = ((address >> 8) + i) % 256;

		enc_WBM(buffer, address, 256);
	}

	for (address = 0; address < ENC_RAMSIZE; address += 256)
	{
		enc_RBM(buffer, address, 256);

		for (i = 0; i < 256; ++i)
			if (buffer[i] != ((address >> 8) + i) % 256)
				return 1;
	}

	/* dma checksum */

	/* we don't use dma at all, so we can just as well not test it.
	enc_WCR16(ENC_EDMASTL, 0);
	enc_WCR16(ENC_EDMANDL, ENC_RAMSIZE-1);

	enc_BFS(ENC_ECON1, ENC_ECON1_CSUMEN | ENC_ECON1_DMAST);

	while (enc_RCR(ENC_ECON1) & ENC_ECON1_DMAST) DEBUG(".");

	DEBUG("csum %08x", enc_RCR16(ENC_EDMACSL));
	*/

	return 0;
}

uint8_t enc28j60::command(uint8_t first, uint8_t second)
{
	uint8_t result;
	hwdev.select();
	hwdev.exchangebyte(first);
	result = hwdev.exchangebyte(second);
	hwdev.unselect();
	return result;
}

/* this would recurse infinitely if ENC_ECON1 was not ENC_BANKALL */
void enc28j60::select_page(uint8_t page)
{
	uint8_t set = page & 0x03;
	uint8_t clear = (~page) & 0x03;
	if(set)
		enc_BFS(ENC_ECON1, set);
	if(clear)
		enc_BFC(ENC_ECON1, clear);
}

void enc28j60::ensure_register_accessible(uint8_t r)
{
	if ((r & ENC_BANKMASK) == ENC_BANKALL) return;
	if ((r & ENC_BANKMASK) == this->last_used_register) return;

	select_page(r >> 6);
}

/** @todo applies only to eth registers, not to mii ones */
uint8_t enc28j60::enc_RCR(uint8_t reg) {
	ensure_register_accessible(reg);
	return command(reg & ENC_REGISTERMASK, 0);
}
void enc28j60::enc_WCR(uint8_t reg, uint8_t data) {
	ensure_register_accessible(reg);
	command(0x40 | (reg & ENC_REGISTERMASK), data);
}
void enc28j60::enc_BFS(uint8_t reg, uint8_t data) {
	ensure_register_accessible(reg);
	command(0x80 | (reg & ENC_REGISTERMASK), data);
}
void enc28j60::enc_BFC(uint8_t reg, uint8_t data) {
	ensure_register_accessible(reg);
	command(0xa0 | (reg & ENC_REGISTERMASK), data);
}

void enc28j60::enc_RBM(uint8_t *dest, uint16_t start, uint16_t length)
{
	if (start != ENC_READLOCATION_ANY)
		enc_WCR16(ENC_ERDPTL, start);

	hwdev.select();
	hwdev.exchangebyte(0x3a);
	while(length--)
		*(dest++) = hwdev.exchangebyte(0);
	hwdev.unselect();
}

void enc28j60::WBM_raw(const uint8_t *src, uint16_t length)
{
	hwdev.select();
	hwdev.exchangebyte(0x7a);
	while(length--)
		hwdev.exchangebyte(*(src++));
	hwdev.unselect();
	/** @todo this is actually just triggering another pause */
	hwdev.unselect();
}

void enc28j60::enc_WBM(const uint8_t *src, uint16_t start, uint16_t length)
{
	enc_WCR16(ENC_EWRPTL, start);

	WBM_raw(src, length);
}

/** 16-bit register read. This only applies to ENC28J60 registers whose low
 * byte is at an even offset and whose high byte is one above that. Can be
 * passed either L or H sub-register.
 *
 * @todo could use enc_register16_t
 * */
uint16_t enc28j60::enc_RCR16(enc_register_t reg) {
	return (enc_RCR(reg|1) << 8) | enc_RCR(reg&~1);
}
/** 16-bit register write. Compare enc_RCR16. Writes the lower byte first, then
 * the higher, as required for the MII interfaces as well as for ERXRDPT. */
void enc28j60::enc_WCR16(uint8_t reg, uint16_t data) {
	enc_WCR(reg&~1, data & 0xff); enc_WCR(reg|1, data >> 8);
}

void enc28j60::enc_SRC() {
	hwdev.exchangebyte(0xff);
}

/** Wait for the ENC28J60 clock to be ready. Returns 0 on success,
 * and an unspecified non-zero integer on timeout. */
int enc28j60::enc_wait()
{
	/** @todo as soon as we need a clock somewhere else, make this time and
	 * not iteration based */

	/** It has been observed that during power-up, MISO reads 1
	 * continuously for some time, typically the time of 3 readouts; most
	 * times, this gives 0xff, but occasionally starts with 0x1f or 0x03 or
	 * even the expected (CLKRDY) value of 0x01. Requiring a much larger
	 * number of consecutive identical reads to compensate for faster SPI
	 * configurations. */
	const int stable_required = 100;

	int stable = 0;
	uint8_t estat, estat_last = 0;
	for (int i = 0; i < 100000; ++i) {
		estat = enc_RCR(ENC_ESTAT);
		if (estat != 0){
			DEBUG("At %d, ESTAT is %02x\n", i, estat);
                }
		if (estat == 0xff) /* sometimes happens right at startup */
			continue;

		if (estat == estat_last) stable++;
		estat_last = estat;

		if (stable >= stable_required && estat & ENC_ESTAT_CLKRDY)
			return 0;
	}
	return 1;
}

uint16_t enc28j60::enc_MII_read(enc_phreg_t mireg)
{
	uint16_t result = 0;

	enc_WCR(ENC_MIREGADR, mireg);
	enc_BFS(ENC_MICMD, ENC_MICMD_MIIRD);

	while(enc_RCR(ENC_MISTAT) & ENC_MISTAT_BUSY);

	result = enc_RCR16(ENC_MIRDL);

	enc_BFC(ENC_MICMD, ENC_MICMD_MIIRD);

	return result;
}

void enc28j60::enc_MII_write(uint8_t mireg, uint16_t data)
{
	while(enc_RCR(ENC_MISTAT) & ENC_MISTAT_BUSY);

	enc_WCR(ENC_MIREGADR, mireg);
	enc_WCR16(ENC_MIWRL, data);
}


void enc28j60::enc_LED_set(enc_lcfg_t ledconfig, enc_led_t led)
{
	uint16_t state;
	state = enc_MII_read(ENC_PHLCON);
	state = (state & ~(ENC_LCFG_MASK << led)) | (ledconfig << led);
	enc_MII_write(ENC_PHLCON, state);
}

/** Configure the ENC28J60 for network operation, whose initial parameters get
 * passed as well. */
void enc28j60::enc_ethernet_setup(uint16_t rxbufsize, const uint8_t mac[6])
{
	/* practical consideration: we don't come out of clean reset, better do
	 * this -- discard all previous packages */

	enc_BFS(ENC_ECON1, ENC_ECON1_TXRST | ENC_ECON1_RXRST);
	while(enc_RCR(ENC_EPKTCNT))
	{
		enc_BFS(ENC_ECON2, ENC_ECON2_PKTDEC);
	}
	enc_BFC(ENC_ECON1, ENC_ECON1_TXRST | ENC_ECON1_RXRST); /** @todo this should happen later, but when i don't do it here, things won't come up again. probably a problem in the startup sequence. */

	/********* receive buffer setup according to 6.1 ********/

	enc_WCR16(ENC_ERXSTL, 0); /* see errata, must be 0 */
	set_erxnd(rxbufsize);
	enc_WCR16(ENC_ERXRDPTL, 0);

	this->next_frame_location = 0;

	/******** for the moment, the receive filters are good as they are (6.3) ******/

	/******** waiting for ost (6.4) already happened in _setup ******/

	/******** mac initialization acording to 6.5 ************/

	/* enable reception and flow control (shouldn't hurt in simplex either) */
	enc_BFS(ENC_MACON1, ENC_MACON1_MARXEN | ENC_MACON1_TXPAUS | ENC_MACON1_RXPAUS);

	/* generate checksums for outgoing frames and manage padding automatically */
	enc_WCR(ENC_MACON3, ENC_MACON3_TXCRCEN | ENC_MACON3_FULLPADDING | ENC_MACON3_FRMLEN);

	/* setting defer is mandatory for 802.3, but it seems the default is reasonable too */

	/* MAMXF has reasonable default */

	/* it's not documented in detail what these do, just how to program them */
	enc_WCR(ENC_MAIPGL, 0x12);
	enc_WCR(ENC_MAIPGH, 0x0C);

	/* MACLCON registers have reasonable defaults */

	/* set the mac address */
	enc_WCR(ENC_MAADR1, mac[0]);
	enc_WCR(ENC_MAADR2, mac[1]);
	enc_WCR(ENC_MAADR3, mac[2]);
	enc_WCR(ENC_MAADR4, mac[3]);
	enc_WCR(ENC_MAADR5, mac[4]);
	enc_WCR(ENC_MAADR6, mac[5]);

	/******* mac initialization as per 6.5 ********/

	/* filter out looped packages; otherwise our own ND6 packages are
	 * treated as DAD failures. (i can't think of a reason why one would
	 * not want that; let me know if there is and it culd become configurable) */
	/* set ENC_PHCON2 bit 8 (HDLDIS) */
	enc_MII_write(ENC_PHCON1, 0x0100);

	/*************** enabling reception as per 7.2 ***********/

	/* enable reception */
	enc_BFS(ENC_ECON1, ENC_ECON1_RXEN);

	/* pull transmitter and receiver out of reset */
	enc_BFC(ENC_ECON1, ENC_ECON1_TXRST | ENC_ECON1_RXRST);
}

/** Configure whether multicasts should be received.
 *
 * The more cmplex hash table mechanism that would allow filtering for
 * particular groups is not exposed yet. */
void enc28j60::enc_set_multicast_reception(bool enable)
{
	if (enable)
		enc_BFS(ENC_ERXFCON, 0x2);
	else
		enc_BFC(ENC_ERXFCON, 0x2);
}

uint16_t enc28j60::transmit_start_address() const
{
	uint16_t earliest_start = this->rxbufsize + 1; /* +1 because it's not actually the size but the last byte */

	/* It is recommended that an even address be used for ETXST. */
	return (earliest_start + 1) & ~1;
}

/* Partial function of enc_transmit. Always call this as transmit_start /
 * {transmit_partial * n} / transmit_end -- and use enc_transmit or
 * enc_transmit_pbuf unless you're just implementing those two */
void enc28j60::transmit_start()
{
	/* according to section 7.1 */
	uint8_t control_byte = 0; /* no overrides */

	/* 1. */
	/** @todo we only send a single frame blockingly, starting at the end of rxbuf */
	enc_WCR16(ENC_ETXSTL, transmit_start_address());
	/* 2. */
	enc_WBM(&control_byte, transmit_start_address(), 1);
}

void enc28j60::transmit_partial(const uint8_t *data, uint16_t length)
{
	WBM_raw(data, length);
}

void enc28j60::transmit_end(uint16_t length)
{
	uint8_t result[7];

	/* calculate checksum */

//	enc_WCR16(ENC_EDMASTL, start + 1);
//	enc_WCR16(ENC_EDMANDL, start + 1 + length - 3);
//	enc_BFS(ENC_ECON1, ENC_ECON1_CSUMEN | ENC_ECON1_DMAST);
//	while (enc_RCR(ENC_ECON1) & ENC_ECON1_DMAST);
//	uint16_t checksum = enc_RCR16(ENC_EDMACSL);
//	checksum = ((checksum & 0xff) << 8) | (checksum >> 8);
//	enc_WBM(&checksum, start + 1 + length - 2, 2);

	/* 3. */
	enc_WCR16(ENC_ETXNDL, transmit_start_address() + 1 + length - 1);
	
	/* 4. */
	/* skipped because not using interrupts yet */
	/* 5. */
	enc_BFS(ENC_ECON1, ENC_ECON1_TXRTS);

	/* block */
	for (int i = 0; i < 10000; ++i) {
		if (!(enc_RCR(ENC_ECON1) & ENC_ECON1_TXRTS))
			goto done;
	}
	/* Workaround for 80349c.pdf (errata) #12 and #13: Reset the
	 * transmission logic after an arbitrary timeout.
	 *
	 * This is not a particularly good workaround, neither in terms of
	 * networking behavior (no retransmission is attempted as suggested for
	 * #13) nor in terms of driver (just blocking for some time that is
	 * hopefully long enough but not too long to bother the watchdog), but
	 * it should work.
	 * */
	DEBUG("Econ1 TXRTS did not clear; resetting transmission logic.\n");
	enc_BFS(ENC_ECON1, ENC_ECON1_TXRST);
	enc_BFC(ENC_ECON1, ENC_ECON1_TXRST);

	return;
done:
	enc_RBM(result, transmit_start_address() + length, 7);
	DEBUG("transmitted. %02x %02x %02x %02x %02x %02x %02x\n", result[0], result[1], result[2], result[3], result[4], result[5], result[6]);

	/** @todo parse that and return reasonable state */
}

void enc28j60::enc_transmit(const uint8_t *data, uint16_t length)
{
	/** @todo check buffer size */
	transmit_start();
	transmit_partial(data, length);
	transmit_end(length);
}

/** Like enc_transmit, but read from a pbuf. This is not a trivial wrapper
 * around enc_transmit as the pbuf is not guaranteed to have a contiguous
 * memory region to be transmitted. */
void enc28j60::enc_transmit_pbuf(const struct pbuf *buf)
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

void enc28j60::receive_start(uint8_t header[6], uint16_t *length)
{
	enc_RBM(header, this->next_frame_location, 6);
	*length = header[2] | ((header[3] & 0x7f) << 8);
}

void enc28j60::receive_end(const uint8_t header[6])
{
	this->next_frame_location = header[0] + (header[1] << 8);

	/* workaround for 80349c.pdf (errata) #14 start.
	 *
	 * originally, this would have been
	 * enc_WCR16(ENC_ERXRDPTL, next_location);
	 * but thus: */
	if (this->next_frame_location == /* enc_RCR16(ENC_ERXSTL) can be simplified because of errata item #5 */ 0)
		enc_WCR16(ENC_ERXRDPTL, enc_RCR16(ENC_ERXNDL));
	else
		enc_WCR16(ENC_ERXRDPTL, this->next_frame_location - 1);
	/* workaround end */

	DEBUG("before %d, ", enc_RCR(ENC_EPKTCNT));
	enc_BFS(ENC_ECON2, ENC_ECON2_PKTDEC);
	DEBUG("after %d.\n", enc_RCR(ENC_EPKTCNT));

	DEBUG("read with header (%02x %02x) %02x %02x %02x %02x.\n", header[1], /* swapped due to endianness -- i want to read 1234 */ header[0], header[2], header[3], header[4], header[5]);
}

/** Read a received frame into data; may only be called when one is
 * available. Writes up to maxlength bytes and returns the total length of the
 * frame. (If the return value is > maxlength, parts of the frame were
 * discarded.) */
uint16_t enc28j60::enc_read_received(uint8_t *data, uint16_t maxlength)
{
	uint8_t header[6];
	uint16_t length;

	receive_start(header, &length);

	if (length > maxlength)
	{
		enc_RBM(data, ENC_READLOCATION_ANY, maxlength);
		DEBUG("discarding some bytes\n");
		/** @todo should that really be accepted at all? */
	} else {
		enc_RBM(data, ENC_READLOCATION_ANY, length);
	}

	receive_end(header);

	return length;
}

/** Like enc_read_received, but allocate a pbuf buf. Returns 0 on success, or
 * unspecified non-zero values on errors. */
int enc28j60::enc_read_received_pbuf(struct pbuf **buf)
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


bool enc28j60::linkstate(){
   return enc_MII_read(ENC_PHSTAT1) & (1<<2);
}

err_t enc28j60::netif_init(struct netif *netif){
   enc28j60 &dev=*static_cast<enc28j60*>(netif->state);
   dev.netif=netif;
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
err_t enc28j60::linkoutput(struct netif *netif, struct pbuf *p){
   enc28j60 &dev=*static_cast<enc28j60*>(netif->state);
   dev.enc_transmit_pbuf(p);
   LWIP_DEBUGF(NETIF_DEBUG, ("sent %d bytes.\n", p->tot_len));
   // TODO: Evaluate result
   return ERR_OK;
}
void enc28j60::poll(){
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
         result = netif->input(buf, netif);
         LWIP_DEBUGF(NETIF_DEBUG, ("received with result %d\n", result));
      } else {
         /* FIXME: error reporting */
         LWIP_DEBUGF(NETIF_DEBUG, ("didn't receive.\n"));
      }
   }

}
