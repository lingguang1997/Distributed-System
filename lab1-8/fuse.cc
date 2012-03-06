/*
 * receive request from fuse and call methods of yfs_client
 *
 * started life as low-level example in the fuse distribution.  we
 * have to use low-level interface in order to get i-numbers.  the
 * high-level interface only gives us complete paths.
 */

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sstream>
#include "yfs_client.h"

using namespace std;

int myid;
yfs_client *yfs;

int id() { 
  return myid;
}

// <lab2>
yfs_client::inum get_new_inum(bool is_file)
{
  yfs_client::inum new_inum;
  //srand(time(NULL));
  new_inum = rand();

  if (is_file)
    new_inum |= 0x80000000;
  else
    new_inum &= 0x7FFFFFFF;

  return new_inum;
}
// </lab2>

yfs_client::status
getattr(yfs_client::inum inum, struct stat &st)
{
  
  std::cout << "[getattr]" << std::endl;
  yfs_client::status ret;

  bzero(&st, sizeof(st));

  st.st_ino = inum;
  printf("getattr %016llx %d\n", inum, yfs->isfile(inum));
  if(yfs->isfile(inum)){
     yfs_client::fileinfo info;
     ret = yfs->getfile(inum, info);
     if(ret != yfs_client::OK)
       return ret;
     st.st_mode = S_IFREG | 0666;
     st.st_nlink = 1;
     st.st_atime = info.atime;
     st.st_mtime = info.mtime;
     st.st_ctime = info.ctime;
     st.st_size = info.size;
     printf("   getattr -> %llu\n", info.size);
   } else {
     yfs_client::dirinfo info;
     ret = yfs->getdir(inum, info);
     if(ret != yfs_client::OK)
       return ret;
     st.st_mode = S_IFDIR | 0777;
     st.st_nlink = 2;
     st.st_atime = info.atime;
     st.st_mtime = info.mtime;
     st.st_ctime = info.ctime;
     printf("   getattr -> %lu %lu %lu\n", info.atime, info.mtime, info.ctime);
   }
   return yfs_client::OK;
}


void
fuseserver_getattr(fuse_req_t req, fuse_ino_t ino,
          struct fuse_file_info *fi)
{
    std::cout << "[getattr]" << std::endl;
    struct stat st;
    yfs_client::inum inum = ino; // req->in.h.nodeid;
    yfs_client::status ret;
// <lab4>
    yfs->acquire_lock(inum);
// </lab4>
    ret = getattr(inum, st);
// <lab4>
    yfs->release_lock(inum);
// </lab4>
    if(ret != yfs_client::OK){
      fuse_reply_err(req, ENOENT);
      return;
    }
    fuse_reply_attr(req, &st, 0);
}

void
fuseserver_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi)
{
  printf("fuseserver_setattr 0x%x\n", to_set);
  if (FUSE_SET_ATTR_SIZE & to_set) {
    printf("   fuseserver_setattr set size to %llu\n", attr->st_size);
    struct stat st;
// <lab3>
    yfs_client::inum finum = ino;
    std::string file_content;
    if (yfs->isfile(finum))
    {
// <lab4>
      yfs->acquire_lock(finum);
// </lab4>
      yfs->get_file_or_dir_content(finum, file_content);
      size_t size_diff = file_content.size() - attr->st_size;
      if (size_diff > 0)
      {
        file_content = file_content.substr(0, attr->st_size);
      }     
      else
      {
        file_content.append(-size_diff, '\0');
      }
      yfs->put(finum, file_content);
      getattr(finum, st);
// <lab4>
      yfs->release_lock(finum);
// </lab4>
      fuse_reply_attr(req, &st, 0);
      return;  
    }
// </lab3>
  }
  fuse_reply_err(req, ENOSYS);
}

void
fuseserver_read(fuse_req_t req, fuse_ino_t ino, size_t size,
      off_t off, struct fuse_file_info *fi)
{
   std::cout << "[read]" << std::endl;
// <lab3>
  yfs_client::inum finum = ino;
  if (yfs->isfile(finum))
  {
    std::string file_content, required_content;
// <lab4>
    yfs->acquire_lock(finum);
    yfs->get_file_or_dir_content(finum, file_content);
    yfs->release_lock(finum);
// <lab4>
    required_content = file_content.substr(off, size);
    fuse_reply_buf(req, required_content.c_str(), required_content.size());
  }
  else
  {
    fuse_reply_err(req, ENOSYS);
  }
// </lab3>
}

void
fuseserver_write(fuse_req_t req, fuse_ino_t ino,
  const char *buf, size_t size, off_t off,
  struct fuse_file_info *fi)
{
  std::cout << "[write]" << std::endl;
// <lab3>
  yfs_client::inum finum = ino;
  if (yfs->isfile(finum))
  {
    std::string file_content, buf_str = buf, to_write;
// <lab4>
    yfs->acquire_lock(finum);
// </lab4>
    yfs->get_file_or_dir_content(finum, file_content);
    
    if (buf_str.size() >= size)
    {
      to_write = buf_str.substr(0, size);
    }
    else
    {
      to_write = buf_str.append(size - buf_str.size(), '\0');
    }

    if (file_content.size() < off)
    {
      file_content.append(off - file_content.size(), '\0');
      file_content.append(to_write);
    }
    else if (file_content.size() <= off + size)
    {
      file_content = file_content.substr(0, off);
      file_content.append(to_write);
    }
    else
    {
      file_content.replace(off, size, to_write);
    }
    yfs->put(finum, file_content);
// <lab4>
    yfs->release_lock(finum);
// </lab4> 
    fuse_reply_write(req, size);
  }
  else
  {
    fuse_reply_err(req, ENOSYS);
  }
// </lab3>
}

yfs_client::status
fuseserver_createhelper(fuse_ino_t parent, const char *name,
     mode_t mode, struct fuse_entry_param *e)
{
  // You fill this in
// <lab2>
  std::cout << "[createhelper] parent: " << parent <<" name " << name << std::endl;
  int code = yfs_client::NOENT;
  if (yfs->isdir(parent))
  {  
// <lab4>
    yfs->acquire_lock(parent);
    std::string dir_struct;
    yfs->get_file_or_dir_content(parent, dir_struct);
    std::string name_str = name;
    std::string::size_type pos = dir_struct.find(name_str + "=");
    if (pos != std::string::npos)
    { 
      std::cout << "[createhelper] the file or dir already exists" << name << std::endl;
      yfs->release_lock(parent);
      return yfs_client::EXIST;      
    }
// </lab4>
    yfs_client::inum finum = get_new_inum(true);
// <lab4>
    yfs->acquire_lock(finum);
// </lab4>
    dir_struct.append(name);
    dir_struct.append("="); 
    dir_struct.append(yfs->filename(finum));
    dir_struct.append(";");

    yfs->put(finum, "");
    yfs->put(parent, dir_struct);

    e->attr_timeout = 0.0;
    e->entry_timeout = 0.0;
    e->ino = finum;
    getattr(e->ino, e->attr);
// <lab4>
    yfs->release_lock(finum);
    yfs->release_lock(parent);
// </lab4>
    code = yfs_client::OK;
  }
  return code;
// </lab2>
}

void
fuseserver_create(fuse_req_t req, fuse_ino_t parent, const char *name,
   mode_t mode, struct fuse_file_info *fi)
{
  struct fuse_entry_param e;
  yfs_client::status status = fuseserver_createhelper(parent, name, mode, &e); 
  if(status == yfs_client::OK ) {
    fuse_reply_create(req, &e, fi);
  } else if (status == yfs_client::EXIST) {
    fuse_reply_err(req, EEXIST); 
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void fuseserver_mknod( fuse_req_t req, fuse_ino_t parent, 
    const char *name, mode_t mode, dev_t rdev ) {
  struct fuse_entry_param e;
  if( fuseserver_createhelper( parent, name, mode, &e ) == yfs_client::OK ) {
    fuse_reply_entry(req, &e);
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void
fuseserver_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  std::cout << "[fuse_lookup]: parent " << parent << " name " << name << std::endl;
  struct fuse_entry_param e;

  e.attr_timeout = 0.0;
  e.entry_timeout = 0.0;

  // You fill this in:
  // Look up the file named `name' in the directory referred to by
  // `parent' in YFS. If the file was found, initialize e.ino and
  // e.attr appropriately.
// <lab2>  
  std::string parent_struct, name_str = name;
// <lab4>
  yfs->acquire_lock(parent);
// </lab4>
  yfs->get_file_or_dir_content(parent, parent_struct);
  size_t pos = parent_struct.find(name_str + "=");

  if (pos == std::string::npos)
  {
    yfs->release_lock(parent);
    fuse_reply_err(req, ENOENT);
  } 
  else
  {
    std::string temp = parent_struct.substr(pos);
    size_t start = temp.find_first_of('=');
    size_t end = temp.find_first_of(';');
    std::string inum_str = temp.substr(start + 1, end - 1);

    e.ino = yfs->n2i(inum_str);
// <lab4>
    yfs->acquire_lock(e.ino);
    if (getattr(e.ino, e.attr) != yfs_client::OK)
    {
      yfs->release_lock(e.ino);
      yfs->release_lock(parent);
      fuse_reply_err(req, ENOENT);
      return;
    }
    yfs->release_lock(e.ino);
    yfs->release_lock(parent);
// </lab4>
    fuse_reply_entry(req, &e); 
  } 
// </lab2>
}


struct dirbuf {
    char *p;
    size_t size;
};

void dirbuf_add(struct dirbuf *b, const char *name, fuse_ino_t ino)
{
    struct stat stbuf;
    size_t oldsize = b->size;
    b->size += fuse_dirent_size(strlen(name));
    b->p = (char *) realloc(b->p, b->size);
    memset(&stbuf, 0, sizeof(stbuf));
    stbuf.st_ino = ino;
    fuse_add_dirent(b->p + oldsize, name, &stbuf, b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
          off_t off, size_t maxsize)
{
  if (off < bufsize)
    return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

void
fuseserver_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
          off_t off, struct fuse_file_info *fi)
{
  yfs_client::inum inum = ino; // req->in.h.nodeid;
  struct dirbuf b;
  yfs_client::dirent e;

  std::cout << "[readdir]" << std::endl;

  if(!yfs->isdir(inum)){
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  memset(&b, 0, sizeof(b));


  // fill in the b data structure using dirbuf_add
// <lab2>
  string dir_struct;
// <lab4>
  yfs->acquire_lock(inum);
// </lab4>
  yfs->get_file_or_dir_content(inum, dir_struct);

  stringstream ss(dir_struct);
  vector<string> list;

  string file;
  while (getline(ss, file, ';'))
    list.push_back(file);

  size_t pos;
  string file_name;
  yfs_client::inum file_inum;
  for (int i = 0; i < list.size(); i++)
  {
    pos = list[i].find("=");
    file_name = list[i].substr(0, pos);
    file_inum = yfs->n2i(list[i].substr(pos + 1));
    dirbuf_add(&b, file_name.c_str(), file_inum); 
  }    
// <lab4>
  yfs->release_lock(inum);
// </lab4>
  reply_buf_limited(req, b.p, b.size, off, size);
  free(b.p);
// </lab2>
}


void
fuseserver_open(fuse_req_t req, fuse_ino_t ino,
     struct fuse_file_info *fi)
{
  std::cout << "[open]" << std::endl;
  // <lab3>
  yfs_client::inum inum = ino;
  if (yfs->isfile(inum))
  {
    fi->fh = fi->flags;
    fuse_reply_open(req, fi);
  }
  else
  {
    fuse_reply_err(req, ENOSYS);
  }
  // </lab3>
}

void
fuseserver_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
     mode_t mode)
{ 
  std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << std::endl;
  struct fuse_entry_param e; 
  // <lab4>
  if (yfs->isdir(parent))
  {
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " acquire_lock of pairent" << std::endl;
    yfs->acquire_lock(parent);
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " get the lock of pairent" << std::endl;
    yfs_client::inum new_inum = get_new_inum(false);
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " get_new_inum of a dir: " << new_inum << std::endl;
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " get the lock of new_inum: " << new_inum << std::endl;
    yfs->acquire_lock(new_inum);
    std::string parent_struct;
    yfs->get_file_or_dir_content(parent, parent_struct);
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " get the file dir or dir content" << std::endl;
    parent_struct.append(name);
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " appended the name of the parent struct" << std::endl;
    parent_struct.append("=");
    parent_struct.append(yfs->filename(new_inum));
    parent_struct.append(";");

    yfs->put(new_inum, "");
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " put new inum " << new_inum << " and empty string" << std::endl;
    yfs->put(parent, parent_struct);
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " put the parent struct " << parent_struct << " to parent " << std::endl;

    e.attr_timeout = 0.0;
    e.entry_timeout = 0.0;
    e.ino = new_inum;
    getattr(e.ino, e.attr);    
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << __LINE__ << std::endl;

    yfs->release_lock(new_inum);
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " release_lock of new_inum" << std::endl;
    yfs->release_lock(parent);    
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << " release_lock of pairent" << std::endl;
    fuse_reply_entry(req, &e);
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << __LINE__ << std::endl;
  }
  else
  {
    fuse_reply_err(req, ENOSYS);
    std::cout << "[fuse_mkdir]: parent: " << parent << " name: " << name << __LINE__ << std::endl;
  }
  // </lab4>
}

void
fuseserver_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{

  // You fill this in
  // Success:	fuse_reply_err(req, 0);
  // Not found:	fuse_reply_err(req, ENOENT);
  std::cout << "[fuse_unlink] parent " << parent << " name " << name << std::endl; 
  if (yfs->isdir(parent))
  {
    std::string parent_struct;
    yfs->acquire_lock(parent);
    yfs->get_file_or_dir_content(parent, parent_struct);
    std::string uname = name;
    std::string::size_type name_start = parent_struct.find(uname + "=");
    if (name_start != std::string::npos)
    {
      std::string from_name_start = parent_struct.substr(name_start);
      std::string::size_type inum_start = from_name_start.find_first_of("=");
      std::string::size_type inum_end = from_name_start.find_first_of(";");
      std::string inum_name = from_name_start.substr(inum_start + 1, inum_end - 1);
      yfs_client::inum unlink_inum = yfs->n2i(inum_name);

      parent_struct.erase(name_start, inum_end + 1);

      yfs_client::inum parent_inum = parent;
      yfs->put(parent_inum, parent_struct);
      yfs->acquire_lock(unlink_inum);
      yfs->remove(unlink_inum);
      yfs->release_lock(unlink_inum);
      yfs->release_lock(parent);
      fuse_reply_err(req, 0);
      return;
    }
    else
    {
      yfs->release_lock(parent);
    }
  }
  fuse_reply_err(req, ENOSYS);
}

void
fuseserver_statfs(fuse_req_t req)
{
  struct statvfs buf;

  printf("statfs\n");

  memset(&buf, 0, sizeof(buf));

  buf.f_namemax = 255;
  buf.f_bsize = 512;

  fuse_reply_statfs(req, &buf);
}

struct fuse_lowlevel_ops fuseserver_oper;

int
main(int argc, char *argv[])
{
  char *mountpoint = 0;
  int err = -1;
  int fd;

  setvbuf(stdout, NULL, _IONBF, 0);

  if(argc != 4){
    fprintf(stderr, "Usage: yfs_client <mountpoint> <port-extent-server> <port-lock-server>\n");
    exit(1);
  }
  mountpoint = argv[1];

  srandom(getpid());

  myid = random();

  yfs = new yfs_client(argv[2], argv[3]);

  fuseserver_oper.getattr    = fuseserver_getattr;
  fuseserver_oper.statfs     = fuseserver_statfs;
  fuseserver_oper.readdir    = fuseserver_readdir;
  fuseserver_oper.lookup     = fuseserver_lookup;
  fuseserver_oper.create     = fuseserver_create;
  fuseserver_oper.mknod      = fuseserver_mknod;
  fuseserver_oper.open       = fuseserver_open;
  fuseserver_oper.read       = fuseserver_read;
  fuseserver_oper.write      = fuseserver_write;
  fuseserver_oper.setattr    = fuseserver_setattr;
  fuseserver_oper.unlink     = fuseserver_unlink;
  fuseserver_oper.mkdir      = fuseserver_mkdir;

  const char *fuse_argv[20];
  int fuse_argc = 0;
  fuse_argv[fuse_argc++] = argv[0];
#ifdef __APPLE__
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "nolocalcaches"; // no dir entry caching
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "daemon_timeout=86400";
#endif

  // everyone can play, why not?
  //fuse_argv[fuse_argc++] = "-o";
  //fuse_argv[fuse_argc++] = "allow_other";

  fuse_argv[fuse_argc++] = mountpoint;
  fuse_argv[fuse_argc++] = "-d";

  fuse_args args = FUSE_ARGS_INIT( fuse_argc, (char **) fuse_argv );
  int foreground;
  int res = fuse_parse_cmdline( &args, &mountpoint, 0 /*multithreaded*/, 
        &foreground );
  if( res == -1 ) {
    fprintf(stderr, "fuse_parse_cmdline failed\n");
    return 0;
  }
  
  args.allocated = 0;

  fd = fuse_mount(mountpoint, &args);
  if(fd == -1){
    fprintf(stderr, "fuse_mount failed\n");
    exit(1);
  }

  struct fuse_session *se;

  se = fuse_lowlevel_new(&args, &fuseserver_oper, sizeof(fuseserver_oper),
       NULL);
  if(se == 0){
    fprintf(stderr, "fuse_lowlevel_new failed\n");
    exit(1);
  }

  struct fuse_chan *ch = fuse_kern_chan_new(fd);
  if (ch == NULL) {
    fprintf(stderr, "fuse_kern_chan_new failed\n");
    exit(1);
  }

  fuse_session_add_chan(se, ch);
  // err = fuse_session_loop_mt(se);   // FK: wheelfs does this; why?
  err = fuse_session_loop(se);
    
  fuse_session_destroy(se);
  close(fd);
  fuse_unmount(mountpoint);

  return err ? 1 : 0;
}
