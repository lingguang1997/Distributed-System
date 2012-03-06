// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"
#include "lock_protocol.h"

using namespace std;

struct comp_file {
  extent_protocol::attr attr;
  string content;
  bool is_dirty;
  bool should_remove;
  bool is_existed_in_server;
};

class extent_client {
 private:
  rpcc *cl;
  Mutex data_cache_mutex;
  map<extent_protocol::extentid_t, comp_file> data_cache;
  extent_protocol::status get_comp_file(extent_protocol::extentid_t eid,
                                                       std::string &buf,
                                                       extent_protocol::attr &attr);

 public:
  extent_client(std::string dst);

  extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  extent_protocol::status flush(extent_protocol::extentid_t eid);
};

#endif
