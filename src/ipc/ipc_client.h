#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

/**
 * @file ipc_client.h
 * @brief Client side of the device FIFO protocol (camera tools → device daemon).
 *
 * Line formats the daemon understands (see main handle_command):
 * - @c N\\n          absolute move / motor command (integer)
 * - @c bN\\n         illumination brightness 0..255
 * - @c 1001\\n       illumination on  (CMD_LASER_ON)
 * - @c 1002\\n       illumination off (CMD_LASER_OFF)
 * - @c 4000\\n       get position → reply on POS_PIPE
 * - @c -1\\n         stop daemon
 */

int fifo_open(void);
int fifo_send(long cmd);
int fifo_enable(void);
int fifo_is_enabled(void);
int fifo_get_position(void);
int fifo_move_and_wait(int pos);

/** Illumination helpers (text protocol, not bare integers). */
int fifo_illum_on(void);
int fifo_illum_off(void);
int fifo_set_brightness(int brightness);

void fifo_close(void);
void fifo_shutdown(void);

#endif
