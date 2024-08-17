/*** includes ***/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define MACHO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum editorKeyV {
    ARROW_LEFTV = 'h',
    ARROW_RIGHTV = 'l',
    ARROW_UPV = 'k',
    ARROW_DOWNV = 'j',
};

/*** variables ***/

struct editorConfig {
    int cx;     // cursor x position
    int cy;     // cursor y position
    int screenRows;
    int screenColumns;
    struct termios origTermios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(EXIT_FAILURE);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.origTermios) == -1) {
        die("tcsetattr error");
    }
}

void enableRawMode() {

    // get current terminal attributes.
    if (tcgetattr(STDIN_FILENO, &E.origTermios) == -1) {
        die("tcgetattr error");
    }
    // call disableRawMode when program exits.
    atexit(disableRawMode);

struct termios raw = E.origTermios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr error");
    }   
}

int readEditorKey() {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read error");
        }
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        } else if (seq[0] == 'o') {
            switch (seq[1]) {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buffer[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    while (i < sizeof(buffer) - 1) {
        if (read(STDIN_FILENO, &buffer[i], 1) != 1) {
            break;
        }
        if (buffer[i] == 'R') {
            break;
        }
        i++;
    }
    buffer[i] = '\0';

    if (buffer[0] != '\x1b' ||buffer[1] != '[') {
        return -1;
    }
    if (sscanf(&buffer[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/

struct abuf {
    char *b;
    int length;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = (char *)realloc(ab->b, ab->length + len);

    if (new == NULL) {
        return;
    }

    memcpy(&new[ab->length], s, len);
    ab->b = new;
    ab->length += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

void drawEditorRows(struct abuf *ab) {
    int y;

    for (y = 0; y < E.screenRows; y++) {

        if (y == E.screenRows / 3) {
            char welcome[80];
            int welcomeLen = snprintf(welcome, sizeof(welcome), "Macho Editor -- version %s", MACHO_VERSION);

            if (welcomeLen > E.screenColumns) {
                welcomeLen = E.screenColumns;
            }

            int padding = (E.screenColumns - welcomeLen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }

            while(padding--) {
                abAppend(ab, " ", 1);
            }

            abAppend(ab, welcome, welcomeLen);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenRows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void refreshEditorScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    drawEditorRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.length);

    abFree(&ab);
}

/*** input ***/

void moveEditorCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.cx <= 0) {
                break;
            }
            E.cx--;
            break;
        case ARROW_RIGHT:
            if (E.cx >= E.screenColumns) {
                break;
            }
            E.cx++;
            break;
        case ARROW_UP:
            if (E.cy <= 0) {
                break;
            }
            E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy >= E.screenRows) {
                break;
            }
            E.cy++;
            break;
        case ARROW_LEFTV:
            if (E.cx <= 0) {
                break;
            }
            E.cx--;
            break;
        case ARROW_RIGHTV:
            if (E.cx >= E.screenColumns) {
                break;
            }
            E.cx++;
            break;
        case ARROW_UPV:
            if (E.cy <= 0) {
                break;
            }
            E.cy--;
            break;
        case ARROW_DOWNV:
            if (E.cy >= E.screenRows) {
                break;
            }
            E.cy++;
            break;
    }
}

void processEditorKeypress() {
    int c = readEditorKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screenColumns;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenRows;
                while (times--) {
                    moveEditorCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UPV:
        case ARROW_DOWNV:
        case ARROW_RIGHTV:
        case ARROW_LEFTV:
            moveEditorCursor(c);
            break;
    }
}

/*** init ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screenRows, &E.screenColumns) == -1) {
        die("getWindowSize error");
    }
}

int main() {

    enableRawMode();
    initEditor();

    while (1) {
        refreshEditorScreen();
        processEditorKeypress();
    }

    return 0;
}
