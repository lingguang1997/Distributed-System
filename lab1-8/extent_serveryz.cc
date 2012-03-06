// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server()
{
   printf("@yuzhui [extent_server]: constructor()\n");
  pthread_mutex_init(&extent_mutex, NULL); 
  int ret = init();
  if(extent_protocol::OK != ret)
    printf("@yuzhui: [extent_server]: constructor() can not init()\n");
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
printf("@yuzhui: [extent_server]: put() put %016llx\n",id);
  cout<<"with string "<<buf<<endl;
  entry en;
  en.attr.atime = 0;
  time_t t = time(NULL);
  en.attr.atime = (unsigned int)t;
  en.attr.mtime = en.attr.atime;
  en.attr.ctime = en.attr.atime;
  en.attr.size = buf.size();
  en.content = buf;
  printf("@yuzhui: [extent_server]: put() ");
  std::cout<<"put the inum: "<<id<<" and str "<<buf<<" in extent_server"<<endl;
  pthread_mutex_lock(&extent_mutex);
  datamap[id] = en;
  pthread_mutex_unlock(&extent_mutex);
  return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
printf("@yuzhui: [extent_server]: get() get inum %016llx \n",id);
  pthread_mutex_lock(&extent_mutex);
  if(datamap.find(id)!=datamap.end()){
    buf = datamap[id].content;	
    datamap[id].attr.atime = time(NULL);
    printf("@yuzhui: [extent_server]: get() get the content succ for inum %016llx ",id);
    std::cout<<"content = "<<buf<<endl;
    pthread_mutex_unlock(&extent_mutex);
    return extent_protocol::OK;
  }
  else
  {
    printf("@yuzhui: [extent_server]: get() can not get the content for inum %016llx \n",id);	
    pthread_mutex_unlock(&extent_mutex);
    return extent_protocol::NOENT;
  }
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.
pthread_mutex_lock(&extent_mutex);
  if(datamap.find(id)!=datamap.end()){
    a.size = datamap[id].attr.size;
    a.atime = datamap[id].attr.atime;
    a.mtime = datamap[id].attr.mtime;
    a.ctime = datamap[id].attr.ctime;
    pthread_mutex_unlock(&extent_mutex);
    printf("@yuzhui: [extent_server]: getattr() get the attr succ inum %016llx \n",id);
    return extent_protocol::OK;
  }
  else
  {
    a.size = 0;
    a.atime = 0;
    a.mtime = 0;
    a.ctime = 0;
    pthread_mutex_unlock(&extent_mutex);
    printf("@yuzhui: [extent_server]: getattr() can not get the attr inum %016llx \n",id);
    return extent_protocol::NOENT;
  }
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
 printf("@yuzhui: [extent_server]: remove() id = %016llx \n",id);
  pthread_mutex_lock(&extent_mutex);
  if(datamap.find(id)!=datamap.end())
  {
    datamap.erase(id);
    pthread_mutex_unlock(&extent_mutex);
    return extent_protocol::OK;
  }
  else
  {
    pthread_mutex_unlock(&extent_mutex);
    return extent_protocol::NOENT;
  }
}

// <lab2>
bool extent_server::is_file_existed(extent_protocol::extentid_t id) { 
  return (file_cache.find(id) != file_cache.end());
}
// </lab2>

// <lab6>
int extent_server::setattr(extent_protocol::extentid_t id, extent_protocol::attr attr, int &r) {
 printf("@yuzhui: [extent_server]: setattr() id = %016llx \n",id);
  pthread_mutex_lock(&extent_mutex);
  datamap[id].attr.atime = attr.atime; 
  datamap[id].attr.atime = attr.mtime; 
  datamap[id].attr.atime = attr.ctime; 
  datamap[id].attr.atime = attr.size; 
  pthread_mutex_unlock(&extent_mutex);
 
  return extent_protocol::OK;
}
// </lab6>

int extent_server::init()
{
  printf("@yuzhui: [extent_server]: init() begin()");
  int r;
  assert(extent_protocol::OK == put(0x00000001,"",r));
  printf("@yuzhui: [extent_server]: init() begin() SUCC");
  return extent_protocol::OK; 
}
