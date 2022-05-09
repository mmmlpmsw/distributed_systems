#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include "banking.h"
#include "pipes.h"

void transfer(void * parent_data, local_id src, local_id dst,
              balance_t amount)
{
    TransferOrder order;
    order.s_amount = amount;
    order.s_dst = dst;
    order.s_src = src;

    Message message;
    MessageHeader header;

    header.s_local_time = get_physical_time();
    header.s_magic = MESSAGE_MAGIC;
    header.s_payload_len = sizeof(order);
    header.s_type = TRANSFER;

    message.s_header = header;
    memcpy(&message.s_payload, &order, sizeof order);

    send(&pipes[0][src].write_fd, 0, &message);

    Message msg;
    while(receive(&pipes[dst][0].read_fd, 0, &msg) == 1);

    if (msg.s_header.s_type == ACK)
        return;
    else {
        printf("Parent got wrong type\n");
        exit(EXIT_FAILURE);
    }
}
