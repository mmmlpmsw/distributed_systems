#include "banking_additional.h"

timestamp_t time = 0;
timestamp_t get_lamport_time() {
    return time;
}

timestamp_t increment_lamport_time() {
    return ++time;
}

timestamp_t pull_up_lamport_time(timestamp_t value) {
    if (value > time)
        time = value;
    return time;
}
