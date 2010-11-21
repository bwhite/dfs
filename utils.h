
#ifndef	__UTILS_H__
#define	__UTILS_H__

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sys/stat.h>
#include	<assert.h>
#include 	<string.h>
#include	<fcntl.h>
#include	<gcrypt.h>
#include	<stdarg.h>
#include	<pthread.h>
#define HASH_SIZE 20
#define A_HASH_SIZE (2 * HASH_SIZE + 1)

void 			dfs_out(const char *s, ...);
void 			dfs_die(const char *s, ...);
void 			dfs_assert(int val, const char *fmt, ...);

char 			*hash_to_ascii(char *md);	// dyn allocs space
void 			ascii_to_hash(char *to, char *from);

char 			*read_text_file(char *fname);

//=============================================================================

void *wrapped_tfind(const void *key, void *const *rootp,
		    int (*compar) (const void *key1, const void *key2));

void *wrapped_tsearch(const void *key, void **rootp,
		      int (*compar) (const void *key1, const void *key2));

void *wrapped_tdelete(const void *key, void **rootp,
		      int (*compar) (const void *key1, const void *key2));

//=============================================================================



#ifndef	MIN
#define	MIN(x,y)	(((x) < (y)) ? (x) : (y))
#endif

#ifndef	MAX
#define	MAX(x,y)	(((x) > (y)) ? (x) : (y))
#endif

char 		*timeToString(time_t tm);
extern int	dfsbug;

#endif
