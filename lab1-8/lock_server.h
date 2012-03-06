// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"

#include <map>



using namespace std;

struct resource_control {
  ConditionVar *cv;
  Mutex *m;
  bool allocated;
};

class lock_server {

 protected:
  int nacquire;

 public:
  lock_server();
  ~lock_server();
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);

 private:
   map<lock_protocol::lockid_t, resource_control*> resource_allocations;
   Mutex mutex;

   bool isResourceExist(const lock_protocol::lockid_t& lid);
};

#endif
