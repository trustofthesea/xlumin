#include "pty.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pty.h>
#include <sys/ioctl.h>

int pty_master_fd = -1;
pid_t pty_child_pid = -1;

int pty_spawn(char *shell) {
    pty_child_pid = forkpty(&pty_master_fd, NULL, NULL, NULL);

    if (pty_child_pid < 0) {
        perror("forkpty failed");
        return -1;
    } else if (pty_child_pid == 0) {
        // xterm larp so gnutils like clear actually know how to talk to us
        setenv("TERM", "xlumin", 1);
        execlp(shell, shell, NULL);
        perror("execlp failed");
        exit(1);
    }

    return 0;
}

void pty_resize(int rows, int cols) {
    struct winsize ws = {0};
    ws.ws_row = rows;
    ws.ws_col = cols;
    ioctl(pty_master_fd, TIOCSWINSZ, &ws);
}
