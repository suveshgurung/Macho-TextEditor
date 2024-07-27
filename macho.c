#include <stdlib.h>
#include <termios.h>
#include <unistd.h>


struct termios origTermios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
}

void enableRawMode() {

    tcgetattr(STDIN_FILENO, &origTermios);
    atexit(disableRawMode);

    struct termios raw = origTermios;
    raw.c_lflag &= ~(ECHO | ICANON);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {

    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');

    return 0;
}
