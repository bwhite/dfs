
// incr quantity to next double boundary if necessary

typedef struct {
    int			type;
    int			id;
    int			version;
    long		len;
    in_addr_t		creator;	// ip addr
} LogHdr;


typedef struct {
    LogHdr		hdr;
    time_t		mtime;
    long		recipelen;
    long		flags;	// or mode
    long		flen;
    //  char		recipe[];
    //	path, null-terminated
} LogFileVersion;


typedef struct {
    LogHdr		hdr;
    int			flags;
    time_t		mtime;
    //	path, null-terminated
} LogOther;


// types of log events
#define	LOG_NOTE		1
#define LOG_FILE_VERSION	2
#define LOG_UNLINK		3
#define LOG_MKDIR		4
#define LOG_RMDIR		5
#define LOG_CHMOD		6
#define LOG_MACHINE		8	// must be last for printLog.c


typedef struct {
    char	*data;// Log data
    long	alloced; // Amount of space allocated
    long	used; // Amount of space used
    long	id; // Last ID used
    long	saved; // 
    long	served; // Amount sent to the server (only used by client)
    int		file_fd;
    int		net_fd;
} Log;

extern Log	opLog;

void 	logFileVersion(DfsFile *f);
void 	logExtent(char *sig, int sz);
void 	logOther(int type, const char *path, int flags, struct stat *stat);
void	logRecreate(char *lname);
void 	playLog(char *buf, int len);
