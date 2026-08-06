#ifndef __QUEUE_H
#define __QUEUE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define BUFFER_SIZE 128

bool queue_in(char *data);
bool queue_out(char *data);

#endif /* __QUEUE_H */
