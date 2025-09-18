#ifndef MOTOR_TASK_H
#define MOTOR_TASK_H
#include <stdbool.h>

extern volatile bool motor_run;        // 👈 declaración
extern volatile int pulse_width_us;    // 👈 declaración

void motor_task(void *arg);

#endif
