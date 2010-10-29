
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


int tuple_serialize_log(char **buf, size_t *sz, char *from, size_t from_len)
{
  /*
    Args:
        buf: Where we dump our output
	sz: Buf size when done
	from: Where we get our input
	from_len: Input length
    TPL Format: Sequence of "iiiiiIIsA(s)" or "iiiiiIs" */
  tpl_node *tn;
  char	*end = from + from_len;
  *buf = NULL;
  *sz = 0;
  while (from < end) {
    char *cur_buf;
    size_t cur_sz;
    int step;
    if (((LogHdr *)from)->type == LOG_FILE_VERSION) {
      LogFileVersion *l = (LogFileVersion *)from;
      /* TPL Format: Sequence of "iiiiiIIsA(s)" */
      /* type, id, version, len, flags, mtime, flen, path, sig* */
      uint32_t type = l->hdr.type, id = l->hdr.id, version = l->hdr.version, len = l->hdr.len, flags = l->flags;
      uint64_t mtime = l->mtime, flen = l->flen;
      char  *recipe = from + sizeof(LogFileVersion);
      char *path = recipe + l->recipelen;
      tpl_node *tn;
      step = sizeof(LogFileVersion) + strlen(path) + 1;
      tn = tpl_map(TPL_FORMAT_VERSION, &type, &id, &version, &len, &flags, &mtime, &flen, &path, &recipe);
      if (tpl_pack(tn, 0) == -1)
	return -1;
      while (recipe < path) {
	if (tpl_pack(tn, 1) == -1)
	  return -1;
	step += A_HASH_SIZE;
	recipe += A_HASH_SIZE;
      }
      if (tpl_dump(tn, TPL_MEM, &cur_buf, &cur_sz) == -1)
	return -1;
      tpl_free(tn);
    } else {
      LogOther *l = (LogOther *)from;
      /* TPL Format: Sequence of "iiiiiIs" */
      /* type, id, version, len, flags, mtime, path */
      uint32_t type = l->hdr.type, id = l->hdr.id, version = l->hdr.version, len = l->hdr.len, flags = l->flags;
      uint64_t mtime = l->mtime;
      char  *path = from + sizeof(LogOther);
      tpl_node *tn;
      step = sizeof(logOther) + strlen(path) + 1;
      tn = tpl_map(TPL_FORMAT_VERSION, &type, &id, &version, &len, &flags, &mtime, &path);
      if (tpl_pack(tn, 0) == -1)
	return -1;
      if (tpl_dump(tn, TPL_MEM, &cur_buf, &cur_sz) == -1)
	return -1;
      tpl_free(tn);
    }
    *buf = realloc(*buf, *sz + cur_sz);
    memcpy(*buf + *sz, cur_buf, cur_sz);
    free(cur_buf);
    *sz += cur_sz;
    from += step;
  }
  return 0;
}

int tuple_unserialize_log_cb(void *from, size_t from_len, void *data) {
  BufT *buft = (BufT*)data;
  uint32_t cur_type;
  free(tpl_peek(TPL_MEM | TPL_DATAPEEK, from, from_len, "i", &cur_type));
  void *cur_buf;
  int cur_sz;
  if (cur_type == LOG_FILE_VERSION) {
    uint32_t type, id, version, len, flags;
    uint64_t mtime, flen;
    char  *cur_recipe = NULL;
    char  *recipe = NULL;
    int recipelen = 0;
    char *path;
    tpl_node *tn;
    tn = tpl_map(TPL_FORMAT_VERSION, &type, &id, &version, &len, &flags, &mtime, &flen, &path, &cur_recipe);
    if (tpl_load(tn, TPL_MEM, from, from_len) == -1)
      return -1;
    if (tpl_unpack(tn, 0) == -1)   /* allocates space */
      return -1;
    while (tpl_unpack(tn, 1) > 0) {
      recipe = realloc(recipe, recipelen + A_HASH_SIZE);
      memcpy(recipe + recipelen, cur_recipe, A_HASH_SIZE);
      recipelen += A_HASH_SIZE;
      free(cur_recipe);
    }
    tpl_free(tn);
    int path_len = strlen(path);
    cur_sz = sizeof(LogFileVersion) + recipelen + path_len + 1 + sizeof(long);
    if (cur_sz % sizeof(double))
	cur_sz += sizeof(double) - (cur_sz % sizeof(double));
    LogFileVersion *l = malloc(cur_sz);
    cur_buf = l;
    l->hdr.type = type;
    l->hdr.id = id;
    l->hdr.version = version;
    //l->hdr.len = len; Set below
    l->mtime = mtime;
    l->recipelen = recipelen;
    l->flags = flags;
    l->flen = flen;
    memcpy(l + sizeof(LogFileVersion), recipe, recipelen);
    memcpy(l + sizeof(LogFileVersion) + recipelen, path, path_len + 1);
    free(path);
    free(recipe);
  } else {
    uint32_t type, id, version, len, flags;
    uint64_t mtime;
    char *path;
    tpl_node *tn;
    tn = tpl_map(TPL_FORMAT_OTHER, &type, &id, &version, &len, &flags, &mtime, &path);
    if (tpl_load(tn, TPL_MEM, from, from_len) == -1)
      return -1;
    if (tpl_unpack(tn, 0) == -1)   /* allocates space */
      return -1;
    tpl_free(tn);
    int path_len = strlen(path);
    cur_sz = sizeof(LogFileVersion) + path_len + 1 + sizeof(long);
    if (cur_sz % sizeof(double))
	cur_sz += sizeof(double) - (cur_sz % sizeof(double));
    LogFileVersion *l = malloc(cur_sz);
    cur_buf = l;
    l->hdr.type = type;
    l->hdr.id = id;
    l->hdr.version = version;
    //l->hdr.len = len; Set below
    l->mtime = mtime;
    l->flags = flags;
    memcpy(l + sizeof(LogFileVersion), path, path_len + 1);
    free(path);
  }
  // Set length
  ((LogHdr*)cur_buf)->len = cur_sz;
  *((long*)(cur_buf + cur_sz - sizeof(long))) = cur_sz;
  buft->data = realloc(buft->data, buft->used + cur_sz);
  memcpy(buft->data + buft->used, cur_buf, cur_sz);
  buft->used += cur_sz;
  free(cur_buf);
  return 0;
}

int tuple_unserialize_log(char **buf, size_t *sz, char *from, size_t from_len)
{
  /*
    Args:
        buf: Where we dump our output
	sz: Buf size when done
	from: Where we get our input
	from_len: Input length
    TPL Format: Sequence of "iiiiiIIsA(s)" or "iiiiiIs" */
  char	*end = from + from_len;
  BufT buft;
  buft.used = 0;
  buft.data = NULL;
  tpl_gather_t *gt = NULL;
  tpl_gather(TPL_GATHER_MEM, from, from_len, &gt, tuple_unserialize_log_cb, &buft);
  *buf = buft.data;
  *sz = buft.used;
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
