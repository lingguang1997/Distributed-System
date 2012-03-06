#include "rsm_client.h"
#include <vector>
#include <arpa/inet.h>
#include <stdio.h>


rsm_client::rsm_client(std::string dst)
{
  printf("create rsm_client\n");
  std::vector<std::string> mems;
  pthread_mutex_init(&rsm_client_mutex, NULL);
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  primary.id = dst;
  primary.cl = new rpcc(dstsock);
  primary.nref = 0;
  int ret = primary.cl->bind(rpcc::to(1000));
  if (ret < 0) {
    printf("rsm_client::rsm_client bind failure %d failure w %s; exit\n", ret, 
     primary.id.c_str());
    exit(1);
  }
  assert(pthread_mutex_lock(&rsm_client_mutex)==0);
  assert (init_members(true));
  assert(pthread_mutex_unlock(&rsm_client_mutex)==0);
  printf("rsm_client: done\n");
}

// Assumes caller holds rsm_client_mutex 
void
rsm_client::primary_failure()
{
// <lab8>
  cout << "[primary_failure]: enter" << endl;
  while (true)
  {
    cout << "[primary_failure]: current primary=" << primary.id.c_str() << endl;
    for (vector<string>::iterator it = known_mems.begin();
         it != known_mems.end();
         it++)
    {
      if (primary.id.compare(*it) == 0)
      {
        cout << "[primary_failure]: remove current primary=" << primary.id.c_str() << endl;
        known_mems.erase(it);
        break;
      }
    }
    assert(known_mems.size() > 0);

    string new_primary = known_mems.front();
    cout << "[primary_failure]: get new_primary=" << new_primary << " from the known_mem" << endl;
    assert(primary.nref == 0);
    delete primary.cl;
    sockaddr_in dstsock;
    make_sockaddr(new_primary.c_str(), &dstsock);
    primary.id = new_primary;
    primary.cl = new rpcc(dstsock);
    primary.nref = 0;
    cout << "[primary_failure]: try to bind the new primary=" << primary.id.c_str() << endl;
    int ret = primary.cl->bind(rpcc::to(1000));
    if (ret < 0)
    {
      cout << "[primary_failure]: fail to bind the new primary=" << primary.id.c_str() << endl;
      continue;
    }
    else
    {
      cout << "[primary_failure]: succeed to bind the new primary=" << primary.id.c_str()
           << "try to initmembers"<< endl;
      //sleep(3);
      bool succ = init_members(true);  
      if (succ)
      {
        cout << "[primary_failure]: init members success" << endl;
        break;
      }
      else
      {
        cout << "[primary_failure]: init members fails" << endl;
      }
    }
  }
  cout << "[primary_failure]: exit" << endl;
// </lab8>
}

rsm_protocol::status
rsm_client::invoke(int proc, std::string req, std::string &rep)
{
  int ret;
  rpcc *cl;
  assert(pthread_mutex_lock(&rsm_client_mutex)==0);
  while (1) {
    printf("[invoke]: proc %x primary %s\n", proc, primary.id.c_str());
    cl = primary.cl;
    primary.nref++;
    assert(pthread_mutex_unlock(&rsm_client_mutex)==0);
    ret = primary.cl->call(rsm_client_protocol::invoke, proc, req, 
        rep, rpcc::to(5000));
    assert(pthread_mutex_lock(&rsm_client_mutex)==0);
    primary.nref--;
    printf("[invoke]: proc %x primary %s ret %d\n", proc, 
     primary.id.c_str(), ret);
    if (ret == rsm_client_protocol::OK) {
      break;
    }
    if (ret == rsm_client_protocol::BUSY) {
      printf("[invoke]: rsm is busy %s\n", primary.id.c_str());
      sleep(3);
      continue;
    }
    if (ret == rsm_client_protocol::DELAY) {
      printf("[invoke]: rsm has slave timeout %s\n", primary.id.c_str());
      sleep(6);
      break;
    }
    if (ret == rsm_client_protocol::NOTPRIMARY) {
      printf("[invoke]: primary %s isn't the primary--let's get a complete list of mems\n", 
          primary.id.c_str());
      if (init_members(true))
        continue;
    }
    printf("[invoke]: primary %s failed ret %d\n", primary.id.c_str(), ret);
    primary_failure();
    printf ("[invoke]: retry new primary %s\n", primary.id.c_str());
  }
  assert(pthread_mutex_unlock(&rsm_client_mutex)==0);
  return ret;
}

bool
rsm_client::init_members(bool send_member_rpc)
{
  if (send_member_rpc) {
    printf("[init_members]: get members!\n");
    assert(pthread_mutex_unlock(&rsm_client_mutex)==0);
    int ret = primary.cl->call(rsm_client_protocol::members, 0, known_mems, 
            rpcc::to(1000)); 
    assert(pthread_mutex_lock(&rsm_client_mutex)==0);
    if (ret != rsm_protocol::OK)
      return false;
  }
  if (known_mems.size() < 1) {
    printf("[init_members] do not know any members!\n");
    assert(0);
  }

  std::string new_primary = known_mems.back();
  known_mems.pop_back();

  printf("[init_members]: primary %s\n", new_primary.c_str());

  if (new_primary != primary.id) {
    sockaddr_in dstsock;
    make_sockaddr(new_primary.c_str(), &dstsock);
    primary.id = new_primary;
    if (primary.cl) {
      assert(primary.nref == 0);  // XXX fix: delete cl only when refcnt=0
      delete primary.cl; 
    }
    primary.cl = new rpcc(dstsock);

    if (primary.cl->bind(rpcc::to(1000)) < 0) {
      printf("rsm_client::rsm_client cannot bind to primary\n");
      return false;
    }
  }
  return true;
}

