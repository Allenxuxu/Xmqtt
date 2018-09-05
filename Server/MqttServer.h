#ifndef MQTTSERVER_H
#define MQTTSERVER_H

#include <list>
#include <vector>
#include <map>
#include <muduo/net/TcpServer.h>
#include <muduo/net/TcpConnection.h>

#include "MqttClient.h"
#include "MqttofflineClientList.h"

using namespace net;

class MqttServer
{
public:
  MqttServer(EventLoop* loop, const InetAddress& addr,const int numThreads);

  void start()
  { tcpServer_.start(); }

private:
  void onConnection(const TcpConnectionPtr& conn);
  void onMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time);
  void sendConnack(const TcpConnectionPtr& conn, uint8_t ack, uint8_t result);
  bool mqttHandleConnect(const TcpConnectionPtr& conn, Buffer& buffer);
  int readMqttString(string& buf, Buffer& buffer);

  net::TcpServer tcpServer_;
  //已下线的保留客户端
  MqttofflineClientList offlineClients_;
  const string protocolNameV311_;
  const int waitConnectTime_;
};

#endif // MQTTSERVER_H
