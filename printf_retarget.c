#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#include "dma_uart.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int _isatty(int fd)
{
    if (fd >= STDIN_FILENO && fd <= STDERR_FILENO)
        return 1;

    errno = EBADF;
    return 0;
}

int _write(int fd, char *ptr, int len)
{
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
    {
        // int tx_len = dma_uart_send(ptr, len);
        // if (tx_len > 0)
        // {
        //     return tx_len;
        // }
        // else
        // {
        //     errno = EIO;
        //     return -1;
        // }
        dma_uart_send(ptr, len);
        return len;
    }
    errno = EBADF;
    return -1;
}

int _close(int fd)
{
    if (fd >= STDIN_FILENO && fd <= STDERR_FILENO)
        return 0;

    errno = EBADF;
    return -1;
}

int _lseek(int fd, int ptr, int dir)
{
    (void)fd;
    (void)ptr;
    (void)dir;

    errno = EBADF;
    return -1;
}

int _read(int fd, char *ptr, int len)
{
    if (fd == STDIN_FILENO)
    {
        errno = EIO;
        return -1;
    }
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    if (fd >= STDIN_FILENO && fd <= STDERR_FILENO)
    {
        st->st_mode = S_IFCHR;
        return 0;
    }

    errno = EBADF;
    return 0;
}
