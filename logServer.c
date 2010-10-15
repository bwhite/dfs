#define FUSE_USE_VERSION  26
   
#include "utils.h"
#include "dfs.h"
#include "comm.h"
#include "log.h"
#include <unistd.h>

Log				opLog;

static pthread_mutex_t		serverMut = PTHREAD_MUTEX_INITIALIZER;

//=============================================================================


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
	    checkSpace(stat.st_size + BLOCK_SIZE /* make sure to alloc even if zero len log */);
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
    

static void receiveLog(Client *c, Msg *m)
{
}


static void *listen_proc(void *arg) 
{
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
