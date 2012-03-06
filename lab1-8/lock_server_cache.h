#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"

#include "rsm.h"
#include <map>
#include <set>
#include <utility>
using namespace std;

struct lock_resource
{
  bool is_allocated;
  unsigned seq_num;
  int clt;
};

class lock_server_cache : public rsm_state_transfer {
 private:
  class rsm *rsm;
// <lab8>
  void clear();
// </lab8>
 public:
  lock_server_cache(class rsm *rsm = 0);
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  Mutex m;
  ConditionVar rtr_cv;
  ConditionVar rvk_cv;
  map<lock_protocol::lockid_t, lock_resource*> lock_allocation_map;
  map<int, map<lock_protocol::lockid_t, unsigned int> > clt_lid_seq_map;
  map<lock_protocol::lockid_t, set<int> > retry_map;
  map<int, map<lock_protocol::lockid_t, unsigned int> > revoke_map;
  map<int, rpcc*> client_map;
// <lab8>
  map<int, int> client_port_map;
// </lab8>

  bool quit;
  unsigned int has_release;
  void add_retry_request(lock_protocol::lockid_t lid, int clt);
  void add_revoke_request(int clt, lock_protocol::lockid_t lid);

// <lab8>
  string marshal_state();
  void unmarshal_state(string);
// </lab8>
 public:
  lock_server_cache();
  lock_protocol::status stat(int clt, lock_protocol::lockid_t, int &);
  lock_protocol::status acquire(int clt, lock_protocol::lockid_t, unsigned int, int &);
  lock_protocol::status release(int clt, lock_protocol::lockid_t, unsigned int, int &);
  lock_protocol::status subscribe(int clt, int rlock_port, int &);
  lock_protocol::status unsubscribe(int clt, int &);
  void revoker();
  void retryer();
};

#endif
