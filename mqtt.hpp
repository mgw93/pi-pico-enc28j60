//#include "lwip/ip_addr.h"
#include "lwip/apps/mqtt.h"

class mqtt_client{
public:
   mqtt_client(const mqtt_client&)=delete;
   mqtt_client operator=(mqtt_client)=delete;
   mqtt_client(const char *id, const char *user=nullptr, const char *pass=nullptr)
   : client_info{
      .client_id=id,
      .client_user=user,
      .client_pass=pass,
      .keep_alive=20
   }
   {
      client=mqtt_client_new();
   }


   ~mqtt_client(){
      mqtt_client_free(client);
   }

   bool is_connected(){return mqtt_client_is_connected(client);}
   void disconnect(){mqtt_disconnect(client);}
   err_t publish(const char *topic, const void *payload, u16_t payload_length, u8_t qos=2, u8_t retain=false){
      return mqtt_publish(client,topic,payload,payload_length,qos,retain,pub_request_cb,this);
   }
   err_t connect(ip_addr_t ip,u16_t port){
      server_ip=ip;
      server_port=port;
      return mqtt_client_connect(client,&server_ip,port,connection_cb,this,&client_info);
   }
   
private:
   virtual void process_publish(const char *topic, u32_t tot_len){}
   virtual void process_data(const u8_t *data, u16_t len, u8_t flags){}
   virtual void process_conn_status(mqtt_connection_status_t status){
      const char * msg;
      switch(status){
         case MQTT_CONNECT_ACCEPTED: msg="Accepted"; break;
         case MQTT_CONNECT_REFUSED_PROTOCOL_VERSION: msg="Refused version"; break;
         case MQTT_CONNECT_REFUSED_IDENTIFIER: msg="Refused identifier"; break;
         case MQTT_CONNECT_REFUSED_SERVER: msg="Refused server"; break;
         case MQTT_CONNECT_REFUSED_USERNAME_PASS: msg="Refused user/pass"; break;
         case MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_: msg="Refused authorization"; break;
         case MQTT_CONNECT_DISCONNECTED: msg="Disconnected"; break;
         case MQTT_CONNECT_TIMEOUT: msg="Timeout"; break;
         default: msg="Unknown"; break;
      }
      printf("MQTT connection status: %s\n",msg);
      if(status==MQTT_CONNECT_TIMEOUT || status==MQTT_CONNECT_DISCONNECTED){
         printf("Attempting reconnect\n");
         connect(server_ip,server_port);
      }
   }
   virtual void process_pub_result(err_t err){
      printf("Publish result: %s\n", err==ERR_TIMEOUT ? "Timeout":"OK");
   }
   virtual void process_sub_result(err_t err){
      printf("Subscribe result: %s\n", err==ERR_TIMEOUT ? "Timeout":"OK");
   }
   virtual void process_unsub_result(err_t err){
      printf("Unsubscribe result: %s\n", err==ERR_TIMEOUT ? "Timeout":"OK");
   }

   static void connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status){
      static_cast<mqtt_client*>(arg)->process_conn_status(status);
   }
   static void incoming_publish_cb(void *arg, const char *topic, u32_t tot_len){
      static_cast<mqtt_client*>(arg)->process_publish(topic,tot_len);
   }
   static void incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags){
      static_cast<mqtt_client*>(arg)->process_data(data,len,flags);
   }
   static void pub_request_cb(void *arg, err_t err){
      static_cast<mqtt_client*>(arg)->process_pub_result(err);
   }
   static void sub_request_cb(void *arg, err_t err){
      static_cast<mqtt_client*>(arg)->process_sub_result(err);
   }
   static void unsub_request_cb(void *arg, err_t err){
      static_cast<mqtt_client*>(arg)->process_unsub_result(err);
   }

   mqtt_client_t *client;
   mqtt_connect_client_info_t client_info;
   ip_addr_t server_ip;
   u16_t server_port;
};
