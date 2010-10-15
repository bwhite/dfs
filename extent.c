#define FUSE_USE_VERSION  26
   
#ifdef	LINUX
#define	 __need_timespec 1
#endif


#include	 <stdlib.h>
#include	 <unistd.h>
#include	"utils.h"
#include	"dfs.h"
#include	"comm.h"
#include	<search.h>
#include	<dirent.h>

void 		ex_put_extent(char *buf, long sz);
int 		ex_poll_extent(char *sig);
Extent		*ex_get_extent(char *sig);


static char 	*xstorage = ".extents";
static void 	*extentRoot = NULL;
static char	*saveDir = NULL;

void 	read_extents();
void 	flush_extents();
int 	poll_extent(char *sig);

static int extent_compare(const void *node1, const void *node2) {
    return strcmp(((const Extent *) node1)->sig,
		  ((const Extent *) node2)->sig);
}

static void extent_print(const void *node, VISIT order, int level) {
    if (order == preorder || order == leaf) {
	printf("sig '%s', sz %d\n", (*(Extent **)node)->sig, (*(Extent **)node)->sz);
    }
}


void *listen_proc(void *arg) 
{
    Client	*c = arg;

    dfs_out("\n\tLISTEN PROC IN, sock %d! (id %d, tid %d)\n\n", c->fd, c->id, c->tid);

    pthread_detach(pthread_self());

    Msg *m;
    while (m = comm_read(c->fd)) {
	Extent		*ex;
	char		*sig, *data;

	switch (m->type) {
	case DFS_MSG_GET_EXTENT:
	    if (ex = ex_get_extent(m->data))
		comm_reply(c->fd, m, REPLY_OK, ex->data, ex->sz, NULL);
	    else
		comm_reply(c->fd, m, REPLY_ERR, NULL);
	    break;

	case DFS_MSG_POLL_EXTENT:
	    if (ex_poll_extent(m->data))
		comm_reply(c->fd, m, REPLY_OK, NULL);
	    else
		comm_reply(c->fd, m, REPLY_ERR, NULL);
	    break;

	case DFS_MSG_PUT_EXTENT:
	    if (m->len <= A_HASH_SIZE) {
		comm_reply(c->fd, m, REPLY_ERR, NULL);
	    } else {
		sig = m->data;
		data = m->data + A_HASH_SIZE;

		comm_reply(c->fd, m, REPLY_OK, NULL);
		ex_put_extent(data, m->len - A_HASH_SIZE);
	    }
	    break;
	default:
	    dfs_die("BAD MSG TYPE %d\n", m->type);
	}

	free(m);
    }

    dfs_out("\n\tLISTEN PROC EXIT, sock %d! (id %d, tid %d)\n\n", c->fd, c->id, c->tid);

    twalk(extentRoot, extent_print);

    flush_extents();

    free(c);

    return NULL;
}


//=============================================================================

    
int ex_poll_extent(char *sig)
{
    Extent	**xh;

    printf("GET sig '%s'", sig);

    if (xh = wrapped_tfind((void *)sig, &extentRoot, extent_compare)) {
	printf("YES, sz %d\n", (*(Extent **)xh)->sz);
	return 1;
    } else  {
	printf("NO\n");
	return 0;
    }
}


Extent	*ex_get_extent(char *sig)
{
    Extent	**xh;

    printf("GET sig '%s'", sig);

    if (xh = wrapped_tfind(sig, &extentRoot, extent_compare)) {
	printf("YES, sz %d\n", (*(Extent **)xh)->sz);
	return *xh;
    } else  {
	printf("NO\n");
	return NULL;
    }
}


// Puts sig into sigbuf, allocs space.   '0' on success.
void ex_put_extent(char *buf, long sz)
{
    int			i;
    Extent		**xh;
    Extent		*ex;
    char		*sig;

    assert(buf && sz && ((long)sz < (1024L * 1024L * 1024L)));

    sig = hash_bytes(buf, sz);

    if (ex_get_extent(sig))
	return;

    ex = malloc(sizeof(Extent) + sz);
    assert(ex);

    memcpy(ex->data, buf, sz);
    strcpy(ex->sig, sig);
    ex->sz = sz;

    wrapped_tsearch(ex, &extentRoot, extent_compare);

    dfs_out("extent '%s' CREATED\n", sig);
    flush_extents();
    dfs_out("flushing");
    free(sig);
}

//=============================================================================

void read_extents()
{
    DIR		*dir;

    if (!(dir = opendir(xstorage))) {
	if (mkdir(xstorage, 0755))
	    dfs_die("Not able to make storage directory '%s'\n", xstorage);
	dir = opendir(xstorage);
    }
    if (dir) {
	struct dirent	*ent;

	printf("Reading");
	while (ent = readdir(dir)) {
	    if (strlen(ent->d_name) == (2 * HASH_SIZE)) {
		int		fd;
		char		fname[255];

		strcpy(fname, xstorage);
		strcat(fname, "/");
		strcat(fname, ent->d_name);

		printf(".");
		if ((fd = open(fname, O_RDONLY)) > 0) {
		    Extent	*ex = malloc(sizeof(Extent) + 1 + BLOCK_SIZE);
		    int		len = read(fd, ex->data, BLOCK_SIZE + 1);

		    assert(len && (len <= BLOCK_SIZE));
		    close(fd);

		    strcpy(ex->sig, ent->d_name);  
		    ex->sz = len;
		    
		    // insert that puppy. don't worry about dups because this is initialization
		    wrapped_tsearch((void *)ex, &extentRoot, extent_compare);
		}
	    }
	}
	printf("\n");
	closedir(dir);
    }
}



static void extent_save(const void *node, VISIT order, int level) {
    if (order == preorder || order == leaf) {
	char		fname[255];
	int		fd;
	Extent		*ex = *((Extent **)node);
	struct stat	dummy;

	strcpy(fname, xstorage);
	strcat(fname, "/");
	strcat(fname, ex->sig);

	// if not there yet
	if (stat(fname, &dummy)) {
	    if ((fd = open(fname, O_WRONLY | O_TRUNC | O_CREAT, 0644)) > 0) {
		write(fd, ex->data, ex->sz);
		close(fd);
	    } else {
		printf("No write '%s'\n", ex->sig);
	    }
	}
    }
}



void flush_extents()
{
    twalk(extentRoot, extent_save);
}


//=============================================================================
  
// Multi-threaded
int main(int argc, char *argv[])
{
    int		c, port = EXTENT_PORT;

    while ((c = getopt(argc, argv, "p:x:")) != -1) {
	switch (c) {
	case 'p':
	    port = atoi(optarg);
	    break;
	case 'x':
	    xstorage = strdup(optarg);
	    break;
	default:
	    fprintf(stderr, "USAGE: %s [-p <#>] [-x <extent dir>]\n", argv[0]);
	    exit(1);
	}
    }

    read_extents();
	
    comm_register_msgtypes(sizeof(messages) / sizeof(messages[0]), messages);

    printf("Extent store in '%s', port %d\n", xstorage, port);

    comm_server_socket_mt(EXTENT_PORT, listen_proc);
}
  
