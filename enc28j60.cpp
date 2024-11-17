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

//#define DEBUG(...) printf(__VA_ARGS__)
#define DEBUG(...)

//#define DEBUG(...) LWIP_DEBUGF(NETIF_DEBUG, (__VA_ARGS__))

#ifndef DEBUG
#error "Please provide a DEBUG(...) macro that behaves like a printf."
#endif


/** Initialize an ENC28J60 device. Returns 0 on success, or an unspecified
 * error code if something goes wrong.
 *
 * This function needs to be called first whenever the MCU or the network
 * device is powered up. It will not configure transmission or reception; use
 * @ref ethernet_setup for that, possibly after having run self tests.
 * */
int enc28j60::setup_basic()
{
        hwdev.setup();

	uint8_t revid = RCR(ENC_EREVID);
        DEBUG("Revision: %hhd\n",revid);
	if (wait()){
           DEBUG("ENC Timeout");
           return 1;
        }

	this->last_used_register = ENC_BANK_INDETERMINATE;
	this->rxbufsize = ~0;

	//uint8_t revid = RCR(ENC_EREVID);
	if (revid != ENC_EREVID_B1 && revid != ENC_EREVID_B4 &&
			revid != ENC_EREVID_B5 && revid != ENC_EREVID_B7)
		return 1;

	BFS(ENC_ECON2, ENC_ECON2_AUTOINC);

	return 0;
}

void enc28j60::set_erxnd(uint16_t erxnd)
{
	if (erxnd != this->rxbufsize) {
		this->rxbufsize = erxnd;
		WCR16(ENC_ERXNDL, erxnd);
	}
}

/** Run the built-in diagnostics. Returns 0 on success or an unspecified
 * error code.
 *
 * @todo doesn't produce correct results (waits indefinitely on DMA, doesn't
 * alter pattern, addressfill produces correct checksum but wrong data (0xff
 * everywhere)
 * */
uint8_t enc28j60::bist()
{
	/* according to 15.1 */
	/* 1. */
	WCR16(ENC_EDMASTL, 0);
	/* 2. */
	WCR16(ENC_EDMANDL, 0x1fff);
	set_erxnd(0x1fff);
	/* 3. */
	BFS(ENC_ECON1, ENC_ECON1_CSUMEN);
	/* 4. */
	WCR(ENC_EBSTSD, 0x0c);

	/* 5. */
	WCR(ENC_EBSTCON, ENC_EBSTCON_PATTERNSHIFTFILL | (1 << 5) | ENC_EBSTCON_TME);
//	WCR(ENC_EBSTCON, ENC_EBSTCON_ADDRESSFILL | ENC_EBSTCON_PSEL | ENC_EBSTCON_TME);
	/* 6. */
	BFS(ENC_EBSTCON, ENC_EBSTCON_BISTST);
	/* wait a second -- never took any time yet */
	while(RCR(ENC_EBSTCON) & ENC_EBSTCON_BISTST)
		DEBUG("(%02x)", RCR(ENC_EBSTCON));
	/* 7. */
	BFS(ENC_ECON1, ENC_ECON1_DMAST);
	/* 8. */
	while(RCR(ENC_ECON1) & ENC_ECON1_DMAST)
		DEBUG("[%02x]", RCR(ENC_ECON1));

	/* 9.: @todo pull this in */

	return 0;
}

/* Similar check to bist, but doesn't rely on the BIST of the chip but
 * doesn some own reading and writing */
uint8_t enc28j60::bist_manual()
{
	uint16_t address;
	uint8_t buffer[256];
	int i;

	set_erxnd(ENC_RAMSIZE-1);

	for (address = 0; address < ENC_RAMSIZE; address += 256)
	{
		for (i = 0; i < 256; ++i)
			buffer[i] = ((address >> 8) + i) % 256;

		WBM(buffer, address, 256);
	}

	for (address = 0; address < ENC_RAMSIZE; address += 256)
	{
		RBM(buffer, address, 256);

		for (i = 0; i < 256; ++i)
			if (buffer[i] != ((address >> 8) + i) % 256)
				return 1;
	}

	/* dma checksum */

	/* we don't use dma at all, so we can just as well not test it.
	WCR16(ENC_EDMASTL, 0);
	WCR16(ENC_EDMANDL, ENC_RAMSIZE-1);

	BFS(ENC_ECON1, ENC_ECON1_CSUMEN | ENC_ECON1_DMAST);

	while (RCR(ENC_ECON1) & ENC_ECON1_DMAST) DEBUG(".");

	DEBUG("csum %08x", RCR16(ENC_EDMACSL));
	*/

	return 0;
}

uint8_t enc28j60::command(uint8_t first, uint8_t second,bool dummy)
{
	uint8_t result;
	hwdev.select();
	hwdev.exchangebyte(first);
	result = hwdev.exchangebyte(second);
        if(dummy) result=hwdev.exchangebyte(0);
	hwdev.unselect();
	return result;
}

/* this would recurse infinitely if ENC_ECON1 was not ENC_BANKALL */
void enc28j60::select_page(uint8_t page)
{
	uint8_t set = page & 0x03;
	uint8_t clear = (~page) & 0x03;
	if(set)
		BFS(ENC_ECON1, set);
	if(clear)
		BFC(ENC_ECON1, clear);
}

void enc28j60::ensure_register_accessible(uint8_t r)
{
	if ((r & ENC_BANKMASK) == ENC_BANKALL) return;
	if ((r & ENC_BANKMASK) == this->last_used_register) return;

	select_page(r >> 6);
}

/** @todo applies only to eth registers, not to mii ones */
uint8_t enc28j60::RCR(enc_ethreg reg) {
	ensure_register_accessible(reg);
	return command(reg & ENC_REGISTERMASK, 0);
}
uint8_t enc28j60::RCR(enc_reg reg) {
	ensure_register_accessible(reg);
	return command(reg & ENC_REGISTERMASK, 0,true);
}
void enc28j60::WCR(uint8_t reg, uint8_t data) {
	ensure_register_accessible(reg);
	command(0x40 | (reg & ENC_REGISTERMASK), data);
}
void enc28j60::BFS(uint8_t reg, uint8_t data) {
	ensure_register_accessible(reg);
	command(0x80 | (reg & ENC_REGISTERMASK), data);
}
void enc28j60::BFC(uint8_t reg, uint8_t data) {
	ensure_register_accessible(reg);
	command(0xa0 | (reg & ENC_REGISTERMASK), data);
}

void enc28j60::RBM(uint8_t *dest, uint16_t start, uint16_t length)
{
	if (start != ENC_READLOCATION_ANY)
		WCR16(ENC_ERDPTL, start);

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

void enc28j60::WBM(const uint8_t *src, uint16_t start, uint16_t length)
{
	WCR16(ENC_EWRPTL, start);

	WBM_raw(src, length);
}

/** 16-bit register read. This only applies to ENC28J60 registers whose low
 * byte is at an even offset and whose high byte is one above that. Can be
 * passed either L or H sub-register.
 *
 * @todo could use enc_register16_t
 * */
uint16_t enc28j60::RCR16(enc_ethreg reg) {
   uint16_t low=RCR(enc_ethreg(reg&~1));
   uint16_t high=RCR(enc_ethreg(reg|1));
   return (high << 8) | low;
}
uint16_t enc28j60::RCR16(enc_reg reg) {
   uint16_t low=RCR(enc_reg(reg&~1));
   uint16_t high=RCR(enc_reg(reg|1));
   return (high << 8) | low;
}
/** 16-bit register write. Compare RCR16. Writes the lower byte first, then
 * the higher, as required for the MII interfaces as well as for ERXRDPT. */
void enc28j60::WCR16(uint8_t reg, uint16_t data) {
	WCR(reg&~1, data & 0xff); WCR(reg|1, data >> 8);
}

void enc28j60::SRC() {
	hwdev.exchangebyte(0xff);
}

/** Wait for the ENC28J60 clock to be ready. Returns 0 on success,
 * and an unspecified non-zero integer on timeout. */
int enc28j60::wait()
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
		estat = RCR(ENC_ESTAT);
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

uint16_t enc28j60::MII_read(enc_phreg mireg)
{
	uint16_t result = 0;

	WCR(ENC_MIREGADR, mireg);
	BFS(ENC_MICMD, ENC_MICMD_MIIRD);

	while(RCR(ENC_MISTAT) & ENC_MISTAT_BUSY);

	result = RCR16(ENC_MIRDL);

	BFC(ENC_MICMD, ENC_MICMD_MIIRD);

	return result;
}

void enc28j60::MII_write(uint8_t mireg, uint16_t data)
{
	while(RCR(ENC_MISTAT) & ENC_MISTAT_BUSY);

	WCR(ENC_MIREGADR, mireg);
	WCR16(ENC_MIWRL, data);
}


void enc28j60::LED_set(enc_lcfg ledconfig, enc_led led)
{
	uint16_t state;
	state = MII_read(ENC_PHLCON);
	state = (state & ~(ENC_LCFG_MASK << led)) | (ledconfig << led);
	MII_write(ENC_PHLCON, state);
}

/** Configure the ENC28J60 for network operation, whose initial parameters get
 * passed as well. */
void enc28j60::ethernet_setup(uint16_t rxbufsize, const uint8_t mac[6])
{
	/* practical consideration: we don't come out of clean reset, better do
	 * this -- discard all previous packages */

	BFS(ENC_ECON1, ENC_ECON1_TXRST | ENC_ECON1_RXRST);
	while(RCR(ENC_EPKTCNT))
	{
		BFS(ENC_ECON2, ENC_ECON2_PKTDEC);
	}
	BFC(ENC_ECON1, ENC_ECON1_TXRST | ENC_ECON1_RXRST); /** @todo this should happen later, but when i don't do it here, things won't come up again. probably a problem in the startup sequence. */

	/********* receive buffer setup according to 6.1 ********/

	WCR16(ENC_ERXSTL, 0); /* see errata, must be 0 */
	set_erxnd(rxbufsize);
	WCR16(ENC_ERXRDPTL, 0);

	this->next_frame_location = 0;

	/******** for the moment, the receive filters are good as they are (6.3) ******/

	/******** waiting for ost (6.4) already happened in _setup ******/

	/******** mac initialization acording to 6.5 ************/

	/* enable reception and flow control (shouldn't hurt in simplex either) */
	BFS(ENC_MACON1, ENC_MACON1_MARXEN | ENC_MACON1_TXPAUS | ENC_MACON1_RXPAUS);

	/* generate checksums for outgoing frames and manage padding automatically */
	WCR(ENC_MACON3, ENC_MACON3_TXCRCEN | ENC_MACON3_FULLPADDING | ENC_MACON3_FRMLEN);

	/* setting defer is mandatory for 802.3, but it seems the default is reasonable too */

	/* MAMXF has reasonable default */

	/* it's not documented in detail what these do, just how to program them */
	WCR(ENC_MAIPGL, 0x12);
	WCR(ENC_MAIPGH, 0x0C);

	/* MACLCON registers have reasonable defaults */

	/* set the mac address */
	WCR(ENC_MAADR1, mac[0]);
	WCR(ENC_MAADR2, mac[1]);
	WCR(ENC_MAADR3, mac[2]);
	WCR(ENC_MAADR4, mac[3]);
	WCR(ENC_MAADR5, mac[4]);
	WCR(ENC_MAADR6, mac[5]);

	/******* mac initialization as per 6.5 ********/

	/* filter out looped packages; otherwise our own ND6 packages are
	 * treated as DAD failures. (i can't think of a reason why one would
	 * not want that; let me know if there is and it culd become configurable) */
	/* set ENC_PHCON2 bit 8 (HDLDIS) */
	MII_write(ENC_PHCON1, 0x0100);

	/*************** enabling reception as per 7.2 ***********/

	/* enable reception */
	BFS(ENC_ECON1, ENC_ECON1_RXEN);

	/* pull transmitter and receiver out of reset */
	BFC(ENC_ECON1, ENC_ECON1_TXRST | ENC_ECON1_RXRST);
}

/** Configure whether multicasts should be received.
 *
 * The more cmplex hash table mechanism that would allow filtering for
 * particular groups is not exposed yet. */
void enc28j60::set_multicast_reception(bool enable)
{
	if (enable)
		BFS(ENC_ERXFCON, 0x2);
	else
		BFC(ENC_ERXFCON, 0x2);
}

uint16_t enc28j60::transmit_start_address() const
{
	uint16_t earliest_start = this->rxbufsize + 1; /* +1 because it's not actually the size but the last byte */

	/* It is recommended that an even address be used for ETXST. */
	return (earliest_start + 1) & ~1;
}

/* Partial function of transmit. Always call this as transmit_start /
 * {transmit_partial * n} / transmit_end -- and use transmit or
 * transmit_pbuf unless you're just implementing those two */
void enc28j60::transmit_start()
{
	/* according to section 7.1 */
	uint8_t control_byte = 0; /* no overrides */

	/* 1. */
	/** @todo we only send a single frame blockingly, starting at the end of rxbuf */
	WCR16(ENC_ETXSTL, transmit_start_address());
	/* 2. */
	WBM(&control_byte, transmit_start_address(), 1);
}

void enc28j60::transmit_partial(const uint8_t *data, uint16_t length)
{
	WBM_raw(data, length);
}

void enc28j60::transmit_end(uint16_t length)
{
	uint8_t result[7];

	/* calculate checksum */

//	WCR16(ENC_EDMASTL, start + 1);
//	WCR16(ENC_EDMANDL, start + 1 + length - 3);
//	BFS(ENC_ECON1, ENC_ECON1_CSUMEN | ENC_ECON1_DMAST);
//	while (RCR(ENC_ECON1) & ENC_ECON1_DMAST);
//	uint16_t checksum = RCR16(ENC_EDMACSL);
//	checksum = ((checksum & 0xff) << 8) | (checksum >> 8);
//	WBM(&checksum, start + 1 + length - 2, 2);

	/* 3. */
	WCR16(ENC_ETXNDL, transmit_start_address() + 1 + length - 1);
	
	/* 4. */
	/* skipped because not using interrupts yet */
	/* 5. */
	BFS(ENC_ECON1, ENC_ECON1_TXRTS);

	/* block */
	for (int i = 0; i < 10000; ++i) {
		if (!(RCR(ENC_ECON1) & ENC_ECON1_TXRTS))
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
	BFS(ENC_ECON1, ENC_ECON1_TXRST);
	BFC(ENC_ECON1, ENC_ECON1_TXRST);

	return;
done:
	RBM(result, transmit_start_address() + length, 7);
	DEBUG("transmitted. %02x %02x %02x %02x %02x %02x %02x\n", result[0], result[1], result[2], result[3], result[4], result[5], result[6]);

	/** @todo parse that and return reasonable state */
}

void enc28j60::transmit(const uint8_t *data, uint16_t length)
{
	/** @todo check buffer size */
	transmit_start();
	transmit_partial(data, length);
	transmit_end(length);
}


void enc28j60::receive_start(uint8_t header[6], uint16_t *length)
{
	RBM(header, this->next_frame_location, 6);
	*length = header[2] | ((header[3] & 0x7f) << 8);
}

void enc28j60::receive_end(const uint8_t header[6])
{
	this->next_frame_location = header[0] + (header[1] << 8);

	/* workaround for 80349c.pdf (errata) #14 start.
	 *
	 * originally, this would have been
	 * WCR16(ENC_ERXRDPTL, next_location);
	 * but thus: */
	if (this->next_frame_location == /* RCR16(ENC_ERXSTL) can be simplified because of errata item #5 */ 0)
		WCR16(ENC_ERXRDPTL, RCR16(ENC_ERXNDL));
	else
		WCR16(ENC_ERXRDPTL, this->next_frame_location - 1);
	/* workaround end */

	DEBUG("before %d, ", RCR(ENC_EPKTCNT));
	BFS(ENC_ECON2, ENC_ECON2_PKTDEC);
	DEBUG("after %d.\n", RCR(ENC_EPKTCNT));

	DEBUG("read with header (%02x %02x) %02x %02x %02x %02x.\n", header[1], /* swapped due to endianness -- i want to read 1234 */ header[0], header[2], header[3], header[4], header[5]);
}

/** Read a received frame into data; may only be called when one is
 * available. Writes up to maxlength bytes and returns the total length of the
 * frame. (If the return value is > maxlength, parts of the frame were
 * discarded.) */
uint16_t enc28j60::read_received(uint8_t *data, uint16_t maxlength)
{
	uint8_t header[6];
	uint16_t length;

	receive_start(header, &length);

	if (length > maxlength)
	{
		RBM(data, ENC_READLOCATION_ANY, maxlength);
		DEBUG("discarding some bytes\n");
		/** @todo should that really be accepted at all? */
	} else {
		RBM(data, ENC_READLOCATION_ANY, length);
	}

	receive_end(header);

	return length;
}


bool enc28j60::linkstate(){
   return MII_read(ENC_PHSTAT1) & (1<<2);
}

uint8_t enc28j60::packetcount(){
   return RCR(ENC_EPKTCNT);
}

void enc28j60::receive_partial(uint8_t *dest, uint16_t length){
   RBM(dest, ENC_READLOCATION_ANY, length);
}
