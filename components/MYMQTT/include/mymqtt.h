#ifndef __MYMQTT__H
#define __MYMQTT__H


int MQTTInit(void);
void mqtt_subscribe_topic(const char* topic, int qos);
void mqtt_publish_message(const char* topic, const char* message, int qos);

#endif
