#pragma once
#include "banking.h"

timestamp_t increment_lamport_time();
timestamp_t pull_up_lamport_time(timestamp_t value);
