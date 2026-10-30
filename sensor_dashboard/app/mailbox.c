#include "mailbox.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct mailbox {
    char name[MAIL_NAME_MAX];
    void *(*task)(void *);
    struct mail_msg *ring;
    unsigned int capacity, head, count;
    unsigned long dropped;
    int closing, started;
    pthread_t tid;
    pthread_mutex_t lock;
    pthread_cond_t ready;
};

static struct mailbox boxes[MAIL_BOX_MAX];
static unsigned int box_count;
static clockid_t wait_clock = CLOCK_REALTIME;

static struct mailbox *find(const char *name)
{
    unsigned int i;
    for (i = 0; name && i < box_count; i++)
        if (!strcmp(boxes[i].name, name))
            return &boxes[i];
    return NULL;
}

int create_mailbox_system(void)
{
    memset(boxes, 0, sizeof boxes);
    box_count = 0;
    return 0;
}

int register_thread_mail_task(const char *name, void *(*task)(void *),
                              unsigned int capacity)
{
    struct mailbox *b;
    pthread_condattr_t attr;
    if (!name || !*name || strlen(name) >= MAIL_NAME_MAX || !task ||
        !capacity || box_count == MAIL_BOX_MAX || find(name)) {
        errno = EINVAL;
        return -1;
    }
    b = &boxes[box_count];
    memset(b, 0, sizeof *b);
    b->ring = calloc(capacity, sizeof *b->ring);
    if (!b->ring)
        return -1;
    strcpy(b->name, name);
    b->task = task;
    b->capacity = capacity;
    pthread_mutex_init(&b->lock, NULL);
    /* Monotonic timeouts survive wall-clock changes (NTP, date). */
    pthread_condattr_init(&attr);
    wait_clock = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) ?
                 CLOCK_REALTIME : CLOCK_MONOTONIC;
    pthread_cond_init(&b->ready, &attr);
    pthread_condattr_destroy(&attr);
    box_count++;
    return 0;
}

int start_mail_tasks(void)
{
    unsigned int i;
    for (i = 0; i < box_count; i++) {
        if (boxes[i].started)
            continue;
        if (pthread_create(&boxes[i].tid, NULL, boxes[i].task, boxes[i].name))
            return -1;
        boxes[i].started = 1;
    }
    return 0;
}

int send_mail_msg(const char *sender, const char *receiver, struct mail_msg *msg)
{
    struct mailbox *b = find(receiver);
    int ret = -1;
    if (!b || !msg)
        return -1;
    memset(msg->sender, 0, sizeof msg->sender);
    strncpy(msg->sender, sender ? sender : "", MAIL_NAME_MAX - 1);
    pthread_mutex_lock(&b->lock);
    /* A full box drops the new message: producers never wait on consumers. */
    if (!b->closing && b->count < b->capacity) {
        b->ring[(b->head + b->count) % b->capacity] = *msg;
        b->count++;
        pthread_cond_signal(&b->ready);
        ret = 0;
    } else {
        b->dropped++;
    }
    pthread_mutex_unlock(&b->lock);
    return ret;
}

int recv_mail_msg(const char *self, struct mail_msg *msg, int timeout_ms)
{
    struct mailbox *b = find(self);
    struct timespec until;
    int ret = 0;
    if (!b || !msg)
        return -1;
    clock_gettime(wait_clock, &until);
    until.tv_sec += timeout_ms / 1000;
    until.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (until.tv_nsec >= 1000000000L) {
        until.tv_sec++;
        until.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&b->lock);
    while (!b->count && !b->closing && ret != ETIMEDOUT)
        ret = pthread_cond_timedwait(&b->ready, &b->lock, &until);
    if (b->count) {
        *msg = b->ring[b->head];
        b->head = (b->head + 1) % b->capacity;
        b->count--;
        ret = 0;
    } else {
        ret = b->closing ? -1 : 1;
    }
    pthread_mutex_unlock(&b->lock);
    return ret;
}

unsigned long mail_dropped(const char *name)
{
    struct mailbox *b = find(name);
    unsigned long n = 0;
    if (b) {
        pthread_mutex_lock(&b->lock);
        n = b->dropped;
        pthread_mutex_unlock(&b->lock);
    }
    return n;
}

void destroy_mailbox_system(void)
{
    unsigned int i;
    /* Close every box first so tasks posting to each other stop cleanly. */
    for (i = 0; i < box_count; i++) {
        pthread_mutex_lock(&boxes[i].lock);
        boxes[i].closing = 1;
        pthread_cond_broadcast(&boxes[i].ready);
        pthread_mutex_unlock(&boxes[i].lock);
    }
    for (i = 0; i < box_count; i++)
        if (boxes[i].started)
            pthread_join(boxes[i].tid, NULL);
    for (i = 0; i < box_count; i++) {
        pthread_cond_destroy(&boxes[i].ready);
        pthread_mutex_destroy(&boxes[i].lock);
        free(boxes[i].ring);
    }
    box_count = 0;
}
