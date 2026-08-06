#ifndef __SHELL_H
#define __SHELL_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "queue.h"
#include "usr_uart.h"
#include "list.h"

#define KEY_UP "\x1b\x5b\x41"    /* [up] key: 0x1b 0x5b 0x41 */
#define KEY_DOWN "\x1b\x5b\x42"  /* [down] key: 0x1b 0x5b 0x42 */
#define KEY_RIGHT "\x1b\x5b\x43" /* [right] key: 0x1b 0x5b 0x43 */
#define KEY_LEFT "\x1b\x5b\x44"  /* [left] key: 0x1b 0x5b 0x44 */
#define KEY_ENTER '\r'           /* [enter] key */
#define KEY_BACKSPACE '\b'       /* [backspace] key */

#define PRINTF(...) printf(__VA_ARGS__)
#define SPRINTF(...) sprintf(__VA_ARGS__)

#define ERR(fmt)                      \
    do                                \
    {                                 \
        TERMINAL_FONT_RED();          \
        PRINTF("[ERR]: " fmt "\r\n"); \
        TERMINAL_FONT_WHITE();        \
    } while (0)

#define INFO(fmt)                     \
    do                                \
    {                                 \
        TERMINAL_FONT_CYAN();         \
        PRINTF("[SYS]: " fmt "\r\n"); \
        TERMINAL_FONT_WHITE();        \
    } while (0)

/* font color */
#define TERMINAL_FONT_BLACK() PRINTF("\033[1;30m")
#define TERMINAL_FONT_L_RED() PRINTF("\033[0;31m") /* light red */
#define TERMINAL_FONT_RED() PRINTF("\033[1;31m")   /* red */
#define TERMINAL_FONT_GREEN() PRINTF("\033[1;32m")
#define TERMINAL_FONT_YELLOW() PRINTF("\033[1;33m")
#define TERMINAL_FONT_BLUE() PRINTF("\033[1;34m")
#define TERMINAL_FONT_PURPLE() PRINTF("\033[1;35m")
#define TERMINAL_FONT_CYAN() PRINTF("\033[1;36m")
#define TERMINAL_FONT_WHITE() PRINTF("\033[1;37m")

/* terminal clear end */
#define TERMINAL_CLEAR_END() PRINTF("\033[K")

/* terminal clear all */
#define TERMINAL_DISPLAY_CLEAR() PRINTF("\033[2J")

/* cursor reset */
#define TERMINAL_RESET_CURSOR() PRINTF("\033[H")

/* reverse display */
#define TERMINAL_HIGH_LIGHT() PRINTF("\033[7m")
#define TERMINAL_UN_HIGH_LIGHT() PRINTF("\033[27m")

/* terminal display-------------------------------------------------------END */

#define COMMAND_LEN 24
#define HELP_LEN 200

typedef int (*console_cmd_func_t)(int argc, char **argv);

typedef struct cmd_item_
{
    /**
     * Command name (statically allocated by application)
     */
    char command[COMMAND_LEN];
    /**
     * Help text (statically allocated by application), may be NULL.
     */
    char help[HELP_LEN];

    console_cmd_func_t func; //!< pointer to the command handler

    struct list_head list;

    unsigned char success;
    unsigned char fail;

} cmd_item_t;

void console_init(void);
void console_cmd_register(cmd_item_t *cmd);
void console_run(void);

#endif /*__SHELL_H*/
