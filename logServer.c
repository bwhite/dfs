#define FUSE_USE_VERSION  26
   
#include "utils.h"
#include "dfs.h"
#include "comm.h"
#include "log.h"
#include <unistd.h>

Log				opLog;
static pthread_mutex_t		serverMut = PTHREAD_MUTEX_INITIALIZER;
Client *my_clients;
int push_updates = 1;

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
    /* Assumes mutex is locked */
    // flush to disk
    fwrite(opLog.data, opLog.used, 1, opLog.file_fd);
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
    opLog.id = 0;
    if ((opLog.file_fd = fopen(oname, "w")) < 0)
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

int check_collision(Msg *m, char **start, char **stop) {
    char *data = opLog.data;
    char *end = opLog.data + opLog.used;
    char *path;
    int version;
    if (m->type == LOG_FILE_VERSION) {
	LogFileVersion *fv = (LogFileVersion*)(m->data);
	path = ((char *)(fv + 1)) + fv->recipelen;
	version = fv->hdr.id;
    } else {
	LogOther *fv = (LogOther*)(m->data);
	path = ((char *)(fv + 1));
	version = fv->hdr.id;
    }
    while (data < end) {
	char *cur_path;
	int cur_version;
	switch ( ((LogHdr *)data)->type ) {
	case LOG_FILE_VERSION:
	    {
		LogFileVersion	*fv = (LogFileVersion *)data;
		char		*recipes = (char *)(fv + 1);
		cur_version = fv->hdr.id;
		cur_path = recipes + fv->recipelen;
	    }
	    break;
	case LOG_UNLINK:
	case LOG_CHMOD:
	case LOG_MKDIR:
	    {
		LogOther *fv = (LogOther *)data;
		cur_path = (char *)(fv + 1);
		cur_version = fv->hdr.id;
	    }
	    break;
	case LOG_RMDIR:
	    {
		LogOther *fv = (LogOther *)data;
		cur_path = (char *)(fv + 1);
		cur_version = fv->hdr.id;
		// Second condition: If this removes any subpath
	    }
	    break;
	default:
	    printf("BAD RECORD\n");
	    exit(1);
	}
	//dfs_out("Checking path [%s][%d][%d]\n", path, version, ((LogHdr *)data)->len);
	// First condition: If we are modifying any path along the way
	if (cur_version >= version && !strcmp(path, cur_path)) {
	    *start = data;
	    *stop = end;
	    return 1;
	}
	// Third condition: If the command is a rmdir, check to see if it remove any cur_subpath
	data += ((LogHdr *)data)->len;
    }
    return 0;
}

static void *listen_proc(void *arg) 
{
    Client	*c = arg;
    // Make chain
    pthread_mutex_lock(&serverMut);
    dfs_out("Adding client[%p]\n", my_clients);
    if (my_clients == NULL) {
	my_clients = c;
	dfs_out("Added first client\n");
    } else {
	Client *cur_c = my_clients;
	while (cur_c->next != NULL) {
	    cur_c = cur_c->next;
	}
	cur_c->next = c;
	dfs_out("Added another\n");
    }
    c->next = NULL;
    pthread_mutex_unlock(&serverMut);

    dfs_out("\n\tLISTEN PROC IN, sock %d! (id %d, tid %d)\n\n", c->fd, c->id, c->tid);

    pthread_detach(pthread_self());

    Msg *m;
    while (m = comm_read(c->fd)) {
	Extent		*ex;
	char		*sig, *data;
	dfs_out("Pushed Length[%d]\n", ((LogHdr*)m->data)->len);
	switch (m->type) {
	case DFS_MSG_GET_LOG:
	    {
		pthread_mutex_lock(&serverMut);
		comm_reply(c->fd, m, REPLY_OK, opLog.data, opLog.used, NULL);
		pthread_mutex_unlock(&serverMut);
		break;
	    }
	case DFS_MSG_PUSH_LOG:
	    {
		dfs_out("Trying to get lock\n");
		pthread_mutex_lock(&serverMut);
		char *start, *stop;

		dfs_out("Col Check\n");
		if (check_collision(m, &start, &stop)) {
		    dfs_out("***Collision***\n");
		    comm_reply(c->fd, m, REPLY_ERR, start, stop - start, NULL);
		} else {
		    comm_reply(c->fd, m, REPLY_OK, NULL);
		    // Append to log
		    ((LogHdr *)m->data)->id = ++opLog.id; // Forces all commits to be sequential
		    dfs_out("Record gets id[%d]\n", opLog.id);
		    checkLogSpace(m->len);
		    memcpy(opLog.data + opLog.used, m->data, m->len);
		    opLog.used = opLog.used + m->len;
		    if (push_updates){
			// flush to other clients
			dfs_out("Outside while\n");
			Client *cur_c = my_clients;
			while(cur_c != NULL) {
			    dfs_out("Sending again[%p]...\n", cur_c);
			    comm_send(cur_c->fd, DFS_MSG_PUSH_LOG, m->data, m->len, NULL);
			    cur_c = cur_c->next;
			}
		    }
		    serverFlush(1);
		}
		dfs_out("Unlocking\n");
		pthread_mutex_unlock(&serverMut);
	    }
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

    while ((c = getopt(argc, argv, "i:o:p:c")) != -1) {
	switch (c) {
	case 'c':
	    push_updates = 0;
	    break;
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
    my_clients = NULL;
    logInit(iname, oname, port);

    comm_register_msgtypes(sizeof(messages) / sizeof(messages[0]), messages);

    comm_server_socket_mt(port, listen_proc);
}
