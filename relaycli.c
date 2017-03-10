#if 0
(
set -e
declare -r tmp='./relaycli'
declare -r addr=':: 3031'
case "${1:-}" in

win1)

(sleep 0.5; printf "%s\0" \
	TEST2 entr hello \
	TEST2 hudo "how're you doing?" \
	TEST2 leav bye \
) | \
valgrind --quiet --leak-check=full --track-origins=yes "$tmp" -0vvr $addr TEST1

;;

win2)

(sleep 1; printf "%s\0" \
	TEST1 entr hello \
	TEST1 hido "busy testing, go away" \
	TEST1 leav bye \
) | \
valgrind --quiet --leak-check=full --track-origins=yes "$tmp" -0vvr $addr TEST2

;;

"")
# trap "rm -f -- '$tmp'" EXIT ERR
gcc -Wall -Wextra -Werror -g -O3 -std=gnu11 -DSIMPLE_LOGGING -Ic_modules -o "$tmp" $(find -name '*.c') -lpthread
tmux split-window -d bash "$0" win1
tmux split-window -d bash "$0" win2
tmux select-layout even-vertical
DUMP=yes node c_modules/relay/server.js
;;

*)
echo "Invalid parameter"
exit 1
;;

esac
)
exit 0
#endif
#include <cstd/std.h>
#include <cstd/unix.h>
#include <relay/relay_client.h>
#include <getdelim/getdelim.h>
#include <debug/hexdump.h>

static void show_help(const char *name)
{
	printf("Syntax: %s [-vv0r] <server-addr> <server-port> <node-name>\n"
		"\n"
		"Where:\n"
		"\t-v\tIncrease verbosity\n"
		"\t-0\tLines are null-terminated\n"
		"\t-r\tReceive packets also (dumped to stdout)\n"
		"\n"
		"Standard input consists of 3-tuples, one item per line:\n"
		" 1. target node name\n"
		" 2. packet type\n"
		" 3. packet data\n"
		"\n", name);
}

static struct relay_client client;
static char delim = '\n';
static int verbosity;
static bool rx;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static pthread_t rx_tid;

static void *rx_thread(void *p)
{
	(void) p;
	pthread_mutex_lock(&mx);
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&mx);
	struct relay_packet *packet;
	while (relay_client_recv_packet(&client, &packet) && packet != NULL) {
		if (verbosity >= 2) {
			log_info("Packet of type '%s' received from '%s' containing %zu bytes of data", packet->type, packet->remote, packet->length);
			hexcat(packet->data, packet->length, 0);
		}
		printf("%s%c%s%c%.*s%c", packet->remote, delim, packet->type, delim, (int) packet->length, packet->data, delim);
		free(packet);
	}
	if (verbosity >= 1) {
		log_info("Reception ended");
	}
	return NULL;
}

static void tx_thread()
{
	struct fstr target = FSTR_INIT;
	struct fstr type = FSTR_INIT;
	struct fstr data = FSTR_INIT;
	while (fstr_getdelim(&target, delim, stdin) && fstr_getdelim(&type, delim, stdin) && fstr_getdelim(&data, delim, stdin)) {
		if (verbosity == 1) {
			log_info("Sending packet type '" PRIfs "' to node '" PRIfs "' containing %zu bytes of data", prifs(&type), prifs(&target), fstr_len(&data));
		} else if (verbosity >= 2) {
			log_info("Sending packet type '" PRIfs "' to node '" PRIfs "' containing %zu bytes of data: <" PRIfs ">", prifs(&type), prifs(&target), fstr_len(&data), prifs(&data));
		}
		if (!relay_client_send_packet(&client, fstr_get(&type), fstr_get(&target), fstr_get(&data), fstr_len(&data))) {
			log_error("Failed to send packet");
			break;
		}
	}
	fstr_destroy(&data);
	fstr_destroy(&type);
	fstr_destroy(&target);
}

int main(int argc, char *argv[])
{
	int ch;
	while ((ch = getopt(argc, argv, "v0r")) != -1) {
		switch (ch) {
		case '0': delim = 0; break;
		case 'v': verbosity++; break;
		case 'r': rx = true; break;
		case '?': show_help(argv[0]); return 1;
		}
	}
	if (argc - optind != 3) {
		show_help(argv[0]);
		return 1;
	}
	const char *addr = argv[optind+0];
	const char *port = argv[optind+1];
	const char *name = argv[optind+2];
	if (verbosity > 0) {
		log_info("Connecting to %s:%s as '%s'", addr, port, name);
	}
	if (!relay_client_init_socket(&client, name, addr, port) != 0) {
		log_error("Failed to connect to %s:%s as %s", addr, port, name);
		return 2;
	}
	if (rx) {
		pthread_mutex_lock(&mx);
		pthread_create(&rx_tid, NULL, rx_thread, NULL);
		pthread_cond_wait(&cv, &mx);
		pthread_mutex_unlock(&mx);
	}
	tx_thread();
	if (rx) {
		if (verbosity >= 1) {
			log_info("Transmission ended, only receiving data from now");
		}
		pthread_join(rx_tid, NULL);
	}
	relay_client_destroy(&client);
	return 0;
}
