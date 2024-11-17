#ifndef ENC28J60_NETIF_HPP
#define ENC28J60_NETIF_HPP

#include "enc28j60.hpp"
#include "enc28j60-consts.h"
#include <lwip/pbuf.h>
#include <lwip/netif.h>


class enc28j60_netif : enc28j60 , public netif{

public:
   enc28j60_netif(struct enchw_device_t& spi_dev, const uint8_t mac[6], const char name[2]="e0");

   //static void status_callback(){};
   void poll();
   using enc28j60::linkstate;
private:
   err_t init();
   void transmit_pbuf(const struct pbuf *buf);
   int read_received_pbuf(struct pbuf* &buf);

   // static callback functions for lwip
   static err_t netif_init(struct netif *netif);
   static err_t send(struct netif *netif, struct pbuf *p);
};

#endif // ENC28J60_NETIF_HPP
