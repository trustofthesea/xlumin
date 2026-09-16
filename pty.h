#ifndef PTY_H
#define PTY_H

#include <sys/types.h>

extern int pty_master_fd;
extern pid_t pty_child_pid;

int pty_spawn(char *shell);
void pty_resize(int rows, int cols);

#endif
