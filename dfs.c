
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

pthread_mutex_t			replyLogserverMut = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t			replyLogserverCond = PTHREAD_COND_INITIALIZER;
extern Msg			*replyQueue;


//=============================================================================
    
static void *listener(void *arg) 
{
    int		fd = *(int *)arg;

    dfs_out("\n\tLISTEN PROC IN, sock %d!\n\n", fd);
    
    Msg *m;
    while (m = comm_read(fd)) {

	switch (m->type) {
	case DFS_MSG_PUSH_LOG:

	    // write me

	    break;
	case MSG_REPLY:

	    // write me

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
		memcpy((void *)&p->child[i], (void *)&p->child[i+1], sizeof(DfsFile *) * (p->num_children - (i+1)));
		break;
	    }
	}
	assert(i < p->num_children);
	p->num_children--;
    }
    free(f);
}


//=============================================================================

// Return dynamically allocated extent, must be free'd
Extent	*get_extent(char *sig)
{
    Extent 	*ex;

    assert(sig);

    Msg	*reply = comm_send_and_reply(extentSock, DFS_MSG_GET_EXTENT, sig, A_HASH_SIZE, NULL);

    if (!reply || (reply->res != REPLY_OK)) 
	return NULL;
    assert(reply->len);

    ex = malloc(sizeof(Extent) + reply->len);
    assert(ex);

    // verify sig
    char *s = hash_bytes(reply->data, reply->len);
    strcpy(ex->sig, s);
    free(s);

    memcpy(ex->data, reply->data, reply->len);
    ex->sz = reply->len;

    free(reply);

    return ex;
}


// Like 'get_extent', but just return boolean 1 for found, or 0 for not.
int	poll_extent(char *sig)
{
    Extent 	*ex;

    assert(sig);

    Msg	*reply = comm_send_and_reply(extentSock, DFS_MSG_POLL_EXTENT, sig, A_HASH_SIZE, NULL);

    int ret =  (reply && (reply->res == REPLY_OK));
    free(reply);
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

    Msg	*reply = comm_send_and_reply(extentSock, DFS_MSG_PUT_EXTENT, s, A_HASH_SIZE, buf, sz, NULL);
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
    DfsFile	*f = findFile((char *)path);
    int		i;

    dfs_out("READDIR: '%s'\n", path);

    if (!f) return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (i = 0; i < f->num_children; i++) {
	filler(buf, f->child[i]->name, NULL, 0);
    }

    return 0;
}

static int dfs_open(const char *path, struct fuse_file_info *fi)
{
    DfsFile	*f;

    dfs_out("\n\tFUSE OPEN '%s'\n\n", path);

    if (!(f = findFile((char *)path))) 
	return -ENOENT;

    long	flags = fi ? fi->flags : 0;

    dfs_out("\tOPEN : '%s', flags %o, len %d, reclen %d, recipe %x, data %x\n", path, flags, f->len, f->recipelen, f->recipe, f->data);

    if (f->stat.st_mode & S_IFDIR) return -EISDIR;

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


static int dfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    size_t len;

    dfs_out("READ: '%s', sz %d, offset %d\n", path, size, offset);

    DfsFile	*f = findFile((char *)path);

    if (!f) return -ENOENT;

    if (size && !f->len) 
	return 0;

    if (f->stat.st_mode & S_IFDIR) return -EISDIR;

    len = f->len;
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, f->data + offset, size);
    } else
        size = 0;

    f->stat.st_atime = time(NULL);
    return size;
}


static int dfs_write(const char *path, const char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    size_t len;

    dfs_out("WRITE: '%s', sz %d, offset %d\n", path, size, offset);

    DfsFile	*f = findFile((char *)path);

    if (!f) return -ENOENT;

    if (f->stat.st_mode & S_IFDIR) return -EISDIR;

    assert(!f->recipelen || f->data);

    if ((size + offset) > f->len) {
	f->data = (char *)realloc(f->data, size + offset);
	f->stat.st_size = f->len = size + offset;
    }
    memcpy(f->data + offset, buf, size);

    f->dirty = 1;

    f->stat.st_mtime = f->stat.st_atime = time(NULL);
    return size;
}


int dfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    dfs_out("CREATE: '%s'\n", path);

    DfsFile	*f = findFile((char *)path);
    DfsFile	*dir;
    char	*dname, *fname;

    if (f) return -EEXIST;

    if (!(fname = strrchr(path, '/'))) 
	return -EINVAL;

    dname = strdup(path);
    dname[fname - path] = 0;
    fname++;

    if (!(dir = findFile(dname))) {
	free(dname);
	return -EINVAL;
    }

    f = mkNode(path, fname, dir, DEF_FILE_MODE);

    dfs_out("CREATE OUT, now %d children in '%s'\n", dir->num_children, dname);

    free(dname);

    f->version++;

    logFileVersion(f);

    return 0;
}


int dfs_chmod(const char *path, mode_t mode)
{
    DfsFile	*f;

    dfs_out("CHMOD: '%s' %o\n", path, mode);

    if (!(f = findFile((char *)path)))
	return -ENOENT;
    
    f->stat.st_mode = (f->stat.st_mode &  ~(S_IRWXU | S_IRWXG | S_IRWXO)) | mode;
    dfs_out("\tend mode: %o\n", f->stat.st_mode);

    logOther(LOG_CHMOD, path, mode, &f->stat);

    return 0;
}


int dfs_mkdir(const char *path, mode_t mode)
{
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

    logOther(LOG_MKDIR, path, mode, NULL);

    return 0;
}


int dfs_rmdir(const char *path)
{
    DfsFile	*f;

    if (!(f = findFile((char *)path)))
	return -ENOENT;

    if (!(f->stat.st_mode & S_IFDIR))
	return -ENOTDIR;

    if (f->num_children) 
	return -ENOTEMPTY;

    freeNode(path, f);

    logOther(LOG_RMDIR, path, 0, NULL);

    return 0;
}

	
int dfs_unlink(const char *path)
{
    DfsFile	*f;

    dfs_out("Unlink '%s'\n", path);
    if (!(f = findFile((char *)path)))
	return -ENOENT;

    if (f->stat.st_mode & S_IFDIR) 
	return -EISDIR;

    if (--f->stat.st_nlink)
	return 0;

    freeNode(path, f);

    logOther(LOG_UNLINK, path, 0, NULL);

    return 0;
}


// If file dirty, chunkify, sent extents to extentserver, and send
// recipe to recipes server. In all cases, toss local file from hash table.
static int dfs_flush(const char *path, struct fuse_file_info *fi)
{
    DfsFile	*f;

    dfs_out("DFS_FLUSH '%s\n", path);

    if (!(f = findFile((char *)path)))
	return -EINVAL;

    assert(f);

    dfs_out("\tflush '%s': len %d, dirty %d, data %x\n", f->path, f->len, f->dirty, f->data);

    // If no data, nothing to flush...
    if (!f->data || !f->dirty) {
	free(f->data);
	f->data = NULL;
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
    
    return 0;
}


static int dfs_truncate(const char *path, off_t sz)
{
    DfsFile	*f;

    dfs_out("\n\tFUSE TRUNCATE\n\n");

    if (!(f = findFile((char *)path)))
	return -ENOENT;

    dfs_out("TRUNCATE to %d, was %d\n", sz, f->len);

    if (sz < f->len) {
	f->len = f->stat.st_size = sz;
	f->dirty = 1;
    }
    return 0;
}


static void init(char *sname, int sport, char *xname, int xport)
{
    dfs_out("INIT!\n");
    root = mkNode("", "", NULL, DEF_DIR_MODE);

    comm_register_msgtypes(sizeof(messages) / sizeof(messages[0]), messages);

    if (!(extentSock = comm_client_socket(xname, xport))) 
	dfs_die("NO setup client socket to extent server at %d on '%s'\n", xport, xname);

    /*
    if (sname && ((opLog.net_fd = comm_client_socket(sname, sport)) > 0)) {
	// create a new thread reading this socket
	pthread_t		tid;
	pthread_create(&tid, NULL, listener, &opLog.net_fd);

	// grab current FS from server
	Msg *reply = comm_send_and_reply_mutex(&replyLogserverMut, &replyLogserverCond, opLog.net_fd, DFS_MSG_GET_LOG, NULL);
	if (reply) {
	    if (reply->len) {
		long	lastID = ((long *)(reply->data + reply->len))[-1];

		dfs_out("received %d bytes (%ld records) from GET_LOG request\n", reply->len, lastID);

		playLog(reply->data, reply->len);
		opLog.served = opLog.used;
	    }
	    free(reply);
	}
    }
    */
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
};


int main(int argc, char *argv[])
{
    int			i, arg = 0, c;
    char		*sname = "localhost";
    char		*xname = "localhost";
    int			sport = LOG_PORT;
    int			xport = EXTENT_PORT;

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

    init(sname, sport, xname, xport);

    return fuse_main(argc - arg, argv, &dfs_oper, NULL);
}