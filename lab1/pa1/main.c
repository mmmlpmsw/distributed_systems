#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/wait.h>

#include "common.h"
#include "ipc.h"
#include "pa1.h"

struct fd_pair {
    int read_fd;
    int write_fd;
};
typedef struct fd_pair fd_pair;

int my_id = 0;
FILE* events_log_fd;
FILE* pipes_log_fd;
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
    pipes = malloc(N * sizeof(fd_pair*));
    memset(pipes, 0, N * sizeof(fd_pair*));
    for (int i = 0; i < N; i++) {
        pipes[i] = malloc(N * sizeof(fd_pair));
        memset(pipes[i], 0, N * sizeof(fd_pair));
    }
    for (int from = 0; from < N; from ++) {
        for (int to = 0; to < N; to ++) {
            if (from == to) {
                pipes[from][to].read_fd = -1;
                pipes[from][to].write_fd = -1;
            } else {
                int fd_pr[2];
                pipe(fd_pr);
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
    for (int i = 0; i < X + 1; i++) {
        if (i != my_id) {
            Message* message = malloc(MAX_MESSAGE_LEN);
            memset(message, 0, MAX_MESSAGE_LEN);
            message->s_header.s_magic = MESSAGE_MAGIC;
            message->s_header.s_local_time = 0;
            message->s_header.s_type = type;
            strcpy(message->s_payload, text);
            message->s_header.s_payload_len = strlen(text);
            send(&pipes[my_id][i].write_fd, 0, message);
            free(message);
        }
    }
    printf("%s", text);
    fprintf(events_log_fd, "%s", text);
    va_end(args);
}

void receive_all(int X, MessageType type) {
    for (int i = 1; i < X + 1; i++) {
        if (my_id != i) {
            Message* message = malloc(MAX_MESSAGE_LEN);
            memset(message, 0, MAX_MESSAGE_LEN);
            receive(&pipes[i][my_id].read_fd, 0, message);
            if (message->s_header.s_type != type) {
                printf("[pid: %d] Ожидается тип %d, пришел тип %d\n", getpid(), type, message->s_header.s_type);
                exit(-1);
            }
            free(message);
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

void child_task(int X) {
    close_unused_pipes(X);
    send_all(X, STARTED, log_started_fmt, my_id, getpid(), getppid());
    receive_all(X, STARTED);
    printf(log_received_all_started_fmt, my_id);
    fprintf(events_log_fd, log_received_all_started_fmt, my_id);
    send_all(X, DONE, log_done_fmt, my_id);
    receive_all(X, DONE);
    printf(log_received_all_done_fmt, my_id);
    fprintf(events_log_fd, log_received_all_done_fmt, my_id);
    close_pipes(X);
    close_logs();
}

void parent_task(int X) {
    close_unused_pipes(X);

    for (int i = 1; i < X + 1; i++) {
        Message* message = malloc(MAX_MESSAGE_LEN);
        memset(message, 0, MAX_MESSAGE_LEN);
        receive(&pipes[i][my_id].read_fd, 0, message);
        if (message->s_header.s_type != STARTED) {
            printf("ошибка родителя\n");
            exit(-1);
        }
        free(message);
    }

    for (int i = 1; i < X + 1; i++) {
        Message* message = malloc(MAX_MESSAGE_LEN);
        memset(message, 0, MAX_MESSAGE_LEN);
        receive(&pipes[i][my_id].read_fd, 0, message);
        if (message->s_header.s_type != DONE) {
            printf("ошибка родителя 2\n");
            exit(-1);
        }
        free(message);
    }

    int status = 0;
    while (wait(&status) > 0);

    close_pipes(X);
    close_logs();
}

void create_processes(int X) {
    for (int i = 1; i < X + 1; i++) {
        if (fork() == 0) {
            my_id = i;
            child_task(X);
            return;
        }
    }
    parent_task(X);
}

int main(int argc, char** argv) {
    int X;

    if (argc >= 3) {
        if (strcmp(argv[1], "-p") == 0) {
            char* invalid = NULL;
            X = (int) strtol(argv[2], &invalid, 10);
            create_logs();
            create_pipes(X);
            create_processes(X);
        }
    } else {
        printf("Wrong args\n");
        return 1;
    }
    return 0;
}



