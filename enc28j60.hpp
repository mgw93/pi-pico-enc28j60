#ifndef ENC28J60_HPP
#define ENC28J60_HPP

/**
 *
 * @addtogroup enc28j60 ENC28J60 driver
 *
 * Driver for the MicroChip ENC28J60 Ethernet module.
 *
 * This software requires an implementation of the `enchw_*` interface
 * specified by `enchw.h` (which describes a very basic SPI interface), and
 * provides the various command types as well as read and write access to the
 * ENC28J60 memory.
 *
 * @{
 */

#include "enc28j60-consts.h"

class enc28j60{

public:
   enc28j60(struct enchw_device_t& spi_dev) : hwdev{spi_dev}{};
   int setup_basic();
   uint8_t bist();
   uint8_t bist_manual();
   void LED_set(enc_lcfg ledconfig, enc_led led);
   void ethernet_setup(uint16_t rxbufsize, const uint8_t mac[6]);
   void set_multicast_reception(bool enable);
   void transmit(const uint8_t *data, uint16_t length);
   uint16_t read_received(uint8_t *data, uint16_t maxlength);

   bool linkstate();
   uint8_t packetcount();

protected:
   void transmit_start();
   void transmit_partial(const uint8_t *data, uint16_t length);
   void transmit_end(uint16_t length);
   void receive_start(uint8_t header[6], uint16_t *length);
   void receive_partial(uint8_t *dest, uint16_t length);
   void receive_end(const uint8_t header[6]);

private:
   uint8_t command(uint8_t first, uint8_t second, bool dummy=false);
   uint8_t RCR(enum enc_ethreg reg) ;
   uint8_t RCR(enum enc_reg reg) ;
   void WCR(uint8_t reg, uint8_t data);
   void BFS(uint8_t reg, uint8_t data);
   void BFC(uint8_t reg, uint8_t data);
   void RBM(uint8_t *dest, uint16_t start, uint16_t length);
   void WBM_raw(const uint8_t *src, uint16_t length);
   void WBM(const uint8_t *src, uint16_t start, uint16_t length);
   uint16_t RCR16(enum enc_ethreg reg);
   uint16_t RCR16(enum enc_reg reg);
   void WCR16(uint8_t reg, uint16_t data);
   void SRC();
   int wait();
   uint16_t MII_read(enum enc_phreg mireg);
   void MII_write(uint8_t mireg, uint16_t data);
   void set_erxnd(uint16_t erxnd);
   void select_page(uint8_t page);
   void ensure_register_accessible(uint8_t r);
   uint16_t transmit_start_address() const;


   /** The chip's active register page ENC_ECON1[0:1] */
   uint8_t last_used_register;
   /** Configured receiver buffer size; cached value of of ERXND[H:L] */
   uint16_t rxbufsize;

   /** Where to start reading the next received frame */
   uint16_t next_frame_location;

   /** Reference to the hardware implementation to access device
   * information */
   struct enchw_device_t &hwdev;
};

#endif // ENC28J60_HPP
