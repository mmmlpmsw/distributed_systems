#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/wait.h>

#include "common.h"
#include "ipc.h"
#include "pipes.h"
#include "banking_additional.h"
#include "pa2345.h"

FILE* events_log_fd;
FILE* pipes_log_fd;
int my_id;
BalanceHistory balance_history = {0};
fd_pair** pipes;

void create_logs() {
    events_log_fd = fopen(events_log, "a");
    if (events_log_fd == NULL) printf("Cannot create events.log\n");

    pipes_log_fd = fopen(pipes_log, "a");
    if (pipes_log_fd == NULL) printf("Cannot create pipes.log\n");
}

void close_logs() {
    fclose(events_log_fd);
    fclose(pipes_log_fd);
}

void create_pipes(int X) {
    int N = X + 1;
    pipes = calloc(N, sizeof(fd_pair*));
    for (int i = 0; i < N; i++)
        pipes[i] = calloc(N, sizeof(fd_pair));

    for (int from = 0; from < N; from ++) {
        for (int to = 0; to < N; to ++) {
            if (from == to) {
                pipes[from][to].read_fd = -1;
                pipes[from][to].write_fd = -1;
            } else {
                int fd_pr[2];
                if (pipe(fd_pr) != 0)
                    perror("Failed to create pipe");

                fcntl(fd_pr[0], F_SETFL, O_NONBLOCK);
                fcntl(fd_pr[1], F_SETFL, O_NONBLOCK);
                pipes[from][to].read_fd = fd_pr[0];
                pipes[from][to].write_fd = fd_pr[1];
            }
        }
    }
}

void send_all(int X, MessageType type, const char* format, ...) {
    char text[MAX_PAYLOAD_LEN] = {0};
    memset(text, 0, MAX_PAYLOAD_LEN);
    va_list args;
    va_start(args, format);
    vsprintf(text, format, args);
    Message* message = malloc(MAX_MESSAGE_LEN);
    memset(message, 0, MAX_MESSAGE_LEN);
    message->s_header.s_magic = MESSAGE_MAGIC;
    message->s_header.s_local_time = increment_lamport_time();
    message->s_header.s_type = type;
    strcpy(message->s_payload, text);
    message->s_header.s_payload_len = strlen(text);
    for (int i = 0; i < X + 1; i++) {
        if (i != my_id)
            send(&pipes[my_id][i].write_fd, 0, message);
    }
    free(message);
    printf("%s", text);
    fprintf(events_log_fd, "%s", text);
    va_end(args);
}

void receive_all(int X, MessageType type) {
    for (int i = 1; i < X + 1; i++) {
        if (my_id != i) {
            Message* message = malloc(MAX_MESSAGE_LEN);
            memset(message, 0, MAX_MESSAGE_LEN);
            while (receive(&pipes[i][my_id].read_fd, 0, message) == 1);
            pull_up_lamport_time(message->s_header.s_local_time);
            increment_lamport_time();
            if (message->s_header.s_type != type) {
                printf("[pid: %d] Ожидается тип %d, пришел тип %d\n", getpid(), type, message->s_header.s_type);
                exit(-1);
            }
            free(message);
        }
    }
}

local_id receive_from_any(int X, Message* msg_buffer) {
    while (true) {
        for (int i = 0; i <= X; i++) {
            if (my_id != i) {
                int result = receive(&pipes[i][my_id].read_fd, 0, msg_buffer);
                if (result == 0) {
                    pull_up_lamport_time(msg_buffer->s_header.s_local_time);
                    increment_lamport_time();
                    return (local_id) i;
                }
                if (result == -1) {
                    perror("[receive_from_any] error getting message");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

void close_unused_pipes(int X) {
    for (int from = 0; from < X + 1; from ++) {
        for (int to = 0; to < X + 1; to ++) {
            if (from != my_id && to != my_id && from != to) {
                close(pipes[from][to].write_fd);
                close(pipes[from][to].read_fd);
            }
        }
    }

    for (int i = 0; i < X + 1; i++) {
        if (my_id != i) {
            close(pipes[my_id][i].read_fd);
            close(pipes[i][my_id].write_fd);
        }
    }
}

void close_pipes(int X) {
    for (int i = 0; i < X + 1; i++) {
        if (my_id != i) {
            close(pipes[my_id][i].write_fd);
            close(pipes[i][my_id].read_fd);
        }
    }
}

void update_balance(balance_t new_balance) {
    timestamp_t time = get_lamport_time();
    while (balance_history.s_history_len > 0 && balance_history.s_history_len < time) {
        balance_history.s_history[balance_history.s_history_len].s_balance = balance_history.s_history[balance_history.s_history_len - 1].s_balance;
        balance_history.s_history[balance_history.s_history_len].s_time = (timestamp_t)(balance_history.s_history[balance_history.s_history_len - 1].s_time + 1);
        balance_history.s_history_len ++;
    }
    balance_history.s_history[time].s_balance = new_balance;
    balance_history.s_history[time].s_time = time;
    balance_history.s_history_len = time + 1;
}

balance_t get_balance() {
    return balance_history.s_history[balance_history.s_history_len - 1].s_balance;
}

void child_task(int X, int balance) {
    close_unused_pipes(X);
    balance_history.s_id = (local_id) my_id;
    update_balance((balance_t) balance);
    send_all(X, STARTED, log_started_fmt, get_lamport_time(), my_id, getpid(), getppid(), get_balance());
    receive_all(X, STARTED);
    printf(log_received_all_started_fmt, get_lamport_time(), my_id);
    fprintf(events_log_fd, log_received_all_started_fmt, get_lamport_time(), my_id);
    Message message;
    bool* processes_finished = calloc(X + 1, sizeof(bool));

    while (1) {
        local_id id_from_msg = receive_from_any(X, &message);

        if (message.s_header.s_type == TRANSFER) {
            TransferOrder* order = (TransferOrder*)message.s_payload;
            if (id_from_msg == PARENT_ID) {
                message.s_header.s_local_time = increment_lamport_time();
                update_balance((balance_t)(get_balance() - order->s_amount));
                balance_history.s_history[balance_history.s_history_len - 1].s_balance_pending_in = order->s_amount;
                send(&pipes[my_id][order->s_dst].write_fd, 0, &message);
                printf(log_transfer_out_fmt, get_lamport_time(), my_id, order->s_amount, order->s_dst);
                fprintf(events_log_fd, log_transfer_out_fmt, get_lamport_time(), my_id, order->s_amount, order->s_dst);
            } else {
                update_balance((balance_t)(get_balance() + order->s_amount));
                printf(log_transfer_in_fmt, get_lamport_time(), my_id, order->s_amount, order->s_src);
                fprintf(events_log_fd, log_transfer_in_fmt, get_lamport_time(), my_id, order->s_amount, order->s_src);
                send(&pipes[my_id][PARENT_ID].write_fd, 0, & (Message) {
                    .s_header = {
                            .s_type = ACK,
                            .s_payload_len = 0,
                            .s_magic = MESSAGE_MAGIC,
                            .s_local_time = increment_lamport_time()
                    }
                });
            }
        } else if (message.s_header.s_type == STOP || message.s_header.s_type == DONE) {
            if (message.s_header.s_type == STOP) {
                send_all(X, DONE, log_done_fmt, get_lamport_time(), my_id, get_balance());
                processes_finished[my_id] = true;
            }
            processes_finished[id_from_msg] = true;
            for (int i = 0; i <= X; i++) {
                if (!processes_finished[i])
                    goto continue_outer;
            }
            break;
            continue_outer:;
        } else {
            printf("Wrong message type (%d)\n", message.s_header.s_type);
            exit(EXIT_FAILURE);
        }
    }
    printf(log_received_all_done_fmt, get_lamport_time(), my_id);
    fprintf(events_log_fd, log_received_all_done_fmt, get_lamport_time(), my_id);

    update_balance(get_balance()); // Хитрый грязный трюк

    Message msg;
    msg.s_header.s_type = BALANCE_HISTORY;
    msg.s_header.s_payload_len = sizeof(balance_history);
    msg.s_header.s_magic = MESSAGE_MAGIC;
    msg.s_header.s_local_time = increment_lamport_time();

    memcpy(&msg.s_payload, &balance_history, sizeof(balance_history));

    send(&pipes[my_id][0].write_fd, 0, &msg);

    free(processes_finished);
    close_pipes(X);
    close_logs();
}

void parent_task(int X) {
    close_unused_pipes(X);

    for (int i = 1; i < X + 1; i++) {
        Message* message = malloc(MAX_MESSAGE_LEN);
        memset(message, 0, MAX_MESSAGE_LEN);
        while (receive(&pipes[i][my_id].read_fd, 0, message) != 0);
        if (message->s_header.s_type != STARTED) {
            printf("ошибка родителя\n");
            exit(-1);
        }
        pull_up_lamport_time(message->s_header.s_local_time);
        increment_lamport_time();
        free(message);
    }

    bank_robbery(0, (local_id)X);

    send_all(X, STOP, "");

    for (int i = 1; i < X + 1; i++) {
        Message* message = malloc(MAX_MESSAGE_LEN);
        memset(message, 0, MAX_MESSAGE_LEN);
        while (receive(&pipes[i][my_id].read_fd, 0, message) != 0);
        if (message->s_header.s_type != DONE) {
            printf("ошибка родителя 2\n");
            exit(-1);
        }
        pull_up_lamport_time(message->s_header.s_local_time);
        increment_lamport_time();
        free(message);
    }

    AllHistory history = {0};
    history.s_history_len = X;

    for (int i = 1; i < X + 1; i++) {
        Message* message = malloc(MAX_MESSAGE_LEN);
        memset(message, 0, MAX_MESSAGE_LEN);
        while (receive(&pipes[i][my_id].read_fd, 0, message) != 0);
        if (message->s_header.s_type != BALANCE_HISTORY) {
            printf("ошибка родителя 3\n");
            exit(-1);
        }
        pull_up_lamport_time(message->s_header.s_local_time);
        increment_lamport_time();
        BalanceHistory* child_history = (BalanceHistory*) message->s_payload;
        memcpy(&history.s_history[i - 1], child_history, sizeof(BalanceHistory));
        free(message);
    }

    print_history(&history);

    int status = 0;
    while (wait(&status) > 0);

    close_pipes(X);
    close_logs();
}

void create_processes(int X, int* balances) {
    for (int i = 1; i < X + 1; i++) {
        if (fork() == 0) {
            my_id = i;
            child_task(X, balances[i - 1]);
            return;
        }
    }
    parent_task(X);
}

int main(int argc, char** argv) {
    int X;

    if (argc >= 3) {
        if (strcmp(argv[1], "-p") == 0) {
            char* endptr = NULL;
            X = (int) strtol(argv[2], &endptr, 10);
            if (argc >= 3 + X) {
                int* balances = calloc(X, sizeof(int));
                for (int i = 0, j = 3; i < X; i++, j++)
                    balances[i] = (int) strtol(argv[j], &endptr, 10);
                create_logs();
                create_pipes(X);
                create_processes(X, balances);
                free(balances);
            } else {
                puts("Fuck you");
                return 1;
            }
        }
    } else {
        puts("Wrong args");
        return 1;
    }
    return 0;
}



