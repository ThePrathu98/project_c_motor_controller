#ifndef COMMAND_SERVER_H
#define COMMAND_SERVER_H

/*
 * command_server.h
 *
 * Public interface for the Wi-Fi/TCP command server.
 *
 * Implementation details:
 *   - Uses ESP8266 station mode, same style as Day 3-4.
 *   - Uses lwIP BSD sockets.
 *   - Listens on TCP port 5005.
 *   - Accepts simple line-based text commands from the PC.
 *
 * Commands:
 *   ARM
 *   DISARM
 *   STOP
 *   SET_SPEED <rpm>
 *   STATUS
 *   CLEAR_FAULT
 *   STEP_TEST
 */
void command_server_start(void);

#endif /* COMMAND_SERVER_H */
