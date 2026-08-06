#include "queue.h"

// 环形缓冲区的结构体
__packed typedef struct
{
    char buffer[BUFFER_SIZE];
    int head;
    int tail;
} CircularBuffer;

static CircularBuffer cirbuffer = {0};

// 初始化缓冲区
void initBuffer(void)
{
    cirbuffer.head = 0;
    cirbuffer.tail = 0;
}

// 写入缓冲区
bool queue_in(char *data)
{
    // 如果写入指针追上读取指针，设置满标志
    if (((cirbuffer.head + 1) % BUFFER_SIZE) == cirbuffer.tail)
    {
        return false;
    }

    cirbuffer.buffer[cirbuffer.head] = *data;
    cirbuffer.head = (cirbuffer.head + 1) % BUFFER_SIZE;

    return true;
}

// 从缓冲区读取数据
bool queue_out(char *data)
{
    if (cirbuffer.head == cirbuffer.tail)
    {
        return false; // 缓冲区为空
    }

    *data = cirbuffer.buffer[cirbuffer.tail];
    cirbuffer.tail = (cirbuffer.tail + 1) % BUFFER_SIZE;

    return true;
}
