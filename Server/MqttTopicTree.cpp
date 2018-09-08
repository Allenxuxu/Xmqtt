#include "MqttTopicTree.h"
#include <boost/bind.hpp>
#include <boost/algorithm/string/split.hpp>

#include <boost/algorithm/string/classification.hpp>
#include <vector>
#include <muduo/base/Logging.h>


MqttTopicTree::MqttTopicTree()
  : topicMapPtr_(new type_topicMap()),
    wildcardTopicMapPtr_(new type_wildcardsTopicMap())
{
  assert(topicMapPtr_);
}


void MqttTopicTree::addSubscriber(const string& topic, const boost::shared_ptr<MqttClientSession>& subscriber)
{
  if( !haveWildcards(topic))
  {
    MutexLockGuard lock(mutexTopicMap_);
    if(!topicMapPtr_.unique())
    {
      type_topicMapPtr newTopicMapPtr(new type_topicMap(*topicMapPtr_));
      topicMapPtr_.swap(newTopicMapPtr);
    }
    content& v = (*topicMapPtr_)[topic];
    v.subscribers_.push_back(subscriber);
    if(v.retainMsg_)
    {
      subscriber->publish(v.retainMsg_);
    }
  }
  else // have wildcards
  {
    {
      MutexLockGuard lock(mutexWildcardTopicMap_);
      if(!wildcardTopicMapPtr_.unique())
      {
        type_wildcardsTopicMapPtr newwildcardsTopicMapPtr(new type_wildcardsTopicMap(*wildcardTopicMapPtr_));
        wildcardTopicMapPtr_.swap(newwildcardsTopicMapPtr);
      }
      type_subscribersList& subscribers = (*wildcardTopicMapPtr_)[topic];
      subscribers.push_back(subscriber);
    }
    std::vector<boost::shared_ptr<MqttMessage> > retainMsgs = getRetainMsg(topic);
    for(std::vector<boost::shared_ptr<MqttMessage> >::iterator it=retainMsgs.begin();
        it!=retainMsgs.end(); ++it)
    {
      subscriber->publish(*it);
    }

  }
}

void MqttTopicTree::unSubscriber(const string& topic, const boost::shared_ptr<MqttClientSession>& subscriber)
{
  if( !haveWildcards(topic))
  {
    LOG_DEBUG << "unsub " << topic;
    MutexLockGuard lock(mutexTopicMap_);
    if(!topicMapPtr_.unique())
    {
      type_topicMapPtr newTopicMapPtr(new type_topicMap(*topicMapPtr_));
      topicMapPtr_.swap(newTopicMapPtr);
    }
    content& v = (*topicMapPtr_)[topic];
    type_subscribersList::iterator it = std::find_if( v.subscribers_.begin(),
                                                      v.subscribers_.end(),
                                                      findSubscriber(subscriber));
    if(it != v.subscribers_.end())
      v.subscribers_.erase(it);

    if(v.subscribers_.size() == 0 && !v.retainMsg_)
      topicMapPtr_->erase(topic);
  }
  else
  {
    LOG_DEBUG << "unsub # " << topic;
    MutexLockGuard lock(mutexWildcardTopicMap_);
    if(!wildcardTopicMapPtr_.unique())
    {
      type_wildcardsTopicMapPtr newwildcardsTopicMapPtr(new type_wildcardsTopicMap(*wildcardTopicMapPtr_));
      wildcardTopicMapPtr_.swap(newwildcardsTopicMapPtr);
    }
    type_subscribersList& subscribers = (*wildcardTopicMapPtr_)[topic];
    type_subscribersList::iterator it = std::find_if( subscribers.begin(),
                                                      subscribers.end(),
                                                      findSubscriber(subscriber));
    if(it != subscribers.end())
      subscribers.erase(it);

    if(subscribers.size() == 0)
      wildcardTopicMapPtr_->erase(topic);
  }
}

MqttTopicTree::type_subscribersList MqttTopicTree::querySubscribers(const string& topic)
{
  assert(!haveWildcards(topic));
  type_subscribersList ret;

  {
    type_topicMapPtr ptr;
    {
      MutexLockGuard lock(mutexTopicMap_);
      ptr = topicMapPtr_;
    }

    type_topicMap::iterator it = ptr->find(topic);
    if(it != ptr->end())
    {
      type_subscribersList tmp = it->second.subscribers_;
      ret.merge(tmp);
    }
  }

  //通配符订阅者
  {
    type_wildcardsTopicMapPtr ptr;
    {
      MutexLockGuard lock(mutexWildcardTopicMap_);
      ptr = wildcardTopicMapPtr_;
    }
    for(type_wildcardsTopicMap::iterator it=ptr->begin(); it!=ptr->end(); ++it)
    {
      if(matchingWildcard(it->first, topic))
      {
        type_subscribersList tmp = it->second;
        ret.merge(tmp);
      }
    }
  }

  return ret;
}

std::vector<boost::shared_ptr<MqttMessage> > MqttTopicTree::getRetainMsg(const string& topic)
{
  type_topicMapPtr ptr;
  {
    MutexLockGuard lock(mutexTopicMap_);
    ptr = topicMapPtr_;
  }

  std::vector<boost::shared_ptr<MqttMessage> > ret;
  for(type_topicMap::iterator it=topicMapPtr_->begin(); it!=topicMapPtr_->end(); ++it)
  {
    string itTopic = it->first;
    if(matchingWildcard(topic,itTopic) && it->second.retainMsg_)
      ret.push_back(it->second.retainMsg_);
  }

  return ret;
}


bool MqttTopicTree::haveWildcards(const string& topic) const
{
  string::size_type pos1 = topic.find_first_of('#');
  string::size_type pos2 = topic.find_last_of('+');

  return (pos1 != string::npos || pos2 != string::npos);
}


bool MqttTopicTree::matchingWildcard(const string& wildcardTopic, const string& topic) const
{
  std::vector<string> vWildcardTopic;
  boost::split(vWildcardTopic, wildcardTopic, boost::is_any_of( "/" ));
  std::vector<string> vTopic;
  boost::split(vTopic, topic, boost::is_any_of( "/" ));

  typedef std::vector<string>::iterator Viterator;
  for(Viterator Wit=vWildcardTopic.begin(),it=vTopic.begin();
      Wit!=vWildcardTopic.end() && it!=vTopic.end();
      ++Wit, ++it)
  {
    if(*Wit == *it || *Wit == "+")
      continue;
    else if(*Wit == "#")
      return true;
    else
      return false;
  }

  if(vWildcardTopic.size() == vTopic.size())  //数量相等，for循环全部匹配成功
    return true;
  else
    return false;
}

void MqttTopicTree::Publish(const string& topic, const boost::shared_ptr<MqttMessage>& msg)
{
  if(msg->retain && msg->payload.size() > 0)
    addRetainMsg(msg);

  MqttTopicTree::type_subscribersList list = querySubscribers(topic);
  for(Iterator it=list.begin(); it!=list.end(); ++it)
  {
    boost::shared_ptr<MqttClientSession> ptr = it->lock();
    if(ptr)
      ptr->publish(msg);
  }
}

void MqttTopicTree::addRetainMsg(const boost::shared_ptr<MqttMessage>& msg)
{
  MutexLockGuard lock(mutexTopicMap_);
  if(!topicMapPtr_.unique())
  {
    type_topicMapPtr newTopicMapPtr(new type_topicMap(*topicMapPtr_));
    topicMapPtr_.swap(newTopicMapPtr);
  }
  assert(msg->topic.length() != 0);
  content& v = (*topicMapPtr_)[msg->topic];
  v.retainMsg_ = msg;
}

void MqttTopicTree::delRetainMsg(const string& topic)
{
  MutexLockGuard lock(mutexTopicMap_);
  if(!topicMapPtr_.unique())
  {
    type_topicMapPtr newTopicMapPtr(new type_topicMap(*topicMapPtr_));
    topicMapPtr_.swap(newTopicMapPtr);
  }

  type_topicMap::iterator it = topicMapPtr_->find(topic);
  if(it != topicMapPtr_->end())
  {
    content& v = it->second;
    v.retainMsg_.reset();
  }
}



