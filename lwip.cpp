#include "lwip/inet.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/init.h"
#include "lwip/stats.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/apps/mqtt.h"
#include "lwip/apps/sntp.h"
#include <cstring>
#include <cstdio>
#include <cstddef>
#include "pico/stdlib.h"

#include "enc28j60_netif.hpp"
#include "enchw.h"
#include "mqtt.hpp"


// based on example from: https://www.nongnu.org/lwip/2_0_x/group__lwip__nosys.html
#define ETHERNET_MTU 1500

constexpr uint8_t mac[6] = {0xAA, 0x6F, 0x77, 0x47, 0x75, 0x8C};



void link_callback(netif* netif){
   printf("Link status changed to %s\n.", netif->flags&NETIF_FLAG_LINK_UP ? "Up":"Down");
}



int main(void)
{
    stdio_init_all();
    lwip_init();

    for (int i = 5; i > 0; i--)
    {
        printf("Sleeping for %d seconds...\n", i);
        sleep_ms(1000);
    }

    enchw_device_t spidev;
    enc28j60_netif netif(spidev,mac);


    ip_addr_t gateway, mask, static_ip, ntp_serv;
    IP4_ADDR(&static_ip, 192, 168, 20, 211);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 192, 168, 20, 1);
    IP4_ADDR(&ntp_serv, 192, 168, 20, 1);

    // IP4_ADDR_ANY if using DHCP client
    netif_set_addr(&netif,&static_ip,&mask,&gateway);
    netif_set_link_callback(&netif,link_callback);

    netif_set_default(&netif);
    netif_set_up(&netif);
    //run the polling function to update the link status.
    //netif.poll();

    dhcp_inform(&netif);
    //dhcp_start(&netif);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setserver(0,&ntp_serv);
    sntp_init();
    //MQTT
    mqtt_client mqtt_client{"pico-test","influx","influx"};
    ip_addr_t mqtt_server;
    IP4_ADDR(&mqtt_server, 192, 168, 20, 71);


    err_t connect_err=mqtt_client.connect(mqtt_server,1883);
    printf("MQTT connect: %d\n",connect_err);

    char msgbuf[50];
    int msgcount=0;

    int counter=0;
    while (1)
    {
        /* Cyclic lwIP timers check */
        sys_check_timeouts();
        netif.poll();

        if(counter++>=10000 && mqtt_client.is_connected()){
           timeval tv;
           gettimeofday(&tv,nullptr);
           snprintf(msgbuf,sizeof(msgbuf),"MQTT Test: %d %lld",msgcount,tv.tv_sec);
           printf("%s\n",msgbuf);
           err_t err;
           err = mqtt_client.publish("test_topic", msgbuf, strlen(msgbuf));
           if(err != ERR_OK) {
             printf("Publish err: %d\n", err);
           }else{
              msgcount++;
           }
           counter=0;
        }
    }
}
