#include "paxos.h"
#include "handle.h"
// #include <signal.h>
#include <stdio.h>

// This module implements the proposer and acceptor of the Paxos
// distributed algorithm as described by Lamport's "Paxos Made
// Simple".  To kick off an instance of Paxos, the caller supplies a
// list of nodes, a proposed value, and invokes the proposer.  If the
// majority of the nodes agree on the proposed value after running
// this instance of Paxos, the acceptor invokes the upcall
// paxos_commit to inform higher layers of the agreed value for this
// instance.


bool
operator> (const prop_t &a, const prop_t &b)
{
  return (a.n > b.n || (a.n == b.n && a.m > b.m));
}

bool
operator>= (const prop_t &a, const prop_t &b)
{
  return (a.n > b.n || (a.n == b.n && a.m >= b.m));
}

std::string
print_members(const std::vector<std::string> &nodes)
{
  std::string s;
  s.clear();
  for (unsigned i = 0; i < nodes.size(); i++) {
    s += nodes[i];
    if (i < (nodes.size()-1))
      s += ",";
  }
  return s;
}

bool isamember(std::string m, const std::vector<std::string> &nodes)
{
  for (unsigned i = 0; i < nodes.size(); i++) {
    if (nodes[i] == m) return 1;
  }
  return 0;
}

bool
proposer::isrunning()
{
  bool r;
  assert(pthread_mutex_lock(&pxs_mutex)==0);
  r = !stable;
  assert(pthread_mutex_unlock(&pxs_mutex)==0);
  return r;
}

// check if the servers in l2 contains a majority of servers in l1
bool
proposer::majority(const std::vector<std::string> &l1, 
		const std::vector<std::string> &l2)
{
  unsigned n = 0;

  for (unsigned i = 0; i < l1.size(); i++) {
    if (isamember(l1[i], l2))
      n++;
  }
  return n >= (l1.size() >> 1) + 1;
}

proposer::proposer(class paxos_change *_cfg, class acceptor *_acceptor, 
		   std::string _me)
  : cfg(_cfg), acc (_acceptor), me (_me), break1 (false), break2 (false), 
    stable (true)
{
  assert (pthread_mutex_init(&pxs_mutex, NULL) == 0);

}

void
proposer::setn()
{
  my_n.n = acc->get_n_h().n + 1 > my_n.n + 1 ? acc->get_n_h().n + 1 : my_n.n + 1;
	my_n.m = me;
}

bool
proposer::run(int instance, std::vector<std::string> newnodes, std::string newv)  
{
  std::vector<std::string> accepts;
  std::vector<std::string> nodes;
  std::vector<std::string> nodes1;
  std::string v;
  bool r = false;

  pthread_mutex_lock(&pxs_mutex);
  printf("start: initiate paxos for %s w. i=%d v=%s stable=%d\n",
	 print_members(newnodes).c_str(), instance, newv.c_str(), stable);
  if (!stable) {  // already running proposer?
    printf("proposer::run: already running\n");
    pthread_mutex_unlock(&pxs_mutex);

    return false;
  }
	stable = false;
	
//	printf("check~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~0\n");
//	printf("highest instance we have decided %d\n",acc -> get_instance_h());
//	printf("highest propose view we have accept %d\n",acc ->get_n_a().n);
//	printf("current instance is %d\n",instance);
//	printf("get_n_h is %d\n",acc->get_n_h().n);
//	printf("check~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~0\n");
	
//	if( acc->get_n_h().n >= instance ){
//		printf("wait~~~~~~~~~~~~~~~~~~\n");
//		pthread_mutex_unlock(&pxs_mutex);
//		sleep(10);
//		pthread_mutex_lock(&pxs_mutex);
//	}

	c_v = newv;
	c_nodes = newnodes;
	//before start sending check the log, finish the agreeing


	
//	if (acc->get_instance_h() <= acc ->get_n_a().n ) {
//		printf("paxos:recommit\n");
//		acc -> commit(instance, acc ->get_v_a());
//		c_nodes = acc -> value(instance);
//		c_v = newv;
////		stable = true;
////		pthread_mutex_unlock(&pxs_mutex);
//
//	}
	
	
  setn();   //chose n, unique and higher than any n seen so far
  accepts.clear();
  nodes.clear();
  v.clear();
  nodes = c_nodes;
  if (prepare(instance, accepts, nodes, v)) {    //send prepare to all servers including self
	  //if prepare ok
    if (majority(c_nodes, accepts)) {  //if from majority
      printf("paxos::manager: received a majority of prepare responses\n");
		
      if (v.size() == 0) {
		  v = c_v;    //v contains the new added node info
      }
	  	

      breakpoint1();

      nodes1 = accepts;
      accepts.clear();
      accept(instance, accepts, nodes1, v);   //send accept to all, now v is the newv

      if (majority(c_nodes, accepts)) {   //if accept ok from the majority
	printf("paxos::manager: received a majority of accept responses\n");

	breakpoint2();
		  
		  //before decide check
//		  printf("check~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
//		  printf("highest instance we have decided %d\n",acc -> get_instance_h());
//		  printf("highest propose view we have accept %d\n",acc ->get_n_a().n);
//		  printf("current instance is %d\n",instance);
//		  printf("check~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

		  
	decide(instance, accepts, v);   //send decide
	r = true;
      } else {
	printf("paxos::manager: no majority of accept responses\n");
      }
    } else {
      printf("paxos::manager: no majority of prepare responses\n");
    }
  } else {
    printf("paxos::manager: prepare is rejected %d\n", stable);
  }
//	pthread_mutex_unlock(&pxs_mutex);
//	
//	pthread_mutex_lock(&pxs_mutex);
  stable = true;
  pthread_mutex_unlock(&pxs_mutex);
  return r;
}

bool
proposer::prepare(unsigned instance, std::vector<std::string> &accepts, 
         std::vector<std::string> nodes,
         std::string &v)
{
	//send prepare to all servers including it self
	paxos_protocol::preparearg a;
	a.instance = instance;
	a.v = c_v;   //c_v = newv 
	a.n = my_n;  //my_n = new id 
	std::string tmpva;
	unsigned tmpna = 0;

	for (std::vector<std::string>::iterator it = nodes.begin(); it != nodes.end(); it++) {
		handle h((*it));
		paxos_protocol::prepareres r;
		
		rpcc *pxc = h.get_rpcc();
		int ret = rpc_const::timeout_failure;
		//first check instance 
		
		
		//!!!!! if pxc is null, no rpc call should be invoked
		if (pxc) {
			pthread_mutex_unlock(&pxs_mutex);
			ret = pxc -> call(paxos_protocol::preparereq, me, a, r, rpcc::to(1000) );  //send rpc to all
			pthread_mutex_lock(&pxs_mutex);
		}
		if (ret != paxos_protocol::OK){
			continue;
		}
		
		if (r.oldinstance == 0 && 1 == r.accept ) {
			//add to accept
			
			if (tmpna < r.n_a.n) {
				tmpna = r.n_a.n;
				v = r.v_a;
			}
			
			accepts.push_back((*it));
			
		}else if (0 != r.oldinstance && 0 == r.accept){
			//old do nothind
			printf("old instance\n");
			//commit it
			
			return false;
			
		}else {
			//ie. the node is delelted , do nothing
		}

		
	}
	//chose v with highest n_a
//	v = tmpva; 

  return true;
}


void
proposer::accept(unsigned instance, std::vector<std::string> &accepts,
        std::vector<std::string> nodes, std::string v)
{
	
	paxos_protocol::acceptarg a;
	a.instance = instance;
	a.v = v;
	a.n = my_n;
	
	//send accept to all 
	for (std::vector<std::string>::iterator it = nodes.begin(); it != nodes.end(); it++) {
		handle h((*it));
		int r = -1;
		
		rpcc *pxc = h.get_rpcc();
		int ret = rpc_const::timeout_failure;
		
		if (pxc) {
			pthread_mutex_unlock(&pxs_mutex);
			ret = pxc -> call(paxos_protocol::acceptreq, me, a, r, rpcc::to(1000));  //send rpc to all
			pthread_mutex_lock(&pxs_mutex);
		}
		if (ret != paxos_protocol::OK){
			continue;
		}
		
		if ( -1 != r ) { // return accept OK
			accepts.push_back((*it));
		}else {
			//do nothing
		}

	}
}

void
proposer::decide(unsigned instance, std::vector<std::string> accepts, 
	      std::string v)
{
	//send decide to all
	
	
	paxos_protocol::decidearg a;
	a.instance = instance;
	a.v = v;
	
	for (std::vector<std::string>::iterator it = accepts.begin(); it != accepts.end(); it++) {
		handle h((*it));
		int r;
		rpcc *pxc = h.get_rpcc();
		int ret = rpc_const::timeout_failure;
		
		if (pxc) {
			pthread_mutex_unlock(&pxs_mutex);
			ret = pxc -> call(paxos_protocol::decidereq, me, a, r, rpcc::to(1000));
			pthread_mutex_lock(&pxs_mutex);

		}
		
		if (ret != paxos_protocol::OK){
			continue;
		}
		
	}
}

acceptor::acceptor(class paxos_change *_cfg, bool _first, std::string _me, 
	     std::string _value)
  : cfg(_cfg), me (_me), instance_h(0)
{
  assert (pthread_mutex_init(&pxs_mutex, NULL) == 0);

  n_h.n = 0;
  n_h.m = me;
  n_a.n = 0;
  n_a.m = me;
  v_a.clear();
	
	printf("paxos:cread a log\n");
  l = new log (this, me);

  if (instance_h == 0 && _first) {  //the first node init view1 to itself
    values[1] = _value;
    l->loginstance(1, _value);
    instance_h = 1;
  }

  pxs = new rpcs(atoi(_me.c_str()));
  pxs->reg(paxos_protocol::preparereq, this, &acceptor::preparereq);
  pxs->reg(paxos_protocol::acceptreq, this, &acceptor::acceptreq);
  pxs->reg(paxos_protocol::decidereq, this, &acceptor::decidereq);
}

paxos_protocol::status
acceptor::preparereq(std::string src, paxos_protocol::preparearg a,
    paxos_protocol::prepareres &r)
{
  // handle a preparereq message from proposer
//	printf("------------from %s--------------------\n",src.c_str());
//	printf("acceptor:pre instance_h is %d\n",instance_h);
//	printf("acceptor:pre a.instance is %d\n",a.instance);
//	printf("acceptor:pre n_h is %d \n",n_h.n);
//	printf("acceptor:pre a.n is %d\n",a.n.n);

	
	if (a.instance <= instance_h) {
		//old instance
		printf("acceptor:pre OLD INSTANCE\n");
		r.oldinstance = instance_h; //set old instance true
		r.accept = 0;
		r.n_a = a.n;
		r.v_a = value(a.instance);
			printf("--------------------------------\n");
		return paxos_protocol::OK;
		
	}else if (a.n > n_h) {
		
		n_h = a.n;
		r.n_a = n_a;
		r.v_a = v_a;
		r.accept = 1;
		r.oldinstance = 0;
		l ->loghigh(n_h);
			printf("--------------------------------\n");
		return paxos_protocol::OK;
	}
	printf("acceptor:pre ERR");
	printf("--------------------------------\n");
	return paxos_protocol::ERR;

}

paxos_protocol::status
acceptor::acceptreq(std::string src, paxos_protocol::acceptarg a, int &r)
{

  // handle an acceptreq message from proposer
	prop_t n = a.n;
	std::string v = a.v;
	if (n >= n_h) {
		n_a = n;
		v_a = v;
		
		r = n.n;
		
		l -> logprop(n_a, v_a);
		return paxos_protocol::OK;
	}
	printf("acceptor:acc  ERR\n");
	return paxos_protocol::ERR;
}

paxos_protocol::status
acceptor::decidereq(std::string src, paxos_protocol::decidearg a, int &r)
{

  // handle an decide message from proposer
	printf("------------from %s--------------------\n",src.c_str());
	commit(a.instance, a.v);  //notice the lock
	printf("paxos:commited called\n");

	printf("----------------------------\n");
  return paxos_protocol::OK;
}

void
acceptor::commit_wo(unsigned instance, std::string value)
{
  //assume pxs_mutex is held
  printf("acceptor::commit: instance=%d has v= %s\n", instance, value.c_str());
  if (instance > instance_h) {
    printf("commit: highestaccepteinstance = %d\n", instance);
    values[instance] = value;
    l->loginstance(instance, value);   //after commit, record the instance and value
    instance_h = instance;
    n_h.n = 0;                 //n_h should be 0
    n_h.m = me;
    n_a.n = 0;					//n_a should be 0
    n_a.m = me;
    v_a.clear();
    if (cfg) {
      pthread_mutex_unlock(&pxs_mutex);
      cfg->paxos_commit(instance, value);  //up call config module
      pthread_mutex_lock(&pxs_mutex);
    }
  }
}

void
acceptor::commit(unsigned instance, std::string value)
{
  pthread_mutex_lock(&pxs_mutex);
  commit_wo(instance, value);  
  pthread_mutex_unlock(&pxs_mutex);
}

std::string
acceptor::dump()
{
  return l->dump();
}

void
acceptor::restore(std::string s)
{
  l->restore(s);
  l->logread();
}



// For testing purposes

// Call this from your code between phases prepare and accept of proposer
void
proposer::breakpoint1()
{
  if (break1) {
    printf("Dying at breakpoint 1!\n");
    exit(1);
  }
}

// Call this from your code between phases accept and decide of proposer
void
proposer::breakpoint2()
{
  if (break2) {
    printf("Dying at breakpoint 2!\n");
    exit(1);
  }
}

void
proposer::breakpoint(int b)
{
  if (b == 3) {
    printf("Proposer: breakpoint 1\n");
    break1 = true;
  } else if (b == 4) {
    printf("Proposer: breakpoint 2\n");
    break2 = true;
  }
}
