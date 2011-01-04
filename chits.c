#include	"chits.h"
#include	<stdarg.h>

char *rightsTags[] = { "",
		       "read", "write", "delete", "create",
		       "narrow", "remove", "public", "delegate", "label",
};



int tagname_to_int(char *name)
{
    int		i;

    for (i = 0; i < sizeof(rightsTags)/sizeof(rightsTags[0]); i++) {
	if (!strcmp(rightsTags[i], name))
	    return i;
    }
    return 0;
}


// dups all bufs/strings. 
chit_t *chit_new(char *server, long id, long version, char *ascii_public_key_hash, char *ascii_private_key_hash)
{
    chit_t	*c = calloc(1, sizeof(chit_t));

    if (!server || !ascii_public_key_hash || !ascii_private_key_hash) return NULL;

    c->server = strdup(server);
    c->id = id;
    c->version = version;
    cry_ascii_to_hash(c->serverprint, ascii_public_key_hash);
    cry_ascii_to_hash(c->fingerprint, ascii_private_key_hash);

    return c;
}


    
void chit_add_long_attr(chit_t *c, int tag, long val_l)
{
    attr_t	*a = calloc(1, sizeof(attr_t));
    
    assert(c);
    a->tag = tag;
    a->val_l = val_l;
    if (c->attrs) {
	c->attrs_last->next = a;
    } else {
	c->attrs = a;
    }
    c->attrs_last = a;

    hash_in_string(c->fingerprint, c->fingerprint, tag, rightsTags[val_l]);
}


void chit_add_string_attr(chit_t *c, int tag, char *s)
{
    attr_t	*a = calloc(1, sizeof(attr_t));
    
    a->tag = tag;
    a->val_s = s;
    if (c->attrs) {
	c->attrs_last->next = a;
    } else {
	c->attrs = a;
    }
    c->attrs_last = a;

    hash_in_string(c->fingerprint, c->fingerprint, tag, s);
}


chit_t *chit_copy(chit_t *old)
{
    chit_t	*c = calloc(1, sizeof(chit_t));

    assert(old);
    memcpy(c, old, sizeof(chit_t));
    c->attrs = c->attrs_last = NULL;

    attr_t	*a, *b = NULL, *last = NULL;
    for (a = old->attrs; a; a = a->next) {
	b = calloc(1, sizeof(attr_t));
	if (last) 
	    last->next = b;
	else 
	    c->attrs = b;
	last = b;

	b->tag = a->tag;
	if (a->val_s)
	    b->val_s = strdup(a->val_s);
	b->val_l = a->val_l;
    }
    c->attrs_last = b;
    return c;
}



void chit_free(chit_t *chit)
{
    if (!chit) return;

    free(chit->server);

    attr_t	*next, *c;
    for (c = chit->attrs; c; c = next) {
	next = c->next;
	free(c->val_s);
	free(c);
    }
    
    free(chit);
}


// 'secret' is ascii
int chit_verify(chit_t *chit, char *secret)
{
    char	digest[HASH_SIZE];
    attr_t	*at;

    cry_ascii_to_hash(digest, secret);

    for (at = chit->attrs; at; at = at->next) {
	if (at->tag == TAG_REMOVE_RIGHT) {
	    hash_in_string(digest, digest, at->tag, rightsTags[at->val_l]);
	} else {
	    hash_in_string(digest, digest, at->tag, at->val_s);
	}
    }

    int res = memcmp(chit->fingerprint, digest, sizeof(digest));
    dfs_out("verifying chit %s\n", res ? "FAILED" : "SUCCEEDED");
    return res;
}

//=============================================================================



void hash_in_string(char *dig_new, char *dig, int tag, char *val)
{
    assert((tag > 0) && (tag <= TAG_LAST));

    char	*buf = malloc(strlen(rightsTags[tag]) + strlen(val) + 1);

    strcpy(buf, rightsTags[tag]);
    strcpy(buf + strlen(rightsTags[tag]), val);

    cry_digest_hmac_string(dig_new, dig, buf, strlen(rightsTags[tag]) + strlen(val));
    free(buf);
}


int digests_match(void *one, void *two)
{
    uint32_t	*a = one, *b = two;

    assert(HASH_SIZE == 20);
    return ((a[0] == b[0]) &&
	    (a[1] == b[1]) &&
	    (a[2] == b[2]) &&
	    (a[3] == b[3]) &&
	    (a[4] == b[4]));
}
