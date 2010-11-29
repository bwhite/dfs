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
#include	"tuple.h"
#include "cry.h"

void 		ex_put_extent(char *buf, long sz);
int 		ex_poll_extent(char *sig);
Extent		*ex_get_extent(char *sig);


static char 	*xstorage = ".extents";
static void 	*extentRoot = NULL;
static char	*saveDir = NULL;
int		serialize = 1;

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


void *listener(void *arg) 
{
    Client	*c = arg;

    dfs_out("\n\tLISTEN PROC IN, sock %d! (id %d, tid %d)\n\n", c->fd, c->id, c->tid);

    pthread_detach(pthread_self());

    Msg *m;
    while (m = comm_read(0, c->fd)) {
	Extent		*ex;
	char		*sig, *data;

	switch (m->type) {
	case DFS_MSG_GET_EXTENT:
	    if (tuple_unserialize_sig(&sig, m->data, m->len))
		comm_reply(0, c->fd, m, REPLY_ERR, NULL);
	    else if (ex = ex_get_extent(sig)) {
		char	*buf;
		size_t	sz;
		tuple_serialize_extent(&buf, &sz, ex->data, ex->sz);
		comm_reply(0, c->fd, m, REPLY_OK, buf, sz, NULL);
		free(buf);
	    } else
		comm_reply(0, c->fd, m, REPLY_ERR, NULL);
	    break;

	case DFS_MSG_POLL_EXTENT:
	    if (tuple_unserialize_sig(&sig, m->data, m->len))
		comm_reply(0, c->fd, m, REPLY_ERR, NULL);
	    else if (ex_poll_extent(sig))
		comm_reply(0, c->fd, m, REPLY_OK, NULL);
	    else
		comm_reply(0, c->fd, m, REPLY_ERR, NULL);
	    break;

	case DFS_MSG_PUT_EXTENT:
	    {
		size_t		sz;

		if (tuple_unserialize_sig_extent(&sig, &data, &sz, m->data, m->len)) {
		    comm_reply(0, c->fd, m, REPLY_ERR, NULL);
		} else {
		    comm_reply(0, c->fd, m, REPLY_OK, NULL);
		    ex_put_extent(data, sz);
		    free(sig);
		    free(data);
		}
	    }
	    break;
	default:
	    dfs_die("BAD MSG TYPE %d\n", m->type);
	}

	free(m);
    }

    dfs_out("\n\tLISTEN PROC EXIT, sock %d! (id %d, tid %d)\n\n", c->fd, c->id, c->tid);

    //    twalk(extentRoot, extent_print);

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

    sig = cry_hash_bytes(buf, sz);

    if (ex_get_extent(sig))
	return;

    ex = malloc(sizeof(Extent) + sz);
    assert(ex);

    memcpy(ex->data, buf, sz);
    strcpy(ex->sig, sig);
    ex->sz = sz;

    wrapped_tsearch(ex, &extentRoot, extent_compare);

    dfs_out("extent '%s' CREATED\n", sig);
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
		    struct stat		stat;

		    fstat(fd, &stat);

		    Extent	*ex = malloc(sizeof(Extent) + stat.st_size);
		    int		len = read(fd, ex->data, stat.st_size);
		    ex->sz = len;

		    assert(len);
		    close(fd);

		    if (serialize) {
			char		*buf;
			size_t		sz;

			tuple_unserialize_extent(&buf, &sz, ex->data, len);
			free(ex);
			ex = malloc(sizeof(Extent) + sz);
			ex->sz = sz;
			memcpy(ex->data, buf, sz);
			free(buf);
		    }

		    strcpy(ex->sig, ent->d_name);  
		    
		    char *s = cry_hash_bytes(ex->data, ex->sz);
		    //assert(!strcmp(s, ex->sig));

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
		char		*buf;
		size_t		sz;

		tuple_serialize_extent(&buf, &sz, ex->data, ex->sz);
		write(fd, buf, sz);
		close(fd);
		free(buf);
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

    while ((c = getopt(argc, argv, "dp:x:")) != -1) {
	switch (c) {
	case 'd':
	    dfsbug = 1 - dfsbug;
	    break;
	case 'p':
	    port = atoi(optarg);
	    break;
	case 'x':
	    xstorage = strdup(optarg);
	    break;
	default:
	    fprintf(stderr, "USAGE: %s [-d] [-p <#>] [-x <both dirs>]\n", argv[0]);
	    exit(1);
	}
    }

    read_extents();
	
    comm_register_msgtypes(sizeof(messages) / sizeof(messages[0]), messages);

    printf("Extent store in '%s', port %d\n", xstorage, port);

    comm_server_socket_mt(EXTENT_PORT, listener);
}
  
