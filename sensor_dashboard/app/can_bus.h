#ifndef CAN_BUS_H
#define CAN_BUS_H
#include "inspect.h"

struct can_bus { int fd, state; };

/* bitrate 0 keeps the interface's existing timing and only brings it up.
 * Otherwise the link is restarted with this bitrate and automatic bus-off
 * recovery, so BusyBox systems need no iproute2.
 */
int can_bus_open(struct can_bus *c, const char *ifname, unsigned int bitrate);
/* Drains pending frames; closes the socket on interface failure.
 * on_result, if set, runs once for every newly accepted inspection result.
 */
void can_bus_read(struct can_bus *c, struct inspect_data *d, double now,
                  void (*on_result)(const struct inspect_data *d));
void can_bus_close(struct can_bus *c);
#endif
