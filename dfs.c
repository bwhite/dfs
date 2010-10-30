
//
//   Pete's RAM filesystem. No persistance, no links, slow linear searches.
//   Basically, this was an afternoon's hack, so be nice.
//

// Blech. Wrote this first on a mac, easy. Getting to compile on linux much
// harder. Right now, mac is the #else of the linux ifdefs. To get to compile
// on linux, download all fuse source, cp hellofs.c into the examples subdir,
// modify the makefile to do everything for hellofs that it does for hello,
// for example, and then it works. Might also want to remove  -Wmissing-declarations.
// 
// In both cases, run by "./dfs /tmp/hello" (after you create the dir),
// kill by 'killall dfs'.
//
#define FUSE_USE_VERSION  26
   
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include <search.h>

#include <fuse.h>

#include "utils.h"
#include "dfs.h"
#include "comm.h"
#include "log.h"


//=============================================================================
// On Mac:
//            (-lfuse_ino64 needed on snow leopard)
//      gcc -O0 -std=c99 -o dfs dfs.c -lfuse_ino64
//	mkdir /tmp/dfs
//	./dfs /tmp/dfs -oping_diskarb,volname=Dfs
//	./dfs /tmp/dfs -odebug
//
// On Linux:
//	mkdir /tmp/dfs
//	./dfs /tmp/dfs -odebug
// gcc -O0 -std=c99 -D_FILE_OFFSET_BITS=64 -o dfs dfs.c -lfuse
//=============================================================================
 
static int			extentSock;
Log				opLog;
static void			*pathRoot;

DfsFile 			*root;
static int			debug = 1;
extern pthread_mutex_t replyLogserverMut;
extern pthread_cond_t replyLogserverCond;
pthread_mutex_t treeMut;
extern Msg			*replyQueue;
int pending_change_id = -1;

// Needed by init
char *sname = "localhost";
char *xname = "localhost";
int sport = LOG_PORT;
int xport = EXTENT_PORT;

//=============================================================================
    
static void *listener(void *arg) 
{
    int		fd = *(int *)arg;

    dfs_out("\n\tLISTEN PROC IN, sock %d!\n\n", fd);
    
    Msg *m;
    while (m = comm_read(fd)) {

	switch (m->type) {
	case DFS_MSG_PUSH_LOG:
	    /* Lock tree and replay log.  Assumes we get the whole log.  */
	    pthread_mutex_lock(&treeMut);
	    pthread_mutex_lock(&replyLogserverMut);
	    dfs_out("Push calling play log\n");
	    char *log;
	    size_t log_sz;
	    assert(tuple_unserialize_log(&log, &log_sz, m->data, m->len) == 0);
	    playLog(log, log_sz);
	    free(log);
	    pthread_mutex_unlock(&replyLogserverMut);
	    pthread_mutex_unlock(&treeMut);
	    break;

	case MSG_REPLY:
	    /* Put on reply queue.  Waits until main thread gets the item
	     and sets the replyQueue to NULL to prevent losing messages */
	    pthread_mutex_lock(&replyLogserverMut);
	    assert(replyQueue == NULL);
	    replyQueue = m;
	    pthread_cond_signal(&replyLogserverCond);
	    pthread_cond_wait(&replyLogserverCond, &replyLogserverMut);
	    pthread_mutex_unlock(&replyLogserverMut);
	    break;
	default:
	    dfs_die("BAD MSG TYPE %d\n", m->type);
	}
    }
    
    dfs_out("\n\tLISTEN PROC EXIT, sock %d!\n\n", fd);
    return NULL;
}


//=============================================================================
 

static int file_compare(const void *node1, const void *node2) {
    return strcmp(((const DfsFile *) node1)->path,
		  ((const DfsFile *) node2)->path);
}

//=============================================================================



DfsFile *mkNode(const char *path, const char *name, DfsFile *parent, mode_t mode) {
    DfsFile *f = (DfsFile *)calloc(1, sizeof(DfsFile));

    dfs_out("mknode '%s'\n", path);
    
    f->stat.st_mtime = f->stat.st_atime = f->stat.st_ctime = time(NULL);
    f->stat.st_mode = mode;
    f->stat.st_nlink = 1;
    f->stat.st_uid = getuid();
    f->stat.st_gid = getgid();
    f->name = strdup(name);
    strncpy(f->path, path, sizeof(f->path)-1);
    f->parent = parent;

    if (parent) {
	parent->child = (DfsFile **)realloc(parent->child, (parent->num_children + 1) * sizeof(DfsFile *));
	parent->child[parent->num_children++] = f;
    }

    tsearch(f, &pathRoot, file_compare);
    return f;
}

static void freeNode(const char *path, DfsFile *f) {
    DfsFile 	*p = f->parent;

    dfs_out("mknode '%s'\n", path);
    tdelete(path, &pathRoot, file_compare);

    assert(!f->num_children);
    free(f->data);
    free(f->name);
    if (p) {
	int	i;

	for (i = 0; i < p->num_children; i++) {
	    if (p->child[i] == f) {
		assert(p->num_children);
		dfs_out("memcpy %x, %x, %d (i %d #%d)\n", (void *)&p->child[i], (void *)&p->child[i+1], 
		   sizeof(DfsFile *) * (p->num_children - (i+1)), i, p->num_children);
		memmove((void *)&p->child[i], (void *)&p->child[i+1], sizeof(DfsFile *) * (p->num_children - (i+1)));
		break;
	    }
	}
	assert(i < p->num_children);
	p->num_children--;
    }
    free(f);
}

void destroy_node(void *node) {
    DfsFile *f = ((DfsFile *) node);
    if (!f)
	return;
    dfs_out("Destroy Data\n");
    free(f->data);
    dfs_out("Destroy Name\n");
    free(f->name);
    dfs_out("Destroy Recipe\n");
    free(f->recipe);
}

void destroy_tree() {
    tdestroy(pathRoot, destroy_node);
    pathRoot = NULL;
}


//=============================================================================

// Return dynamically allocated extent, must be free'd
Extent	*get_extent(char *sig)
{
    Extent 	*ex;

    assert(sig);
    char *serialized;
    size_t serialized_sz;
    assert(tuple_serialize_sig(&serialized, &serialized_sz, sig) == 0);
    Msg	*reply = comm_send_and_reply(extentSock, DFS_MSG_GET_EXTENT, serialized, serialized_sz, NULL);
    free(serialized);
    if (!reply || (reply->res != REPLY_OK)) 
	return NULL;
    assert(reply->len);
    char *unserialized;
    size_t unserialized_sz;
    assert(tuple_unserialize_extent(&unserialized, &unserialized_sz, reply->data, reply->len) == 0);
    ex = malloc(sizeof(Extent) + unserialized_sz);
    assert(ex);

    // verify sig
    char *s = hash_bytes(unserialized, unserialized_sz);
    strcpy(ex->sig, s);
    free(s);

    memcpy(ex->data, unserialized, unserialized_sz);
    ex->sz = unserialized_sz;

    free(reply);
    free(unserialized);

    return ex;
}


// Like 'get_extent', but just return boolean 1 for found, or 0 for not.
int	poll_extent(char *sig)
{
    Extent 	*ex;

    assert(sig);
    char *serialized;
    size_t serialized_sz;
    assert(tuple_serialize_sig(&serialized, &serialized_sz, sig) == 0);
    Msg	*reply = comm_send_and_reply(extentSock, DFS_MSG_POLL_EXTENT, serialized, serialized_sz, NULL);
    int ret =  (reply && (reply->res == REPLY_OK));
    free(reply);
    free(serialized);
    return ret;
}


// Puts sig into sigbuf.   '0' on success.
char *put_extent(char *buf, long sz)
{
    int			i;

    assert(buf && sz && (sz < (1024L * 1024L * 1024L)));

    char *s = hash_bytes(buf, sz);

    if (poll_extent(s)) {
	printf("Server already has EXTENT\n");
	return s;
    }
    char *serialized;
    size_t serialized_sz;
    assert(tuple_serialize_sig_extent(s, &serialized, &serialized_sz, buf, sz) == 0);
    Msg	*reply = comm_send_and_reply(extentSock, DFS_MSG_PUT_EXTENT, serialized, serialized_sz, NULL);
    free(serialized);
    if (!reply) dfs_die("No get reply\n");

    dfs_out("extent '%s' CREATED\n", s);
    free(reply);

    return s;
}

//=============================================================================
    
DfsFile *findFile(char *path)
{
    if (!path[0] || !strcmp(path, "/")) 
	return root;

    DfsFile **fh = tfind(path, &pathRoot, file_compare);
    return fh ? *fh : NULL;
}


static int dfs_getattr(const char *path, struct stat *stbuf)
{
    DfsFile	*f = findFile((char *)path);

    dfs_out("GETATTR: '%s' (%ld)\n", path, f);
    if (!f) return -ENOENT;

    *stbuf = f->stat;
    return 0;
}


static int dfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    pthread_mutex_lock(&treeMut);
    DfsFile	*f = findFile((char *)path);
    int		i;

    dfs_out("READDIR: '%s'\n", path);

    if (!f) {
	pthread_mutex_unlock(&treeMut);
	return -ENOENT;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (i = 0; i < f->num_children; i++) {
	filler(buf, f->child[i]->name, NULL, 0);
    }
    pthread_mutex_unlock(&treeMut);
    return 0;
}

static int _dfs_open(const char *path, struct fuse_file_info *fi)
{
    DfsFile	*f;
    dfs_out("\n\tFUSE OPEN '%s'\n\n", path);

    if (!(f = findFile((char *)path))) {
	return -ENOENT;
    }

    long	flags = fi ? fi->flags : 0;

    dfs_out("\tOPEN : '%s', flags %o, len %d, reclen %d, recipe %x, data %x\n", path, flags, f->len, f->recipelen, f->recipe, f->data);

    if (f->stat.st_mode & S_IFDIR) {
	return -EISDIR;
    }

    if (0 && !(fi->flags & f->stat.st_mode)) {
	dfs_out("OPEN permissions problem: %o, %o\n", fi->flags, f->stat.st_mode);
        return -EACCES;
    }

    // Why? don't remember....
    fi->fh = (uint64_t)f->stat.st_mode;

    if (f->len && !f->data) {
	assert(f->recipe);

	f->data = malloc(f->len);
	assert(f->data);

	char		*data = f->data, *dataEnd = f->data + f->len;
	char		*sig = f->recipe, *sigEnd = f->recipe + f->recipelen;

	while ((data < dataEnd) && (sig < sigEnd)) {
	    Extent	*ex = get_extent(sig);
	    if (!ex) dfs_die("No get signature '%s'\n", sig);
	
	    memcpy(data, ex->data, ex->sz);
	    data += ex->sz;

	    sig += strlen(sig) + 1;
	    free(ex);
	}
	assert((data == dataEnd) && (sig == sigEnd));
    }
    return 0;
}
static int dfs_open(const char *path, struct fuse_file_info *fi) {
    int out;
    pthread_mutex_lock(&treeMut);
    out = _dfs_open(path, fi);
    pthread_mutex_unlock(&treeMut);
    return out;
}
    

static int dfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    size_t len;
    pthread_mutex_lock(&treeMut);
    dfs_out("READ: '%s', sz %d, offset %d\n", path, size, offset);

    DfsFile	*f = findFile((char *)path);

    if (!f) {
	pthread_mutex_unlock(&treeMut);
	return -ENOENT;
    }

    if (size && !f->len)  {
	pthread_mutex_unlock(&treeMut);
	return 0;
    }

    if (f->stat.st_mode & S_IFDIR) {
	pthread_mutex_unlock(&treeMut);
	return -EISDIR;
    }

    len = f->len;
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, f->data + offset, size);
    } else
        size = 0;

    f->stat.st_atime = time(NULL);
    pthread_mutex_unlock(&treeMut);
    return size;
}


static int dfs_write(const char *path, const char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    size_t len;
    pthread_mutex_lock(&treeMut);
    dfs_out("WRITE: '%s', sz %d, offset %d\n", path, size, offset);

    DfsFile	*f = findFile((char *)path);

    if (!f) {
	pthread_mutex_unlock(&treeMut);
	return -ENOENT;
    }

    if (f->stat.st_mode & S_IFDIR) {
	pthread_mutex_unlock(&treeMut);
	return -EISDIR;
    }
    // Handles issue where a collision can cause us to fail this check
    if(!(!f->recipelen || f->data)) {
	struct fuse_file_info fii;
	fii.flags = 0;
	_dfs_open(path, &fii);
    }
    //assert(!f->recipelen || f->data);

    if ((size + offset) > f->len) {
	f->data = (char *)realloc(f->data, size + offset);
	f->stat.st_size = f->len = size + offset;
    }
    memcpy(f->data + offset, buf, size);

    f->dirty = 1;

    f->stat.st_mtime = f->stat.st_atime = time(NULL);
    pthread_mutex_unlock(&treeMut);
    return size;
}


int dfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    pthread_mutex_lock(&treeMut);
    dfs_out("CREATE: '%s'\n", path);

    DfsFile	*f = findFile((char *)path);
    DfsFile	*dir;
    char	*dname, *fname;

    if (f) {
	pthread_mutex_unlock(&treeMut);
	return -EEXIST;
    }

    if (!(fname = strrchr(path, '/'))) {
	pthread_mutex_unlock(&treeMut);
	return -EINVAL;
    }

    dname = strdup(path);
    dname[fname - path] = 0;
    fname++;

    if (!(dir = findFile(dname))) {
	free(dname);
	pthread_mutex_unlock(&treeMut);
	return -EINVAL;
    }

    f = mkNode(path, fname, dir, DEF_FILE_MODE);

    dfs_out("CREATE OUT, now %d children in '%s'\n", dir->num_children, dname);

    free(dname);

    f->version++;

    logFileVersion(f);
    pthread_mutex_unlock(&treeMut);
    return 0;
}


int _dfs_chmod(const char *path, mode_t mode)
{
    /* Assumes treeMut is locked */
    DfsFile	*f;

    dfs_out("CHMOD: '%s' %o\n", path, mode);

    if (!(f = findFile((char *)path)))
	return -ENOENT;
    
    f->stat.st_mode = (f->stat.st_mode &  ~(S_IRWXU | S_IRWXG | S_IRWXO)) | mode;
    dfs_out("\tend mode: %o\n", f->stat.st_mode);

    return 0;
}

int dfs_chmod(const char *path, mode_t mode) {
    int out;
    DfsFile *f;
    pthread_mutex_lock(&treeMut);
    out = _dfs_chmod(path, mode);
    if (out) {
	pthread_mutex_unlock(&treeMut);
	return out;
    }
    f = findFile((char *)path);
    logOther(LOG_CHMOD, path, mode, &f->stat);
    pthread_mutex_unlock(&treeMut);
    return out;
}



int _dfs_mkdir(const char *path, mode_t mode)
{
    /* Assumes treeMut is locked */
    //S_IFDIR
    DfsFile	*dir;
    char	*dname = strdup(path), *fname;

    dfs_out("MKDIR: '%s', %o\n", path, mode);

    if (dname[strlen(dname) - 1] == '/') 
	dname[strlen(dname) - 1] = 0;

    if (!dname[0]) {
	free(dname);
	return -EEXIST;
    }

    if (dir = findFile((char *)dname)) {
	free(dname);
	return -EEXIST;
    }
	
    fname = strrchr(dname, '/');
    dfs_out("STRRCHR '%s', '%s'\n", dname, fname ? fname : "''");
    if (!fname) {
	free(dname);
	return -EINVAL;
    }

    *fname++ = 0;
    dfs_out("dname %x ('%s'), fname %x ('%s')\n", dname, dname, fname, fname);

    if (!(dir = findFile((char *)dname))) {
	free(dname);
	return -EINVAL;
    }
	
    dfs_out("MKDIR2: '%s', '%s'\n", dname, fname);

    mkNode(path, fname, dir, S_IFDIR | mode);

    free(dname);

    return 0;
}

int dfs_mkdir(const char *path, mode_t mode) {
    int out;
    pthread_mutex_lock(&treeMut);
    out = _dfs_mkdir(path, mode);
    if (out) {
	pthread_mutex_unlock(&treeMut);
	return out;
    }
    logOther(LOG_MKDIR, path, mode, NULL);
    pthread_mutex_unlock(&treeMut);
    return out;
}

int _dfs_rmdir(const char *path)
{
    /* Assumes treeMut is locked */
    DfsFile	*f;

    if (!(f = findFile((char *)path)))
	return -ENOENT;

    if (!(f->stat.st_mode & S_IFDIR))
	return -ENOTDIR;

    if (f->num_children) 
	return -ENOTEMPTY;

    freeNode(path, f);

    return 0;
}

int dfs_rmdir(const char *path) {
    int out;
    pthread_mutex_lock(&treeMut);
    out = _dfs_rmdir(path);
    if (out) {
	pthread_mutex_unlock(&treeMut);
	return out;
    }
    logOther(LOG_RMDIR, path, 0, NULL);
    pthread_mutex_unlock(&treeMut);
    return out;
}
     
int _dfs_unlink(const char *path)
{
    /* Assumes treeMut is locked */
    DfsFile	*f;

    dfs_out("Unlink '%s'\n", path);
    if (!(f = findFile((char *)path)))
	return -ENOENT;

    if (f->stat.st_mode & S_IFDIR) 
	return -EISDIR;

    if (--f->stat.st_nlink)
	return 0;

    freeNode(path, f);

    return 0;
}

int dfs_unlink(const char *path) {
    int out;
    pthread_mutex_lock(&treeMut);
    out = _dfs_unlink(path);
    if (out) {
	pthread_mutex_unlock(&treeMut);
	return out;
    }
    logOther(LOG_UNLINK, path, 0, NULL);
    pthread_mutex_unlock(&treeMut);
    return out;
}


// If file dirty, chunkify, sent extents to extentserver, and send
// recipe to recipes server. In all cases, toss local file from hash table.
static int dfs_flush(const char *path, struct fuse_file_info *fi)
{
    DfsFile	*f;
    pthread_mutex_lock(&treeMut);
    dfs_out("DFS_FLUSH '%s\n", path);

    if (!(f = findFile((char *)path))) {
	pthread_mutex_unlock(&treeMut);
	return -EINVAL;
    }

    assert(f);

    dfs_out("\tflush '%s': len %d, dirty %d, data %x\n", f->path, f->len, f->dirty, f->data);

    // If no data, nothing to flush...
    if (!f->data || !f->dirty) {
	free(f->data);
	f->data = NULL;
	pthread_mutex_unlock(&treeMut);
	return 0;
    }

    // Need to create extents. For now, just chop up into 4k blocks.
    char	*buf = f->data, *bufend = buf + f->len;
    char	*sig;
    Extent	*ex, *last;

    int		blocks = f->len / BLOCK_SIZE;
    if (f->len % BLOCK_SIZE) blocks++;

    f->recipelen = blocks * A_HASH_SIZE;
    free(f->recipe);
    f->recipe = malloc(f->recipelen);
    char	*curr = f->recipe;

    while (buf < bufend) {
	int		sz = MIN(bufend - buf, BLOCK_SIZE);
	char		*hash;

	hash = put_extent(buf, sz);
	assert(hash);
	dfs_out("PUT extent size %d, '%s'\n", sz, hash);

	strcpy(curr, hash);
	curr += A_HASH_SIZE;
	free(hash);

	buf += sz;
    }
    assert(curr == (f->recipe + f->recipelen));

    free(f->data);
    f->data = NULL;
    f->version++;
    f->dirty = 0;

    logFileVersion(f);
    pthread_mutex_unlock(&treeMut);
    return 0;
}


static int dfs_truncate(const char *path, off_t sz)
{
    DfsFile	*f;
    pthread_mutex_lock(&treeMut);
    dfs_out("\n\tFUSE TRUNCATE\n\n");

    if (!(f = findFile((char *)path))) {
	pthread_mutex_unlock(&treeMut);
	return -ENOENT;
    }

    dfs_out("TRUNCATE to %d, was %d\n", sz, f->len);

    if (sz < f->len) {
	f->len = f->stat.st_size = sz;
	f->dirty = 1;
    }
    pthread_mutex_unlock(&treeMut);
    return 0;
}

static void * dfs_init(struct fuse_conn_info *conn)
{
    pthread_mutex_lock(&treeMut);
    dfs_out("INIT!\n");
    root = mkNode("", "", NULL, DEF_DIR_MODE);

    comm_register_msgtypes(sizeof(messages) / sizeof(messages[0]), messages);

    if (!(extentSock = comm_client_socket(xname, xport))) 
	dfs_die("NO setup client socket to extent server at %d on '%s'\n", xport, xname);

    if (sname && ((opLog.net_fd = comm_client_socket(sname, sport)) > 0)) {
	// create a new thread reading this socket
	pthread_t		tid;
	pthread_create(&tid, NULL, listener, &opLog.net_fd);

	// grab current FS from server
	Msg *reply = comm_send_and_reply_mutex(&replyLogserverMut, &replyLogserverCond, opLog.net_fd, DFS_MSG_GET_LOG, NULL);
	char *log;
	size_t log_sz;
	assert(tuple_unserialize_log(&log, &log_sz, reply->data, reply->len) == 0);
	if (reply) {
	    if (reply->len) {
		playLog(log, log_sz);
		opLog.served = log_sz;
	    }
	    free(reply);
	}
	free(log);
    } else {
      dfs_die("NO setup client socket to log server at %d on '%s'\n", sport, sname);
    }
    pthread_mutex_unlock(&treeMut);
}


static struct fuse_operations dfs_oper = {
    .getattr   = dfs_getattr,
    .readdir = dfs_readdir,
    .create   = dfs_create,
    .open   = dfs_open,
    .read   = dfs_read,
    .write   = dfs_write,
    .unlink   = dfs_unlink,
    .chmod   = dfs_chmod,
    .mkdir   = dfs_mkdir,
    .rmdir   = dfs_rmdir,
    .flush   = dfs_flush,
    .truncate   = dfs_truncate,
    .init = dfs_init
};


int main(int argc, char *argv[])
{
    int			i, arg = 0, c;
    while ((c = getopt(argc, argv, "i:o:s:S:x:X:")) != -1) {
	switch (c) {
	case 's':
	    sname = optarg;
	    arg += 2;
	    break;
	case 'S':
	    sport = atoi(optarg);
	    arg += 2;
	    break;
	case 'x':
	    xname = optarg;
	    arg += 2;
	    break;
	case 'X':
	    xport = atoi(optarg);
	    arg += 2;
	    break;
	default:
	    fprintf(stderr, "USAGE: \n"
		    "\t[-s <logserver host>]\n"
		    "\t[-S <logserver port>]\n"
		    "\t[-x <extent server host>]\n"
		    "\t[-X <extent server port>]\n", argv[0]);
	    exit(1);
	}
    }
    arg++;
    int		counter = 0;

    for (i = arg+1; i < argc; ++i) {
	argv[++counter] = argv[i];
    }

    return fuse_main(argc - arg, argv, &dfs_oper, NULL);
}

// called to reply records from logs returned from server
void playLog(char *buf, int len)
{
    /* Assumes treeMut and replyLogserverMut are locked */
    /* Find first ID of the new entry, go through whole log
       and truncate if that ID or greater is found, then append
       the new log.  It is assumed that the server has successfully
       managed any conflicts and any of that is taken care of. */
    // If we aren't forcing, check if this entry is in the log
    char *data = opLog.data;
    char *end = opLog.data + opLog.used;
    int first_version = ((LogHdr *)buf)->id;
    /* If we already have this version then quit.
       We only check the first record as if we asked
       for the log we have an empty log.  If we get
       pushed then we recieve one log entry and we just
       need to check if we have already committed it.
     */
    if (pending_change_id == first_version) {
      dfs_out("PlayLog: Server sent us what we are waiting to commit!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      return;
    }
    while (data != NULL && data < end) {
	int version = ((LogHdr *)data)->id;
	if (version == first_version) {
	    dfs_out("PlayLog: Already have element in log\n");
	    return;
	}
	data += ((LogHdr *)data)->len;
    }
    dfs_out("PlayLog: Adding new element\n");
    checkLogSpace(len);
    data = opLog.data;
    dfs_out("PlayLog: Adding new element[%p][%d][%d]\n", opLog.data, opLog.used, opLog.alloced);
    memcpy(data + opLog.used, buf, len);
    opLog.used += len;
    end = opLog.data + opLog.used;
    destroy_tree();
    root = mkNode("", "", NULL, DEF_DIR_MODE);
    int last_id = 1;
    while (data < end) {
	switch ( ((LogHdr *)data)->type ) {
	case LOG_FILE_VERSION:
	    {
		LogFileVersion	*fv = (LogFileVersion *)data;
		char		*recipes = (char *)(fv + 1);
		char		*path = recipes + fv->recipelen;
		DfsFile	*f;
		dfs_out("Push[%s] id[%d]\n", path, fv->hdr.id);
		f = findFile((char *)path);
		if (f == NULL) {
		    DfsFile *dir;
		    char *dname, *fname;
		    fname = strrchr(path, '/');
		    dname = strdup(path);
		    dname[fname - path] = 0;
		    fname++;
		    dir = findFile(dname);
		    f = mkNode(path, fname, dir, DEF_FILE_MODE);
		    free(dname);
		}
		// Update mtime
		f->stat.st_mtime = fv->mtime;
		// Update version
		f->version = fv->hdr.version;
		// Update recipelen
		f->recipelen = fv->recipelen;
		// Update mode
		f->stat.st_mode = fv->flags;
		// Update length
		f->stat.st_size = f->len = fv->flen;
		// Update recipe
		free(f->recipe);
		f->recipe = malloc(fv->recipelen);
		memcpy(f->recipe, recipes, fv->recipelen);
		dfs_out("Push[%s] id[%d] version[%d] rlength[%d] recipe[%s] len[%d] dirty[%d]\n", path, fv->hdr.id, f->version, f->recipelen, recipes, f->len, f->dirty);
	    }
	    break;
	case LOG_UNLINK:
	    {
		char *path = (char *)((LogOther *)data + 1);
		_dfs_unlink(path);
	    }
	    break;
	case LOG_MKDIR:
	    {
		char *path = (char *)((LogOther *)data + 1);
		long mode = ((LogOther *)data)->flags;
		_dfs_mkdir(path, mode);
	    }
	    break;
	case LOG_RMDIR:
	    {
		char *path = (char *)((LogOther *)data + 1);
		_dfs_rmdir(path);
	    }
	    break;
	case LOG_CHMOD:
	    {
		char *path = (char *)((LogOther *)data + 1);
		long mode = ((LogOther *)data)->flags;
		_dfs_chmod(path, mode);
	    }
	    break;
	default:
	    printf("BAD RECORD\n");
	    exit(1);
	}
	last_id = ((LogHdr *)data)->id;
	data += ((LogHdr *)data)->len;
    }
    dfs_out("Last ID[%d]\n", last_id);
    opLog.id = last_id;
}
