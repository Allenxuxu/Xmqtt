#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include <map>
#include <list>
#include <boost/weak_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/base/Types.h>

#include "MqttMessage.h"

using namespace net;

class MqttMsgList
{
public:
  typedef uint16_t type_mid;
  typedef std::map<type_mid,boost::shared_ptr<MqttMessage> > type_msgs;
  typedef type_msgs::iterator Iterator;

  void push(type_mid mid,const boost::shared_ptr<MqttMessage>& msg);

  void push(const boost::shared_ptr<MqttMessage>& msg);

  void insert(Iterator first,Iterator last);

  boost::shared_ptr<MqttMessage> getandDelMsg(type_mid mid);

  void deleteMsg(type_mid mid);

  type_msgs copy();

  size_t size() const
  { return msgs_.size(); }

private:
  type_msgs msgs_;
  MutexLock mutex_;
};


class MqttClientSession : public boost::enable_shared_from_this<MqttClientSession>
{
public:
  MqttClientSession(EventLoop* loop,uint16_t keepalive);
  ~MqttClientSession();

  void publishOfflineMsg();
  void publish(const boost::shared_ptr<MqttMessage>& msg);

  void setWill(bool will)
  { will_ = will; }

  bool will()const
  { return will_; }

  string clientID() const
  { return clientID_; }

  bool cleanSession() const
  { return clean_session_; }

  void setCleanSession(bool cleanSession)
  { clean_session_ = cleanSession; }

  void ResetWillMsg(boost::shared_ptr<MqttMessage>& willmsgPtr)
  { willMsgPtr_ = willmsgPtr; }

  boost::shared_ptr<MqttMessage> willMsg() const
  { return willMsgPtr_; }

  void setClientID(const string& clientID)
  { clientID_ = clientID; }

  void setUserName(const string& userName)
  { username_ = userName; }

  void setPassWord(const string& passWord)
  { password_ = passWord; }

  void setTcpConnection(const TcpConnectionPtr& conn)
  { TcpConWeakPtr_ = conn; }

  void onMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time);

  std::list<string>& subTopics()
  { return topics_; }
private:
  void mqttHandlePublishAck(const TcpConnectionPtr& conn, Buffer& buffer,const size_t len);
  void mqttHandlePublishRel(const TcpConnectionPtr& conn, Buffer& buffer,const size_t len);
  void mqttHandlePublishRec(const TcpConnectionPtr& conn, Buffer& buffer,const size_t len);
  void mqttHandlePublishComp(const TcpConnectionPtr& conn, Buffer& buffer,const size_t len);
  void mqttHandlePingReq(const TcpConnectionPtr& conn);
  bool mqttHandleDisconnect(const TcpConnectionPtr& conn, const size_t len);
  bool mqttHandleUnsubcribe(const TcpConnectionPtr& conn, Buffer& buffer, const size_t len);
  bool mqttHandleSubcribe(const TcpConnectionPtr& conn, Buffer& buffer, const size_t len);
  bool mqttHadnlePublish(const TcpConnectionPtr& conn, Buffer& buffer, uint8_t header, const size_t len);

  void sendPingResp(const TcpConnectionPtr& conn);
  void sendUnsuback(const TcpConnectionPtr& conn, uint16_t mid);
  void sendPublishAck(const TcpConnectionPtr& conn, uint16_t mid);
  void sendPubRec(const TcpConnectionPtr& conn, uint16_t mid);
  void sendPubRel(const TcpConnectionPtr& conn, uint16_t mid);
  void sendPubComp(const TcpConnectionPtr& conn, uint16_t mid);
  void sendSuback(const TcpConnectionPtr& conn, uint16_t mid, const std::vector<uint8_t>& payload);

  void checkAlive();
  int readMqttString(string& buf,Buffer& buffer);
  std::vector<uint8_t> encodeRemainingLenth(uint32_t remainingLength);
  std::vector<uint8_t> packageMsg(const boost::shared_ptr<MqttMessage>& msg,const size_t& remainglen);


  EventLoop* loop_;
  Timestamp lastInTime_;

  bool will_;
  bool clean_session_;
  std::list<string> topics_;
  string clientID_;
  string username_;
  string password_;
  boost::weak_ptr<TcpConnection> TcpConWeakPtr_;

  TimerId timerId_;
  boost::shared_ptr<MqttMessage> willMsgPtr_;

  MqttMsgList sendUnconfdMsgs_;
  MqttMsgList recvUnconfdMsgs_;
  const uint16_t keepalive_;
};

#endif // MQTTCLIENT_H
