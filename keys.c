
//
//   Pete's RAM filesystem. No persistance, no links, slow linear searches.
//   Basically, this was an afternoon's hack, so be nice.
//

// Blech. Wrote this first on a mac, easy. Getting to compile on linux much
// harder. Right now, mac is the #else of the linux ifdefs. To get to compile
// on linux, download all fuse source, cp hellofs.c into the examples subdir,
// modify the makefile to do everything for hellofs that it does for hello,
// for example, and then it works. Might also want to remove  -Wmissing-declarations.
// 
// In both cases, run by "./dfs /tmp/hello" (after you create the dir),
// kill by 'killall dfs'.
//
   
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

#include "dfs.h"
#include <fuse.h>

#include "utils.h"
#include "comm.h"
#include "log.h"
#include "tuple.h"
#include "cry.h"
#include "chits.h"


char		*sessionKey;		// not used



static void writefile(char *fname, char *s)
{
    int		fd;

    if (!(fd = open(fname, O_TRUNC | O_CREAT | O_WRONLY, 0666))) {
	fprintf(stderr, "No write to file \'%s\'\n", fname);
	exit(1);
    }
    write(fd, s, strlen(s));
    close(fd);
}


int main(int argc, char **argv)
{
    chit_t	*c;
    char	*buf, *buf2;
    int		fd;
    struct stat	stat;
    extern int	dfsbug;

    dfsbug = 0;

    if ((argc == 4) && !strncmp(argv[1], "pub", 3)) {
	char	*pk1, *sk1;

        cry_asym_create_keys(&pk1, &sk1);
        writefile(argv[2], pk1);
        writefile(argv[3], sk1);
    }
    else if ((argc == 3) && !strncmp(argv[1], "hash", 3)) {
	if (((fd = open(argv[2], O_RDONLY)) <= 0) ||
	    fstat(fd, &stat)) goto usage;
		buf = calloc(1, stat.st_size);
	read(fd, buf, stat.st_size);
	buf2 = cry_hash_key(buf);
	printf("%s\n", buf2);
    }
    else if ((argc == 8) && !strncmp(argv[1], "chit", 3)) {
	// public key
	buf = read_text_file(argv[5]);
	buf = cry_hash_key(buf);

	// private key
	printf("arg[6]=[%s]\n", argv[6]);
	buf2 = read_text_file(argv[6]);
	printf("buf2[%x]\n", buf2);
	buf2 = cry_hash_key(buf2);

	// chit_new(char *server, long id, long version, char *public_hash, char *private_hash)
	c = chit_new(argv[2], atol(argv[3]), atol(argv[4]), buf, buf2);
	chit_save(c, argv[7]);
    }
    else if ((argc >= 6) && !(argc % 2) && !strncmp(argv[1], "derive", 3) &&
	     (c = chit_read(argv[2]))) {
	int		i;

	for (i = 4; i < argc; i += 2) {
	    int right, name = tagname_to_int(argv[i]);
	    char *key, *hash, *val = argv[i+1];
	    switch (name) {
	    case TAG_REMOVE_RIGHT:
		right = tagname_to_int(val);
		chit_add_long_attr(c, TAG_REMOVE_RIGHT, right);
		break;
	    case TAG_NARROW:
		chit_add_string_attr(c, name, val);
		break;
	    case TAG_PUBLIC_KEY:
		// use fingerprint of key
		key = read_text_file(val);
		if (!key) dfs_die("No read public key\n");
		hash = cry_hash_key(key);
		if (!hash) dfs_die("No find public key\n");
		chit_add_string_attr(c, name, hash);
		break;
	    }
	}
	chit_save(c, argv[3]);
    }
    else {
    usage:
	fprintf(stderr, 
		"USAGE:\t%s public <pub fname> <sec fname>\n"
		"\t%s hash <key file>\n"
		"\t%s chit <server> <id> <vers> <pubfile> <secfile> <chitfile>\n"
		"\t%s derive <inchitfile> <outchitfile> <tagname> <val>\n",
		argv[0], argv[0], argv[0], argv[0]);
	exit(1);
    }

    return 0;
}
