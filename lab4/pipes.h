#pragma once

struct fd_pair {
    int read_fd;
    int write_fd;
};
typedef struct fd_pair fd_pair;

extern fd_pair** pipes;
extern local_id my_id;
