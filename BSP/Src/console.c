#include "console.h"
#include "queue.h"

#define HANDLE_LEN 128
#define MAX_ARGS 6

__packed typedef struct
{
    uint8_t buff[BUFFER_SIZE];
    int len;
} HANDLE_TYPE_S;

static struct list_head cmd_list;
static HANDLE_TYPE_S Handle = {0};

extern size_t console_split_argv(char *line, char **argv, size_t argv_size);

void console_run(void)
{
    char data = 0;
    cmd_item_t *pos, *ppos;
    /* 通过死循环一个一个读出所有串口数据 */
    while (queue_out(&data))
    {
        if (Handle.len > BUFFER_SIZE)
        {
            /* 缓冲区太满，提示对方输入超出限制 */
            break;
        }

        switch (data)
        {
        case KEY_BACKSPACE:
            if (Handle.len)
            {
                /* 缓冲区不是空的，光标左移，删除最后一个字符 */
                TERMINAL_CLEAR_END();
                /* 开始删除字符 */
                Handle.len--;
            }
            break;

        case KEY_ENTER:
            /* 只输入了一个回车键 */
            if (!Handle.len)
            {
                Handle.len = 0;
                PRINTF("\r\n-> console:");
                break;
            }
            /* 处理解析函数 */
            goto deal;
        default:
            Handle.buff[Handle.len++] = data;
            break;
        }
    }

    return;

deal:
    Handle.buff[Handle.len] = '\0';
    char *argv[MAX_ARGS];

    /* 解析命令
     *    console_split_argv
     *	  返回解析的参数个数，入参:要解析的字符串，存储解析参数的区域，最大解析参数个数
     *
     * -> console:set -i 192.168.1.66 -o test
     *		Parsed 5 arguments:
     *		argv[0]: set
     *		argv[1]: -i
     *		argv[2]: 192.168.1.66
     *		argv[3]: -o
     *		argv[4]: test
     *
     */
    size_t argc = console_split_argv((char *)Handle.buff, argv, MAX_ARGS);

    /* 调用链表寻找解析函数处理 */
    list_for_each_entry_safe(pos, ppos, &cmd_list, list)
    {
        if (strcmp(pos->command, argv[0]) == 0)
        {
            /* 调用回调函数 */
            if (!pos->func(argc, argv))
            {
                PRINTF("\r\n-> PARAME. ERR\r\n");
            }
            goto clear;
        }
    }

    PRINTF("\r\n-> CMD ERR, try: help\r\n\r\n");

clear:
    PRINTF("\r\n-> console:");
    memset(Handle.buff, 0, BUFFER_SIZE);
    Handle.len = 0;
}

static int console_clear(int argc, char **argv)
{
    TERMINAL_RESET_CURSOR();
    TERMINAL_DISPLAY_CLEAR();

    return true;
}

static int console_show(int argc, char **argv)
{

    PRINTF("\r\n");
    cmd_item_t *pos, *ppos;
    list_for_each_entry_safe(pos, ppos, &cmd_list, list)
    {
        if (strlen(pos->command) > 3)
        {
            // 过滤掉一些系统命令
            continue;
        }
        PRINTF("%s %s         success %d  fail  %d\r\n", pos->command, pos->help, pos->success, pos->fail);
    }

    return true;
}

static int console_test(int argc, char **argv)
{

    cmd_item_t *pos, *ppos;
    list_for_each_entry_safe(pos, ppos, &cmd_list, list)
    {
        if (strlen(pos->command) > 3)
        {
            // 过滤掉一些系统命令

            continue;
        }

        if (pos->func(argc, argv))
        {
            pos->success++;
        }
        else
        {
            pos->fail++;
        }
    }
    return true;
}

static int console_help(int argc, char **argv)
{

    cmd_item_t *pos, *ppos;
    list_for_each_entry_safe(pos, ppos, &cmd_list, list)
    {
        PRINTF("\r\n-> CMD[%s] %s\r\n", pos->command, pos->help);
    }

    return 1;
}

/* 注册命令到系统链表中 */
void console_cmd_register(cmd_item_t *cmd)
{
    cmd_item_t *node;
    if (!cmd)
    {
        printf("cmd is error\n");
        return;
    }

    node = (cmd_item_t *)malloc(sizeof(cmd_item_t));
    strcpy(node->command, (const char *)cmd->command);
    strcpy(node->help, (const char *)cmd->help);
    node->func = cmd->func;

    list_add_tail(&node->list, &cmd_list);
}

static void system_cmd_register(void)
{
    const cmd_item_t clearcmd = {
        .command = "clear",
        .help = "\r\n* Clear the screen\r\n",
        .func = &console_clear,
    };

    console_cmd_register((cmd_item_t *)&clearcmd);

    const cmd_item_t helpcmd = {
        .command = "help",
        .help = "\r\n* Show all commands\r\n",
        .func = &console_help,
    };

    console_cmd_register((cmd_item_t *)&helpcmd);

    const cmd_item_t showcmd = {
        .command = "show",
        .help = "\r\n* 显示测试结果\r\n",
        .func = &console_show,
    };

    console_cmd_register((cmd_item_t *)&showcmd);

    const cmd_item_t testcmd = {
        .command = "auto",
        .help = "\r\n* 自动测试 \r\n",
        .func = &console_test,
    };

    console_cmd_register((cmd_item_t *)&testcmd);
}

void console_init(void)
{
    INIT_LIST_HEAD(&cmd_list);
    system_cmd_register();

    TERMINAL_DISPLAY_CLEAR();
    TERMINAL_RESET_CURSOR();

    PRINTF("-------------------------------\r\n\r\n");
    TERMINAL_HIGH_LIGHT();
    INFO("    Console version: V1.0          \r\n\r\n");
    INFO("    coder:  XS                 	   \r\n\r\n");
    TERMINAL_UN_HIGH_LIGHT();
    PRINTF("-------------------------------\r\n\r\n");
    PRINTF("\r\n-> console:");
}
