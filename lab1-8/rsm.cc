//
// Replicated state machine implementation with a primary and several
// backups. The primary receives requests, assigns each a view stamp (a
// vid, and a sequence number) in the order of reception, and forwards
// them to all backups. A backup executes requests in the order that
// the primary stamps them and replies with an OK to the primary. The
// primary executes the request after it receives OKs from all backups,
// and sends the reply back to the client.
//
// The config module will tell the RSM about a new view. If the
// primary in the previous view is a member of the new view, then it
// will stay the primary.  Otherwise, the smallest numbered node of
// the previous view will be the new primary.  In either case, the new
// primary will be a node from the previous view.  The configuration
// module constructs the sequence of views for the RSM and the RSM
// ensures there will be always one primary, who was a member of the
// last view.
//
// When a new node starts, the recovery thread is in charge of joining
// the RSM.  It will collect the internal RSM state from the primary;
// the primary asks the config module to add the new node and returns
// to the joining the internal RSM state (e.g., paxos log). Since
// there is only one primary, all joins happen in well-defined total
// order.
//
// The recovery thread also runs during a view change (e.g, when a node
// has failed).  After a failure some of the backups could have
// processed a request that the primary has not, but those results are
// not visible to clients (since the primary responds).  If the
// primary of the previous view is in the current view, then it will
// be the primary and its state is authoritive: the backups download
// from the primary the current state.  A primary waits until all
// backups have downloaded the state.  Once the RSM is in sync, the
// primary accepts requests again from clients.  If one of the backups
// is the new primary, then its state is authoritative.  In either
// scenario, the next view uses a node as primary that has the state
// resulting from processing all acknowledged client requests.
// Therefore, if the nodes sync up before processing the next request,
// the next view will have the correct state.
//
// While the RSM in a view change (i.e., a node has failed, a new view
// has been formed, but the sync hasn't completed), another failure
// could happen, which complicates a view change.  During syncing the
// primary or backups can timeout, and initiate another Paxos round.
// There are 2 variables that RSM uses to keep track in what state it
// is:
//    - inviewchange: a node has failed and the RSM is performing a view change
//    - insync: this node is syncing its state
//
// If inviewchange is false and a node is the primary, then it can
// process client requests. If it is true, clients are told to retry
// later again.  While inviewchange is true, the RSM may go through several
// member list changes, one by one.   After a member list
// change completes, the nodes tries to sync. If the sync complets,
// the view change completes (and inviewchange is set to false).  If
// the sync fails, the node may start another member list change
// (inviewchange = true and insync = false).
//
// The implementation should be used only with servers that run all
// requests run to completion; in particular, a request shouldn't
// block.  If a request blocks, the backup won't respond to the
// primary, and the primary won't execute the request.  A request may
// send an RPC to another host, but the RPC should be a one-way
// message to that host; the backup shouldn't do anything based on the
// response or execute after the response, because it is not
// guaranteed that all backup will receive the same response and
// execute in the same order.
//
// The implementation can be viewed as a layered system:
//       RSM module     ---- in charge of replication
//       config module  ---- in charge of view management
//       Paxos module   ---- in charge of running Paxos to agree on a value
//
// Each module has threads and internal locks. Furthermore, a thread
// may call down through the layers (e.g., to run Paxos's proposer).
// When Paxos's acceptor accepts a new value for an instance, a thread
// will invoke an upcall to inform higher layers of the new value.
// The rule is that a module releases its internal locks before it
// upcalls, but can keep its locks when calling down.

#include <fstream>
#include <iostream>

#include "handle.h"
#include "rsm.h"
#include "rsm_client.h"

static void *
recoverythread(void *x)
{
  rsm *r = (rsm *) x;
  r->recovery();
  return 0;
}



rsm::rsm(std::string _first, std::string _me) 
  : stf(0), primary(_first), insync (false), inviewchange (false), nbackup (0), partitioned (false), dopartition(false), break1(false), break2(false)
{
  cout << "[rsm]: enter" << endl;
  pthread_t th;

  last_myvs.vid = 0;
  last_myvs.seqno = 0;
  myvs = last_myvs;
  myvs.seqno = 1;

  pthread_mutex_init(&rsm_mutex, NULL);
  pthread_mutex_init(&invoke_mutex, NULL);
  pthread_cond_init(&recovery_cond, NULL);
  pthread_cond_init(&sync_cond, NULL);
  pthread_cond_init(&join_cond, NULL);

  cfg = new config(_first, _me, this);

  rsmrpc = cfg->get_rpcs();
  rsmrpc->reg(rsm_client_protocol::invoke, this, &rsm::client_invoke);
  rsmrpc->reg(rsm_client_protocol::members, this, &rsm::client_members);
  rsmrpc->reg(rsm_protocol::invoke, this, &rsm::invoke);
  rsmrpc->reg(rsm_protocol::transferreq, this, &rsm::transferreq);
  rsmrpc->reg(rsm_protocol::transferdonereq, this, &rsm::transferdonereq);
  rsmrpc->reg(rsm_protocol::joinreq, this, &rsm::joinreq);

  // tester must be on different port, otherwise it may partition itself
  testsvr = new rpcs(atoi(_me.c_str()) + 1);
  testsvr->reg(rsm_test_protocol::net_repair, this, &rsm::test_net_repairreq);
  testsvr->reg(rsm_test_protocol::breakpoint, this, &rsm::breakpointreq);

  assert(pthread_mutex_lock(&rsm_mutex)==0);

  assert(pthread_create(&th, NULL, &recoverythread, (void *) this) == 0);

  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  cout << "[rsm]: exit" << endl;
}

void
rsm::reg1(int proc, handler *h)
{
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  cout << "[reg1]: name=" << name << endl;
  name = "reg1";
  procs[proc] = h;
  name = "";
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
}

// The recovery thread runs this function
void
rsm::recovery()
{
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  cout << "[recovery]: name=" << name << endl;
  name = "recovery, outside while";
  while (1) {
    cout << "[recovery]: run" << endl;
    inviewchange = true;
    while (!cfg->ismember(cfg->myaddr())) {
      cout << "[recovery]: is not a member" << endl;
      if (join(primary)) {
	    printf("recovery: joined\n");
      } else {
        name = "";
	    assert(pthread_mutex_unlock(&rsm_mutex)==0);
	    sleep (30); // XXX make another node in cfg primary?
	    cout << "[recovery]: name=" << name << endl;
	    assert(pthread_mutex_lock(&rsm_mutex)==0);
	    name = "recovery, join";
      }
    }
    if (cfg->myaddr() != primary)
    {
      cout << "[recovery]: sync with the primary " << primary << endl;
      if (sync_with_primary())
      {
        cout << "[recovery]: sync with primary " << primary << " success" << endl;
        cout << "[recovery]: before update sync myvs.vid=" << myvs.vid
             << " myvs.seqno=" << myvs.seqno
             << " last_myvs.vid=" << last_myvs.vid
             << " last_myvs.seqno=" << last_myvs.seqno << endl;
        myvs.vid = last_myvs.vid;
        myvs.seqno = last_myvs.seqno + 1;
        cout << "[recovery]: after update sync myvs.vid=" << myvs.vid
             << " myvs.seqno=" << myvs.seqno
             << " last_myvs.vid=" << last_myvs.vid
             << " last_myvs.seqno=" << last_myvs.seqno << endl;
      }
      else
      {
        cout << "[recovery]: sync with primary " << primary << " failed!" << endl;
      }
    }

    inviewchange = false;
    printf("recovery: go to sleep %d %d\n", insync, inviewchange);
    name = "";
    pthread_cond_wait(&recovery_cond, &rsm_mutex);
    cout << "[recovery]: name=" << name << endl;
    name = "recovery, wait";
  }
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
}

bool
rsm::sync_with_backups()
{
  // For lab 8
  return true;
}


bool
rsm::sync_with_primary()
{
// <lab8>
  cout << "[sync_with_primary], enter" << endl;
  insync++;
  int r;
retry:
  bool ret = statetransfer(primary, r);
  if (ret)
  {
    cout << "[sync_with_primary], statetransfer success" << endl;
    statetransferdone(primary);
  }
  else
  {
    if (r == rsm_protocol::BUSY)
    {
      cout << "[sync_with_primary]: primary is busy, should retry" << endl;
      sleep(2);
      goto retry;
    }
    cout << "[sync_with_primary], statetransfer failed" << endl;
  }
  insync--;
  cout << "[sync_with_primary], exit, ret=" << ret << endl;
  return ret;
// </lab8>
}


/**
 * Call to transfer state from m to the local node.
 * Assumes that rsm_mutex is already held.
 */
bool
rsm::statetransfer(std::string m, int &status)
{
  cout << "[statetransfer]: enter" << endl;
  rsm_protocol::transferres r;
  handle h(m);
  int ret;
  printf("[statetransfer]: contact %s w. my last_myvs(%d,%d)\n", 
	 m.c_str(), last_myvs.vid, last_myvs.seqno);
  if (h.get_rpcc()) {
    name = "";
    assert(pthread_mutex_unlock(&rsm_mutex)==0);
    cout << "[statetransfer]: before send transferreq" << endl;
    ret = h.get_rpcc()->call(rsm_protocol::transferreq, cfg->myaddr(), 
			     last_myvs, r, rpcc::to(1000));
    cout << "[statetransfer]: after send transferreq, ret=" << ret << endl;
    cout << "[statetransfer]: name=" << name << endl;
    assert(pthread_mutex_lock(&rsm_mutex)==0);
    name = "statetransfer after send transferreq";
  }
  if (h.get_rpcc() == 0 || ret != rsm_protocol::OK) {
    printf("[statetransfer]: couldn't reach %s %lx %d\n", m.c_str(), 
	   (long unsigned) h.get_rpcc(), ret);
	cout << "[statetransfer]: exit" << endl;
	status = ret;
    return false;
  }
  if (stf && last_myvs != r.last) {
    stf->unmarshal_state(r.state);
  }
  last_myvs = r.last;
  printf("[statetransfer]: transfer from %s success, vs(%d,%d)\n", 
	 m.c_str(), last_myvs.vid, last_myvs.seqno);
  cout << "[statetransfer]: exit" << endl;
  status = ret;
  return true;
}

bool
rsm::statetransferdone(std::string m) {
// <lab8>
  cout << "[statetransferdone]: enter" << endl;
  handle h(m);
  int ret;
  cout << "[statetransferdone]: contact " << m.c_str() << " last_myvs.vid="
       << last_myvs.vid << " last_myvs.seqno=" << last_myvs.seqno << endl;
  if (h.get_rpcc())
  {
    name = "";
    assert(pthread_mutex_unlock(&rsm_mutex)==0);
    name = "statetransferdone";
    int r;
    cout << "[statetransferdone]: before send transferdonereq" << endl;
    ret = h.get_rpcc()->call(rsm_protocol::transferdonereq, cfg->myaddr(), 
			                 r, rpcc::to(1000));
    cout << "[statetransferdone]: after send transferdonereq, ret=" << ret << endl;
    assert(ret == rsm_protocol::OK);
    cout << "[statetransfer]: name=" << name << endl;
    assert(pthread_mutex_lock(&rsm_mutex)==0);
    name = "statetransferdone";
  }
// </lab8>
  cout << "[statetransferdone]: exit" << endl;
  return true;
}


bool
rsm::join(std::string m) {
  handle h(m);
  int ret;
  rsm_protocol::joinres r;

  if (h.get_rpcc() != 0) {
    printf("[join]: %s mylast (%d,%d)\n", m.c_str(), last_myvs.vid, 
	   last_myvs.seqno);
	name = "";
    assert(pthread_mutex_unlock(&rsm_mutex)==0);
    ret = h.get_rpcc()->call(rsm_protocol::joinreq, cfg->myaddr(), last_myvs, 
			     r, rpcc::to(120000));
	cout << "[join]: name=" << name << endl;
    assert(pthread_mutex_lock(&rsm_mutex)==0);
    name = "join";
  }
  if (h.get_rpcc() == 0 || ret != rsm_protocol::OK) {
    printf("[join]: couldn't reach %s %p %d\n", m.c_str(), 
	   h.get_rpcc(), ret);
    return false;
  }
  printf("[join]: succeeded %s\n", r.log.c_str());
  cfg->restore(r.log);
  return true;
}


/*
 * Config informs rsm whenever it has successfully 
 * completed a view change
 */

void 
rsm::commit_change() 
{
  cout << "[commit_change]: name=" << name << endl;
  pthread_mutex_lock(&rsm_mutex);
  name = "commit_change";
  // Lab 7:
  // - If I am not part of the new view, start recovery
  // - Notify any joiners if they were successful
// <lab8>
  inviewchange = true;
// </lab8>
// <lab7>
  set_primary();
  cout << "[commit_change]: me=" << cfg->myaddr() << " primary=" << primary << endl;
  if (cfg->myaddr() == primary)
  {
    insync = cfg->get_curview().size() - 1;
    cout << "[commit_change]: set insync to " << cfg->get_curview().size() - 1 << endl;
    cout << "[commit_change]: before update cfg->vid()=" << cfg->vid()
         << " last_myvs.vid=" << last_myvs.vid
         << " myvs.vid=" << myvs.vid << endl;
    last_myvs.vid = cfg->vid();
    myvs.vid = last_myvs.vid;
    cout << "[commit_change]: after update cfg->vid()=" << cfg->vid()
         << " last_myvs.vid=" << last_myvs.vid
         << " myvs.vid=" << myvs.vid << endl;
  }
  //if (!cfg->ismember(cfg->myaddr()))
  //{
  pthread_cond_signal(&recovery_cond);
  cout << "[commit_change]: signal the recovery thread" << endl;
  //}
// </lab7>
  name = "";
  pthread_mutex_unlock(&rsm_mutex);
  if (cfg->ismember(cfg->myaddr()))
  {
    breakpoint2();
  }
}


std::string
rsm::execute(int procno, std::string req)
{
  printf("execute\n");
  handler *h = procs[procno];
  assert(h);
  unmarshall args(req);
  marshall rep;
  std::string reps;
  rsm_protocol::status ret = h->fn(args, rep);
  marshall rep1;
  rep1 << ret;
  rep1 << rep.str();
  return rep1.str();
}

//
// Clients call client_invoke to invoke a procedure on the replicated state
// machine: the primary receives the request, assigns it a sequence
// number, and invokes it on all members of the replicated state
// machine.
//
rsm_client_protocol::status
rsm::client_invoke(int procno, std::string req, std::string &r)
{
// <lab8>
  cout << "[client_invoke]: enter" << endl;
  if (inviewchange)
  {
    cout << "[client_invoke]: inviewchange=" << inviewchange
         << " procno=" << procno << " req=" << req << endl;
    cout << "[client_invoke]: exit" << endl;
    return rsm_client_protocol::BUSY;
  }
  if (cfg->myaddr() != primary)
  {
    cout << "[client_invoke]: I am not a primary"
         << " procno=" << procno << " req=" << req << endl;
    cout << "[client_invoke]: exit" << endl;
    return rsm_client_protocol::NOTPRIMARY;
  }
  if (insync > 0)
  {
    cout << "[client_invoke]: insync=" << insync
         << " procno=" << procno << " req=" << req << endl;
    cout << "[client_invoke]: exit" << endl;
    return rsm_client_protocol::BUSY;
  }
//========================================================
  cout << "[client_invoke]: name=" << name << endl;
  pthread_mutex_lock(&rsm_mutex);
  name = "client_invoke";
  cout << "[client_invoke]: primary=" << primary << " me=" << cfg->myaddr() << endl;
  if (inviewchange)
  {
    cout << "[client_invoke]: inviewchange=" << inviewchange
         << " procno=" << procno << " req=" << req << endl;
    name = "";
    pthread_mutex_unlock(&rsm_mutex);
    cout << "[client_invoke]: exit" << endl;
    return rsm_client_protocol::BUSY;
  }
  if (cfg->myaddr() != primary)
  {
    cout << "[client_invoke]: notprimary procno=" << procno << " req=" << req << endl;
    name = "";
    pthread_mutex_unlock(&rsm_mutex);
    cout << "[client_invoke]: exit" << endl;
    return rsm_client_protocol::NOTPRIMARY;
  }
  if (insync > 0)
  {
    cout << "[client_invoke]: insync=" << insync
         << " procno=" << procno << " req=" << req << endl;
    name = "";
    pthread_mutex_unlock(&rsm_mutex);
    cout << "[client_invoke]: exit" << endl;
    return rsm_client_protocol::BUSY;
  }
  cout << "[client_invoke]: will handle invoke procno=" << procno << " req=" << req << endl;
  name = "";
  pthread_mutex_unlock(&rsm_mutex);
  pthread_mutex_lock(&invoke_mutex);
  // the new seq num
  cout << "[client_invoke]: before update: myvs.seqno=" << myvs.seqno << " myvs.vid=" << myvs.vid << endl;
  cout << "[client_invoke]: before update: last_myvs.seqno=" << last_myvs.seqno << " last_myvs.vid=" << last_myvs.vid << endl;
  myvs.seqno += 1;
  viewstamp vs = myvs;
  last_myvs = myvs;
  cout << "[client_invoke]: after update: myvs.seqno=" << myvs.seqno << " myvs.vid=" << myvs.vid << endl;
  cout << "[client_invoke]: after update: last_myvs.seqno=" << last_myvs.seqno << " last_myvs.vid=" << last_myvs.vid << endl;
  vector<string> c = cfg->get_curview();
  bool all_slaves_suc = true;
  bool has_timeout = false;
  int result = rsm_client_protocol::OK;
  for (vector<string>::iterator it = c.begin();
       it != c.end();
       it++)
  {
    if (*it != primary)
    {
      handle h(*it);
      rpcc *pxc = h.get_rpcc();
      if (pxc)
      {
        int dummy;
        int ret = pxc->call(rsm_protocol::invoke, procno, vs, req, dummy, rpcc::to(1000));
        if (ret == rsm_protocol::ERR)
        {
          cout << "[client_invoke]: slave " << *it << " fail" << endl;
          all_slaves_suc = false;
          result = rsm_client_protocol::BUSY;
            // should not break here
            // just let all the slaves execute the request to update their viewstamp;
          cout << "[client_invoke]: before breakpoint1" << endl;
          breakpoint1();
          cout << "[client_invoke]: after breakpoint1" << endl;
          partition1();
          break;
        }
        else if (ret == rpc_const::timeout_failure)
        {
            // do I need to handle timeout?
          cout << "[client_invoke]: slave " << *it << " time out! ret=" << ret << endl;
          has_timeout = true;
          result = rsm_client_protocol::BUSY;
          cout << "[client_invoke]: before breakpoint1" << endl;
          breakpoint1();
          cout << "[client_invoke]: after breakpoint1" << endl;
          partition1();
        }
        else if (ret == rsm_protocol::BUSY)
        {
          cout << "[client_invoke]: slave " << *it << " is busy" << endl;
          cout << "[client_invoke]: before breakpoint1" << endl;
          result = rsm_client_protocol::BUSY;
          breakpoint1();
          cout << "[client_invoke]: after breakpoint1" << endl;
          partition1();
          break;
        }
        else
        {
          cout << "[client_invoke]: slave " << *it << " succeeds" << endl;
          cout << "[client_invoke]: before breakpoint1" << endl;
          breakpoint1();
          cout << "[client_invoke]: after breakpoint1" << endl;
          partition1();
        }
      }
    }
  }
  if (all_slaves_suc)
  {
    cout << "all slaves succeed!" << endl;
    r = execute(procno, req);
    pthread_mutex_unlock(&invoke_mutex);
    if (has_timeout)
    {
      cout << "DELAY" << endl;
      cout << "[client_invoke]: exit" << endl;
      //return rsm_client_protocol::BUSY;
    }
    else
    {
      cout << "[client_invoke]: exit" << endl;
      //return rsm_client_protocol::OK;
    }
  }
  else
  {
    pthread_mutex_unlock(&invoke_mutex);
    cout << "[client_invoke]: exit" << endl;
    //return rsm_client_protocol::BUSY;
  }
  return result;
// </lab8>
}

// 
// The primary calls the internal invoke at each member of the
// replicated state machine 
//
// the replica must execute requests in order (with no gaps) 
// according to requests' seqno 

rsm_protocol::status
rsm::invoke(int proc, viewstamp vs, std::string req, int &dummy)
{
  rsm_protocol::status ret = rsm_protocol::OK;
  // For lab 8
// <lab8>
  if (inviewchange)
  {
    cout << "[invoke]: inviewchange, so return busy" << endl;
    ret = rsm_protocol::BUSY;
    return ret;
  }
  if (insync)
  {
    cout << "[invoke]: insync, so return busy" << endl;
    ret = rsm_protocol::BUSY;
    return ret;
  }
  
// ==================================================
  cout << "[invoke]: name=" << name << endl;
  pthread_mutex_lock(&rsm_mutex);
  name = "invoke";
  if (inviewchange || cfg->myaddr() == primary)
  {
    if (inviewchange)
    {
      cout << "[invoke]: inviewchange, so fail" << endl;
    }
    if (cfg->myaddr() == primary)
    {
      cout << "[invoke]: I am a primary, so fail" << endl;
    }
    ret = rsm_protocol::ERR;
  }
  else
  {
    cout << "[invoke]: will handle" << endl;
    if (vs.vid == myvs.vid)
    {
      cout << "[invoke]: view number match, vs.vid=" << vs.vid << endl;    
      if (vs.seqno == myvs.seqno + 1)
      {
        cout << "[invoke]: myvs.seqno=" << myvs.seqno << " executing" << endl;
        execute(proc, req);
        last_myvs = myvs;
        myvs = vs;  
        cout << "[invoke]: update myvs.vid=" << myvs.vid<< " myvs.seqno=" << myvs.seqno
                      << " last_myvs.vid=" << last_myvs.vid << " last_myvs.seqno" << last_myvs.seqno << endl;
      }
      else
      {
        cout << "[invoke]: seqno is not expected. vs.seqno=" << vs.seqno << " last_myvs.seqno=" << last_myvs.seqno << " myvs.seqno=" << myvs.seqno << endl;
        ret = rsm_protocol::ERR;
      }
    }
    else
    {
      cout << "[invoke]: view number is not expected. vs.vid=" << vs.vid << " myvs.vid=" << myvs.vid << endl;
      ret = rsm_protocol::ERR;
    }
  }
  breakpoint1();
  name = "";
  pthread_mutex_unlock(&rsm_mutex);
  cout << "[invoke]: exit" << endl;
// </lab8>
  return ret;
}

/**
 * RPC handler: Send back the local node's state to the caller
 */
rsm_protocol::status
rsm::transferreq(std::string src, viewstamp last, rsm_protocol::transferres &r)
{
  cout << "[transferreq]: enter, insync=" << insync << endl;
  cout << "[transferreq]: name=" << name << endl;
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  name = "transferreq";
// <lab8>
  cout << "[transferreq]: insync=" << insync << endl;
  if (!cfg->ismember(src))
  {
    name = "";
    assert(pthread_mutex_unlock(&rsm_mutex)==0);
    cout << "[transferreq]: " << src.c_str() << " is not a member" << endl;
    return rsm_protocol::ERR;
  }
  // sometimes, a transferreq may arrive before the primary gets ready to sync.
  if (insync == 0)
  {
    name = "";
    assert(pthread_mutex_unlock(&rsm_mutex)==0);
    cout << "[transferreq]: primary is not ready to sync" << endl;
    return rsm_protocol::BUSY;
  }
// <lab8>
  int ret = rsm_protocol::OK;
  printf("[transferreq]: transferreq from %s (%d,%d) vs (%d,%d)\n", src.c_str(), 
	 last.vid, last.seqno, last_myvs.vid, last_myvs.seqno);
  if (stf && last != last_myvs)
  {
    r.state = stf->marshal_state();
  }
  r.last = last_myvs;
  name = "";
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  cout << "[transferreq]: exit" << endl;
  return ret;
}

/**
  * RPC handler: Send back the local node's latest viewstamp
  */
rsm_protocol::status
rsm::transferdonereq(std::string m, int &r)
{
  cout << "[transferdonereq]: enter" << endl;
  int ret = rsm_protocol::OK;
  cout << "[transferdonereq]: name=" << name << endl;
  assert (pthread_mutex_lock(&rsm_mutex) == 0);
  name = "transferdonereq";
// <lab8>
  insync--;
  cout << "[transferdonereq]: insync=" << insync << endl;
  r = 0;
// </lab8>
  name = "";
  assert (pthread_mutex_unlock(&rsm_mutex) == 0);
  cout << "[transferdonereq]: exit" << endl;
  return ret;
}

rsm_protocol::status
rsm::joinreq(std::string m, viewstamp last, rsm_protocol::joinres &r)
{
  int ret = rsm_protocol::OK;
  cout << "[joinreq]: name=" << name << endl;
  assert (pthread_mutex_lock(&rsm_mutex) == 0);
  name = "joinreq";
  printf("[joinreq]: src %s last (%d,%d) mylast (%d,%d)\n", m.c_str(), 
	 last.vid, last.seqno, last_myvs.vid, last_myvs.seqno);
  if (cfg->ismember(m)) {
    printf("[joinreq]: is still a member\n");
    r.log = cfg->dump();
  } else if (cfg->myaddr() != primary) {
    printf("[joinreq]: busy\n");
    ret = rsm_protocol::BUSY;
  } else {
    // Lab 7: invoke config to create a new view that contains m
// <lab7>
    if (cfg->add(m))
    {
      r.log = cfg->dump();
    }
    else
    {
      ret = rsm_protocol::ERR;
    }
// </lab7>
  }
  name = "";
  assert (pthread_mutex_unlock(&rsm_mutex) == 0);
  return ret;
}

/*
 * RPC handler: Send back all the nodes this local knows about to client
 * so the client can switch to a different primary 
 * when it existing primary fails
 */
rsm_client_protocol::status
rsm::client_members(int i, std::vector<std::string> &r)
{
  cout << "[client_members]: enter" << endl;
  std::vector<std::string> m;
  cout << "[client_members]: name=" << name << endl;
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  name = "client_members";
  m = cfg->get_curview();
  m.push_back(primary);
  r = m;
  printf("[client_members] return %s m %s\n", cfg->print_curview().c_str(),
	 primary.c_str());
  name = "";
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  cout << "[client_members]: exit" << endl;
  return rsm_client_protocol::OK;
}

// if primary is member of new view, that node is primary
// otherwise, the lowest number node of the previous view.
// caller should hold rsm_mutex
void
rsm::set_primary()
{
  std::vector<std::string> c = cfg->get_curview();
  std::vector<std::string> p = cfg->get_prevview();
  assert (c.size() > 0);

  if (isamember(primary,c)) {
    printf("set_primary: primary stays %s\n", primary.c_str());
    return;
  }

  assert(p.size() > 0);
  for (unsigned i = 0; i < p.size(); i++) {
    if (isamember(p[i], c)) {
      primary = p[i];
      printf("set_primary: primary is %s\n", primary.c_str());
      return;
    }
  }
  assert(0);
}

// Assume caller holds rsm_mutex
bool
rsm::amiprimary_wo()
{
  return primary == cfg->myaddr() && !inviewchange;
}

bool
rsm::amiprimary()
{
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  bool r = amiprimary_wo();
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  return r;
}


// Testing server

// Simulate partitions

// assumes caller holds rsm_mutex
void
rsm::net_repair_wo(bool heal)
{
  std::vector<std::string> m;
  m = cfg->get_curview();
  for (unsigned i  = 0; i < m.size(); i++) {
    if (m[i] != cfg->myaddr()) {
        handle h(m[i]);
	printf("rsm::net_repair_wo: %s %d\n", m[i].c_str(), heal);
	if (h.get_rpcc()) h.get_rpcc()->set_reachable(heal);
    }
  }
  rsmrpc->set_reachable(heal);
}

rsm_test_protocol::status 
rsm::test_net_repairreq(int heal, int &r)
{
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  printf("rsm::test_net_repairreq: %d (dopartition %d, partitioned %d)\n", 
	 heal, dopartition, partitioned);
  if (heal) {
    net_repair_wo(heal);
    partitioned = false;
  } else {
    dopartition = true;
    partitioned = false;
  }
  r = rsm_test_protocol::OK;
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  return r;
}

// simulate failure at breakpoint 1 and 2

void 
rsm::breakpoint1()
{
  cout << "[breakpoint1], enter" << endl;
  if (break1) {
    printf("Dying at breakpoint 1 in rsm!\n");
    exit(1);
  }
  cout << "[breakpoint1], exit" << endl;
}

void 
rsm::breakpoint2()
{
  cout << "[breakpoint2], enter" << endl;
  if (break2) {
    printf("Dying at breakpoint 2 in rsm!\n");
    exit(1);
  }
  cout << "[breakpoint2], exit" << endl;
}

void 
rsm::partition1()
{
  if (dopartition) {
    net_repair_wo(false);
    dopartition = false;
    partitioned = true;
  }
}

rsm_test_protocol::status
rsm::breakpointreq(int b, int &r)
{
  cout << "[breakpointreq]: enter" << endl;
  r = rsm_test_protocol::OK;
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  printf("[breakpointreq]: %d\n", b);
  if (b == 1) break1 = true;
  else if (b == 2) break2 = true;
  else if (b == 3 || b == 4) cfg->breakpoint(b);
  else r = rsm_test_protocol::ERR;
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  cout << "[breakpointreq]: exit, break1=" << break1
       << " break2=" << break2 << endl;
  return r;
}
