#ifndef MQTTTOPICTREE_H
#define MQTTTOPICTREE_H

#include <list>
#include <map>
#include <algorithm>
#include <boost/weak_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <muduo/base/Mutex.h>
#include <muduo/base/Types.h>

#include "MqttMessage.h"
#include "MqttClient.h"


class MqttTopicTree : boost::noncopyable
{
  struct content;
public:
  typedef std::list<boost::weak_ptr<MqttClientSession> > type_subscribersList;
  typedef std::map<string,content> type_topicMap;
  typedef std::map<string,type_subscribersList> type_wildcardsTopicMap;
  typedef boost::shared_ptr<type_topicMap> type_topicMapPtr;
  typedef boost::shared_ptr<type_wildcardsTopicMap> type_wildcardsTopicMapPtr;
  typedef type_subscribersList::iterator Iterator;


  MqttTopicTree();

  void addSubscriber(const string& topic,const boost::shared_ptr<MqttClientSession>& subscriber);

  void unSubscriber(const string& topic,const boost::shared_ptr<MqttClientSession>& subscriber);

  void Publish(const string& topic, const boost::shared_ptr<MqttMessage>& msg);

  void addRetainMsg(const boost::shared_ptr<MqttMessage>& msg);
  void delRetainMsg(const string& topic);

private:
  bool haveWildcards(const string& topic) const;
  bool matchingWildcard(const string& wildcardTopic, const string& topic) const;
  type_subscribersList  querySubscribers(const string& topic);
  std::vector<boost::shared_ptr<MqttMessage> > getRetainMsg(const string& topic);



  struct findSubscriber
  {
    findSubscriber(const boost::shared_ptr<MqttClientSession>& subscriber)
      :subscriber_(subscriber)
    { }
    bool operator ()( type_subscribersList::value_type& subscriber)
    {
      boost::shared_ptr<MqttClientSession> ptr = subscriber.lock();
      return ptr == subscriber_;
    }
  private:
    const boost::shared_ptr<MqttClientSession>& subscriber_;
  };

  struct content
  {
    type_subscribersList subscribers_;
    boost::shared_ptr<MqttMessage> retainMsg_;
  };

  type_topicMapPtr topicMapPtr_;
  MutexLock mutexTopicMap_;

  type_wildcardsTopicMapPtr  wildcardTopicMapPtr_;
  MutexLock mutexWildcardTopicMap_;
};

#endif // MQTTTOPICTREE_H
