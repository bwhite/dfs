#define FUSE_USE_VERSION  26
   
#include "utils.h"
#include "dfs.h"
#include "comm.h"
#include "log.h"
#include <unistd.h>

Log				opLog;

static pthread_mutex_t		serverMut = PTHREAD_MUTEX_INITIALIZER;

//=============================================================================


// re-alloc log, if necessary, to ensure room
void *checkLogSpace(int newbytes)
{
    int new_size = opLog.used + newbytes;
    opLog.data = realloc(opLog.data, new_size);
    opLog.alloced = new_size;
}

// flush logs received from clients
static void serverFlush(int force)
{
    // flush to disk

    // flush to other clients
}


static void exit_proc() {
    dfs_out("EXIT_PROC!!!\n");
    serverFlush(1);
}


// automatically rotate through LOG_SERVER names
void logNames(char *base, char **iname, char **oname)
{
    struct stat		dummy;
    char		s[MAX_PATH];

    if (!*iname) {
	int		i=0;

	do {
	    sprintf(s, "%s%03d", base, i++);
	} while (!lstat(s, &dummy));
	if (i > 1) {
	    sprintf(s, "%s%03d", base, i-2);
	    *iname = strdup(s);
	}
    }
    if (*iname && (**iname == '-'))
	*iname = NULL;
    
    if (!*oname) {
	int		i=-1;

	do {
	    sprintf(s, "%s%03d", base, ++i);
	} while (!lstat(s, &dummy));
	*oname = strdup(s);
    }
    if (*iname && !strcmp(*iname, *oname))
	dfs_die("input and output log names must differ\n");

    dfs_out("LOG_NAMES: input '%s', output '%s'\n", *iname ? *iname : "", *oname ? *oname : "");
}


static void logInit(char *iname, char *oname, int sport)
{
    logNames("LOG_SERVER", &iname, &oname);

    if ((opLog.file_fd = open(oname, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
	dfs_die("No open log '%s' for writing\n", oname);

    if (iname) {

	int		fd;

	if ((fd = open(iname, O_RDONLY)) < 0) {
	    fprintf(stderr, "Can't open '%s'\n", iname);
	    exit(1);
	}
	
	struct stat		stat;
	fstat(fd, &stat);
    
	assert(!opLog.data);

	if (stat.st_size) {
	    checkLogSpace(stat.st_size + BLOCK_SIZE /* make sure to alloc even if zero len log */);
	    read(fd, opLog.data, stat.st_size);
	    close(fd);

	    opLog.used = stat.st_size;

	    // rip through log to find last id used :-(
	    long	back = ((long *)(opLog.data + opLog.used))[-1];
	    opLog.id = ((LogHdr *)(opLog.data + opLog.used - back))->id;
	
	    dfs_out("Read %d bytes from '%s', last ID %ld\n", stat.st_size, iname, opLog.id);
	}
    }

    atexit(exit_proc);
}
    

/*
static void receiveLog(Client *c, Msg *m)
{
}
*/

static void *listen_proc(void *arg) 
{
    Client	*c = arg;

    dfs_out("\n\tLISTEN PROC IN, sock %d! (id %d, tid %d)\n\n", c->fd, c->id, c->tid);

    pthread_detach(pthread_self());

    Msg *m;
    while (m = comm_read(c->fd)) {
	Extent		*ex;
	char		*sig, *data;

	switch (m->type) {
	case DFS_MSG_GET_LOG:
	    pthread_mutex_lock(&serverMut);
	    comm_reply(c->fd, m, REPLY_OK, opLog.data, opLog.used, NULL);
	    pthread_mutex_unlock(&serverMut);
	    break;

	case DFS_MSG_PUSH_LOG:
	    pthread_mutex_lock(&serverMut);
	    // TODO Check to see if this log entry is a collision
	    comm_reply(c->fd, m, REPLY_OK, NULL);
	    // Append to log
	    checkLogSpace(m->len);
	    memcpy(opLog.data + opLog.used, m->data, m->len);
	    opLog.used = opLog.used + m->len;
	    pthread_mutex_unlock(&serverMut);
	    break;
	default:
	    dfs_die("BAD MSG TYPE %d\n", m->type);
	}

	free(m);
    }
    free(c);
    return NULL;
}


int main(int argc, char *argv[])
{
    int			i, c;
    char		*iname = NULL;
    char		*oname = NULL;
    int			port = LOG_PORT;

    while ((c = getopt(argc, argv, "i:o:p:")) != -1) {
	switch (c) {
	case 'i':
	    iname = optarg;
	    break;
	case 'o':
	    oname = optarg;
	    break;
	case 'p':
	    port = atoi(optarg);
	    break;
	default:
	    fprintf(stderr, "USAGE: %s [-i <input log name>]\n"
		    "\t[-o <output log name>]\n",
		    argv[0]);
	    exit(1);
	}
    }

    logInit(iname, oname, port);

    comm_register_msgtypes(sizeof(messages) / sizeof(messages[0]), messages);

    comm_server_socket_mt(port, listen_proc);
}
