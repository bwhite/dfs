
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
#include "tpl.h"
#include "tuple.h"


int tuple_serialize_log(char **to, size_t *to_len, char *from, size_t from_len)
{

}


int tuple_unserialize_log(char **to, size_t *to_len, char *from, size_t from_len)
{

}

int tuple_serialize_msg(char **buf, size_t *sz, Msg *m)
{
  /*
    Args:
        buf: Where we dump our output
	sz: Buf size when done
	m: Message
    TPL Format: "iiii" */
  uint32_t seq = m->seq, type = m->type, res = m->res, len = m->len;
  tpl_node *tn;
  tn = tpl_map(TPL_FORMAT_MSG_HDR, &seq, &type, &res, &len);
  if (tpl_pack(tn, 0) == -1)
    return -1;
  if (tpl_dump(tn, TPL_MEM, buf, sz) == -1)
    return -1;
  tpl_free(tn);
  return 0;
}


int tuple_unserialize_msg(char *in, size_t insz, Msg *m)
{
  /*
    Args:
        in: Where we get our input
	insz: Buf size
	m: Message
    TPL Format: "iiii" */
  tpl_node *tn;
  uint32_t seq, type, res, len;
  tn = tpl_map(TPL_FORMAT_MSG_HDR, &seq, &type, &res, &len);
  if (tpl_load(tn, TPL_MEM, in, insz) == -1)
    return -1;
  if (tpl_unpack(tn, 0) == -1)   /* allocates space */
    return -1;
  tpl_free(tn);
  m->seq = seq;
  m->type = type;
  m->res = res;
  m->len = len;
  return 0;
}



int tuple_serialize_extent(char **buf, size_t *sz, char *ex, size_t exsz)
{
  /*
    Args:
        buf: Where we dump our output
	sz: Buf size when done
	ex: Extent data (binary bytes)
	exsz: Size of binary bytes
    TPL Format: "B" */
  tpl_node *tn;
  tpl_bin tb;
  tb.addr = ex;
  tb.sz = exsz;
  tn = tpl_map(TPL_FORMAT_EXTENT, &tb);
  if (tpl_pack(tn, 0) == -1)
    return -1;
  if (tpl_dump(tn, TPL_MEM, buf, sz) == -1)
    return -1;
  tpl_free(tn);
  return 0;
}


int tuple_unserialize_extent(char **buf, size_t *sz, char *in, size_t insz)
{
  /*
    Args:
	buf: Extent data (binary bytes)
	sz: Extent bytes
	in: In bytes
	insz: Number of input bytes
    TPL Format: "s" */
  tpl_node *tn;
  tpl_bin tb;
  tn = tpl_map(TPL_FORMAT_EXTENT, &tb);
  if (tpl_load(tn, TPL_MEM, in, insz) == -1)
    return -1;
  if (tpl_unpack(tn, 0) == -1)   /* allocates space */
    return -1;
  tpl_free(tn);
  *buf = tb.addr;
  *sz = tb.sz;
  return 0;
}



int tuple_serialize_sig_extent(char *sig, char **buf, size_t *sz, char *ex, size_t exsz)
{ 
  /*
    Args:
	sig: The null_terminated signature with size A_HASH_SIZE (includes delimiter)
        buf: Where we dump our output
	sz: Buf size when done
	ex: Extent data (binary bytes)
	exsz: Size of binary bytes
    TPL Format: "sB" */
  tpl_node *tn;
  tpl_bin tb;
  tb.addr = ex;
  tb.sz = exsz;
  tn = tpl_map(TPL_FORMAT_SIG_EXTENT, &sig, &tb);
  if (tpl_pack(tn, 0) == -1)
    return -1;
  if (tpl_dump(tn, TPL_MEM, buf, sz) == -1)
    return -1;
  tpl_free(tn);
  return 0;
}


int tuple_unserialize_sig_extent(char **sig, char **buf, size_t *sz, char *in, size_t insz)
{
  /*
    Args:
	sig: The null_terminated signature with size A_HASH_SIZE (includes delimiter)
        buf: Where we dump our output
	sz: Buf size when done
	ex: Extent data (binary bytes)
	exsz: Size of binary bytes
    TPL Format: "sB" */
  tpl_node *tn;
  tpl_bin tb;
  tn = tpl_map(TPL_FORMAT_SIG_EXTENT, sig, &tb);
  if (tpl_load(tn, TPL_MEM, in, insz) == -1)
    return -1;
  if (tpl_unpack(tn, 0) == -1)   /* allocates space */
    return -1;
  tpl_free(tn);
  *buf = tb.addr;
  *sz = tb.sz;
  return 0;
}



int tuple_serialize_sig(char **buf, size_t *sz, char *sig)
{
  /*
    Args:
        buf: Where we dump our output
	sz: Buf size when done
	sig: The null_terminated signature with size A_HASH_SIZE (includes delimiter)
    TPL Format: "s" */
  tpl_node *tn;
  tn = tpl_map(TPL_FORMAT_SIG, &sig);
  if (tpl_pack(tn, 0) == -1)
    return -1;
  if (tpl_dump(tn, TPL_MEM, buf, sz) == -1)
    return -1;
  tpl_free(tn);
  return 0;
}


int tuple_unserialize_sig(char **sig, char *in, size_t insz)
{
  /*
    Args:
        sig: The null_terminated signature with size A_HASH_SIZE (includes delimiter)
	in: Input bytes
	insz: Number of input bytes
    TPL Format: "s" */
  tpl_node *tn;
  tn = tpl_map(TPL_FORMAT_SIG, sig);
  if (tpl_load(tn, TPL_MEM, in, insz) == -1)
    return -1;
  if (tpl_unpack(tn, 0) == -1)   /* allocates space */
    return -1;
  tpl_free(tn);
  return 0;
}
