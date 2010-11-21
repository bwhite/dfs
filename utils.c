
#include	"dfs.h"
#include	"utils.h"
#include	<errno.h>
#include	<pthread.h>
#include	<search.h>
#include	<sys/stat.h>
#include	<sys/types.h>
#include 	<sys/uio.h>
#include 	<unistd.h>

int			dfsbug = 1;


//=============================================================================

char *read_text_file(char *fname)
{
    char		*buf;
    int			fd;
    struct stat	stat;

    if ((fd = open(fname, O_RDONLY)) <= 0) {
	dfs_out("No open '%s'\n");
	return NULL;
    }
    fstat(fd, &stat);
    if (!(buf = malloc(stat.st_size + 1))) {
	dfs_out("Not able to malloc %ld bytes for %s\n", (long)stat.st_size, fname);
	return NULL;
    }
    if (read(fd, buf, stat.st_size) != stat.st_size) {
	dfs_out("Not able to read %ld bytes for %s\n", (long)stat.st_size, fname);
	return NULL;
    }
    close(fd);
    buf[stat.st_size] = 0;
    return buf;
}


void dfs_out(const char *s, ...)
{
    va_list	ap;

    if (dfsbug) {
	va_start(ap, s);
	fprintf(stderr, "DFS %3lx ", (long)pthread_self());
	vfprintf(stderr, s, ap);
	va_end(ap);
    }
}

void dfs_die(const char *s, ...)
{
    va_list	ap;

    va_start(ap, s);
    fprintf(stderr, "DFS DIE %3ld: %s : ", (long)pthread_self(), strerror(errno));
    vfprintf(stderr, s, ap);
    va_end(ap);
    exit(1);
}


void dfs_assert(int val, const char *fmt, ...)
{
    va_list	ap;

    if (val) return;

    fprintf(stderr, "DFS-ASSERT %3ld: %s: ", (long)pthread_self(), strerror(errno));
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    assert(0);
}


//=============================================================================

char *timeToString(time_t tm)
{
    static char	s[200];

    strftime(s, sizeof(s), "%F %R", localtime(&tm));
    return s;
}


//=============================================================================
    
static pthread_mutex_t       search_mutex = PTHREAD_MUTEX_INITIALIZER;

void *wrapped_tdelete(const void *key, void **rootp,
		    int (*compar) (const void *key1, const void *key2))
{
    pthread_mutex_lock(&search_mutex);
    void *res = tdelete(key, rootp, compar);
    pthread_mutex_unlock(&search_mutex);
    return res;
}


void *wrapped_tfind(const void *key, void *const *rootp,
		    int (*compar) (const void *key1, const void *key2))
{
    pthread_mutex_lock(&search_mutex);
    void *res = tfind(key, rootp, compar);
    pthread_mutex_unlock(&search_mutex);
    return res;
}


void *wrapped_tsearch(const void *key, void **rootp,
		      int (*compar) (const void *key1, const void *key2))
{
    pthread_mutex_lock(&search_mutex);
    void *res = tsearch(key, rootp, compar);
    pthread_mutex_unlock(&search_mutex);
    return res;
}


