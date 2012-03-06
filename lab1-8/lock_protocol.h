// lock protocol

#ifndef lock_protocol_h
#define lock_protocol_h

#include "rpc.h"

class lock_protocol {
 public:
  enum xxstatus { OK, RETRY, RPCERR, NOENT, IOERR };
  typedef int status;
  typedef unsigned long long lockid_t;
  enum rpc_numbers {
    acquire = 0x7001,
    release,
    subscribe,	// for lab 5
    unsubscribe,
    stat
  };
};

class rlock_protocol {
 public:
  enum xxstatus { OK, RPCERR };
  typedef int status;
  enum rpc_numbers {
    revoke = 0x8001,
    retry = 0x8002
  };
};

#include "pthread.h"

// Convenient wrappers around
// + pthread_mutex
// + pthread_cond

class Mutex {
public:
  Mutex()         { pthread_mutex_init(&m_, NULL); }
  ~Mutex()        { pthread_mutex_destroy(&m_); }

  void lock()     { pthread_mutex_lock(&m_); }
  void unlock()   { pthread_mutex_unlock(&m_); }

private:
  friend class ConditionVar;

  pthread_mutex_t m_;

  // Non-copyable, non-assignable
  Mutex(Mutex &);
  Mutex& operator=(Mutex&);
};

class ConditionVar {
public:
  ConditionVar()          { pthread_cond_init(&cv_, NULL); }
  ~ConditionVar()         { pthread_cond_destroy(&cv_); }

  void wait(Mutex* mutex) { pthread_cond_wait(&cv_, &(mutex->m_)); }
  void signal()           { pthread_cond_signal(&cv_); }
  void signalAll()        { pthread_cond_broadcast(&cv_); }

  void timedWait(Mutex* mutex, const struct timespec* timeout) {
    pthread_cond_timedwait(&cv_, &(mutex->m_), timeout);
  }


private:
  pthread_cond_t cv_;

  // Non-copyable, non-assignable
  ConditionVar(ConditionVar&);
  ConditionVar& operator=(ConditionVar&);
};

class MyScopedLock {
public:
  explicit MyScopedLock(Mutex* lock) : m_(lock) { m_->lock(); }
  ~MyScopedLock()   { m_->unlock(); }

private:
  Mutex* m_;

  // Non-copyable, non-assignable
  MyScopedLock(MyScopedLock&);
  MyScopedLock& operator=(MyScopedLock&);
};

#endif 
