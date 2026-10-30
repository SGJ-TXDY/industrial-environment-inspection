#ifndef MAILBOX_H
#define MAILBOX_H
#include <stdint.h>
#include <time.h>

/* Thread mailbox in the style of the course mailbox.c: every worker thread
 * owns a named queue, others post to it by name. Differences: bounded ring
 * instead of malloc per message, all boxes registered before any thread
 * starts, receive by own name with timeout, orderly shutdown.
 */
#define MAIL_NAME_MAX 16
#define MAIL_BOX_MAX 8

enum mail_type { MAIL_SENSOR, MAIL_INSPECT, MAIL_ALARM };

struct mail_msg {
    int type;
    time_t wall;                    /* wall clock when produced */
    char sender[MAIL_NAME_MAX];
    union {
        struct { int32_t temperature_mc; } sensor;
        struct { uint32_t seq; int session, result, confidence; } inspect;
        struct { int kind, active; int32_t value; } alarm;
    } u;
};

int create_mailbox_system(void);
/* Registers a box; the thread starts in start_mail_tasks(). arg is the
 * box name, so a task can call recv_mail_msg((const char *)arg, ...).
 */
int register_thread_mail_task(const char *name, void *(*task)(void *),
                              unsigned int capacity);
int start_mail_tasks(void);
/* Never blocks. -1 when the receiver is unknown, closing or full. */
int send_mail_msg(const char *sender, const char *receiver, struct mail_msg *msg);
/* 0 message, 1 timeout, -1 mailbox closed and drained: the task must exit. */
int recv_mail_msg(const char *self, struct mail_msg *msg, int timeout_ms);
unsigned long mail_dropped(const char *name);
/* Closes all boxes, lets tasks drain them, joins and frees. */
void destroy_mailbox_system(void);
#endif
