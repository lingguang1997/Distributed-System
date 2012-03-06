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
  /*int r;
  int ret = put(0x00000001, "", r);
  assert(ret == extent_protocol::OK);*/
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
// <lab2>
  std::cout << "[put]: eid " << id << " extent server enter" << std::endl;
  extent_protocol::attr attr_;

  attr_.ctime = time(NULL);
  attr_.mtime = attr_.ctime;
  attr_.atime = attr_.ctime;
  attr_.size = buf.size(); 
  file_cache_mutex.lock();
  std::cout << "[get]: eid " << id << " file_cache_mutex locked " << id << std::endl;
  if (is_file_existed(id))
  {
    attr_.atime = file_cache[id].first.atime;
  }
  else
  {
    attr_.atime = attr_.ctime;
  }

  file_cache[id] = make_pair(attr_, buf);
  file_cache_mutex.unlock();
  std::cout << "[get]: eid " << id << " file_cache_mutex unlocked " << id << std::endl;
  std::cout << "[put]: eid " << id << "extent server exit" << std::endl;
  return extent_protocol::OK;
// </lab2>
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
// <lab2>
  std::cout << "[get]: eid" << id << "extent server enter" << std::endl;
  int code = extent_protocol::NOENT;
  file_cache_mutex.lock();
  std::cout << "[get]: eid " << id << " file_cache_mutex locked " << id << std::endl;
  if (is_file_existed(id))
  {
    std::cout << "[get]: extent server get eid " << id << " from cache" << std::endl;
    buf = file_cache[id].second;
    file_cache[id].first.atime = time(NULL);
    code = extent_protocol::OK;  
  }
  else
  {
    std::cout << "[get]: eid "<< id << " extent server doesn't have eid " << id << std::endl;
  }
  file_cache_mutex.unlock();
  std::cout << "[get]: eid " << id << " file_cache_mutex unlocked " << id << std::endl;
  return code;
// </lab2>
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.
// <lab2>
  std::cout << "[getattr]: eid " << id << " extent server enter" << std::endl;
  int code = extent_protocol::NOENT;
  file_cache_mutex.lock();
  std::cout << "[getattr]: eid " << id << " file_cache_mutex locked" << std::endl;
  if (is_file_existed(id))
  {
    std::cout << "[getattr]: eid " << id << " extent server has eid " << id << std::endl;
    extent_protocol::attr attr_ = file_cache[id].first;
    a.size = attr_.size;
    a.atime = attr_.atime;
    a.mtime = attr_.mtime;
    a.ctime = attr_.ctime;
    code = extent_protocol::OK;
  }
  else{
    std::cout << "[getattr]: eid " << id << " extent server doesn't have eid " << id << " ,return all property 0" << std::endl;
    a.size = 0;
    a.atime = 0;
    a.mtime = 0;
    a.ctime = 0;
  }
  file_cache_mutex.unlock();
  std::cout << "[getattr]: eid " << id << " extent server exit" << std::endl;
  return code;
// </lab2>
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
// <lab2>
  std::cout << "[remove]: eid " << id << " extent server enter" << std::endl;
  int code = extent_protocol::NOENT;
  file_cache_mutex.lock();
  std::cout << "[remove]: eid " << id << " file_cache_mutex locked" << std::endl;
  if (is_file_existed(id))
  {
    file_cache.erase(id);
    code = extent_protocol::OK;
  }
  file_cache_mutex.unlock();
  std::cout << "[remove]: eid " << id << " file_cache_mutex unlocked" << std::endl;
  std::cout << "[remove]: eid " << id << " extent server exit" << std::endl;
  return code;
// </lab2>
}

// <lab2>
bool extent_server::is_file_existed(extent_protocol::extentid_t id) { 
  return (file_cache.find(id) != file_cache.end());
}
// </lab2>

// <lab6>
int extent_server::setattr(extent_protocol::extentid_t id, extent_protocol::attr attr, int &r) {
  std::cout << "[setattr]: eid " << id << " extent server enter" << std::endl;
  file_cache_mutex.lock();
  std::cout << "[setattr]: eid " << id << " file_cache_mutex locked" << std::endl;
  file_cache[id].first.atime = attr.atime;
  file_cache[id].first.mtime = attr.mtime;
  file_cache[id].first.ctime = attr.ctime;
  file_cache[id].first.size = attr.size;
  file_cache_mutex.unlock();
  std::cout << "[setattr]: eid " << id << " file_cache_mutex unlocked" << std::endl;
  std::cout << "[setattr]: eid " << id << " extent server exit" << std::endl;
  return extent_protocol::OK;
}
// </lab6>
