
#include	<stdio.h>

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
#include <xlocale.h>
#include <assert.h>
#include <search.h>
#include <arpa/inet.h>

#include <fuse.h>

#include "utils.h"
#include "dfs.h"
#include "comm.h"
#include "log.h"


main(int argc, char **argv)
{
    if (argc < 2) {
	fprintf(stderr, "USAGE: printLog <fname>\n");
	exit(1);
    }

    int		fd;

    if ((fd = open(argv[1], O_RDONLY)) < 0) {
	fprintf(stderr, "Can't open '%s'\n", argv[1]);
	exit(1);
    }
	
    struct stat		stat;
    fstat(fd, &stat);
    
    int		len = stat.st_size;
    char	*buf = malloc(len);
    char	*data = buf;
    char	*end = data + len;

    read(fd, buf, len);

    while (data < end) {
	switch ( ((LogHdr *)data)->type ) {
	case LOG_FILE_VERSION:
	    {
		LogFileVersion	*l = (LogFileVersion *)data;
		char		*recipes = (char *)(l + 1);
		char		*path = recipes + l->recipelen;

		printf("\n%06ld #%d FILE '%s', %s, len %ld, recipelen %ld, VERSION %d:\n", data - buf, l->hdr.id, path, 
		       timeToString(l->mtime), l->flen, l->recipelen, l->hdr.version);
		while (recipes < path) {
		    printf("\t\t%s\n", recipes);
		    recipes += A_HASH_SIZE;
		}
	    }
	    break;
	    
	case LOG_UNLINK:
	case LOG_MKDIR:
	case LOG_RMDIR:
	case LOG_CHMOD:
	    {
		LogOther	*l = (LogOther *)data;
		char		*path = (char *)(l + 1);
		char		*translate[LOG_MACHINE + 1];

		translate[LOG_UNLINK] = "UNLINK";
		translate[LOG_MKDIR] = "MKDIR";
		translate[LOG_RMDIR] = "RMDIR";
		translate[LOG_CHMOD] = "CHMOD";

		printf("\n%06ld #%d %s '%s' mode/flags 0%o\n", data - buf, l->hdr.id, translate[l->hdr.type], path, l->flags);
	    }
	    break;
	default:
	    printf("BAD RECORD\n");
	    exit(1);
	}

	data += ((LogHdr *)data)->len;
    }
}

