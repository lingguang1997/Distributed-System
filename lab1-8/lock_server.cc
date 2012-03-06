// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
}

lock_server::~lock_server()
{
  map<lock_protocol::lockid_t, resource_control*>::iterator it;
  for (it = resource_allocations.begin(); it != resource_allocations.end(); ++it)
  {
    delete it->second->cv;
    delete it->second->m;
    delete it->second;
  }
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  mutex.lock();
  if (isResourceExist(lid))
  {
    mutex.unlock();
    resource_control *rc = resource_allocations[lid];
    rc->m->lock();
    while (rc->allocated)
    {
      rc->cv->wait(rc->m);
    }
    rc->allocated = true;
    rc->m->unlock();
  }
  else
  {
    resource_control *rc = (resource_control *)malloc(sizeof(resource_control));
    rc->allocated = true;
    rc->cv = new ConditionVar();
    rc->m = new Mutex();
    resource_allocations[lid] = rc;
    mutex.unlock();
  }
  lock_protocol::status ret = lock_protocol::OK;
  return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  if (isResourceExist(lid))
  {
    resource_control *rc = resource_allocations[lid];
    rc->m->lock();
    rc->allocated = false;
    rc->cv->signal();
    rc->m->unlock();
  }
  else
  {
    ret = lock_protocol::RPCERR;
  }
  return ret;
}

bool
lock_server::isResourceExist(const lock_protocol::lockid_t& lid) {
  return (resource_allocations.find(lid) != resource_allocations.end());
}
