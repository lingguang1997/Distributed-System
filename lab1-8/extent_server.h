// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include "extent_protocol.h"
#include "lock_protocol.h"

// <lab2>
using namespace std;
// </lab2>

class extent_server {

 public:
  extent_server();

  int put(extent_protocol::extentid_t id, std::string, int &);
  int get(extent_protocol::extentid_t id, std::string &);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
  int setattr(extent_protocol::extentid_t id, extent_protocol::attr, int&);
  int remove(extent_protocol::extentid_t id, int &);
// <lab2> 
 private:
  extent_protocol::extentid_t inum_; 
  map<extent_protocol::extentid_t, pair<extent_protocol::attr, string> > file_cache;

  bool is_file_existed(extent_protocol::extentid_t id);
// </lab2> 

// <lab6>
  Mutex file_cache_mutex;
// </lab6>

};

#endif 
