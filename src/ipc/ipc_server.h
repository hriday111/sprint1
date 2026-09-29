#ifndef IPC_SERVER_H
#define IPC_SERVER_H

#include "motor_api.h"
#include "illumination_api.h"
void make_fifo(const char *path);
void write_position(int pos);
int handle_command(char* line, motor_t *motor, illumination_t *illum);
void serve_fifo(const char *path, motor_t *motor, illumination_t *illum);

#endif