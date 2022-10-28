#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h> // inet_addr()
#include <sys/socket.h>

#include "libxsvf.h"

#define BUFFER_SIZE 1024

static int h_sync(struct libxsvf_host *h);

struct udata_s {
	FILE *f;
	int fd;
	int buffer_size;

	int verbose;

	uint8_t tms[BUFFER_SIZE];
	uint8_t tdi[BUFFER_SIZE];
	uint8_t tdo[BUFFER_SIZE];
	uint8_t mask[BUFFER_SIZE];
	uint8_t bit;
	int byte;

	bool error;
};

static void inc(struct libxsvf_host * h, struct udata_s * u)
{
	u->bit <<= 1;
	if (u->bit == 0) {
		u->bit = 0x1;
		u->byte++;
		if (u->byte == BUFFER_SIZE)
			h_sync(h);
	}
}

static void reset(struct udata_s * u)
{
	u->bit = 0x1;
	u->byte = 0;
	memset(u->tms, 0, BUFFER_SIZE);
	memset(u->tdi, 0, BUFFER_SIZE);
	memset(u->tdo, 0, BUFFER_SIZE);
	memset(u->mask, 0, BUFFER_SIZE);
}

static void h_udelay(struct libxsvf_host *h, long usecs, int tms, long num_tck)
{
	printf("h_udelay: %ld, %d, %ld\n", usecs, tms, num_tck);
	struct udata_s *u = h->user_data;
	for (int i = 0; i < num_tck; i++) {
		if (tms == 1)
			u->tms[u->byte] |= u->bit;
		inc(h, u);
		if (u->error)
			break;
	}
	h_sync(h);
}

static int h_setup(struct libxsvf_host *h)
{
	struct udata_s * u = h->user_data;
	reset(u);

	u->verbose = 0;
	u->error = false;

	return 0;
}

static int h_shutdown(struct libxsvf_host *h)
{
	return h_sync(h);
}

static int h_getbyte(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	return fgetc(u->f);
}

static int h_sync(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	uint32_t bits = u->byte * 8 + __builtin_ctz(u->bit);
	int bytes = (bits + 7) / 8;

	if (bits == 0)
		return 0;
	if (u->error)
		return -1;
	printf("sending %d bits in %d bytes\n", bits, bytes);
	/*printf("data: ");
	for (int i = 0; i < bytes; i++)
		printf("%x/%x/%x ", u->tms[i], u->tdi[i], u->tdo[i]);
	printf("\n");*/

	write(u->fd, "shift:", 6);
	write(u->fd, (void*)&bits, 4);
	write(u->fd, u->tms, bytes);
	write(u->fd, u->tdi, bytes);

	uint8_t tdo[BUFFER_SIZE];
	read(u->fd, tdo, bytes);

	for (int i = 0; i < bytes; i++)
		if (u->tdo[i] != (tdo[i] & u->mask[i])) {
			printf("tdo check failed: i %d, received %x, mask %x, should %x\n", i, tdo[i], u->mask[i], u->tdo[i]);
			u->error = true;
			return -1;
		}
	
	reset(u);

	return 0;
}

static int h_pulse_tck(struct libxsvf_host *h, int tms, int tdi, int tdo, int rmask, int sync)
{
	//printf("h_pulse_tck: %d, %d, %d, %d, %d\n", tms, tdi, tdo, rmask, sync);
	struct udata_s *u = h->user_data;

	if (u->error)
		return -1;

	if (tms == 1)
		u->tms[u->byte] |= u->bit;
	if (tdi == 1)
		u->tdi[u->byte] |= u->bit;
	if (tdo == 1)
		u->tdo[u->byte] |= u->bit;
	if (tdo != -1)
		u->mask[u->byte] |= u->bit;

	inc(h, u);
	if (u->error)
		return -1;
	
	if (sync)
		return h_sync(h);
	return 0;
}

static int h_set_frequency(struct libxsvf_host *h, int v)
{
	return 0;
}

static void h_report_tapstate(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 2)
		printf("[%s]\n", libxsvf_state2str(h->tap_state));
}

static void h_report_device(struct libxsvf_host *h, unsigned long idcode)
{
	printf("idcode=0x%08lx, revision=0x%01lx, part=0x%04lx, manufactor=0x%03lx\n", idcode, (idcode >> 28) & 0xf, (idcode >> 12) & 0xffff, (idcode >> 1) & 0x7ff);
}

static void h_report_status(struct libxsvf_host *h, const char *message)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 1)
		printf("[STATUS] %s\n", message);
}

static void h_report_error(struct libxsvf_host *h, const char *file, int line, const char *message)
{
	fprintf(stderr, "[%s:%d] %s\n", file, line, message);
}

static void *h_realloc(struct libxsvf_host *h, void *ptr, int size, enum libxsvf_mem which)
{
	return realloc(ptr, size);
}

static struct udata_s u = {
};

static struct libxsvf_host h = {
	.udelay = h_udelay,
	.setup = h_setup,
	.shutdown = h_shutdown,
	.getbyte = h_getbyte,
	.sync = h_sync,
	.pulse_tck = h_pulse_tck,
	.set_frequency = h_set_frequency,
	.report_tapstate = h_report_tapstate,
	.report_device = h_report_device,
	.report_status = h_report_status,
	.report_error = h_report_error,
	.realloc = h_realloc,
	.user_data = &u
};


static int my_connect();
int main(int argc, char * argv[])
{
	u.fd = my_connect();
	u.f = fopen("jtag.xsvf", "rb");
	libxsvf_play(&h, LIBXSVF_MODE_XSVF);

}

static int my_connect()
{
	int sockfd;
	struct sockaddr_in servaddr;

	// socket create and verification
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		printf("socket creation failed...\n");
		return -1;
	}
	else
		printf("Socket successfully created..\n");
	bzero(&servaddr, sizeof(servaddr));

	// assign IP, PORT
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(2542);

	// connect the client socket to server socket
	if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
		printf("connection with the server failed...\n");
		return -1;
	}
	else
		printf("connected to the server..\n");

	return sockfd;
}
