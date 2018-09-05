#include "MqttClient.h"

#include <boost/bind.hpp>
#include <muduo/base/Logging.h>
#include <muduo/net/Endian.h>
#include <muduo/base/Singleton.h>
#include <muduo/base/Atomic.h>

#include "MqttTopicTree.h"
#include "MqttProtocol.h"

namespace
{
  AtomicUint32 mid;

  uint16_t NewMid()
  {
    return static_cast<uint16_t>(mid.addAndGet(1) % 0xff);
  }
}

#define MSB(A) static_cast<uint8_t>((A & 0xFF00) >> 8)
#define LSB(A) static_cast<uint8_t>(A & 0x00FF)

MqttClientSession::MqttClientSession(EventLoop* loop,uint16_t keepalive)
  : loop_(loop),
    lastInTime_(0),
    will_(false),
    clean_session_(false),
    keepalive_(keepalive)
{
  if(keepalive_ > 0)
    timerId_ = loop_->runEvery(keepalive/2,boost::bind(&MqttClientSession::checkAlive,this));
}

MqttClientSession::~MqttClientSession()
{
  if(keepalive_ > 0)
    loop_->cancel(timerId_);
}

void MqttClientSession::publishOfflineMsg()
{
  if(sendUnconfdMsgs_.size() > 0)
  {
    TcpConnectionPtr ptr = TcpConWeakPtr_.lock();
    if(ptr)
    {
      MqttMsgList::type_msgs msgs = sendUnconfdMsgs_.copy();
      for(MqttMsgList::Iterator it=msgs.begin(); it!=msgs.end(); ++it)
      {
        boost::shared_ptr<MqttMessage>& msg = it->second;
        if(msg->qos == 2 && msg->state == MqttMessage::ms_wait_for_pubcomp)
        {
          sendPubRel(ptr,msg->mid);
          continue;
        }

        if(msg->qos == 0)
          sendUnconfdMsgs_.deleteMsg(msg->mid);

        std::vector<uint8_t> sendbuf = packageMsg(msg,msg->remainglen);
        ptr->send(sendbuf.data(),static_cast<int>(sendbuf.size()));
      }
    }
  }
}

void MqttClientSession::publish(const boost::shared_ptr<MqttMessage>& msg)
{
  if(msg->qos == 0)
    msg->state = MqttMessage::ms_publish;
  else if(msg->qos == 1)
    msg->state = MqttMessage::ms_wait_for_puback;
  else // msg->qos == 2
    msg->state = MqttMessage::ms_wait_for_pubrec;

  TcpConnectionPtr ptr = TcpConWeakPtr_.lock();
  if(ptr)
  {
    if(msg->qos > 0)
      sendUnconfdMsgs_.push(msg);

    std::vector<uint8_t> sendbuf = packageMsg(msg,msg->remainglen);
    ptr->send(sendbuf.data(),static_cast<int>(sendbuf.size()));
  }
  else
  {
    LOG_INFO  << "offline msg store ";
    if(msg->qos > 0)
      msg->mid = NewMid();
    sendUnconfdMsgs_.push(msg);
  }
}

void MqttClientSession::onMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time)
{
  conn->getLoop()->assertInLoopThread();
  //固定报头（报文类型byte 1 + 剩余长度（最大为4个字节，最小1个字节），
  //后面可变报头必然超过3个字节，所以此处判断 < 5 不会错过正确mqtt消息
  //ping 消息 仅仅两个字节
  if(buffer->readableBytes() < 2)
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

  assert(buffer->readableBytes() >= remaining_length + i + 1);

  if(buffer->readableBytes() < remaining_length + i + 1)
    return;
  else
  {
    lastInTime_ = time;

    buffer->retrieve(i+1);
    uint8_t cmd = msgType & 0xF0;

    switch (cmd)
    {
      case PINGREQ:
        mqttHandlePingReq(conn);
        break;
      case PINGRESP:
        break;
      case PUBACK:
        mqttHandlePublishAck(conn, *buffer,remaining_length);
        break;
      case PUBCOMP:
        mqttHandlePublishComp(conn, *buffer,remaining_length);
        break;
      case PUBLISH:
        mqttHadnlePublish(conn,*buffer,msgType,remaining_length);
        break;
      case PUBREC:
        mqttHandlePublishRec(conn, *buffer, remaining_length);
        break;
      case PUBREL:
        mqttHandlePublishRel(conn,*buffer,remaining_length);
        break;
      case DISCONNECT:
        if(!mqttHandleDisconnect(conn,remaining_length))
        {
          conn->forceClose();
        }
        break;
      case SUBSCRIBE:
        if(!mqttHandleSubcribe(conn,*buffer,remaining_length))
        {
          LOG_ERROR << "SUBSCRIBE ERR";
          conn->forceClose();
        }
        break;
      case UNSUBSCRIBE:
        if(!mqttHandleUnsubcribe(conn,*buffer,remaining_length))
        {
          LOG_ERROR << "UNSUBSCRIBE ERR";
          conn->forceClose();
        }
        break;
      default:
        conn->forceClose();
        assert(!"no this msg type");
        break;

    }
  }
}


bool MqttClientSession::mqttHandleSubcribe(const TcpConnectionPtr& conn, Buffer& buffer,const size_t len)
{
  uint16_t mid = buffer.readInt16();


  std::vector<uint8_t> qosVector;
  size_t readedNum = 0;
  readedNum += sizeof(mid);
  while(readedNum < len)
  {
    //暂存缓冲区中可读数据
    size_t num = buffer.readableBytes();

    string topic;
    readMqttString(topic, buffer);
    //    if(!subTopicCheck(topic.c_str())) return false;

    uint8_t qos = buffer.readInt8();
    if(qos > 2) return false;

    qosVector.push_back(qos);

    MqttTopicTree& topicTree = Singleton<MqttTopicTree>::instance();
    //将客户端加入订阅链表
    if(cleanSession())
    {
      topicTree.addSubscriber(topic,
                              boost::any_cast<boost::shared_ptr<MqttClientSession> >(conn->getContext()));
      topics_.push_back(topic);
    }
    else
    {
      std::list<string>::iterator it = std::find(topics_.begin(),topics_.end(),topic);
      if(it == topics_.end())
      {

        topicTree.addSubscriber(topic,
                                boost::any_cast<boost::shared_ptr<MqttClientSession> >(conn->getContext()));

        topics_.push_back(topic);
      }
    }

    LOG_INFO <<"subTopic "<< topic << ",qos  " << static_cast<int>(qos);
    num -= buffer.readableBytes();
    readedNum += num;
  }

  sendSuback(conn,mid,qosVector);
  return true;
}

bool MqttClientSession::mqttHandleUnsubcribe(const TcpConnectionPtr& conn, Buffer& buffer,const size_t len)
{
  uint16_t mid = buffer.readInt16();

  MqttTopicTree& topicTree = Singleton<MqttTopicTree>::instance();
  size_t readedNum = 0;
  readedNum += sizeof(mid);
  while (readedNum < len)
  {
    //暂存缓冲区中可读数据
    size_t num = buffer.readableBytes();

    string topic;
    readMqttString(topic, buffer);

    topicTree.unSubscriber(topic,boost::shared_ptr<MqttClientSession>(shared_from_this()));
    LOG_INFO << "Unsubcribe " << topic;

    topics_.remove(topic);

    num -= buffer.readableBytes();
    readedNum += num;
  }

  sendUnsuback(conn,mid);
  return true;
}

bool MqttClientSession::mqttHandleDisconnect(const TcpConnectionPtr& conn, const size_t len)
{
  bool ret = (len == 0);
  if(ret)
  {
    //主动disconnect 则删除遗嘱消息
    willMsgPtr_.reset();
    conn->shutdown();
  }
  return ret;
}

void MqttClientSession::mqttHandlePingReq(const TcpConnectionPtr& conn)
{
  uint8_t message[2] = {PINGRESP,0};
  conn->send(message,sizeof(message));
}

bool MqttClientSession::mqttHadnlePublish(const TcpConnectionPtr& conn, Buffer& buffer, uint8_t header, const size_t len)
{
  uint8_t dup = static_cast<uint8_t>((header & 0x08)>>3);
  uint8_t qos = static_cast<uint8_t>((header & 0x06)>>1);
  bool retain = (header & 0x01);
  assert(qos != 3);
  if(qos == 3) return false;

  string topic;
  readMqttString(topic,buffer);
  assert(topic.size() > 0);

  uint16_t mid = 0;
  uint32_t payloadLen = static_cast<uint32_t>(len - (topic.size() + 2));
  if(qos > 0)
  {
    payloadLen -= 2;
    mid = buffer.readInt16();
  }

  boost::shared_ptr<MqttMessage> msgPtr(new MqttMessage());
  msgPtr->dup = dup;
  msgPtr->mid = mid;
  msgPtr->qos = qos;
  msgPtr->state = MqttMessage::ms_publish;
  msgPtr->retain = retain;
  msgPtr->topic = topic;
  msgPtr->remainglen = len;
  msgPtr->timestamp = Timestamp::now();

  MqttTopicTree& topicTree = Singleton<MqttTopicTree>::instance();
  if(payloadLen > 0)
  {
    msgPtr->payload.reserve(payloadLen);
    msgPtr->payload.assign(buffer.peek(),payloadLen);
    buffer.retrieve(payloadLen);
  }
  else //payloadLen == 0
  {
    topicTree.delRetainMsg(msgPtr->topic);
  }

  if(qos != 2)
  {
    if(qos == 1)
    {
      msgPtr->mid = NewMid();
      sendPublishAck(conn,mid);
    }

    topicTree.Publish(topic,msgPtr);
  }
  else //qos == 2
  {
    msgPtr->state = MqttMessage::ms_wait_for_pubrel;
    recvUnconfdMsgs_.push(msgPtr);
    sendPubRec(conn,mid);
  }

  LOG_DEBUG << "topic: " << topic
            << " qos: " << static_cast<int>(qos)
            << " dup: " << static_cast<int>(dup)
            << " mid: " << static_cast<int>(mid)
            << " retain: " << static_cast<int>(retain)
            << " payload: " << msgPtr->payload ;

  return true;
}

void MqttClientSession::mqttHandlePublishAck(const TcpConnectionPtr& conn, Buffer& buffer, const size_t len)
{
  uint16_t mid = buffer.readInt16();

  //  delSendUnconfMsg(mid);
  sendUnconfdMsgs_.deleteMsg(mid);
}

void MqttClientSession::mqttHandlePublishRel(const TcpConnectionPtr& conn, Buffer& buffer, const size_t len)
{
  uint16_t mid = buffer.readInt16();

  boost::shared_ptr<MqttMessage> msg = recvUnconfdMsgs_.getandDelMsg(mid);
  assert(msg);
  if(msg)
  {
    MqttTopicTree& topicTree = Singleton<MqttTopicTree>::instance();

    msg->mid = NewMid();
    topicTree.Publish(msg->topic,msg);

    sendPubComp(conn,mid);
  }

}

void MqttClientSession::mqttHandlePublishRec(const TcpConnectionPtr& conn, Buffer& buffer, const size_t len)
{
  uint16_t mid = buffer.readInt16();
  sendPubRel(conn,mid);
}

void MqttClientSession::mqttHandlePublishComp(const TcpConnectionPtr& conn, Buffer& buffer, const size_t len)
{
  uint16_t mid = buffer.readInt16();

  sendUnconfdMsgs_.deleteMsg(mid);
}

void MqttClientSession::checkAlive()
{
  Timestamp now(Timestamp::now());
  double freeTime = timeDifference(now,lastInTime_);
  if(freeTime > (1.5 * keepalive_))
  {
    TcpConnectionPtr conn = TcpConWeakPtr_.lock();
    if(conn)
    {
      conn->forceClose();
    }
  }
}

std::vector<uint8_t> MqttClientSession::packageMsg(const boost::shared_ptr<MqttMessage>& msg, const size_t& remainglen)
{
  std::vector<uint8_t> remaingBytes = encodeRemainingLenth(static_cast<uint32_t>(remainglen));
  assert(remaingBytes.size() != 0);

  size_t size = remaingBytes.size() + remainglen + 1;

  std::vector<uint8_t> sendBuf(size);

  std::vector<uint8_t>::iterator it = sendBuf.begin();
  *it = static_cast<uint8_t>(PUBLISH | ((msg->dup&0x1)<<3) | (msg->qos<<1) | msg->retain);
  ++it;

  std::copy(remaingBytes.begin(),remaingBytes.end(),it);
  it += remaingBytes.size();

  *it = MSB(msg->topic.size());  ++it;
  *it = LSB(msg->topic.size());  ++it;
  std::copy(msg->topic.begin(),msg->topic.end(),it); it += msg->topic.size();

  if(msg->qos > 0)
  {
    *it = MSB(msg->mid);  ++it;
    *it = LSB(msg->mid);  ++it;
  }

  std::copy(msg->payload.begin(), msg->payload.end(), it);

  return sendBuf;
}

void MqttClientSession::sendSuback(const TcpConnectionPtr& conn, uint16_t mid,const std::vector<uint8_t>& payload)
{
  uint32_t remainingLength = static_cast<uint32_t>(payload.size() + 2);
  std::vector<uint8_t> remainingBytes = encodeRemainingLenth(remainingLength);
  assert(remainingBytes.size() != 0);

  std::vector<uint8_t> sendbuf;
  sendbuf.push_back(SUBACK);
  sendbuf.insert(sendbuf.end(),remainingBytes.begin(),remainingBytes.end());

  sendbuf.push_back(MSB(mid));
  sendbuf.push_back(LSB(mid));

  sendbuf.insert(sendbuf.end(),payload.begin(),payload.end());

  conn->send(sendbuf.data(),static_cast<int>(sendbuf.size()));
}

void MqttClientSession::sendUnsuback(const TcpConnectionPtr& conn, uint16_t mid)
{
  uint8_t message[4] = {UNSUBACK,2,MSB(mid),LSB(mid)};
  conn->send(message,sizeof(message));
}

void MqttClientSession::sendPublishAck(const TcpConnectionPtr& conn, uint16_t mid)
{
  uint8_t message[4] = {PUBACK,2,MSB(mid),LSB(mid)};
  conn->send(message,sizeof(message));

}

void MqttClientSession::sendPubRec(const TcpConnectionPtr& conn, uint16_t mid)
{
  uint8_t message[4] = {PUBREC,2,MSB(mid),LSB(mid)};
  conn->send(message,sizeof(message));
}

void MqttClientSession::sendPubRel(const TcpConnectionPtr& conn, uint16_t mid)
{
  uint8_t message[4] = {PUBREL|0x02,2,MSB(mid),LSB(mid)};
  conn->send(message,sizeof(message));
}

void MqttClientSession::sendPubComp(const TcpConnectionPtr& conn, uint16_t mid)
{
  uint8_t message[4] = {PUBCOMP,2,MSB(mid),LSB(mid)};
  conn->send(message,sizeof(message));
}

void MqttClientSession::sendPingResp(const TcpConnectionPtr& conn)
{
  uint8_t message[2] = {PINGRESP,0};
  conn->send(message,sizeof(message));
}


int MqttClientSession::readMqttString(string& buf, Buffer& buffer)
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

std::vector<uint8_t> MqttClientSession::encodeRemainingLenth(uint32_t remainingLength)
{
  uint8_t remaining_bytes[4] = {0};
  uint8_t byte;
  int i=0;

  do
  {
    byte = remainingLength % 128;
    remainingLength = remainingLength / 128;
    /* If there are more digits to encode, set the top bit of this digit */
    if(remainingLength > 0)
      byte = byte | 0x80;

    remaining_bytes[i++] = byte;
  }while(remainingLength > 0 && i < 5);
  assert(i < 5);
  if(i >= 5)
    return std::vector<uint8_t>();

  return std::vector<uint8_t>(remaining_bytes,remaining_bytes + i);
}

void MqttMsgList::push(MqttMsgList::type_mid mid, const boost::shared_ptr<MqttMessage>& msg)
{
  MutexLockGuard lock(mutex_);
  msgs_[mid] = msg;
}

void MqttMsgList::push(const boost::shared_ptr<MqttMessage>& msg)
{
  MutexLockGuard lock(mutex_);
  type_mid mid = msg->mid;
  msgs_[mid] = msg;
}

void MqttMsgList::insert(MqttMsgList::Iterator first, MqttMsgList::Iterator last)
{
  MutexLockGuard lock(mutex_);
  msgs_.insert(first,last);
}

boost::shared_ptr<MqttMessage> MqttMsgList::getandDelMsg(MqttMsgList::type_mid mid)
{
  MutexLockGuard lock(mutex_);
  boost::shared_ptr<MqttMessage> ret = msgs_.find(mid)->second;
  msgs_.erase(mid);
  return ret;
}

void MqttMsgList::deleteMsg(MqttMsgList::type_mid mid)
{
  MutexLockGuard lock(mutex_);
  msgs_.erase(mid);
}

MqttMsgList::type_msgs MqttMsgList::copy()
{
  MutexLockGuard lock(mutex_);
  return msgs_;
}
