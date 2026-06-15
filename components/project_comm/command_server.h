#ifndef COMMAND_SERVER_H
#define COMMAND_SERVER_H

/*
 * command_server.h
 *
 * Public interface for the Wi-Fi/TCP command server.
 *
 * Implementation details:
 *   - Uses lwIP BSD sockets.
 *   - Listens on TCP port 5005.
 *   - Accepts simple line-based text commands from the PC.
 *   - Calls control_task.c APIs to arm, stop, set RPM, and report status.
 *
 * Commands supported by command_server.c:
 *   ARM
 *   DISARM
 *   STOP
 *   SET_SPEED <rpm>
 *   STATUS
 *   STEP_TEST
 */

/*
 * Start Wi-Fi and the TCP command server task.
 * Called once from app_main() after motor/encoder/control initialization.
 */
void command_server_start(void);

#endif