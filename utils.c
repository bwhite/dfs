
#include	"utils.h"
#include	<errno.h>
#include	<pthread.h>
#include	<search.h>

char 			*hash_to_ascii(char *md);	// dyn allocs space
char 			*ascii_to_hash(char *s);	// dyn allocs space
int			dfsbug = 1;

//=============================================================================

static int cry_initialized;

extern void init_gcrypt() {
    extern int	cry_verbose;

  if (!gcry_check_version (GCRYPT_VERSION))
    dfs_die ("GCRYPT version mismatch\n");

  // we can go whole hog and enable secure memory.  not sure what that
  // does
  gcry_control (GCRYCTL_DISABLE_SECMEM, 0);

  gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);

  //  if (cry_verbose)
  // gcry_control (GCRYCTL_SET_DEBUG_FLAGS, 1u , 0);

  /* no valuable keys are create, so we can speed up our RNG. */
  gcry_control (GCRYCTL_ENABLE_QUICK_RANDOM, 0);

  cry_initialized = 1;
}


// returns dynamically allocated string w/ ASCII of hash
char *hash_bytes (void *in, int in_len){
  int 		hashlen;
  unsigned char *digest;
  char		hash[HASH_SIZE];

  assert(hash && in && in_len);

  if(!cry_initialized) init_gcrypt();
    
  //  hashlen  = gcry_md_get_algo_dlen(GCRY_MD_SHA1);
  //  digest = (unsigned char *) malloc (hashlen);

  gcry_md_hash_buffer (GCRY_MD_SHA1, hash, in, in_len);
  // parse the return;

  char *s = hash_to_ascii(hash);
  dfs_out("GCRY: turned %d byte block into '%s' byte sig.\n", in_len, s);
  return s;
}


char *ascii_to_hash(char *in) {
 int 		i; 
 unsigned char 	*s = malloc(HASH_SIZE);
 unsigned char	t[2 * HASH_SIZE + 1];

 strcpy((char *)t, (char *)in);

 for (i = HASH_SIZE-1; i >= 0; i--) {
     t[(i+1) * 2] = 0;
     s[i] = strtol((char *)(t + i*2), NULL, 16);
 }
 return (char *)s;
}


char *hash_to_ascii(char *hash){
 int i; 
 char *s = malloc(1 + 2 * HASH_SIZE);
 for (i = 0; i < HASH_SIZE; i++) {
     sprintf(s + 2 * i, "%02X", hash[i] & 0xFF);
 }
 s[40] = 0;
 return s;
}


//=============================================================================

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


