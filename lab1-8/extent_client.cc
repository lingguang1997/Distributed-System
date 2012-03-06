// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
}

extent_protocol::status
extent_client::get_comp_file(extent_protocol::extentid_t eid,
                             std::string &buf,
                             extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  data_cache_mutex.lock();
  if (data_cache.find(eid) != data_cache.end())
  {
    if (data_cache[eid].is_existed_in_server
        && !data_cache[eid].should_remove)
    {
      buf = data_cache[eid].content;
      data_cache[eid].attr.atime = time(NULL);
      attr.atime = data_cache[eid].attr.atime;
      attr.mtime = data_cache[eid].attr.mtime;
      attr.ctime = data_cache[eid].attr.ctime;
      attr.size = data_cache[eid].attr.size;
      data_cache[eid].is_dirty = true;
    }
    else
    {
      ret = extent_protocol::NOENT;
    }
  }
  else
  {
    comp_file cf;
    ret = cl->call(extent_protocol::get, eid, buf);
    if (ret == extent_protocol::OK)
    {
      cf.is_existed_in_server = true;
    }
    else if (ret == extent_protocol::NOENT)
    {
      cf.is_existed_in_server = false;
    }
    cf.content = buf;
    cl->call(extent_protocol::getattr, eid, attr);
    cf.attr.atime = attr.atime;
    cf.attr.ctime = attr.ctime;
    cf.attr.mtime = attr.mtime;
    cf.attr.size = attr.size;
    cf.is_dirty = false;
    cf.should_remove = false;
    data_cache[eid] = cf;
  }
  data_cache_mutex.unlock();
  return ret;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::attr attr;
  int ret = get_comp_file(eid, buf, attr);
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  string buf;
  int ret = get_comp_file(eid, buf, attr);
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  std::cout << "[put]: extent client " << eid
            << " enter" << std::endl; 
  extent_protocol::status ret = extent_protocol::OK;
  data_cache_mutex.lock();
  std::cout << "[put]: eid " << eid << " data_cache_mutex locked" << std::endl;
  comp_file cf;
  cf.content = buf;
  cf.attr.atime = time(NULL);
  cf.attr.mtime = cf.attr.atime;
  cf.attr.ctime = cf.attr.atime;
  cf.attr.size = buf.size();
  cf.is_dirty = true;
  cf.should_remove = false;
  cf.is_existed_in_server = true;
  data_cache[eid] = cf; 
  data_cache_mutex.unlock();
  std::cout << "[put]: eid " << eid << " data_cache_mutex unlocked" << std::endl;
  
  std::cout << "[put]: extent client " << eid << " exit" << std::endl;
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  std::cout << "[remove]: extent client " << eid << " enter" << std::endl;
  extent_protocol::status ret = extent_protocol::OK;
  data_cache_mutex.lock();
  std::cout << "[remove]: eid " << eid << " data_cache_mutex locked" << std::endl;
  comp_file cf;
  cf.is_dirty = true;
  cf.should_remove = true;
  data_cache[eid] = cf;
  data_cache_mutex.unlock();
  std::cout << "[remove]: eid " << eid << " data_cache_mutex unlocked" << std::endl;

  std::cout << "[remove]: extent client " << eid << " exit" << std::endl;
  return ret;
}

extent_protocol::status
extent_client::flush(extent_protocol::extentid_t eid)
{
  std::cout << "[flush]: extent client " << eid << " enter" << std::endl;
  int ret = extent_protocol::OK;
  data_cache_mutex.lock();
  std::cout << "[flush]: eid " << eid << " data_cache_mutex locked" << std::endl;
  comp_file cf = data_cache[eid];
  if (cf.is_dirty)
  {
    std::cout << "[flush]: extent client " << eid << " is dirty" << std::endl;
    int r;
    if (cf.should_remove)
    {
      std::cout << "[flush]: extent client " << eid << " should remove" << std::endl;
      ret = cl->call(extent_protocol::remove, eid, r);
      std::cout << "[flush]: extent client " << eid << " remove" << std::endl;
    }
    else
    {
      string buf = cf.content;
      extent_protocol::attr attr = cf.attr;
      std::cout << "[flush]: extent client " << eid << " before put content" << std::endl;
      ret = cl->call(extent_protocol::put, eid, buf, r);
      std::cout << "[flush]: extent client " << eid << " after put content" << std::endl;
      assert(ret == extent_protocol::OK);
      std::cout << "[flush]: extent client " << eid << " before put attr" << std::endl;
      ret = cl->call(extent_protocol::setattr, eid, attr, r);
      std::cout << "[flush]: extent client " << eid << " after put attr" << std::endl;
      assert(ret == extent_protocol::OK);
      std::cout << "[flush]: extent client " << eid << " put" << std::endl;
    }
  }
  data_cache.erase(eid);
  data_cache_mutex.unlock();
  std::cout << "[flush]: eid " << eid << " data_cache_mutex unlocked" << std::endl;
  std::cout << "[flush]: extent client " << eid << " exit" << std::endl;
  return ret;
}

