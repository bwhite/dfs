
#ifndef	__CHITS_H__
#define	__CHITS_H__

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

#include "utils.h"
#include "dfs.h"
#include "comm.h"
#include "log.h"
#include "tuple.h"
#include "cry.h"
#include <expat.h>


typedef struct attr_t {
    int			tag;
    char		*val_s;
    int			val_l;		// id for the right, maybe
    struct attr_t	*next;
} attr_t;


typedef struct chit_t {
    char	*server;
    uint64_t	version;
    uint64_t	id;
    char	fingerprint[HASH_SIZE];
    char	serverprint[HASH_SIZE];
    attr_t	*attrs;
    attr_t	*attrs_last;
} chit_t;




#define	RIGHT_READ		1
#define	RIGHT_WRITE		2
#define	RIGHT_DELETE		3
#define	RIGHT_CREATE		4

#define	RIGHT_READ_MASK		(2 << 1)
#define	RIGHT_WRITE_MASK	(2 << 2)
#define	RIGHT_DELETE_MASK	(2 << 3)
#define	RIGHT_CREATE_MASK	(2 << 4)
#define ALL_RIGHTS_MASK		(RIGHT_READ_MASK | RIGHT_WRITE_MASK | RIGHT_DELETE_MASK | RIGHT_CREATE_MASK)

#define	TAG_NARROW		5
#define	TAG_REMOVE_RIGHT	6
#define	TAG_PUBLIC_KEY		7
#define	TAG_DELEGATE		8
#define	TAG_LABEL		9

#define	TAG_LAST		9

extern char *rightsTags[];


int 		tagname_to_int(char *name);
chit_t 		*chit_new(char *server, long id, long version, char *server_public_key, char *secret);
chit_t	 	*chit_read(char *fname);
void 		chit_save(chit_t* ch, char *outfile);

unsigned char 	*hash_key(char *key);
void 		hash_in_string(char *dig_new, char *dig, int tag, char *val);
void 		chit_add_long_attr(chit_t *c, int tag, long l);
void 		chit_add_string_attr(chit_t *c, int tag, char *s);
int 		chit_verify(chit_t *chit, char *secret);
void		chit_free(chit_t *chit);
int		digests_match(void *one, void *two);

char	 	*xcred_read(char *fname);
chit_t 		*xcred_parse(char *xcred);

#endif
