#ifndef ENC28J60_NETIF_HPP
#define ENC28J60_NETIF_HPP

#include "enc28j60.hpp"
#include "enc28j60-consts.h"
#include <lwip/pbuf.h>
#include <lwip/netif.h>


class enc28j60_netif : enc28j60 , public netif{

public:
   enc28j60_netif(struct enchw_device_t& spi_dev) : enc28j60{spi_dev}{};

   static err_t netif_init(struct netif *netif);
   //static void status_callback(){};
   void poll();
   using enc28j60::linkstate;
private:
   err_t init();
   static err_t send(struct netif *netif, struct pbuf *p);
   void transmit_pbuf(const struct pbuf *buf);
   int read_received_pbuf(struct pbuf *&buf);
};

#endif // ENC28J60_NETIF_HPP
