#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include <muduo/base/AsyncLogging.h>
#include <muduo/base/Singleton.h>
#include <muduo/base/Types.h>
#include <boost/bind.hpp>

#include "MqttServer.h"
#include "MqttTopicTree.h"

using namespace muduo;
off_t kRollSize = 500*1000*1000;

int main(int argc, char* argv[])
{
  Logger::setLogLevel(Logger::DEBUG);
  string logfilename( ::basename(argv[0]));
  AsyncLogging log(logfilename, kRollSize);
  log.start();

  EventLoop loop;
  InetAddress listenAddr(1883);
  MqttServer server(&loop, listenAddr, 4);

  server.start();
  loop.loop();
}
