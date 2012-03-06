#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>

#include "lock_protocol.h"
#include "lock_client.h"
#include "lock_client_cache.h"

class yfs_client : public lock_release_user {
  extent_client *ec;
// <lab4>
//  lock_client *lc;
// </lab4>
// <lab5>
    lock_client_cache *lc;
// </lab5>
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, FBIG, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    unsigned long long inum;
  };

 public:
  static std::string filename(inum);
  static inum n2i(std::string);

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);

// <lab2>
  int put(inum, std::string);
  int get_file_or_dir_content(inum, std::string &);  
// </lab2>

// <lab4>
  int acquire_lock(inum);
  int release_lock(inum);
  int remove(inum);
// </lab4>
// <lab6>
  void dorelease(lock_protocol::lockid_t);
// </lab6>
};

#endif 
