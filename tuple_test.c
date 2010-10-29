#include "tuple.h"
#include "comm.h"
#include <stdio.h>
#include <stdlib.h>

int sig_test(char *sig) {
  char *serialized;
  size_t serialized_sz;
  char *out_sig;
  if (tuple_serialize_sig(&serialized, &serialized_sz, sig))
    return -1;
  if (tuple_unserialize_sig(&out_sig, serialized, serialized_sz))
    return -1;
  printf("In[%s] Out[%s]\n", sig, out_sig);
  if (strcmp(sig, out_sig))
    return -1;
  free(serialized);
  free(out_sig);
  return 0;
}

int extent_test(char *ex, size_t exsz) {
  char *serialized;
  size_t serialized_sz;
  size_t unserialized_sz;
  char *out_ext;
  if (tuple_serialize_extent(&serialized, &serialized_sz, ex, exsz))
    return -1;
  if (tuple_unserialize_extent(&out_ext, &unserialized_sz, serialized, serialized_sz))
    return -1;
  printf("InSz[%d] OutSz[%d]\n", exsz, unserialized_sz);
  if (unserialized_sz != exsz)
    return -1;
  if (memcmp(ex, out_ext, exsz))
    return -1;
  free(serialized);
  free(out_ext);
  return 0;
}

int sig_extent_test(char *sig, char *ex, size_t exsz) {
  char *serialized;
  size_t serialized_sz;
  size_t unserialized_sz;
  char *out_ext;
  char *out_sig;
  if (tuple_serialize_sig_extent(sig, &serialized, &serialized_sz, ex, exsz))
    return -1;
  if (tuple_unserialize_sig_extent(&out_sig, &out_ext, &unserialized_sz, serialized, serialized_sz))
    return -1;
  printf("In[%s] Out[%s]\n", sig, out_sig);
  printf("InSz[%d] OutSz[%d]\n", exsz, unserialized_sz);
  if (unserialized_sz != exsz)
    return -1;
  if (memcmp(ex, out_ext, exsz))
    return -1;
  if (strcmp(sig, out_sig))
    return -1;
  free(serialized);
  free(out_ext);
  free(out_sig);
  return 0;
}

int msg_test(int seq, int type, int res, int len) {
  char *serialized;
  size_t serialized_sz;  
  Msg in_msg, out_msg;
  in_msg.seq = seq;
  in_msg.type = type;
  in_msg.res = res;
  in_msg.len = len;
  if (tuple_serialize_msg(&serialized, &serialized_sz, &in_msg))
    return -1;
  if (tuple_unserialize_msg(serialized, serialized_sz, &out_msg))
    return -1;
  printf("%d %d %d %d | %d %d %d %d\n", in_msg.seq, in_msg.type, in_msg.res, in_msg.len,
	 out_msg.seq, out_msg.type, out_msg.res, out_msg.len);
  if (out_msg.seq != seq || out_msg.type != type || out_msg.res != res || out_msg.len != len)
    return -1;
  free(serialized);
  return 0;
}

int log_test(const char* log_fn) {
  FILE* fp = fopen(log_fn, "r");
  fseek(fp, 0L, SEEK_END);
  size_t sz = ftell(fp), out_sz;
  fseek(fp, 0L, SEEK_SET);
  char *log = malloc(sz);
  char *out_log;
  fread(log, sz, 1, fp);
  fclose(fp);
  char *serialized;
  size_t serialized_sz;  
  if (tuple_serialize_log(&serialized, &serialized_sz, log, sz))
    return -1;
  if (tuple_unserialize_log(&out_log, &out_sz, serialized, serialized_sz))
    return -1;
  if (memcmp(log, out_log, sz))
    return -1;
  free(serialized);
  free(out_log);
  return 0;
}

int main() {
  assert(sig_test("This is my sig!") == 0);
  assert(sig_test("") == 0);
  assert(sig_test("SDKJFSKDFSKJDFKSJDKJFSKDJFKSJDFKSJDKSJDFKJSDFJSDKJDFUSDF*(SDF89 08") == 0);
  assert(extent_test("AB\0C", 4) == 0);
  assert(extent_test("", 0) == 0);
  assert(extent_test("\0\0\0", 3) == 0);
  assert(sig_extent_test("sdfsdfsfd", "\0\0\0", 3) == 0);
  assert(sig_extent_test("", "", 0) == 0);
  assert(sig_extent_test("dfse332 ", "sfsdf\01", 7) == 0);
  assert(msg_test(0, 1, 2, 3) == 0);
  assert(msg_test(0, 0, 0, 0) == 0);
  assert(msg_test(-1, -1, -1, -1) == 0);
  assert(log_test("LOG_SERVER000") == 0);
  printf("Tests Passed!\n");
  return 0;
}
