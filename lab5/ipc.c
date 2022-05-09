#include <unistd.h>
#include <errno.h>

#include "ipc.h"

int send(void * self, __attribute__((unused)) local_id dst, const Message * msg) {
    int fd = *(int*)self;
    ssize_t written_bytes = 0;
    while (1) {
        ssize_t temp = write(fd, (uint8_t*)msg + written_bytes, sizeof(MessageHeader) + msg->s_header.s_payload_len - written_bytes);
        if (temp == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return -1;
        }
        written_bytes += temp;
        if (written_bytes == sizeof(MessageHeader) + msg->s_header.s_payload_len)
            return 0;
    }
}

int receive(void * self, __attribute__((unused)) local_id from, Message * msg) {
    int fd = *(int*)self;
    ssize_t received_bytes = 0;
    while (1) {
        ssize_t temp = read(fd, (uint8_t*)msg + received_bytes, sizeof(MessageHeader) - received_bytes);
        if (temp == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 1; // no messages
            return -1; //error
        }
        if (temp == 0)
            return 1;
        received_bytes += temp;
        if (received_bytes == sizeof(MessageHeader))
            break;
    }
    received_bytes = 0;
    while (received_bytes < msg->s_header.s_payload_len) {
        ssize_t temp = read(fd, (uint8_t*)&msg->s_payload + received_bytes, msg->s_header.s_payload_len - received_bytes);
        if (temp == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return -1; //error
        }
        received_bytes += temp;
    }
    return 0;
}
