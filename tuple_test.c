#include "tuple.h"
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
  printf("Tests Passed!\n");
  return 0;
}
