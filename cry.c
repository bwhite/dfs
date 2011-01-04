
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gcrypt.h>

#include "dfs.h"
#include "cry.h"
#include "utils.h"

#define	HASH_SIZE	20


int cry_verbose = 0;
int cry_debug = 0;
int cry_initialized = 0;


void cry_asym_init() 
{
    if (!gcry_check_version (GCRYPT_VERSION))
	dfs_die ("gcrypt version mismatch\n");

    // we can go whole hog and enable secure memory.  not sure what that
    // does
    gcry_control (GCRYCTL_DISABLE_SECMEM, 0);

    gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);

    if (cry_verbose)
	gcry_control (GCRYCTL_SET_DEBUG_FLAGS, 1u , 0);

    /* no valuable keys are create, so we can speed up our RNG. */
    gcry_control (GCRYCTL_ENABLE_QUICK_RANDOM, 0);

    cry_initialized = 1;
}


static char *sexp_to_string(gcry_sexp_t s)
{
    char *p;
    int len;
    len = gcry_sexp_sprint (s, GCRYSEXP_FMT_ADVANCED, 0x0, 0x0);
    p = (char *) malloc (len);
    len = gcry_sexp_sprint (s, GCRYSEXP_FMT_ADVANCED, p, len);
    return p;
}


// returns 0 on error... 
int cry_asym_create_keys(char **pk, char **sk)
{
    // make strength of key a parameter?
    gcry_sexp_t keyparm, key;
    int rc;

    if (!cry_initialized) cry_asym_init();

    // default to 128 bits for now
    // change to 4:1024 for 1024 bit keys and so  on
    rc = gcry_sexp_new (&keyparm, 
			"(genkey\n"
			" (rsa\n"
			"  (nbits 4:1024)\n"
			" ))", 0, 1);
    if (rc){
	dfs_out( "error creating S-expression: %s\n", gpg_strerror (rc));
	return 0;
    }

    rc = gcry_pk_genkey (&key, keyparm);

    // thats it!

    {
	gcry_sexp_t pub = gcry_sexp_find_token (key, "public-key", 0x0);
	gcry_sexp_t sec = gcry_sexp_find_token (key, "private-key", 0x0);
	assert(pub);
    
	*pk = sexp_to_string (pub);

	assert(sec);
	//    print_sexp(sec);
	*sk = sexp_to_string (sec);
    }

    gcry_sexp_release (keyparm);
    if (rc){
	dfs_out("error generating RSA key: %s\n", gpg_strerror (rc));
    }
    gcry_sexp_release (key);
    //  check_generated_rsa_key (key, 65537);

    return (pk && sk); // both are valid
}


void cry_create_nonce (int len, void* nonce){
    if (!cry_initialized) cry_asym_init();

    gcry_create_nonce (nonce, len);
}


    
char* cry_hash_bytes_binary (void *in, int in_len)
{
    int hashlen;
    unsigned char *digest;

    if (!cry_initialized) cry_asym_init();
    
    hashlen  = gcry_md_get_algo_dlen(GCRY_MD_SHA1);
    digest = (unsigned char *) malloc (hashlen);

    gcry_md_hash_buffer (GCRY_MD_SHA1, digest, in, in_len);
    // parse the return;

    return (char *)digest; // allocates memory
}


#ifdef	NOTDEF
void cry_digest_hmac_string (char *outdig, void *indig, void *s, int slen) {
    int		len = HASH_SIZE + slen;
    char	*b = malloc(len);
    
    memcpy(b, indig, HASH_SIZE);
    memcpy(b + HASH_SIZE, s, slen);
    
    char *out = cry_hash_bytes_binary(b, len);
    memcpy(outdig, out, HASH_SIZE);
    free(out);
}
#endif


// hashes ASCII version of magic numbers in keys, return ASCII digest
char *cry_hash_key(char *key)
{
    char	*buf = NULL, *s1 = key;
    int		len = 0;

    while ((s1 = strstr(s1, " #"))) {
	s1 += 2;
	char	*s2 = strstr(s1, "#)");
	assert(s2);

	int	nlen = s2 - s1;
	buf = realloc(buf, len + nlen);
	memcpy(buf + len, s1, nlen);
	len += nlen;
	s1 = s2 + 1;
    }
    if (!buf) return NULL;

    char *ret = cry_hash_bytes(buf, len);
    free(buf);
    return ret;
}


void cry_ascii_to_hash(char *to, char *from)
{
    int 		i; 
    unsigned char	t[2 * HASH_SIZE + 1];

    strcpy((char *)t, (char *)from);

    for (i = HASH_SIZE-1; i >= 0; i--) {
	t[(i+1) * 2] = 0;
	to[i] = strtol((char *)(t + i*2), NULL, 16);
    }
}


char *cry_hash_to_ascii(char *hash)
{
    int i; 
    char *s = malloc(1 + 2 * HASH_SIZE);
    for (i = 0; i < HASH_SIZE; i++) {
	sprintf(s + 2 * i, "%02X", hash[i] & 0xFF);
    }
    s[40] = 0;
    return s;
}


// returns dynamically allocated string w/ ASCII of hash
char *cry_hash_bytes (void *in, int in_len)
{
  char		hash[HASH_SIZE];

  assert(in && in_len);

  if (!cry_initialized) cry_asym_init();
    
  //  hashlen  = gcry_md_get_algo_dlen(GCRY_MD_SHA1);
  //  digest = (unsigned char *) malloc (hashlen);

  gcry_md_hash_buffer (GCRY_MD_SHA1, hash, in, in_len);
  // parse the return;

  char *s = cry_hash_to_ascii(hash);
  dfs_out("GCRY: turned %d byte block into '%s' byte sig.\n", in_len, s);
  return s;
}


void cry_digest_hmac_string (char *outbuf, void *key, void *s, int slen) {
    static gcry_md_hd_t		digest = NULL;
    gcry_error_t		err;
    void 			*out;

    if (!cry_initialized) cry_asym_init();

    if (!digest) {
	if ((err = gcry_md_open(&digest, GCRY_MD_SHA1, GCRY_MD_FLAG_HMAC))) {
	    dfs_out("gcry_md_open error %d, '%s'\n", err, s);
	    return;
	}
    }
    if ((err = gcry_md_setkey(digest, key, HASH_SIZE))) {
	dfs_out("gcry_md_setkey error %d\n", err);
	return;
    }

    gcry_md_write(digest, s, slen);
    if (!(out = gcry_md_read(digest, 0))) {
	dfs_out("gcry_md_read error %d, '%s'\n", err, s);
	return;
    }
  
    memcpy(outbuf, out, HASH_SIZE);
  
    gcry_md_reset(digest);
}


int cry_asym_encrypt(char **out, size_t *osz, char *in, size_t isz, char *ekey)
{
    // get a key out of this string
    gcry_sexp_t plain, cipher, key;
    gcry_mpi_t data;
    int rc;
    char *cipher_string;

    if (!cry_initialized) cry_asym_init();
  
    rc = gcry_sexp_sscan (&key, NULL, ekey, 
			  strlen (ekey));
    if (rc){
	dfs_out( "Could find  key for encrypt");
	return -1;
    }
  
    rc = gcry_mpi_scan (&data, GCRYMPI_FMT_USG, in, 
			isz, NULL);
  
  
    rc = gcry_sexp_build (&plain, NULL, "(data (flags raw) (value %m))", data);
    if (rc){
	dfs_out( "converting data for encryption failed: %s\n",
		  gcry_strerror (rc));
	return -1;
    }

    rc = gcry_pk_encrypt(&cipher, plain, key);  
    if (rc){
	dfs_out( "encrypt bytes fails: %s\n",
		  gcry_strerror (rc));
	return -1;
    }
  
    cipher_string = sexp_to_string(cipher) ; // check error?

    gcry_sexp_release(plain);
    gcry_sexp_release(cipher);
    gcry_sexp_release(key);

    *out = cipher_string;
    *osz = strlen(cipher_string) + 1;
    return 0;
}


int cry_asym_decrypt(char **out, size_t *osz, char *in, size_t isz, char *dkey)
{
    // get a key out of this string
    gcry_sexp_t plain, cipher, key, l;
    //  gcry_mpi_t data;
    int rc;
    gcry_mpi_t x1;
    // char *plain_string;
    unsigned char *data_string;

    if (!cry_initialized) cry_asym_init();
  
    rc = gcry_sexp_sscan (&key, NULL, dkey, 
			  strlen (dkey));
    if (rc){
	dfs_out( "Could find key for decrypt");
	return -1;
    }
  
    rc = gcry_sexp_sscan (&cipher, NULL, in, isz);

    if (rc){
	dfs_out( "converting data for decryption failed: %s\n",
		  gcry_strerror (rc));
	return -1;
    }

    //
    rc = gcry_pk_decrypt(&plain, cipher, key);  

    if (rc){
	dfs_out( "decrypt bytes fails: %s\n",
		  gcry_strerror (rc));
	return -1;
    }

    //    printf ("decoded to:");
    //      print_sexp (plain);

    // if  with flags in the cipher S-exp
    l = gcry_sexp_find_token (plain, "value", 0);
    if (l)   {
	x1 = gcry_sexp_nth_mpi (l, 1, GCRYMPI_FMT_USG);
	gcry_sexp_release (l);
    }
    else    {
	x1 = gcry_sexp_nth_mpi (plain, 0, GCRYMPI_FMT_USG);
    }
  
    // get data out of x1
    {
	size_t plen;

	gcry_mpi_print (GCRYMPI_FMT_USG, 0x0, 0x0, &plen, x1);
	data_string = (unsigned char *) malloc (plen+1); // we'll add a terminator


	gcry_mpi_print (GCRYMPI_FMT_USG, data_string, plen+1, &plen, x1);

	data_string[plen]=0x0;
	//    printf ("plen is %d, data is %s\n", plen, data_string);
	*osz = plen;
    }
  
    // print_mpi("plain", x1);
    //  *len =  sexp_to_bytes(plain, &plain_string);

    gcry_sexp_release(plain);
    gcry_sexp_release(cipher);
    gcry_sexp_release(key);

    *out = (char *)data_string;
    return 0;
}


//=============================================================================
  
static gcry_cipher_hd_t 	gcryCipherHd;
static int			sym_inited;



// takes next AES_BLK_SIZE bytes (16, for now)
void cry_sym_init(char *key)
{
    gcry_error_t     	gcryError;

    sym_inited = 1;

    size_t keyLength = gcry_cipher_get_algo_keylen(GCRY_CIPHER);
    size_t blkLength = gcry_cipher_get_algo_blklen(GCRY_CIPHER);

    assert((keyLength == AES_BLK_SIZE) && (blkLength == AES_BLK_SIZE));

    gcryError = gcry_cipher_open(
        &gcryCipherHd, // gcry_cipher_hd_t *
        GCRY_CIPHER,   // int
        GCRY_MODE,     // int
        0);            // unsigned int
    if (gcryError)
    {
        printf("gcry_cipher_open failed:  %s/%s\n",
               gcry_strsource(gcryError),
               gcry_strerror(gcryError));
        return;
    }

    gcryError = gcry_cipher_setkey(gcryCipherHd, key, keyLength);
    if (gcryError)
    {
        printf("gcry_cipher_setkey failed:  %s/%s\n",
               gcry_strsource(gcryError),
               gcry_strerror(gcryError));
        return;
    }

    gcryError = gcry_cipher_setiv(gcryCipherHd, INI_VECTOR, blkLength);
    if (gcryError)
    {
        printf("gcry_cipher_setiv failed:  %s/%s\n",
               gcry_strsource(gcryError),
               gcry_strerror(gcryError));
        return;
    }
}

    
int cry_sym_encrypt(char **out, size_t *outsz, char *in, size_t sz)
{
    gcry_error_t	gcryError;
    assert(sym_inited);

    // must pad out to blocksize, multiple of 16 bytes
    int 	pads = AES_BLK_SIZE - (sz % AES_BLK_SIZE);
    if (!pads) pads = AES_BLK_SIZE;
    assert(!((sz + pads) % AES_BLK_SIZE));

    int sz2 = sz + pads;
    char *in2 = malloc(sz2);
    memcpy(in2, in, sz);
    memset(in2 + sz, pads, pads);
    *out = malloc(sz2);	
    *outsz = sz2;

    gcryError = gcry_cipher_encrypt(
        gcryCipherHd, // gcry_cipher_hd_t
        *out,    // void *
        *outsz,    // size_t
        in2,    // const void *
        sz2);   // size_t
    free(in2);
    if (gcryError)
    {
        fprintf(stderr, "gcry_cipher_encrypt failed:  %s/%s\n",
		gcry_strsource(gcryError),
		gcry_strerror(gcryError));
	exit(1);
    }
    dfs_out("in bytes[%d] encrypted %d bytes (pad %d)\n", sz, (int)*outsz, pads);
    
    return 0;
}


int cry_sym_decrypt(char **out, size_t *outsz, char *in, size_t sz)
{
    gcry_error_t     gcryError;

    assert(sym_inited);
    dfs_out("out bytes[%d]\n", sz);
    *out = malloc(sz);
    *outsz = sz;

    gcryError = gcry_cipher_decrypt(
        gcryCipherHd, // gcry_cipher_hd_t
        *out,    // void *
        *outsz,    // size_t
        in,    // const void *
        sz);   // size_t
    if (gcryError)
    {
        fprintf(stderr, "gcry_cipher_decrypt failed:  %s/%s\n",
		gcry_strsource(gcryError),
		gcry_strerror(gcryError));
        exit(1);
    }
    int padding = (*out)[*outsz - 1];
    dfs_out("decrypted %d bytes (pad %d)\n", (int)*outsz, padding);
    *outsz -= padding;
    return 0;
}

  
