#ifndef COMMANDS_H
#define COMMANDS_H

/**
 * @file commands.h
 * @brief Integer command codes for the servo FIFO protocol and homing offsets.
 *
 * Absolute positions use the range [CMD_ANGLE_MIN, CMD_ANGLE_MAX].
 * Values above that range are control commands.
 *
 * @note CMD_DISABLE and CMD_RESET share the value 1003. In motor @c send,
 *       the disable branch is evaluated first, so the reset path is unreachable.
 * @note CMD_LASER_ON / CMD_LASER_OFF are handled by @c main via illumination_api,
 *       not by the motor backend.
 */

#define CMD_ANGLE_MIN 0
#define CMD_ANGLE_MAX 1000
#define CMD_ENABLE 1004
#define CMD_DISABLE 1003
#define CMD_NEXT 1006
#define CMD_PREV 1007
#define CMD_LOADFILE 2000
#define CMD_UPDATE_LID 1008
#define CMD_LASER_ON 1001
#define CMD_LASER_OFF 1002
#define CMD_RESET 1003
#define CMD_GETPOS 4000

/** Nominal home step when the endstop first trips (before DELTA). */
#define CMD_POS_ZERO 400

/** Extra steps added when the endstop trips during motion bookkeeping. */
#define DELTA 34

#endif
