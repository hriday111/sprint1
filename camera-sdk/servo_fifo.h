#ifndef SERVO_FIFO_H
#define SERVO_FIFO_H

/**
 * @file servo_fifo.h
 * @brief Command/position FIFO helpers for camera-sdk tools talking to servo_calib.
 *
 * Keep one write fd open for the whole session. Closing without @c servo_fifo_shutdown
 * does not send @c -1 (daemon keeps running).
 */

#define SERVO_CMD_GETPOS 4000

/**
 * @brief Open the command FIFO for writing (retries while no reader).
 * @return 0 on success, -1 on failure.
 */
int servo_fifo_open(void);

/**
 * @brief Write one integer command line; reopen on transient EPIPE.
 * @param cmd Protocol integer (absolute position or control code).
 * @return 0 on success, -1 on failure.
 */
int servo_fifo_send(long cmd);

/**
 * @brief Read position with retries (open pos_pipe before sending GETPOS).
 * @return Position >= 0, or -1 after exhausting retries.
 */
int servo_fifo_position(void);

/**
 * @brief Command an absolute move and poll until reported position matches.
 * @param pos Target step.
 * @return 0 on arrival, -1 on failure/timeout.
 */
int servo_fifo_move_and_wait(int pos);

/**
 * @brief Close the command FIFO without sending @c -1 (leave daemon running).
 */
void servo_fifo_close(void);

/**
 * @brief Send @c -1 then close (stops servo_calib). Prefer @c servo_fifo_close for tools.
 */
void servo_fifo_shutdown(void);

#endif
