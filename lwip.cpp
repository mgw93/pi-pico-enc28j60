#include "lwip/inet.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/init.h"
#include "lwip/stats.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/apps/mqtt.h"
#include "netif/etharp.h"
#include <cstring>
#include <cstdio>
#include <cstddef>
#include "pico/stdlib.h"
#include "hardware/spi.h"
extern "C"{
#include "enc28j60.h"
}

// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS 17
#define PIN_SCK 18
#define PIN_MOSI 19

// based on example from: https://www.nongnu.org/lwip/2_0_x/group__lwip__nosys.html
#define ETHERNET_MTU 1500

constexpr uint8_t mac[6] = {0xAA, 0x6F, 0x77, 0x47, 0x75, 0x8C};

static err_t netif_output(struct netif *netif, struct pbuf *p)
{
    LINK_STATS_INC(link.xmit);

    // lock_interrupts();
    // pbuf_copy_partial(p, mac_send_buffer, p->tot_len, 0);
    /* Start MAC transmit here */

    printf("enc28j60: Sending packet of len %d\n", p->len);
    enc28j60PacketSend(p->len, (uint8_t *)p->payload);
    // pbuf_free(p);

    // error sending
    if (enc28j60Read(ESTAT) & ESTAT_TXABRT)
    {
        // a seven-byte transmit status vector will be
        // written to the location pointed to by ETXND + 1,
        printf("ERR - transmit aborted\n");
    }

    if (enc28j60Read(EIR) & EIR_TXERIF)
    {
        printf("ERR - transmit interrupt flag set\n");
        enc28j60Init(mac);
    }

    // unlock_interrupts();
    return ERR_OK;
}

static void netif_status_callback(struct netif *netif)
{
    printf("netif status changed %s\n", ip4addr_ntoa(netif_ip4_addr(netif)));
}

static err_t netif_initialize(struct netif *netif)
{
    enc28j60Init(mac);
    netif->linkoutput = netif_output;
    netif->output = etharp_output;
    // netif->output_ip6 = ethip6_output;
    netif->mtu = ETHERNET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP | NETIF_FLAG_MLD6;
    // MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, 100000000);
    SMEMCPY(netif->hwaddr, mac, sizeof(netif->hwaddr));
    netif->hwaddr_len = sizeof(netif->hwaddr);
    return ERR_OK;
}

void mqtt_pub_cb(void* arg, err_t result){
   printf("Publish result: %d\n", result);
}

void mqtt_conn_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status){
   printf("Connect Callback");
   if(status==MQTT_CONNECT_ACCEPTED) *static_cast<bool*>(arg)=true;
}

int main(void)
{
    stdio_init_all();

    // data sheet up to 20 mhz
    spi_init(SPI_PORT, 20'000'000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // END PICO INIT

    for (int i = 5; i > 0; i--)
    {
        printf("Sleeping for %d seconds...\n", i);
        sleep_ms(1000);
    }

    ip_addr_t addr, mask, static_ip;
    IP4_ADDR(&static_ip, 192, 168, 20, 211);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&addr, 192, 168, 20, 1);

    struct netif netif;
    lwip_init();
    // IP4_ADDR_ANY if using DHCP client
    netif_add(&netif, &static_ip, &mask, &addr, NULL, netif_initialize, netif_input);
    netif.name[0] = 'e';
    netif.name[1] = '0';
    // netif_create_ip6_linklocal_address(&netif, 1);
    // netif.ip6_autoconfig_enabled = 1;
    netif_set_status_callback(&netif, netif_status_callback);
    netif_set_default(&netif);
    netif_set_up(&netif);

    dhcp_inform(&netif);
    //dhcp_start(&netif);

    //MQTT
    mqtt_client_t* mqtt_client=mqtt_client_new();
    const char* client_id="pico-test";
    const char* user="influx";
    const char* passwd="influx";
    ip_addr_t mqtt_server;
    IP4_ADDR(&mqtt_server, 192, 168, 20, 71);
    mqtt_connect_client_info_t client_info;
    memset(&client_info,0,sizeof(client_info));
    client_info.client_id=client_id;
    client_info.client_user=user;
    client_info.client_pass=passwd;
    client_info.keep_alive=10;
    bool connected=false;


    std::byte eth_pkt[ETHERNET_MTU];
    struct pbuf *p = NULL;

    netif_set_link_up(&netif);

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
        uint16_t packet_len = enc28j60PacketReceive(ETHERNET_MTU, (uint8_t *)eth_pkt);
        if (packet_len)
        {
            printf("enc: Received packet of length = %d\n", packet_len);
            p = pbuf_alloc(PBUF_RAW, packet_len, PBUF_POOL);
            pbuf_take(p, eth_pkt, packet_len);
            //free(eth_pkt);
            //eth_pkt = malloc(ETHERNET_MTU);
        }
        else
        {
            // printf("enc: no packet received\n");
        }

        if (packet_len && p != NULL)
        {
            LINK_STATS_INC(link.recv);

            if (netif.input(p, &netif) != ERR_OK)
            {
                pbuf_free(p);
            }
        }

        /* Cyclic lwIP timers check */
        sys_check_timeouts();

        /* your application goes here */
        sleep_ms(2);
        if(counter++>=100 && connected){
           //const char *pub_payload= "MQTT Test ABC";
           snprintf(msgbuf,sizeof(msgbuf),"MQTT Test: %d",msgcount++);
           err_t err;
           u8_t qos = 2; /* 0 1 or 2, see MQTT specification */
           u8_t retain = 0; /* No don't retain such crappy payload... */
           err = mqtt_publish(mqtt_client, "test_topic", msgbuf, strlen(msgbuf), qos, retain, mqtt_pub_cb, nullptr);
           if(err != ERR_OK) {
             printf("Publish err: %d\n", err);
           }
           counter=0;
        }
    }
}
