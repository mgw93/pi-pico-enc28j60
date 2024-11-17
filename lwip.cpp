#include "lwip/inet.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/init.h"
#include "lwip/stats.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/apps/mqtt.h"
#include <cstring>
#include <cstdio>
#include <cstddef>
#include "pico/stdlib.h"

#include "enc28j60_netif.hpp"
#include "enchw.h"


// based on example from: https://www.nongnu.org/lwip/2_0_x/group__lwip__nosys.html
#define ETHERNET_MTU 1500

constexpr uint8_t mac[6] = {0xAA, 0x6F, 0x77, 0x47, 0x75, 0x8C};



void mqtt_pub_cb(void* arg, err_t result){
   printf("Publish result: %d\n", result);
}

void link_callback(netif* netif){
   printf("Link status change\n");
}

void mqtt_conn_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status){
   printf("Connect Callback\n");
   if(status==MQTT_CONNECT_ACCEPTED) *static_cast<bool*>(arg)=true;
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


    ip_addr_t gateway, mask, static_ip;
    IP4_ADDR(&static_ip, 192, 168, 20, 211);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 192, 168, 20, 1);

    // IP4_ADDR_ANY if using DHCP client
    netif_set_addr(&netif,&static_ip,&mask,&gateway);
    netif_set_link_callback(&netif,link_callback);

    netif_set_default(&netif);
    netif_set_up(&netif);
    //run the polling function to update the link status.
    //netif.poll();

    dhcp_inform(&netif);
    //dhcp_start(&netif);

    //MQTT
    mqtt_client_t* mqtt_client=mqtt_client_new();
    ip_addr_t mqtt_server;
    IP4_ADDR(&mqtt_server, 192, 168, 20, 71);
    mqtt_connect_client_info_t client_info;
    memset(&client_info,0,sizeof(client_info));
    client_info.client_id="pico-test";
    client_info.client_user="influx";
    client_info.client_pass="influx";
    client_info.keep_alive=10;
    bool connected=false;


    err_t connect_err=mqtt_client_connect(mqtt_client,&mqtt_server,1883,mqtt_conn_cb,&connected,&client_info);
    printf("MQTT connect: %d\n",connect_err);

/*
    struct tcp_pcb * pcb=tcp_new();
    tcp_bind(pcb,&static_ip,1234);
    pcb=tcp_listen(pcb);
*/

    char msgbuf[50];
    int msgcount=0;

    int counter=0;
    while (1)
    {
        /* Cyclic lwIP timers check */
        sys_check_timeouts();
        netif.poll();

        //sleep_ms(2);
        if(counter++>=10000 && connected){
           //const char *pub_payload= "MQTT Test ABC";
           snprintf(msgbuf,sizeof(msgbuf),"MQTT Test: %d",msgcount);
           printf("%s\n",msgbuf);
           err_t err;
           u8_t qos = 2; /* 0 1 or 2, see MQTT specification */
           u8_t retain = 0;
           err = mqtt_publish(mqtt_client, "test_topic", msgbuf, strlen(msgbuf), qos, retain, mqtt_pub_cb, nullptr);
           if(err != ERR_OK) {
             printf("Publish err: %d\n", err);
             if(err==ERR_CONN) {
                connected=false;
             }
           }else{
              msgcount++;
           }
           counter=0;
        }
    }
}
