#ifndef COMMAND_SERVER_H
#define COMMAND_SERVER_H

#include <stdint.h>

/*
 * Line-based command server on TCP port 5005.
 * Commands: ARM, DISARM, STOP, SET_SPEED <rpm>, STATUS, CLEAR_FAULT, STEP_TEST.
 */
void command_server_start(void);

uint32_t command_server_get_wifi_reconnect_count(void);

#endif /* COMMAND_SERVER_H */
