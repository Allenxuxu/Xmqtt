#include "MqttofflineClientList.h"


void MqttofflineClientList::pushClient(const string& clientID, const boost::shared_ptr<MqttClientSession>& client)
{
  MutexLockGuard lock(mutex_);
  clients_[clientID] = client;
}

boost::shared_ptr<MqttClientSession> MqttofflineClientList::popClient(const string& clientID)
{
  boost::shared_ptr<MqttClientSession> ptr;

  {
    MutexLockGuard lock(mutex_);
    Iterator it = clients_.find(clientID);
    if(it != clients_.end())
    {
      ptr = clients_[clientID];
      clients_.erase(clientID);
    }
  }

  return ptr;
}

