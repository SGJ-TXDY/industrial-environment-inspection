#define _DEFAULT_SOURCE
#include "can_bus.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <linux/can/netlink.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define RESTART_MS 100

static int link_up(const char *ifname, int up)
{
    struct ifreq r;
    int fd = socket(AF_INET, SOCK_DGRAM, 0), ret = -1;
    if (fd < 0)
        return -1;
    memset(&r, 0, sizeof r);
    snprintf(r.ifr_name, sizeof r.ifr_name, "%s", ifname);
    if (!ioctl(fd, SIOCGIFFLAGS, &r)) {
        if (up)
            r.ifr_flags |= IFF_UP;
        else
            r.ifr_flags &= ~IFF_UP;
        ret = ioctl(fd, SIOCSIFFLAGS, &r);
    }
    close(fd);
    return ret;
}

static struct rtattr *attr(struct nlmsghdr *n, size_t max, int type,
                           const void *data, size_t len)
{
    struct rtattr *a = (struct rtattr *)((char *)n + NLMSG_ALIGN(n->nlmsg_len));
    size_t total = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(RTA_LENGTH(len));
    if (total > max)
        return NULL;
    a->rta_type = type;
    a->rta_len = RTA_LENGTH(len);
    if (len)
        memcpy(RTA_DATA(a), data, len);
    n->nlmsg_len = total;
    return a;
}

static void nest_end(struct nlmsghdr *n, struct rtattr *a)
{
    a->rta_len = (char *)n + n->nlmsg_len - (char *)a;
}

/* Same request as "ip link set IF type can bitrate N restart-ms 100".
 * Requires CONFIG_CAN_CALC_BITTIMING and the interface to be down.
 */
static int set_bitrate(const char *ifname, unsigned int bitrate)
{
    struct {
        struct nlmsghdr n;
        struct ifinfomsg i;
        char attrs[256];
    } req;
    char reply[512];
    struct can_bittiming timing;
    struct rtattr *info, *data;
    struct nlmsghdr *h = (struct nlmsghdr *)reply;
    struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
    __u32 restart = RESTART_MS;
    int fd, index = if_nametoindex(ifname);
    ssize_t n;
    if (!index)
        return -1;
    memset(&req, 0, sizeof req);
    memset(&timing, 0, sizeof timing);
    timing.bitrate = bitrate;
    req.n.nlmsg_len = NLMSG_LENGTH(sizeof req.i);
    req.n.nlmsg_type = RTM_NEWLINK;
    req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    req.n.nlmsg_seq = 1;
    req.i.ifi_family = AF_UNSPEC;
    req.i.ifi_index = index;
    if (!(info = attr(&req.n, sizeof req, IFLA_LINKINFO, NULL, 0)) ||
        !attr(&req.n, sizeof req, IFLA_INFO_KIND, "can", 3) ||
        !(data = attr(&req.n, sizeof req, IFLA_INFO_DATA, NULL, 0)) ||
        !attr(&req.n, sizeof req, IFLA_CAN_BITTIMING, &timing, sizeof timing) ||
        !attr(&req.n, sizeof req, IFLA_CAN_RESTART_MS, &restart, sizeof restart)) {
        errno = ENOBUFS;
        return -1;
    }
    nest_end(&req.n, data);
    nest_end(&req.n, info);
    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0)
        return -1;
    if (sendto(fd, &req, req.n.nlmsg_len, 0,
               (struct sockaddr *)&kernel, sizeof kernel) < 0) {
        close(fd);
        return -1;
    }
    n = recv(fd, reply, sizeof reply, 0);
    close(fd);
    if (n < 0)
        return -1;
    if (!NLMSG_OK(h, (size_t)n) || h->nlmsg_type != NLMSG_ERROR) {
        errno = EPROTO;
        return -1;
    }
    if (((struct nlmsgerr *)NLMSG_DATA(h))->error) {
        errno = -((struct nlmsgerr *)NLMSG_DATA(h))->error;
        return -1;
    }
    return 0;
}

int can_bus_open(struct can_bus *c, const char *ifname, unsigned int bitrate)
{
    struct sockaddr_can address;
    struct can_filter filter[2] = {
        { INSPECT_RESULT_ID, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG },
        { INSPECT_HEARTBEAT_ID, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG },
    };
    can_err_mask_t errors = CAN_ERR_BUSOFF | CAN_ERR_CRTL | CAN_ERR_RESTARTED;
    int index = if_nametoindex(ifname);
    c->fd = -1;
    c->state = BUS_CLOSED;
    if (!index)
        return -1;
    if (bitrate) {
        link_up(ifname, 0);
        if (set_bitrate(ifname, bitrate)) {
            fprintf(stderr, "CAN: set %s bitrate %u: %s\n",
                    ifname, bitrate, strerror(errno));
            return -1;
        }
    }
    if (link_up(ifname, 1))
        return -1;
    c->fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (c->fd < 0)
        return -1;
    memset(&address, 0, sizeof address);
    address.can_family = AF_CAN;
    address.can_ifindex = index;
    if (fcntl(c->fd, F_SETFL, fcntl(c->fd, F_GETFL) | O_NONBLOCK) ||
        setsockopt(c->fd, SOL_CAN_RAW, CAN_RAW_FILTER, filter, sizeof filter) ||
        setsockopt(c->fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &errors, sizeof errors) ||
        bind(c->fd, (struct sockaddr *)&address, sizeof address)) {
        can_bus_close(c);
        return -1;
    }
    c->state = BUS_ACTIVE;
    return c->fd;
}

void can_bus_read(struct can_bus *c, struct inspect_data *d, double now,
                  void (*on_result)(const struct inspect_data *d))
{
    struct can_frame f;
    int i, rc;
    for (i = 0; c->fd >= 0 && i < 64; i++) {
        ssize_t n = read(c->fd, &f, sizeof f);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN)
            return;
        if (n != (ssize_t)sizeof f) {
            fprintf(stderr, "CAN: receive failed: %s\n",
                    n < 0 ? strerror(errno) : "short frame");
            can_bus_close(c);
            return;
        }
        if (f.can_id & CAN_ERR_FLAG) {
            if (f.can_id & CAN_ERR_BUSOFF)
                c->state = BUS_OFF;
            else if (f.can_id & CAN_ERR_RESTARTED)
                c->state = BUS_ACTIVE;
            else if ((f.can_id & CAN_ERR_CRTL) &&
                     (f.data[1] & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE)))
                c->state = BUS_PASSIVE;
            continue;
        }
        if (f.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG))
            continue;
        /* A frame was received, so the controller is no longer bus-off. */
        rc = inspect_parse(d, f.can_id & CAN_SFF_MASK, f.data, f.can_dlc, now);
        if (rc >= 0)
            c->state = BUS_ACTIVE;
        if (rc == 1 && (f.can_id & CAN_SFF_MASK) == INSPECT_RESULT_ID && on_result)
            on_result(d);
    }
}

void can_bus_close(struct can_bus *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
    c->state = BUS_CLOSED;
}
