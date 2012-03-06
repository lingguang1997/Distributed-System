// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>

#include "rsm_client.h"


static void *
releasethread(void *x)
{
  lock_client_cache *cc = (lock_client_cache *) x;
  cc->releaser();
  return 0;
}

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  cout << "create lock_client_cache" << endl;
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // assert(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  /* register RPC handlers with rlsrpc */
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry);

  int r = pthread_create(&rls_thd, NULL, &releasethread, (void *) this);
  assert (r == 0);
// <lab8>
  rc = new rsm_client(xdst);

  rc->call(lock_protocol::subscribe, cl->id(), rlock_port, r);
// </lab8>
  assert(r == 0);

  quit = false;
}

lock_client_cache::~lock_client_cache()
{
  //cout << "~lock_client_cache enter" << endl;
  quit = true;
  rvk_cv.signal();
  rtr_cv.signal();
  pthread_join(rls_thd, NULL); 
  pthread_join(rtr_thd, NULL);
  int r;
  int ret = rc->call(lock_protocol::unsubscribe, cl->id(), r);
  assert(ret == lock_protocol::OK);
  if (ret == lock_protocol::OK)
  {
    map<lock_protocol::lockid_t, lid_control*>::iterator it;
    MyScopedLock l(&m);
    for (it = lock_map.begin(); it != lock_map.end(); it++)
    {
      lock_protocol::lockid_t lid = it->first;
      lid_control *lc = it->second;
      if (lc->lstatus != NONE)
      {
        while (lock_protocol::OK != 
               rc->call(lock_protocol::release, cl->id(), lid, r));
        lc->lstatus = NONE;
      }
    }
  }
}

void
lock_client_cache::releaser()
{
  while (true)
  {
    cout << "[releaser]: name=" << name << endl;
    m.lock();
    while (revoke_map.empty())
    {
      rvk_cv.wait(&m);
    }
    name = "releaser: after wait";
    cout << "[releaser]: enter" << endl;
    map<lock_protocol::lockid_t, unsigned int> temp_revoke_map = revoke_map;
    name = "";
    m.unlock();
    set<lock_protocol::lockid_t> to_remove;
    for (map<lock_protocol::lockid_t, unsigned int>::iterator \
         it = temp_revoke_map.begin();
         it != temp_revoke_map.end();
         it++)
    {
      lock_protocol::lockid_t lid = it->first;
      unsigned int remote_seq_num = it->second;
      cout << "[releaser]: name=" << name << endl;
      m.lock();
      name = "releaser: for before";
      lid_control *lc = lock_map[lid];
      if (remote_seq_num == lc->seq_num && lc->lstatus == RELEASING)
      {
        int r;
        cout << "[releaser]: client " << cl->id() << " lid=" << lid
             << " before rpc::release" << endl;
        name = "";
        m.unlock();
        if (lu != NULL)
        {
          lu->dorelease(lid);
        }
        int ret = rc->call(lock_protocol::release, cl->id(), lid, lc->seq_num, r);
        cout << "[releaser]: client " << cl->id() << " lid=" << lid
             <<" after rpc::release" << endl;
        cout << "[releaser]: name=" << name << endl;
        m.lock();
        name = "releaser: after";
        assert(ret == lock_protocol::OK);     
        lc->lstatus = NONE;
        cout << "[releaser]: client " << cl->id() << " lid=" << lid
             << " before seq_num=" << lc->seq_num << endl;
        lc->seq_num += 1;
        to_remove.insert(lid);
        cout << "[releaser]: client " << cl->id() << " lid=" << lid
             << " after seq_num++=" << lc->seq_num << endl;
        cout << "[releaser]: client " << cl->id() << " signal the lock thread wait on lock " << lid << endl;
        cv.signal();
      }
      name = "";
      m.unlock();
    }
    m.lock();
    for (map<lock_protocol::lockid_t, unsigned int>::iterator \
         it = revoke_map.begin();
         it != revoke_map.end();
         it++)
    {
      if (it->second < lock_map[it->first]->seq_num)
      {
        revoke_map.erase(it); 
      }
    }
    m.unlock();
    cout << "[releaser]: exit" << endl;
  }
}

lock_protocol::status
lock_client_cache::ask_lock_from_server(lock_protocol::lockid_t lid, lid_control *lc)
{
  cout << "client " << cl->id() << " thread " << pthread_self() << " tries to acquire lock "
       <<  lid << " from server" << endl;
  int r;
  int ret = rc->call(lock_protocol::acquire, cl->id(), lid, lc->seq_num, r);
  if (ret != lock_protocol::OK && ret != lock_protocol::RETRY)
  {
    assert(false);
  }
  return ret;
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  cout << "[acquire]: enter, client " << cl->id() << " thread " << pthread_self()  << " lock " << lid << endl;
  lid_control *lc = NULL;
  {
    //MyScopedLock l(&m);
    cout << "[acquire]: name=" << name << endl;
    m.lock();
    name = "acquire: find lid";
    if (lock_map.find(lid) == lock_map.end())
    {
      lc = (lid_control *)malloc(sizeof(lid_control));
      lc->lstatus = NONE;
      lc->seq_num = 1;
      lock_map[lid] = lc;
      retry_map[lid] = false;
    }
    lc = lock_map[lid];
    name = "";
    m.unlock();
  }

  while (true)
  {
    switch (lc->lstatus)
    {
      case NONE:
      {
        bool is_owner = false;
        {
          //MyScopedLock l(&m);
          cout << "[acquire]: name=" << name << endl;
          m.lock();
          name = "acquire: NONE";
          if (lc->lstatus == NONE)
          {
            cout << "[acquire]: lstatus=NONE, client " << cl->id() << " thread " << pthread_self() << endl;
            lc->lstatus = ACQUIRING;
            is_owner = true;
          }
          name = "";
          m.unlock();
        }
        if (is_owner)
        {
          lock_protocol::status ret = ask_lock_from_server(lid, lc);
          while (ret == lock_protocol::RETRY)
          {
            cout << "[acquire]: name=" << name << endl;
            m.lock();
            name = "acquire: RETRY";
            if (!retry_map[lid])
            {
              cout << "[acquire]: client " << cl->id() << " thread " << pthread_self() << " should retry later, wait on lock " << lid << endl;
              name = "";
              struct timeval now;
              struct timespec next_timeout;
              gettimeofday(&now, NULL);
              next_timeout.tv_sec = now.tv_sec + 3;
              next_timeout.tv_nsec = 0;
              rtr_cv.timedWait(&m, &next_timeout);
              //rtr_cv.wait(&m);
              cout << "[acquire]: name=" << name << endl;
              name = "acquire";
            }
            retry_map[lid] = false;
            name = "";
            m.unlock();
            ret = ask_lock_from_server(lid, lc);
          }
          assert(ret == lock_protocol::OK);
          {
            //MyScopedLock l(&m);
            cout << "[acquire]: name=" << name << endl;
            m.lock();
            name = "acquire: GOT";
            lc->lstatus = LOCKED;
            cout << "[acquire]: exit, client " << cl->id() << " thread " << pthread_self() << "got the lock " << lid << endl;
            name = "";
            m.unlock();
          }
          return ret;
        }
        break;
      }
      case FREE:
      {
        bool is_owner = false;
        {
          //MyScopedLock l(&m);
          cout << "[acquire]: name=" << name << endl;
          m.lock();
          name = "acquire: FREE";
          if (lc->lstatus == FREE)
          {
            cout << "[acquire]: ltatus=FREE, client " << cl->id() << " thread " << pthread_self()
                 << " lock " << lid << endl;
            lc->lstatus = LOCKED;
            is_owner = true;
          }
          name = "";
          m.unlock();
        }
        if (is_owner)
        {
          cout << "[acquire]: exit, client " << cl->id() << " thread " << pthread_self() <<" got the lock " << lid << endl;
          return lock_protocol::OK;
        }
        break;
      }
      default:
        cout << "[acquire]: name=" << name << endl;
        m.lock();
        name = "acquire: default";
        if (lc->lstatus == NONE || lc->lstatus == FREE)
        {
          name = "";
          m.unlock();
          break;
        }
        if (lc->lstatus == LOCKED)
        {
          cout << "[acquire]: lstatus = LOCK, ";
        }
        else if (lc->lstatus == ACQUIRING)
        {
          cout << "[acquire]: lstatus = ACQUIRING, ";
        }
        else if (lc->lstatus == RELEASING)
        {
          cout << "[acquire]: lstatus = LOCK, ";
        }
        cout << "client " << cl->id() << " thread " << pthread_self() << " wait lock " << lid << endl;
        cout << "[acquire]: client " << cl->id() << " thread " << pthread_self() << " wait" << endl;
        name = "";
        cv.wait(&m);
        name = "acquire: after wait";
        name = "";
        m.unlock();
        break;
    }
  }
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  cout << "[release]: name=" << name << endl;
  m.lock();
  name = "release";
  lid_control *lc = lock_map[lid];
  cout << "[release]: enter client " << cl->id() << " thread " << pthread_self() << "lock " << lid
       << " revoke_map seq_num=" << revoke_map[lid]
       << " lc->seq_num=" << lc->seq_num << endl;
  assert(lc->lstatus == LOCKED);
  if (revoke_map.find(lid) != revoke_map.end() && revoke_map[lid] == lc->seq_num)
  {
    cout << "[release]: need to release, " << " client " << cl->id() << " thread " << pthread_self() << " revoke_map[lid]=" << revoke_map[lid] 
         << " lc->seq_num=" << lc->seq_num;
    lc->lstatus = RELEASING;
    rvk_cv.signal();
  }
  else
  {
    lc->lstatus = FREE;
    cout << "[release]: don't need to release, set lstatus=FREE, client " << cl->id() << " thread " << pthread_self() << endl;
    cv.signal();
    cout << "[release]: signal other local threads, client " << cl->id() << " thread " << pthread_self() << endl;    
  }
  cout << "[release]: exit client " << cl->id() << " thread " << pthread_self() << "lock " << lid << endl;
  name = "";
  m.unlock();
  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::revoke(lock_protocol::lockid_t lid, unsigned int seq_num, int &r)
{
//   map<lock_protocol::lockid_t, unsigned int> revoke_map;
  cout << "[revoke]: enter, client " << cl->id() << " thread " << pthread_self() << " lid =" << lid
       << " remote_seq_num =" <<  seq_num
       << " local seq_num=" << lock_map[lid]->seq_num << endl;
  cout << "[revoke]: name=" << name << endl;
  m.lock();
  name = "revoke";
  if ((revoke_map.find(lid) == revoke_map.end() || seq_num > revoke_map[lid])
        && seq_num == lock_map[lid]->seq_num)
  {
    cout << "[revoke]: client " << cl->id() << " thread " << pthread_self() << " add lid=" << lid
         << " seq_num=" << seq_num << " to revoke map" << endl;
    revoke_map[lid] = seq_num;
    lid_control *lc = lock_map[lid];
    if (lc->lstatus == FREE)
    {
      lc->lstatus = RELEASING;
      rvk_cv.signal();
      cout << "[revoke]: client " << cl->id() << " thread " << pthread_self() << " signal the releaser" << endl; 
    }
  }
  cout << "[revoke]: exit lid = " << lid
       << " remote_seq_num = " <<  seq_num << endl;
  name = "";
  m.unlock();
  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::retry(lock_protocol::lockid_t lid, int &r)
{
  cout << "[retry]: enter lid = " << lid << endl;
  cout << "[retry]: name=" << name << endl;
  m.lock();
  name = "retry";
  retry_map[lid] = true;
  rtr_cv.signal();
  cout << "[retry]: exit client " << cl->id() << " singal to retry" << endl;
  name = "";
  m.unlock();
  return lock_protocol::OK;
}
