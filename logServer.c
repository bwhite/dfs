#define FUSE_USE_VERSION  26
   
#include "utils.h"
#include "dfs.h"
#include "comm.h"
#include "log.h"
#include <unistd.h>

Log				opLog;
static pthread_mutex_t		serverMut = PTHREAD_MUTEX_INITIALIZER;
Client *my_clients;
char my_serverprint[] = "FABBD729BCD7AAD7F557B494746E814136FD1A6A";
char my_public_key[] = "(public-key (rsa (n #00A8642A89E77DCE76E04140CA45712493262E01CBA412E6F9CF16AA16F31BF0F6EA79244EA9D99978060217312473F6CC946E1F49FACB6D8542EA7581122B3E4C5DB4985963219773CD09362FBA67525C10E0BDA01A0490D41020CA3C80E346E3C5DCCDCB8A9A2C613243807C25DB672093DFC14D3E808632480057403B4EEDDB#) (e #010001#)))";
char my_private_key[] = "(private-key (rsa (n #00A8642A89E77DCE76E04140CA45712493262E01CBA412E6F9CF16AA16F31BF0F6EA79244EA9D99978060217312473F6CC946E1F49FACB6D8542EA7581122B3E4C5DB4985963219773CD09362FBA67525C10E0BDA01A0490D41020CA3C80E346E3C5DCCDCB8A9A2C613243807C25DB672093DFC14D3E808632480057403B4EEDDB#)(e #010001#)(d #08647DA3993B06F2F12304C4A55E09E6F4EC8415B9DC0B5100B0EE274354982CE640C568CF99A87177F330B9629F63A48C9D49C7EE77A7176634B89DDC8C4A882A76C905038CD6A34B76A6F753F18822391BA9EB26CECC147ECB86E005D50F3B37825DBA5672D7E74247FA499E826F7B802DB87D14938EEB0685311E27983351#)(p #00CB74A0305B60EE9C801610F721A530229C1211FA591968A9DD74C12604ED261289D34F1CA9A332DC8A4267BAAD97DD139C4C66576E803BB4AD1AE2A061B0AF49#)(q #00D3E147BA1C531A294CA3F3DF4EE333B8AAA8F84972E9BA92BBFBDFA2EDFDB9C21A2F7D02197C3CC8479C358D1425E9FCB10BEC348F57FAF8E849B9DADCF5E003#)(u #7ACE3777A3C5DE550657BC19F4B462C4803DE86AA1878C895CFDB4E2F5780DDF7316EC424727FBF07892C4B9020E3D4FC76D6A88EB369A4765AC5EB6FBBDF448#)))";

//=============================================================================


// re-alloc log, if necessary, to ensure room
void *checkLogSpace(int newbytes)
{
    int new_size = opLog.used + newbytes;
    opLog.data = realloc(opLog.data, new_size);
    opLog.alloced = new_size;
}

// flush logs received from clients
static void serverFlush(int force)
{
    /* Assumes mutex is locked */
    // flush to disk
  fseek(opLog.file_fd, 0, SEEK_SET);
  {
    char *serialized;
    size_t serialized_sz;  
    assert(tuple_serialize_log(&serialized, &serialized_sz, opLog.data, opLog.used) == 0);
    fwrite(serialized, serialized_sz, 1, opLog.file_fd);
    free(serialized);
  }
  fflush(opLog.file_fd);
}


static void exit_proc() {
    dfs_out("EXIT_PROC!!!\n");
    serverFlush(1);
}


// automatically rotate through LOG_SERVER names
void logNames(char *base, char **iname, char **oname)
{
    struct stat		dummy;
    char		s[MAX_PATH];

    if (!*iname) {
	int		i=0;

	do {
	    sprintf(s, "%s%03d", base, i++);
	} while (!lstat(s, &dummy));
	if (i > 1) {
	    sprintf(s, "%s%03d", base, i-2);
	    *iname = strdup(s);
	}
    }
    if (*iname && (**iname == '-'))
	*iname = NULL;
    
    if (!*oname) {
	int		i=-1;

	do {
	    sprintf(s, "%s%03d", base, ++i);
	} while (!lstat(s, &dummy));
	*oname = strdup(s);
    }
    if (*iname && !strcmp(*iname, *oname))
	dfs_die("input and output log names must differ\n");

    dfs_out("LOG_NAMES: input '%s', output '%s'\n", *iname ? *iname : "", *oname ? *oname : "");
}


static void logInit(char *iname, char *oname, int sport)
{
    logNames("LOG_SERVER", &iname, &oname);
    opLog.id = 0;
    if ((opLog.file_fd = fopen(oname, "w")) < 0)
	dfs_die("No open log '%s' for writing\n", oname);

    if (iname) {

	int		fd;
	dfs_out("Reading from '%s', init ID %d\n",iname, opLog.id);
	if ((fd = open(iname, O_RDONLY)) < 0) {
	    fprintf(stderr, "Can't open '%s'\n", iname);
	    exit(1);
	}
	
	struct stat		stat;
	fstat(fd, &stat);
    
	assert(!opLog.data);
	if (stat.st_size) {
	    char *log;
	    size_t log_sz;
	    char *serialized = malloc(stat.st_size);
	    read(fd, serialized, stat.st_size);
	    close(fd);
	    assert(tuple_unserialize_log(&log, &log_sz, serialized, stat.st_size) == 0);
	    free(serialized);
	    opLog.data = log;
	    opLog.alloced = opLog.used = log_sz;

	    // rip through log to find last id used :-(
	    {
	      char *data = opLog.data;
	      char *end = opLog.data + opLog.used;
	      char *prev = NULL;
	      while (data < end) {
		prev = data;
		data += ((LogHdr *)data)->len;
	      }
	      if (prev)
		opLog.id = ((LogHdr *)prev)->id;
	      else
		opLog.id = 0;
	    }
	    dfs_out("StartID[%d]\n", opLog.id);
	    //long	back = ((long *)(opLog.data + opLog.used))[-1];
	    //opLog.id = ((LogHdr *)(opLog.data + opLog.used - back))->id;
	    dfs_out("StartID[%d]\n", opLog.id);
	    int cur_size = stat.st_size;
	    dfs_out("Read %d bytes from '%s', last ID %d\n", cur_size, iname, opLog.id);
	}
    }

    atexit(exit_proc);
}

static void *listen_proc(void *arg) 
{
    Client	*c = arg;
    // Make chain
    pthread_mutex_lock(&serverMut);
    dfs_out("Adding client[%p]\n", my_clients);
    if (my_clients == NULL) {
	my_clients = c;
	dfs_out("Added first client\n");
    } else {
	Client *cur_c = my_clients;
	while (cur_c->next != NULL) {
	    cur_c = cur_c->next;
	}
	cur_c->next = c;
	dfs_out("Added another\n");
    }
    c->next = NULL;
    pthread_mutex_unlock(&serverMut);

    dfs_out("\n\tLISTEN PROC IN, sock %d! (id %d, tid %d)\n\n", c->fd, c->id, c->tid);

    pthread_detach(pthread_self());
    // Start auth vars
    int auth_state = 0; // -1: Client is authed 0: Didn't say Hai yet 1: Said Hai
    char server_nonce;
    // Stop auth vars
    Msg *m;
    while (m = comm_read(c->fd)) {
	Extent		*ex;
	char		*sig, *data;
	switch (m->type) {
	case DFS_OHAI_SERVER:
	  {
	    assert(auth_state == 0);
	    auth_state++;
	    /* 0: Make a nonce (1 char), send PK and nonce */
	    cry_create_nonce(1, &server_nonce);
	    printf("Server nonce[%d]\n", (int)server_nonce);
	    char out_nonce = server_nonce;
	    printf("Client said [O Hai!] along with this picture http://is.gd/hyjIm\n");
	    printf("Lets give him a nonce[%d] and pk[%s]\n", (int)out_nonce, my_public_key);
	    comm_reply(c->fd, m, REPLY_OK, &out_nonce, 1, my_public_key, strlen(my_public_key), NULL);
	  }
	  break;
	case DFS_TAKE_CHIT_SERVER:
	  {
	    assert(auth_state == 1);
	    auth_state++;
	    /* 1: */
	    char *encrypted_sym = m->data;
	    int encrypted_chit_bundle_sz = m->len - (strlen(encrypted_sym) + 1);
	    char *encrypted_chit_bundle = m->data + strlen(encrypted_sym) + 1;
	    // Decrypt sym
	    char *sym;
	    int sym_sz;
	    cry_asym_decrypt(&sym, &sym_sz, encrypted_sym, strlen(encrypted_sym), my_private_key);
	    char *chit_bundle;
	    int chit_bundle_sz;
	    printf("Sym Key[%x%x%x%x]\n", (sym), (sym + 4), (sym + 8), (sym + 12));
	    cry_sym_init(sym); // TODO May need to lock here for multiple clients
	    cry_sym_decrypt(&chit_bundle, &chit_bundle_sz, encrypted_chit_bundle, encrypted_chit_bundle_sz);
	    char c_client_nonce = chit_bundle[0];
	    char c_server_nonce = chit_bundle[1];
	    char *c_chit = chit_bundle + 2;
	    // TODO Verify chit
	    printf("Nonce check[%d][%d]\n", (int)c_server_nonce, (int)server_nonce);
	    //assert(c_server_nonce == server_nonce); // TODO just reply error
	    printf("client[%d] server[%d] Chit![%s]\n", (int)c_client_nonce, (int)c_server_nonce, c_chit);
	    char out_nonce = c_client_nonce + 1;
	    printf("Incremented client[%d][%d]", (int)c_client_nonce, (int)out_nonce);
	    char *out_nonce_encrypted;
	    int out_nonce_encrypted_sz;
	    cry_sym_encrypt(&out_nonce_encrypted, &out_nonce_encrypted_sz, &out_nonce, 1);
	    comm_reply(c->fd, m, REPLY_OK, out_nonce_encrypted, out_nonce_encrypted_sz, NULL);
	  }
	  break;
	case DFS_MSG_GET_LOG:
	    {
	      assert(auth_state == -1);
		dfs_out("Get wants lock\n");
		pthread_mutex_lock(&serverMut);
		dfs_out("Get locked\n");
		{
		  char *serialized;
		  size_t serialized_sz;  
		  tuple_serialize_log(&serialized, &serialized_sz, opLog.data, opLog.used);
		  comm_reply(c->fd, m, REPLY_OK, serialized, serialized_sz, NULL);
		  free(serialized);
		}
		pthread_mutex_unlock(&serverMut);
		dfs_out("Get Unlocked\n");
		break;
	    }
	case DFS_MSG_PUSH_LOG:
	    {
	      assert(auth_state == -1);
	      char *log;
	      size_t log_sz;
	      assert(tuple_unserialize_log(&log, &log_sz, m->data, m->len) == 0);
	      int cur_len = ((LogHdr*)log)->len;
	      dfs_out("Pushed Length[%d]\n", cur_len);
	      dfs_out("Push wants lock\n");
	      pthread_mutex_lock(&serverMut);
	      dfs_out("Push locked\n");
	      char *start, *stop;
	      comm_reply(c->fd, m, REPLY_OK, NULL);
	      // Append to log
	      ((LogHdr *)log)->id = ++opLog.id; // Forces all commits to be sequential
	      dfs_out("Record gets id[%d]\n", opLog.id);
	      checkLogSpace(log_sz);
	      memcpy(opLog.data + opLog.used, log, log_sz);
	      opLog.used = opLog.used + log_sz;
	      // flush to other clients
	      Client *cur_c = my_clients;
	      while(cur_c != NULL) {
		dfs_out("Sending again[%p]...\n", cur_c);
		// Send serialized version
		comm_send(cur_c->fd, DFS_MSG_PUSH_LOG, m->data, m->len, NULL);
		cur_c = cur_c->next;
	      }
	      dfs_out("Done sending... Flushing server\n");
	      serverFlush(1);
	      pthread_mutex_unlock(&serverMut);
	      dfs_out("Push Unlocked\n");
	      free(log);
	    }
	    break;
	default:
	    dfs_die("BAD MSG TYPE %d\n", m->type);
	}

	free(m);
    }
    free(c);
    return NULL;
}


int main(int argc, char *argv[])
{
    int			i, c;
    char		*iname = NULL;
    char		*oname = NULL;
    int			port = LOG_PORT;

    while ((c = getopt(argc, argv, "i:o:p:c")) != -1) {
	switch (c) {
	case 'i':
	    iname = optarg;
	    break;
	case 'o':
	    oname = optarg;
	    break;
	case 'p':
	    port = atoi(optarg);
	    break;
	default:
	    fprintf(stderr, "USAGE: %s [-i <input log name>]\n"
		    "\t[-o <output log name>]\n",
		    argv[0]);
	    exit(1);
	}
    }
    my_clients = NULL;
    logInit(iname, oname, port);

    comm_register_msgtypes(sizeof(messages) / sizeof(messages[0]), messages);

    comm_server_socket_mt(port, listen_proc);
}
