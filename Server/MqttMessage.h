#ifndef MQTTMESSAGE_H
#define MQTTMESSAGE_H

#include <stdint.h>
#include <muduo/base/Timestamp.h>
using namespace muduo;
class MqttMessage
{
public:
  enum msgState
  {
    ms_invalid,
    ms_publish,

    ms_wait_for_puback,
    ms_wait_for_pubrec,  
    ms_wait_for_pubrel,
    ms_wait_for_pubcomp,
  };

  size_t remainglen;
  uint16_t mid;
  uint8_t qos;
  uint8_t dup;
  bool retain;
  msgState state;
  string topic;
  string payload;
  Timestamp timestamp;
};

#endif // MQTTMESSAGE_H
