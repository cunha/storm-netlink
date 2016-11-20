/* This program generates a netlink broadcast storm by creating and
 * deleting routes for prefix STORM_PREFIX_FMT/STORM_PFXLEN on
 * interface STORM_IFNAME.  Routes are created on table
 * STORM_TABLENUM.  */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <arpa/inet.h>

#include <linux/rtnetlink.h>

// #include <libmnl/libmnl.h>  /* useful for debugging */

#define NETLINK_BUFSZ (1<<15)
#define STORM_TABLENUM 152
#define STORM_IFNAME "lo"
#define STORM_PREFIX_FMT "10.0.%d.0"
#define STORM_PFXLEN 24

/* define LOGX {{{*/
#define LOGX(x) { \
	char buf[256]; strerror_r(errno, buf, 256); \
	fprintf(stderr, "%s:%d %s [%s]\n", __FILE__, __LINE__, \
			(const char *)(x), buf); \
	exit(EXIT_FAILURE); \
}
/*}}}*/

int nl_open_bind_safe(struct sockaddr_nl *addrout)/*{{{*/
{
	if(addrout == NULL) LOGX("addrout cannot be NULL");

	int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if(nl == -1) LOGX("error opening netlink socket");

	memset(addrout, 0, sizeof(*addrout));
	addrout->nl_family = AF_NETLINK;
	addrout->nl_pid = getpid();
	addrout->nl_groups = 0;
	int r = bind(nl, (const struct sockaddr *)addrout, sizeof(*addrout));
	if(r == -1) LOGX("error binding netlink socket");

	return nl;
}/*}}}*/

void nl_send_safe(int nl, struct nlmsghdr *nlh)/*{{{*/
{
	const struct sockaddr_nl dst = { .nl_family = AF_NETLINK };
	const struct sockaddr *dstptr = (const struct sockaddr *)&dst;
	ssize_t s = sendto(nl, nlh, nlh->nlmsg_len, 0, dstptr, sizeof(dst));
	if(s != nlh->nlmsg_len) LOGX("error sending data to kernel");
}/*}}}*/

struct nlmsghdr * nl_rtadd_prepare(void *buf, int outputif, uint32_t **dstptr)/*{{{*/
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
	uint8_t *payload = NLMSG_DATA(nlh);

	struct rtmsg *rtm = (struct rtmsg *)payload;
	rtm->rtm_family = AF_INET;
	rtm->rtm_dst_len = STORM_PFXLEN;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = 0;
	rtm->rtm_protocol = RTPROT_STATIC;
	rtm->rtm_table = STORM_TABLENUM;
	rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_flags = 0;

	size_t payloadsz = sizeof(*rtm);
	struct rtattr *rta = (struct rtattr *)(payload + payloadsz);
	rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
	rta->rta_type = RTA_DST;
	*(uint32_t *)RTA_DATA(rta) = 0;
	assert(RTA_OK(rta, rta->rta_len));
	*dstptr = (uint32_t *)RTA_DATA(rta);
	payloadsz += RTA_LENGTH(sizeof(uint32_t));

	rta = (struct rtattr *)(payload + payloadsz);
	rta->rta_len = RTA_LENGTH(sizeof(int));
	rta->rta_type = RTA_OIF;
	*(int *)RTA_DATA(rta) = outputif;
	assert(RTA_OK(rta, rta->rta_len));
	payloadsz += RTA_LENGTH(sizeof(uint32_t));

	nlh->nlmsg_len = NLMSG_LENGTH(payloadsz);
	nlh->nlmsg_type = RTM_NEWROUTE;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;
	nlh->nlmsg_seq = 1;
	nlh->nlmsg_pid = (__u32)getpid();
	assert(NLMSG_OK(nlh, NETLINK_BUFSZ));

	return nlh;
}
/*}}}*/

int seqnum = 0;

int main(int argc, char **argv)
{
	char buf[NETLINK_BUFSZ];
	unsigned outputif = if_nametoindex(STORM_IFNAME);
	if(outputif == 0) LOGX("could not find target interface");

	uint32_t *dstptr;
	struct nlmsghdr *nlmsg = nl_rtadd_prepare(buf, outputif, &dstptr);
	assert((void *)dstptr > (void *)buf);
	assert((char *)dstptr < (char *)buf + nlmsg->nlmsg_len);

	struct sockaddr_nl nladdr; int nl = nl_open_bind_safe(&nladdr);

	uint64_t last_seqnum_printed = 0;
	bool create = true;
	while(true) {
		nlmsg->nlmsg_type = (create) ? RTM_NEWROUTE : RTM_DELROUTE;
		for(int i = 0; i < UINT8_MAX; ++i) {
			uint32_t prefix;
			char addr[INET_ADDRSTRLEN];
			snprintf(addr, INET_ADDRSTRLEN, STORM_PREFIX_FMT, i);
			inet_pton(AF_INET, addr, &prefix);
			*dstptr = prefix;
			++(nlmsg->nlmsg_seq);
			// mnl_nlmsg_fprintf(stdout, nlmsg, nlmsg->nlmsg_len,
			//		sizeof(struct rtmsg));
			nl_send_safe(nl, nlmsg);
		}
		create = !create;
		if(nlmsg->nlmsg_seq - last_seqnum_printed > 100000) {
			printf("sent %d messages\n", nlmsg->nlmsg_seq);
			last_seqnum_printed += 100000;
		}
	}

	close(nl);
	exit(EXIT_SUCCESS);
}
