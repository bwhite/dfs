
#ifndef	_TUPLE_H
#define	_TUPLE_H

#include	"comm.h"
#include	"tpl.h"

// log formats
#define	TPL_FORMAT_VERSION	"iiiiiIIsA(s)" /* type, id, version, len, flags, mtime, flen, path, sig* */
#define	TPL_FORMAT_OTHER	"iiiiiIs"      /* type, id, version, len, flags, mtime, path */

// message formats
#define	TPL_FORMAT_MSG_HDR	"iiii" 		/* seq, type, res, len */
#define	TPL_FORMAT_SIG		"s"
#define	TPL_FORMAT_EXTENT	"B"
#define	TPL_FORMAT_SIG_EXTENT	"sB"

// Hold's data and pointers during 'gather' ops for unserializing the log.
// Pass to callback through opaque pointer.
typedef struct {
    long	used;
    long	alloced;
    char	*data;
} BufT;


// caller must free returned buffers
int tuple_serialize_log(char **to, size_t *to_len, char *from, size_t from_len);
int tuple_unserialize_log(char **to, size_t *to_len, char *from, size_t from_len);

int tuple_serialize_msg(char **to, size_t *to_len, Msg *m);
int tuple_unserialize_msg(char *to, size_t to_len, Msg *m);

int tuple_serialize_extent(char **buf, size_t *sz, char *ex, size_t exsz);
int tuple_unserialize_extent(char **buf, size_t *sz, char *in, size_t insz);

int tuple_serialize_sig_extent(char *sig, char **buf, size_t *sz, char *ex, size_t exsz);
int tuple_unserialize_sig_extent(char **sig, char **buf, size_t *sz, char *in, size_t insz);

int tuple_serialize_sig(char **buf, size_t *sz, char *sig);
int tuple_unserialize_sig(char **sig, char *in, size_t inlen);

extern int	serialize;

#endif
