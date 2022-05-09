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

typedef struct CriticalSectionRequest CriticalSectionRequest;
struct CriticalSectionRequest {
    timestamp_t time;
    local_id process_id;
};

FILE* events_log_fd;
FILE* pipes_log_fd;
local_id my_id;
fd_pair** pipes;
CriticalSectionRequest postponed_requests[MAX_PROCESS_ID + 1];
bool* processes_finished;
int postponed_requests_len = 0;
int children_num;

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

void postpone_cs_request(local_id process_id) {
    postponed_requests_len++;
    postponed_requests[postponed_requests_len - 1].process_id = process_id;
}

local_id pop_cs_request() {
    local_id result = postponed_requests[postponed_requests_len - 1].process_id;
    postponed_requests_len--;
    return result;
}

void process_request(Message* message, local_id from) {
    if (message->s_header.s_type == CS_REQUEST) {
        send(&pipes[my_id][from].write_fd, 0, & (Message) {
                .s_header = {
                        .s_type = CS_REPLY,
                        .s_local_time = increment_lamport_time(),
                        .s_magic = MESSAGE_MAGIC,
                        .s_payload_len = 0
                }
        });
    } else if (message->s_header.s_type == DONE) {
        processes_finished[from] = true;
    } else {
        printf("Wrong message type (%d)\n", message->s_header.s_type);
        exit(EXIT_FAILURE);
    }
}

int request_cs(__attribute__((unused)) const void * self) {
    send_all(children_num, CS_REQUEST, "");
    timestamp_t my_time = get_lamport_time();
    bool* replies = calloc(children_num + 1, sizeof(bool));
    replies[my_id] = true;

    while (1) {
        Message message;
        local_id id_from_msg = receive_from_any(children_num, &message);

        timestamp_t their_time = message.s_header.s_local_time;
        if (message.s_header.s_type == CS_REPLY) {
            replies[id_from_msg] = true;
        } else if (message.s_header.s_type == CS_REQUEST) {
            if (my_time < their_time || (my_time == their_time && my_id < id_from_msg)) {
                postpone_cs_request(id_from_msg);
            } else if (my_time > their_time || (my_time == their_time && my_id > id_from_msg)) {
                send(&pipes[my_id][id_from_msg].write_fd, 0, &(Message) {
                        .s_header = {
                                .s_magic = MESSAGE_MAGIC,
                                .s_type = CS_REPLY,
                                .s_payload_len = 0,
                                .s_local_time = increment_lamport_time()
                        }
                });
            }
        } else {
            process_request(&message, id_from_msg);
        }

        bool all_replied = true;
        for (int i = 0; i < children_num + 1; i++) {
            all_replied &= replies[i];
            if (!all_replied)
                break;
        }

        if (all_replied)
            break;
    }

    return 0;
}

int release_cs(__attribute__((unused)) const void * self) {
    while (postponed_requests_len > 0)
        send(&pipes[my_id][pop_cs_request()].write_fd, 0, &(Message) {
                .s_header = {
                        .s_magic = MESSAGE_MAGIC,
                        .s_type = CS_REPLY,
                        .s_payload_len = 0,
                        .s_local_time = increment_lamport_time()
                }
        });
    return 0;
}

void child_task(int X, bool use_mutex) {
    children_num = X;
    close_unused_pipes(X);
    send_all(X, STARTED, log_started_fmt, get_lamport_time(), my_id, getpid(), getppid(), 0);
    receive_all(X, STARTED);
    printf(log_received_all_started_fmt, get_lamport_time(), my_id);
    fprintf(events_log_fd, log_received_all_started_fmt, get_lamport_time(), my_id);
    processes_finished = calloc(X + 1, sizeof(bool));
    processes_finished[PARENT_ID] = true;

    for (int i = 1; i <= my_id * 5; i++) {
        char buffer[256] = {0};
        sprintf(buffer, log_loop_operation_fmt, my_id, i, my_id * 5);

        if (use_mutex)
            request_cs(NULL);

        print(buffer);

        if (use_mutex)
            release_cs(NULL);
    }

    send_all(X, DONE, log_done_fmt, get_lamport_time(), my_id, 0);
    processes_finished[my_id] = true;

    // Waiting for all to done
    while (1) {
        for (int i = 0; i <= children_num; i++) {
            if (!processes_finished[i])
                goto process;
        }
        break;
        process:;
        Message message;
        local_id message_from = receive_from_any(X, &message);
        process_request(&message, message_from);
    }

    printf(log_received_all_done_fmt, get_lamport_time(), my_id);
    fprintf(events_log_fd, log_received_all_done_fmt, get_lamport_time(), my_id);

    free(processes_finished);
    processes_finished = NULL;
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

    bool* ready = calloc(X + 1, sizeof(bool));
    ready[PARENT_ID] = true;

    while (1) {
        Message message;
        local_id from = receive_from_any(X, &message);
        if (message.s_header.s_type == DONE) {
            ready[from] = true;
        } else if (message.s_header.s_type == CS_REQUEST) {
            send(&pipes[PARENT_ID][from].write_fd, 0, & (Message) {
                .s_header = {
                        .s_type = CS_REPLY,
                        .s_local_time = increment_lamport_time(),
                        .s_magic = MESSAGE_MAGIC,
                        .s_payload_len = 0
                }
            });
        } else {
            printf("ошибка родителя 2\n");
            exit(-1);
        }

        for (int i = 0; i < X + 1; i++) {
            if (!ready[i])
                goto continue_outer;
        }

        break;
        continue_outer:;
    }

    int status = 0;
    while (wait(&status) > 0);

    close_pipes(X);
    close_logs();
}

void create_processes(int X, bool use_mutex) {
    for (local_id i = 1; i < (local_id) (X + 1); i++) {
        if (fork() == 0) {
            my_id = i;
            child_task(X, use_mutex);
            return;
        }
    }
    parent_task(X);
}

bool get_use_mutex(int argc, char** argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--mutexl") == 0) {
            return true;
        }
    }
    return false;
}

int get_processes_num(int argc, char** argv) {
    for (int i = 0; i < argc - 1; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            char* endptr = NULL;
            return (int) strtol(argv[i + 1], &endptr, 10);
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    int X;

    X = get_processes_num(argc, argv);
    bool use_mutex = get_use_mutex(argc, argv);
    create_logs();
    create_pipes(X);
    create_processes(X, use_mutex);
    return 0;
}



