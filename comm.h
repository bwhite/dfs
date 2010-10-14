
#ifndef _COMM_H
#define _COMM_H

#define	_REENTRANT

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#ifndef	MIN
#define	MIN(x,y)	(((x) < (y)) ? (x) : (y))
#endif

#ifndef	MAX
#define	MAX(x,y)	(((x) > (y)) ? (x) : (y))
#endif

#define MSG_REPLY		1

#define	REPLY_OK		0
#define	REPLY_ERR		-1

typedef struct Msg {
    int		seq;
    int		type;
    int		res;
    int		len;				// of data, not including Msg
    struct Msg	*next;				// used for MT handoff
    char	data[];
} Msg;


#define MAX_MACH 		255

typedef struct Client {
    char		machine[MAX_MACH];		// client location
    int			fd;				// client socket
    int			id;				// consecutive ints, starting at 1
    pthread_t		tid;				// from pthread_create()
    void		*other;				// whatever you want
    long		seen;				// for use w/ thresholds, logs
    struct Client	*next;
} Client;

extern Client		*clients;


int 	comm_server_socket(int port);
void 	comm_server_socket_mt(int port, void *(* listener)(void *));
int	comm_client_socket(char *host, int port);

Msg    *comm_read(int sock);
int 	comm_send(int sock, int type, ...);
Msg 	*comm_send_and_reply(int sock, int type, ...);
int	comm_sendmsg(int sock, int type, struct msghdr *);
int	comm_reply(int sock, Msg *m, int res, ...);

Msg 	*comm_send_and_reply_mutex(pthread_mutex_t *mut, pthread_cond_t *cond, int sock, int type, ...);

void 	comm_register_msgtypes(int num, char **types);


void 	plog(const char *fmt, ...);
void 	perr(const char *fmt, ...);
char   *pcat(char *first, ...);
void 	pexit(const char *fmt, ...);
void 	passert(int val, const char *fmt, ...);

char	*messageStr(int type);

typedef struct {
    long		dataSent;
    long		dataReceived;
    long		msgs[];
} CommStats;

#endif
