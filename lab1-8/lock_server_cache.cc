// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

static void *
revokethread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->retryer();
  return 0;
}

lock_server_cache::lock_server_cache(class rsm *_rsm)
  : rsm (_rsm)
{
  pthread_t thds[2]; 
  int r = pthread_create(&thds[0], NULL, &revokethread, (void *) this);
  assert (r == 0);
  r = pthread_create(&thds[1], NULL, &retrythread, (void *) this);
  assert (r == 0);

  quit = false;
  has_release = 0;
// <lab8>
  rsm->set_state_transfer(this);
// </lab8>
}

void
lock_server_cache::revoker()
{
//    map<int, map<lock_protocol::lockid_t, unsigned int>> revoke_map;
  while (true)
  {
    m.lock();
    while (revoke_map.empty())
    {
      rvk_cv.wait(&m);
    }
    //cout << "[revoker]: enter!" << endl;

    map<int, map<lock_protocol::lockid_t, unsigned int> > temp_map = revoke_map;
    revoke_map.clear();
    m.unlock();

    if (rsm->amiprimary())
    {
      for (map<int, map<lock_protocol::lockid_t, unsigned int> >::iterator \
           it = temp_map.begin();
           it != temp_map.end();
           it++)
      {
        //cout << "[revoker]: to revoke client " << it->first << endl;
        rpcc *rc = client_map[it->first];
        int r;
        map<lock_protocol::lockid_t, unsigned int> lid_to_seq = it->second;
        for (map<lock_protocol::lockid_t, unsigned int>::iterator \
             rit = lid_to_seq.begin();
             rit != lid_to_seq.end();
             rit++)
        {
          lock_protocol::lockid_t lid = rit->first;
          unsigned int seq_num = rit->second;
          //cout << "[revoker] lid=" << lid << " seq_num=" << seq_num
          //     << " " << rc << endl;
          //cout << "[revoker]: before send revoke rpc" << endl;
          int ret = rc->call(rlock_protocol::revoke, lid, seq_num, r);
          //cout << "[revoker]: after send revoke rpc" << endl;
          //cout << "[revoker]: revoke lid " << lid << " on client " << it->first << endl;
          //cout << "[revoker]: ret=" << ret << endl;
          assert(ret == lock_protocol::OK);
        }
      }
    }
    //cout << "[revoker]: exit" << endl;
  }
  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock
}

void
lock_server_cache::retryer()
{
  // map<lock_protocol::lockid_t, set<int>> retry_map;
  while (true)
  {
    m.lock();
    while (has_release == 0)
    {
      rtr_cv.wait(&m);
    }
    cout << "[retryer]: wakes up!" << endl;
    map<lock_protocol::lockid_t, set<int> > temp_map = retry_map;
    retry_map.clear();
    has_release--;
    m.unlock();
    if (rsm->amiprimary())
    {
      for (map<lock_protocol::lockid_t, set<int> >::iterator it = temp_map.begin();
           it != temp_map.end();
           it++)
      {
        set<int> clts = it->second;
        int r;
        for (set<int>::iterator clt = clts.begin();
             clt != clts.end();
             clt++)
        {
          rpcc *cl = client_map[*clt];
          cout << "[retryer]: before send retry rpc to client " << *clt << endl;
          int ret = cl->call(rlock_protocol::retry, it->first, r);
          cout << "[retryer]: after send retry rpc to client " << *clt << endl;
          assert(ret == rlock_protocol::OK);
        }
      }
    }
  }
  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.
}

void
lock_server_cache::add_revoke_request(int clt, lock_protocol::lockid_t lid)
{
  // clt=lid   map<int, map<lock_protocol::lockid_t, unsigned int>> revoke_map;
  // map<int, map<lock_protocol::lockid_t, unsigned int> > clt_lid_seq_map;
  //cout << "[add_revoke_request]: enter" << endl;
  map<lock_protocol::lockid_t, unsigned int> lid_to_seq;
  if (revoke_map.find(clt) != revoke_map.end())
  {
    lid_to_seq = revoke_map[clt];
  }
  else
  {
    lid_to_seq[lid] = 0;
  }
  assert(clt_lid_seq_map[clt][lid] >= lid_to_seq[lid]);
  lid_to_seq[lid] = clt_lid_seq_map[clt][lid];
  revoke_map[clt] = lid_to_seq;
  //cout << "[add_revoke_request]: exit" << endl;
}

void
lock_server_cache::add_retry_request(lock_protocol::lockid_t lid, int clt)
{
  // lid=clt    map<lock_protocol::lockid_t, set<int>* > retry_map;
  //cout << "[add_retry_request]: enter" << endl;
  set<int> retry_clts;
  if (retry_map.find(lid) != retry_map.end())
  {
    retry_clts = retry_map[lid]; 
  }
  retry_clts.insert(clt);
  retry_map[lid] = retry_clts;
  //cout << "[add_retry_request]: exit" << endl;
}

lock_protocol::status
lock_server_cache::acquire(int clt, lock_protocol::lockid_t lid, unsigned int seq_num, int &r)
{
  cout << "[acquire]: enter, client " << clt
       << " lock " << lid << endl;
  int ret = lock_protocol::OK;
  if (seq_num <= clt_lid_seq_map[clt][lid])
  {
    cout << "[acquire]: server has already handle the aquire" << endl;
    return ret;
  }
  lock_resource *lr = NULL;
  MyScopedLock l(&m);
  if (lock_allocation_map.find(lid) == lock_allocation_map.end())
  {
    lock_resource *lr = (lock_resource *)malloc(sizeof(lock_resource));
    lr->is_allocated = false;
    lr->clt = clt;
    lr->seq_num = 0;
    lock_allocation_map[lid] = lr;
    clt_lid_seq_map[clt][lid] = 0;
  }
  lr = lock_allocation_map[lid];

  if (!lr->is_allocated)
  {
    lr->clt = clt;
    lr->is_allocated = true;
    lr->seq_num = seq_num;
    clt_lid_seq_map[clt][lid] = seq_num;
    cout << "[acquire]: ++++++++++++++++++client " << clt << " got the lock "
         << lid << " since the lock is not allocated, seq_num="
         << seq_num << " inmap seq_num=" << clt_lid_seq_map[clt][lid] <<"++++++++++++++++++" << endl;
  }
  else
  {
    ret = lock_protocol::RETRY;
    cout << "[acquire]: client " << clt
         << " asking for lock " << lid
         << " should retry later, oppcupied by client "
         << lr->clt << ", seq_num=" << clt_lid_seq_map[lr->clt][lid] << endl;

    add_retry_request(lid, clt);
    add_revoke_request(lr->clt, lid);
    rvk_cv.signal();
    cout << "[acquire]: client " << clt << " signal the revoker" << endl;
  }
  cout << "[acquire]: exit, client " << clt
       << " lock " << lid << endl;
  return ret;
}

lock_protocol::status
lock_server_cache::release(int clt, lock_protocol::lockid_t lid, unsigned int seq_num, int &r)
{
  cout << "[release]: enter, client " << clt
       << " lock " << lid << endl; 
  MyScopedLock l(&m);
  lock_resource *lr = lock_allocation_map[lid];
  cout << "[release]: lock condition: lid=" << lid << " clt="
       << lr->clt << " remote seq_num=" << seq_num
       << " map seq_num=" << clt_lid_seq_map[clt][lid]
       << " lr->seq_num=" << lr->seq_num << endl;
  if (seq_num == lr->seq_num && lr->clt == clt)
  {
    assert(lr->is_allocated == true);
    cout << "[release]: ------------------client " << clt << " released the lock "
             << lid << "------------------" << endl;
    lr->is_allocated = false;
    lr->seq_num = 0;
    has_release++;
    cout << "[release]: signal the retryer";
    rtr_cv.signal();
  }
  cout << "[release]: exit, client " << clt
       << " lock " << lid << endl; 
  return lock_protocol::OK;
}

lock_protocol::status
lock_server_cache::subscribe(int clt, int rlock_port, int &r)
{
  //cout << "[subscribe]: client " << clt << endl;
  MyScopedLock l(&m);
  if (client_map.find(clt) == client_map.end())
  {
    ostringstream port;
    port << rlock_port;
    sockaddr_in dstsock;
    make_sockaddr(port.str().c_str(), &dstsock);
    rpcc *rc = new rpcc(dstsock);
    rc->bind();
    client_map[clt] = rc;
    client_port_map[clt] = rlock_port;
  }
  r = 0;
  return lock_protocol::OK;
}

lock_protocol::status
lock_server_cache::unsubscribe(int clt, int &r)
{
  //cout << "[unsubscribe]: client " << clt << endl;
  MyScopedLock l(&m);
  rpcc *rc = client_map[clt];
  delete rc;
  rc = NULL;
  client_map.erase(clt);
  client_port_map.erase(clt);
  r = 0;
  return lock_protocol::OK;
}

// <lab8>
string 
lock_server_cache::marshal_state()
{
  cout << "[marshal_state]: enter" << endl;
  MyScopedLock l(&m);
  marshall rep;
//map<lock_protocol::lockid_t, lock_resource*> lock_allocation_map;
  rep << lock_allocation_map.size();
  for (map<lock_protocol::lockid_t, lock_resource*>::iterator \
       it = lock_allocation_map.begin();
       it != lock_allocation_map.end();
       it++)
  {
    rep << it->first;
    lock_resource *lr = it->second;
    rep << (int)(lr->is_allocated);
    rep << lr->clt;
  }

//map<int, map<lock_protocol::lockid_t, unsigned int> > clt_lid_seq_map;
  rep << clt_lid_seq_map.size();
  for (map<int, map<lock_protocol::lockid_t, unsigned int> >::iterator \
       it = clt_lid_seq_map.begin();
       it != clt_lid_seq_map.end();
       it++)
  {
    rep << it->first;
    map<lock_protocol::lockid_t, unsigned int> lid_seq = it->second;
    rep << lid_seq.size();
    for (map<lock_protocol::lockid_t, unsigned int>::iterator \
         it1 = lid_seq.begin();
         it1 != lid_seq.end();
         it1++)
    {
      rep << it1->first;
      rep << it1->second;
    } 
  }

//map<lock_protocol::lockid_t, set<int> > retry_map;
  rep << retry_map.size();
  for (map<lock_protocol::lockid_t, set<int> >::iterator \
       it = retry_map.begin();
       it != retry_map.end();
       it++)
  {
    rep << it->first;
    set<int> clts = it->second;
    rep << clts.size();
    for (set<int>::iterator it1 = clts.begin();
         it1 != clts.end();
         it1++)
    {
      rep << *it1;
    }
  }

//map<int, map<lock_protocol::lockid_t, unsigned int> > revoke_map;
  rep << revoke_map.size();
  for (map<int, map<lock_protocol::lockid_t, unsigned int> >::iterator \
       it = revoke_map.begin();
       it != revoke_map.end();
       it++)
  {
    rep << it->first;
    map<lock_protocol::lockid_t, unsigned int> lid_seq = it->second;
    rep << lid_seq.size();
    for (map<lock_protocol::lockid_t, unsigned int>::iterator \
         it1 = lid_seq.begin();
         it1 != lid_seq.end();
         it1++)
    {
      rep << it1->first;
      rep << it1->second;
    }
  }

//map<int, int> client_port_map;
  rep << client_map.size();
  for (map<int, int>::iterator it = client_port_map.begin();
       it != client_port_map.end();
       it++)
  {
    rep << it->first;
    rep << it->second;
  }
  cout << "[marshal_state]: exit" << endl;
  return rep.str();
}

void 
lock_server_cache::unmarshal_state(string state)
{
  cout << "[unmarshal_state]: enter" << endl;
  MyScopedLock l(&m);
  clear();
  unmarshall rep(state);
  unsigned int lock_allocation_map_size;
  rep >> lock_allocation_map_size;
  for (unsigned int i = 0; i < lock_allocation_map_size; i++)
  {
    lock_protocol::lockid_t lid;
    rep >> lid;
    lock_resource *lr = (lock_resource *)malloc(sizeof(lock_resource));
    int is_allocated;
    int clt;
    rep >> is_allocated;
    rep >> clt;
    lr->is_allocated = (bool)is_allocated;
    lr->clt = clt;
    lock_allocation_map[lid] = lr;
  }
  unsigned int clt_lid_seq_map_size;
  rep >> clt_lid_seq_map_size;
  for (unsigned int i = 0; i < clt_lid_seq_map_size; i++)
  {
    int clt;
    rep >> clt;
    map<lock_protocol::lockid_t, unsigned int> lid_seq;
    unsigned int lid_seq_size;
    rep >> lid_seq_size;
    for (unsigned int j = 0; j < lid_seq_size; j++)
    {
      lock_protocol::lockid_t lid;
      unsigned int seq;
      rep >> lid;
      rep >> seq;
      lid_seq[lid] = seq;
    }
    clt_lid_seq_map[clt] = lid_seq;
  }
  unsigned int retry_map_size;
  rep >> retry_map_size;
  for (unsigned int i = 0; i < retry_map_size; i++)
  {
    lock_protocol::lockid_t lid;
    rep >> lid;
    unsigned int clts_size;
    rep >> clts_size;
    set<int> clts;
    for (unsigned int j = 0; j< clts_size; j++)
    {
      int clt;
      rep >> clt;
      clts.insert(clt);
    }
    retry_map[lid] = clts;
  }
  unsigned int revoke_map_size;
  rep >> revoke_map_size;
  for (unsigned int i = 0; i < revoke_map_size; i++)
  {
    int clt;
    rep >> clt;
    unsigned int lid_seq_size;
    rep >> lid_seq_size;
    map<lock_protocol::lockid_t, unsigned int> lid_seq;
    for (unsigned int j = 0; j < lid_seq_size; j++)
    {
      lock_protocol::lockid_t lid;
      unsigned int seq;
      rep >> lid;
      rep >> seq;
      lid_seq[lid] = seq;
    }
    revoke_map[clt] = lid_seq;
  }

  unsigned int client_port_map_size;
  rep >> client_port_map_size;
  for (unsigned int i = 0; i < client_port_map_size; i++)
  {
    int clt;
    int port;
    rep >> clt;
    rep >> port;
    if (client_map.find(clt) == client_map.end())
    {
      ostringstream port_stream;
      port_stream << port;
      sockaddr_in dstsock;
      make_sockaddr(port_stream.str().c_str(), &dstsock);
      rpcc *rc = new rpcc(dstsock);
      rc->bind();
      client_map[clt] = rc;
      client_port_map[clt] = port;
    }
  }
  cout << "[unmarshal_state]: exit" << endl;
}

void 
lock_server_cache::clear()
{
  cout << "[clear]: enter" << endl;
  for (map<lock_protocol::lockid_t, lock_resource*>::iterator \
       it = lock_allocation_map.begin();
       it != lock_allocation_map.end();
       it++)
  {
    delete it->second;
    it->second = NULL;
    lock_allocation_map.erase(it);
  }
  clt_lid_seq_map.clear();
  retry_map.clear();
  revoke_map.clear();
  for (map<int, rpcc*>::iterator it = client_map.begin();
       it != client_map.end();
       it++)
  {
    rpcc *rc = client_map[it->first];
    delete rc;
    rc = NULL;
  }
  client_map.clear();
  client_port_map.clear();
}
// </lab8>
