#include "MqttServer.h"

#include <boost/bind.hpp>
#include <algorithm>
#include <muduo/base/Logging.h>
#include <muduo/net/Endian.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Singleton.h>

#include "MqttProtocol.h"
#include "MqttTopicTree.h"

MqttServer::MqttServer(EventLoop* loop,const InetAddress& addr,const int numThreads)
  :tcpServer_(loop,addr,"mqtt server"),
    protocolNameV311_(PROTOCOL_NAME_v311),
    waitConnectTime_(10)
{
  tcpServer_.setConnectionCallback(
        boost::bind(&MqttServer::onConnection, this, _1));
  tcpServer_.setMessageCallback(
        boost::bind(&MqttServer::onMessage, this, _1, _2, _3));
  tcpServer_.setThreadNum(numThreads);
}

void MqttServer::onConnection(const TcpConnectionPtr& conn)
{
  conn->getLoop()->assertInLoopThread();
  if(conn->connected())
  {
        conn->enableCloseAfter(waitConnectTime_);
  }
  else
  {
    if(!conn->getContext().empty())
    {
      MqttTopicTree& topicTree = Singleton<MqttTopicTree>::instance();
      boost::shared_ptr<MqttClientSession> ptr =
          boost::any_cast<boost::shared_ptr<MqttClientSession> >(conn->getContext());

      if(ptr->will())
      {
        boost::shared_ptr<MqttMessage> msg = ptr->willMsg();
        if(msg)
        {
          msg->remainglen = 2 + msg->topic.length() + 2 + msg->payload.length();

          topicTree.Publish(msg->topic,msg);

          if(msg->retain)
            topicTree.addRetainMsg(msg);
        }
      }

      if(!ptr->cleanSession())
      {
        offlineClients_.pushClient(ptr->clientID(),ptr);
      }
      else
      {
        std::list<string>& topics = ptr->subTopics();
        for(std::list<string>::iterator it=topics.begin(); it!=topics.end(); ++it)
          topicTree.unSubscriber(*it, ptr);
      }
    }
  }
}

void MqttServer::onMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time)
{
  conn->getLoop()->assertInLoopThread();
  //连接消息固定报头（报文类型byte 1 + 剩余长度（最大为4个字节，最小1个字节），
  //后面可变报头必然超过3个字节，所以此处判断 < 5 不会错过正确mqtt消息
  if(buffer->readableBytes() < 5)
    return;

  uint8_t msgType = buffer->peekInt8();

  const char* byte = buffer->peek();
  uint32_t remaining_length = 0;
  uint32_t remaining_mult = 1;

  char i=0;
  do
  {
    byte++;
    ++i;

    if(i > 4) //不符合mqtt协议
    {
      conn->forceClose();
      return;
    }
    remaining_length  += (*byte & 127) * remaining_mult;
    remaining_mult *= 128;
  }while((*byte & 128) != 0);

  if(buffer->readableBytes() < remaining_length + i + 1)
    return;
  else
  {
    buffer->retrieve(i+1);

    uint8_t cmd = msgType & 0xF0;
    if(cmd == CONNECT && mqttHandleConnect(conn,*buffer))
            conn->cancelCloseAfter();
    else
      conn->forceClose();
  }
}


bool MqttServer::mqttHandleConnect(const TcpConnectionPtr& conn, Buffer& buffer)
{
  conn->getLoop()->assertInLoopThread();
  uint16_t len = buffer.readInt16();

  if(len != 4)
  {
    sendConnack(conn,0,CONNACK_REFUSED_PROTOCOL_VERSION);
    return false;
  }
  char protocolName[5]={0};
  std::copy(buffer.peek(),buffer.peek()+4,protocolName);
  buffer.retrieve(4);

  uint8_t protocolVersion = buffer.readInt8();

  if(protocolNameV311_ != protocolName && protocolVersion != PROTOCOL_VERSION_v311)
  {
    sendConnack(conn,0,CONNACK_REFUSED_PROTOCOL_VERSION);
    return false;
  }

  uint8_t connect_flags = buffer.readInt8();

  if((connect_flags&0x01) != 0)
    return false;

  bool will_retain;
  uint8_t will, will_qos, clean_session;
  uint8_t username_flag, password_flag;
  clean_session = static_cast<uint8_t>((connect_flags & 0x02) >> 1);
  will = connect_flags & 0x04;
  will_qos = static_cast<uint8_t>((connect_flags & 0x18) >> 3);
  will_retain = ((connect_flags & 0x20) == 0x20);
  password_flag = connect_flags & 0x40;
  username_flag = connect_flags & 0x80;

  if(will_qos >= 3) return false;

  uint8_t connectAck = 0;
  if(clean_session == 0)
    connectAck |= 0x01;

  uint16_t keepalive = buffer.readInt16();

  string clientID;
  if(readMqttString(clientID,buffer) <= 0)
  {
    sendConnack(conn,0,CONNACK_REFUSED_IDENTIFIER_REJECTED);
    return false;
  }

  boost::shared_ptr<MqttClientSession> client;
  if(!clean_session)
  {
    LOG_DEBUG << "clean_session mqtt client";
    client = offlineClients_.popClient(clientID);
    if(!client)
      client.reset(new MqttClientSession(conn->getLoop(),keepalive));
  }
  else
  {
    LOG_DEBUG << "new mqtt client";
    client.reset(new MqttClientSession(conn->getLoop(),keepalive));
  }

  client->setWill(will);
  client->setTcpConnection(conn);
  client->setClientID(clientID);
  client->setCleanSession(clean_session);
  conn->setContext(client);
  conn->setMessageCallback(
        boost::bind(&MqttClientSession::onMessage, client.get(), _1, _2, _3));

  if(will)
  {
    boost::shared_ptr<MqttMessage> willMsgPtr(new MqttMessage);
    willMsgPtr->qos = will_qos;
    willMsgPtr->retain = will_retain;
    willMsgPtr->mid = 0;

    if((readMqttString(willMsgPtr->topic,buffer) <= 0) ||
       (readMqttString(willMsgPtr->payload,buffer) <= 0) )
      return false;

    client->ResetWillMsg(willMsgPtr);
  }

  if(username_flag)
  {
    string userName;
    if(readMqttString(userName, buffer) <= 0)
      return false;
    else
      client->setUserName(userName);

    if(password_flag)
    {
      //TODO  验证用户密码
      string passWord;
      if(readMqttString(passWord, buffer) <= 0)
        return false;
      else
        client->setPassWord(passWord);
    }
  }

  sendConnack(conn,connectAck,CONNACK_ACCEPTED);

  LOG_DEBUG << " clientID: " << clientID
            << ", keepalive: " << keepalive;

  client->publishOfflineMsg();

  return true;
}

void MqttServer::sendConnack(const TcpConnectionPtr& conn, uint8_t ack, uint8_t result)
{
  uint8_t message[4] = {CONNACK,2,ack,result};
  conn->send(message,sizeof(message));
}

int MqttServer::readMqttString(string& buf, Buffer& buffer)
{
  uint16_t len = buffer.readInt16();
  if(len > 0)
  {
    buf.reserve(len);
    buf.assign(buffer.peek(),len);
    buffer.retrieve(len);
  }
  return len;
}
