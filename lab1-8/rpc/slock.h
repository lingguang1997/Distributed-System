#ifndef __SCOPED_LOCK__
#define __SCOPED_LOCK__

#include <pthread.h>
#include <assert.h>
struct ScopedLock {
	private:
		pthread_mutex_t *m_;
	public:
		ScopedLock(pthread_mutex_t *m): m_(m) {
			assert(pthread_mutex_lock(m_)==0);
		}
		~ScopedLock() {
			assert(pthread_mutex_unlock(m_)==0);
		}
};
#endif  /*__SCOPED_LOCK__*/

/*class Mutex {
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
};*/
