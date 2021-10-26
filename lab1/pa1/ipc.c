#include <unistd.h>
#include <errno.h>

#include "ipc.h"

int send(void * self, __attribute__((unused)) local_id dst, const Message * msg) {
    int fd = *(int*)self;
    write(fd, msg, sizeof(MessageHeader) + msg->s_header.s_payload_len);
    return errno == 0 ? 0 : 1;
}

int receive(void * self, __attribute__((unused)) local_id from, Message * msg) {
    int fd = *(int*)self;
    read(fd, msg, sizeof(MessageHeader));
    if (errno != 0)
        return 1;
    read(fd, &msg->s_payload, msg->s_header.s_payload_len);
    return errno == 0 ? 0 : 1;
}
