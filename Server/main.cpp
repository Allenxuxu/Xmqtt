#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include <muduo/base/AsyncLogging.h>
#include <muduo/base/Singleton.h>
#include <muduo/base/Types.h>
#include <boost/bind.hpp>
#include <cmdline.h>

#include "MqttServer.h"
#include "MqttTopicTree.h"

using namespace muduo;
off_t kRollSize = 500*1000*1000;

struct Options
{
  std::string ip;
  uint16_t port;
  int threads;
};

void parseCommandLine(int argc, char* argv[], Options* options)
{
  cmdline::parser par;
  par.add<std::string>("ip", 'i', "mqtt server IP address ", false, "127.0.0.1");
  par.add<uint16_t>("port", 'p', "mqtt server listen port ", false, 1883);
  par.add<int>("threads",'n',"Number of worker threads ",false,3);

  par.parse_check(argc, argv);

  options->ip = par.get<std::string>("ip");
  options->port = par.get<uint16_t>("port");
  options->threads = par.get<int>("threads");

  LOG_INFO << "listen in "<<options->ip<<":"<<options->port << " , "
           << options->threads << " worker threads.";
}

int main(int argc, char* argv[])
{
  Logger::setLogLevel(Logger::DEBUG);
  string logfilename( ::basename(argv[0]));
  AsyncLogging log(logfilename, kRollSize);
  log.start();

  EventLoop loop;
  Options opt;
  parseCommandLine(argc,argv,&opt);
  InetAddress listenAddr(opt.ip,opt.port);
  MqttServer server(&loop, listenAddr, opt.threads);

  server.start();
  loop.loop();
}
