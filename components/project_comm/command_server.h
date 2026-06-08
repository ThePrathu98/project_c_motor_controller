#ifndef COMMAND_SERVER_H
#define COMMAND_SERVER_H

/*
 * TCP command server on lwIP/BSD sockets.
 *
 * Port: 5005
 *
 * Commands:
 *   ARM
 *   DISARM
 *   STOP
 *   SET_SPEED <rpm>
 *   STATUS
 *   STEP_TEST
 */

void command_server_start(void);

#endif