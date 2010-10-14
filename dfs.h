
#include <sys/stat.h>
#include <sys/types.h>
#include <fuse.h>

#ifndef S_IFDIR
#  define S_IFDIR	__S_IFDIR
#  define S_IFREG	__S_IFREG
#endif

#define	DEF_DIR_MODE	(S_IFDIR | 0755)
#define	DEF_FILE_MODE 	(S_IFREG | 0644)

#define	BLOCK_SIZE	4096
#define	MAX_PATH	255

// Used by both files and dirs.
typedef struct DfsFile {
    char		path[MAX_PATH];
    struct stat		stat;
    char		*name;

    struct DfsFile	*parent;
    struct DfsFile	**child;
    int			num_children;

    size_t		len;			// even when not instantiated
    size_t		recipelen;		// length of null-term'd hashes
    long		version;
    char		*recipe;		// if exists
    char		*data;			// while open
    int			dirty;			// modified since read
} DfsFile;


typedef struct Extent {
    char		sig[A_HASH_SIZE];
    int			sz;
    int			seq;
    char		data[];
} Extent;


extern DfsFile *	root;

#define	MAX_PROCS		10

#define	EXTENT_PORT		8080
#define	LOG_PORT		8180

#define DFS_MSG_PUSH_LOG	2
#define DFS_MSG_GET_LOG		3
#define DFS_MSG_INVAL		4
#define DFS_MSG_WANT_EXCL	5
#define DFS_MSG_LOST_EXCL	6
#define DFS_MSG_PUT_EXTENT	7
#define DFS_MSG_GET_EXTENT	8
#define DFS_MSG_POLL_EXTENT	9

static char		*messages[] = {"", "MSG_REPLY", "DFS_MSG_PUSH_LOG", "DFS_MSG_GET_LOG", "DFS_MSG_INVAL", 
				       "DFS_MSG_WANT_EXCL", "DFS_MSG_LOST_EXCL", "DFS_MSG_PUT_EXTENT", 
				       "DFS_MSG_GET_EXTENT", "DFS_MSG_POLL_EXTENT" };
#define			NUM_MSG_TYPES (sizeof(messages) / sizeof(messages[0]))

Extent	*get_extent(char *sig);			// should not be free'd afterwards
char 	*put_extent(char *buf, long sz); 	// sig space should already exist

int 	dfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int 	dfs_mkdir(const char *path, mode_t mode);
int 	dfs_rmdir(const char *path);
int 	dfs_unlink(const char *path);
DfsFile *findFile(char *path);
int 	dfs_chmod(const char *path, mode_t mode);






