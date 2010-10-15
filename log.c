#include	<stdio.h>

#define FUSE_USE_VERSION  26
   
#ifdef	LINUX
#define	 __need_timespec 1
#endif

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
#include <arpa/inet.h>

#include <fuse.h>

#include "utils.h"
#include "dfs.h"
#include "comm.h"
#include "log.h"


static in_addr_t		logCreator;
static char	 		*logName = "LOG_OPS";
static int			replaying;
static int			noLog;

extern pthread_mutex_t		replyLogserverMut;
extern pthread_cond_t		replyLogserverCond;
extern void playLog(char *buf, int len);

//=============================================================================

// re-alloc log, if necessary, to ensure room
void *checkLogSpace(int newbytes)
{
    int new_size = opLog.used + newbytes;
    opLog.data = realloc(opLog.data, new_size);
    opLog.alloced = new_size;
}



// called by next two procs to add a record to the log, and push to server
void pushLog(char *from, long len)
{
    /* Assumes treeMut is locked */
    // need to pad record to make room for long 
    len += sizeof(long);
    if (len % sizeof(double))
	len += sizeof(double) - (len % sizeof(double));
    ((LogHdr*)from)->len = len;
    *((long*)(from + len - sizeof(long))) = len;
    checkLogSpace(len);

    // copy the record into the log, update opLog.used, served
    memcpy(opLog.data + opLog.used, from, len);
    opLog.used = opLog.used + len;
    // flush to server. If server responds, replay them locally.
    // IMPORTANT: Ensure that the replaying doesn't cause PUSH_LOG's to the server.
    // Save the whole log for debugging purposes
    {
	dfs_out("Saving log to pushLog.log\n");
	FILE* o = fopen("pushLog.log", "w");
	fwrite(opLog.data, opLog.used, 1, o);
	fclose(o);
    }
    if (1) {
	Msg *reply = comm_send_and_reply_mutex(&replyLogserverMut, &replyLogserverCond, opLog.net_fd, DFS_MSG_PUSH_LOG, from, len, NULL);
	// TODO If log server gives us any a new log, then we need to replay it
	// for now we are assuming that we will get the whole log
	{
	    long lastID = ((long *)(reply->data + reply->len))[-1];
	    dfs_out("received %d bytes (%ld records) from PUSH_LOG request\n", reply->len, lastID);
	}
	if (reply->len)
	    playLog(reply->data, reply->len);
	free(reply);
    }
}

// add file record
void logFileVersion(DfsFile *f)
{
    /* Assumes treeMut is locked */
    LogFileVersion *fv;
    int recipe_offset = sizeof(LogFileVersion);
    int path_offset = recipe_offset + f->recipelen;
    int len = path_offset + strlen(f->path) + 1;
    fv = calloc(1, len + sizeof(double) + sizeof(long));
    dfs_out("LFV: [%d, %d, %d, %p]\n", recipe_offset, path_offset, len, fv);
    fv->hdr.type = LOG_FILE_VERSION;
    fv->hdr.id = ++opLog.id; // B:TODO: We may need to put a mutex here
    fv->hdr.version = f->version;
    //fv->hdr.len // Set in pushLog
    // fv->hdr.creator // B:TODO: Not sure what to do with it
    fv->mtime = f->stat.st_mtime;
    fv->recipelen = f->recipelen;
    fv->flags = f->stat.st_mode;
    fv->flen = f->len;
    memcpy((char *)fv + recipe_offset, f->recipe, path_offset - recipe_offset);
    memcpy((char *)fv + path_offset, f->path, len - path_offset);
    pushLog((char *)fv, len);
    free(fv);
}


// add record for other types
void logOther(int type, const char *path, int flags, struct stat *stat)
{
    /* Assumes treeMut is locked */
    int path_offset = sizeof(LogOther);
    int len =  path_offset + strlen(path) + 1;
    LogOther *fv = calloc(1, len + sizeof(double) + sizeof(long));
    fv->hdr.type = type;
    fv->hdr.id = ++opLog.id; // B:TODO: We may need to put a mutex here
    fv->hdr.version = 0; // TODO Check this
    //fv->hdr.len // Set in pushLog
    // fv->hdr.creator // B:TODO: Not sure what to do with it
    fv->flags = flags;
    // fv->mtime = stat->st_mtime; // TODO Check this
    memcpy((char *)fv + path_offset, path, len - path_offset);
    pushLog((char *)fv, len);
    free(fv);
    // Replay on delete dir for debugging
    if (0 && type == LOG_RMDIR) {
	dfs_out("Replaying log");
	playLog(0, 0);
	dfs_out("Done Replaying log");
    }
}
