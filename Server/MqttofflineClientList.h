#ifndef MQTTOFFLINECLIENTLIST_H
#define MQTTOFFLINECLIENTLIST_H

#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <muduo/base/Mutex.h>

#include "MqttClient.h"

class MqttofflineClientList : boost::noncopyable
{
  typedef std::map<string,boost::shared_ptr<MqttClientSession> >::iterator Iterator;
public:
  void pushClient(const string& clientID, const boost::shared_ptr<MqttClientSession>& client);

  boost::shared_ptr<MqttClientSession> popClient(const string& clientID);

private:
  std::map<string,boost::shared_ptr<MqttClientSession> > clients_;
  MutexLock mutex_;
};

#endif // MQTTOFFLINECLIENTLIST_H
