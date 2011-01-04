#ifndef _GCRY_CHITS_INCLUDED
#define _GCRY_CHITS_INCLUDED
#include <gcrypt.h>
#define GCRY_CIPHER             GCRY_CIPHER_AES128   // Pick the cipher here
#define GCRY_MODE               GCRY_CIPHER_MODE_CBC
#define INI_VECTOR              "123456789012345"
#define AES_BLK_SIZE            16


void 		cry_asym_init();
void 		cry_create_nonce (int len, void* nonce);  // return a nonce in nonce of length len

// hash
char* 		cry_hash_bytes_binary (void *in, int in_len);
char 		*cry_hash_bytes (void *in, int in_len);
void 		cry_digest_hmac_string (char *outbuf, void *key, void *s, int slen);
void 		cry_ascii_to_hash(char *to, char *from);
char 		*cry_hash_to_ascii(char *hash);
char	 	*cry_hash_key(char *key);

// AES 
void		cry_sym_init(char *);
int 		cry_sym_encrypt(char **out, size_t *outsz, char *in, size_t sz);
int		cry_sym_decrypt(char **out, size_t *outsz, char *in, size_t sz);

// RSA
int 		cry_asym_create_keys(char **pub, char **sec);
int		cry_asym_encrypt(char **out, size_t *osz, char *in, size_t isz, char *ekey);
int		cry_asym_decrypt(char **out, size_t *osz, char *in, size_t isz, char *dkey);

#endif
