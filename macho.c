/* DEFINIION OF THE ESCAPE SEQUENCES.
 *
 * \x1b -> escape character.
 * \x1b[2J -> used to clear the entire screen.
 * \x1b[H -> move the cursor to the home position.
 * \x1b[6n -> query the terminal for the current cursor position.
 * \x1b[999C -> moves the cursor 999 columns to the right.
 * \x1b[999B -> moves the cursor 999 lines down.
 * \x1b[K -> clears the line from the cursor to the end of the line.
 * \x1b[?25l -> hide the cursor in the terminal.
 * \x1b[?25h -> show the cursor in the terminal.
 * \x1b[31m -> changes text color to red.
 * \x1b[39m -> changes text color to default.
 */

/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define MACHO_VERSION "0.0.1"
#define MACHO_TAB_STOP 8
#define MACHO_QUIT_NUM_TIMES 3
#define CTRL_KEY(k) ((k) & 0x1f)

// constants definition of editor keys.
enum editorKey {
    BACKSPACE = 127,
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

enum editorHighlight {
    HL_NORMAL,
    HL_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

/*** variables ***/

struct editorSyntax {
    char *fileType;
    char **fileMatch;
    char **keywords;
    char *singleLineCommentStart;
    int flags;
};

// structure to store the editor text.
typedef struct editorRow {
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *highlight;
} editorRow;

// structure for the editor's configuration.
struct editorConfig {
    int cx;     // cursor x position
    int cy;     // cursor y position
    int rx;     // render position of the tab space.
    int rowOffset;
    int colOffset;
    int screenRows;     // terminal's number of rows.
    int screenColumns;  // terminal's number of columns.
    int numRows;        // number of rows of the text to be written.
    editorRow *row;      // stores the text and the size of the text of each line.
    int dirty;      // tracks if any changes has been made to the file since it has been opened.
    char *fileName;     // stores the name of the current open file.
    char statusMsg[80];     //stores the status message.
    time_t statusMsgTime;   //stores the time at which the status message was written.
    struct editorSyntax *syntax;
    struct termios origTermios;     // struct to store the default (initial) config of the terminal.
};

struct editorConfig E;

/*** filetypes ***/

char *C_HL_extensions[] = { ".c" ,".h", ".cpp", NULL };
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else", "struct", "union", "typedef", "static", "enum", "class", "case", "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void", NULL
};

struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** function prototypes ***/

void setEditorStatusMessage(const char *message, ...);
void refreshEditorScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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

    if (buffer[0] != '\x1b' || buffer[1] != '[') {
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

/*** syntax highlighting ***/

int isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void updateEditorSyntax(editorRow *row) {
    row->highlight = (unsigned char *)realloc(row->highlight, row->rsize);
    memset(row->highlight, HL_NORMAL, row->rsize);

    if (E.syntax == NULL) {
        return;
    }

    char **keywords = E.syntax->keywords;

    char *scs = E.syntax->singleLineCommentStart;
    int scsLen = scs ? strlen(scs) : 0;

    int prevSep = 1;
    int inString = 0;

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prevHighlight = (i > 0) ? row->highlight[i - 1] : HL_NORMAL;

        if (scsLen && !inString) {
            if (!strncmp(&row->render[i], scs, scsLen)) {
                memset(&row->highlight[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (inString) {
                row->highlight[i] = HL_STRING;

                if (c == '\\' && (i + 1 < row->rsize)) {
                    row->highlight[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }

                // string ends.
                if (c == inString) {
                    inString = 0;
                }

                i++;
                prevSep = 1;
                continue;
            } else {
                // string starts.
                if (c == '"' || c == '\'') {
                    inString = c;
                    row->highlight[i] = HL_STRING;

                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prevSep || prevHighlight == HL_NUMBER)) || (c == '.' && prevHighlight == HL_NUMBER)) {
                row->highlight[i] = HL_NUMBER;
                i++;
                prevSep = 0;
                continue;
            }
        }

        if (prevSep) {
            int j;

            // iterate until there are keywords in the array.
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                // check if the keyword has | in the end to color it different.
                int kw2 = keywords[j][klen - 1] == '|';

                if (kw2) {
                    klen--;
                }

                if (!strncmp(&row->render[i], keywords[j], klen) && isSeparator(row->render[i + klen])) {
                    memset(&row->highlight[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }

            if (keywords[j] == NULL) {
                prevSep = 0;
                continue;
            }
        }

        prevSep = isSeparator(c);
        i++;
    }
}

int editorSyntaxToColor(int highlight) {
    switch(highlight) {
        case HL_COMMENT:
            return 36;
        case HL_KEYWORD1:
            return 33;
        case HL_KEYWORD2:
            return 32;
        case HL_STRING:
            return 35;
        case HL_NUMBER:
            return 31;
        case HL_MATCH:
            return 34;
        default:
            return 37;
    }
}

void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.fileName == NULL) {
        return;
    }

    char *extension = strrchr(E.fileName, '.');

    unsigned int j;
    for (j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;

        while (s->fileMatch[i]) {
            int isExtension = (s->fileMatch[i][0] == '.');

            if ((isExtension && extension && !strcmp(extension, s->fileMatch[i])) || (!isExtension && strstr(E.fileName, s->fileMatch[i]))) {
                E.syntax = s;

                int fileRow;
                for (fileRow = 0; fileRow < E.numRows; fileRow++) {
                    updateEditorSyntax(&E.row[fileRow]);
                }

                return;
            }
            i++;
        }
    }
}

/*** row operations ***/

int editorRowCxToRx(editorRow *row, int cx) {
    int rx = 0;
    int j;

    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t') {
            rx += (MACHO_TAB_STOP - 1) - (rx % MACHO_TAB_STOP);
        }
        rx++;
    }

    return rx;
}

int editorRowRxToCx(editorRow *row, int rx) {
    int curRx = 0;
    int cx;

    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t') {
            curRx += (MACHO_TAB_STOP - 1) - (curRx % MACHO_TAB_STOP);
        }
        curRx++;

        if(curRx > rx) {
            return cx;
        }
    }
    
    return cx;
}

void updateEditorRow(editorRow *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            tabs++;
        }
    }

    free(row->render);
    row->render = malloc(row->size + (tabs * (MACHO_TAB_STOP - 1)) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % MACHO_TAB_STOP != 0) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    updateEditorSyntax(row);
}

void insertEditorRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numRows) {
        return;
    }

    E.row = (editorRow *)realloc(E.row, sizeof(editorRow) * (E.numRows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(editorRow) * (E.numRows - at));

    E.row[at].size = len;
    E.row[at].chars = (char *)malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].highlight = NULL;
    updateEditorRow(&E.row[at]);

    E.numRows++;
    E.dirty++;
}

void freeEditorRow(editorRow *row) {
    free(row->render);
    free(row->chars);
    free(row->highlight);
}

void delEditorRow(int at) {
    if (at < 0 || at >= E.numRows) {
        return;
    }

    freeEditorRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(editorRow) * (E.numRows - at - 1));
    E.numRows--;
    E.dirty++;
}

void insertEditorRowCharacter(editorRow *row, int at, int c) {
    if (at < 0 || at > row->size) {
        at = row->size;
    }

    row->chars = (char *)realloc(row->chars, row->size + 2);
    if (row->chars == NULL) {
        die("realloc");
    }

    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;

    updateEditorRow(row);
    E.dirty++;
}

void appendEditorRowString(editorRow *row, char *s, int len) {
    row->chars = (char *)realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    updateEditorRow(row);
    E.dirty++;
}

void delEditorRowChar(editorRow *row, int at) {
    if (at < 0 || at >= row->size) {
        return;
    }

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    updateEditorRow(row);

    E.dirty++;
}

/*** editor operations ***/

void insertEditorChar(int c) {
    if (E.cy == E.numRows) {
        insertEditorRow(E.numRows, "", 0);
    }

    insertEditorRowCharacter(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void insertEditorNewline() {
    if (E.cx == 0) {
        insertEditorRow(E.cy, "", 0);
    } else {
        editorRow *row = &E.row[E.cy];
        insertEditorRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        updateEditorRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void delEditorChar() {
    if (E.cy == E.numRows) {
        E.cy--;
        E.cx = E.row[E.cy].size;
        return;
    }
    if (E.cx == 0 && E.cy == 0) {
        return;
    }

    editorRow *row = &E.row[E.cy];
    if (E.cx > 0) {
        delEditorRowChar(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        appendEditorRowString(&E.row[E.cy - 1], row->chars, row->size);
        delEditorRow(E.cy);
        E.cy--;
    }
}

/*** file i/o ***/

char *editorRowsToString(int *bufLen) {
    int totalLen = 0;
    int j;

    for (j = 0; j < E.numRows; j++) {
        totalLen += E.row[j].size + 1;
    }
    *bufLen = totalLen;

    char *buf = (char *)malloc(totalLen);
    char *p = buf;
    for (j = 0; j < E.numRows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size; 
        *p = '\n';
        p++;
    }

    return buf;
}

void openEditor(char *fileName) {
    free(E.fileName);
    E.fileName = strdup(fileName);
    if (E.fileName == NULL) {
        die("strdup fileName");
    }

    editorSelectSyntaxHighlight();

    FILE *fp = fopen(fileName, "r");
    if (!fp) {
        die("file open error");
    }

    char *line = NULL;
    size_t lineCapacity = 0;
    ssize_t lineLen;

    while ((lineLen = getline(&line, &lineCapacity, fp)) != -1) {
        while (lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) {
            lineLen--; 
        }

        insertEditorRow(E.numRows, line, lineLen);
    }

    free(line);
    fclose(fp);
    E.dirty = 0;
}

void saveEditor() {
    if (E.fileName == NULL) {
        E.fileName = editorPrompt("Save as : %s (ESC to cancel)", NULL);
        if (E.fileName == NULL) {
            setEditorStatusMessage("Save aborted...");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    int len;
    char *buf = editorRowsToString(&len);

    /* 
     * O_RDWR -> allows read and write operation.
     * O_CREAT -> creates new file if it does not exists.
     */
    int fd = open(E.fileName, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty = 0;
                setEditorStatusMessage("\"%s\" %dL, %dB written", E.fileName, E.numRows, len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    setEditorStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

void editorFindCallback(char *query, int key) {
    static int lastMatch = -1;
    static int direction = 1;

    static int savedHighlightLine;
    static unsigned char *savedHighlight = NULL;

    if (savedHighlight) {
        memcpy(E.row[savedHighlightLine].highlight, savedHighlight, E.row[savedHighlightLine].rsize);
        free(savedHighlight);
        savedHighlight = NULL;
    }

    if (key == '\x1b' || key == '\r') {
        lastMatch = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        lastMatch = -1;
        direction = 1;
    }

    if (lastMatch == -1) {
        direction = 1;
    }

    int current = lastMatch;
    int i;
    for (i = 0; i < E.numRows; i++) {
        current += direction;
        if (current == -1) {
            current = E.numRows - 1;
        } else if (current == E.numRows) {
            current = 0;
        }

        editorRow *row = &E.row[current];
        char *match = strstr(row->render, query);

        if (match) {
            lastMatch = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowOffset = E.numRows;

            savedHighlightLine = current;
            savedHighlight = (unsigned char *)malloc(row->rsize);
            memcpy(savedHighlight, row->highlight, row->rsize);

            memset(&row->highlight[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind() {
    int prevCx = E.cx;
    int prevCy = E.cy;
    int prevColOffset = E.colOffset;
    int prevRowOffset = E.rowOffset;

    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

    if (query) {
        free(query);
    } else {
        E.cx = prevCx;
        E.cy = prevCy;
        E.colOffset = prevColOffset;
        E.rowOffset = prevRowOffset;
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

void scrollEditor() {
    E.rx = E.cx;
    if (E.cy < E.numRows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowOffset) {
        E.rowOffset = E.cy;
    }
    if (E.cy >= E.rowOffset + E.screenRows) {
        E.rowOffset = E.cy - E.screenRows + 1;
    }
    if (E.rx < E.colOffset) {
        E.colOffset = E.rx;
    }
    if (E.rx >= E.colOffset + E.screenColumns) {
        E.colOffset = E.rx - E.screenColumns + 1;
    }
}

void drawEditorRows(struct abuf *ab) {
    int y;

    for (y = 0; y < E.screenRows; y++) {
        int fileRow = y + E.rowOffset;
        if (fileRow >= E.numRows) {
            if (E.numRows == 0 && y == E.screenRows / 3) {
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
        } else {
            int len = E.row[fileRow].rsize - E.colOffset;
            if (len < 0) {
                len = 0;
            }
            if (len > E.screenColumns) {
                len = E.screenColumns;
            }

            char *c = &E.row[fileRow].render[E.colOffset];
            unsigned char *highlight = &E.row[fileRow].highlight[E.colOffset];
            int currColor = -1;
            int j;

            for (j = 0; j < len; j++) {
                if (highlight[j] == HL_NORMAL) {
                    if (currColor != -1) {
                        abAppend(ab, "\x1b[39m", 5);
                        currColor = -1;
                    }
                    abAppend(ab, &c[j], 1);
                } else {
                    int color = editorSyntaxToColor(highlight[j]);
                    if (color != currColor) {
                        currColor = color;
                        char buf[16];

                        int writeLen = snprintf(buf, sizeof(buf),   "\x1b[%dm", color);
                        abAppend(ab, buf, writeLen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void drawEditorStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);

    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.fileName ? E.fileName : "[No Name]", E.numRows, E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->fileType : "no filetype", E.cy + 1, E.numRows);
    if (len > E.screenColumns) {
        len = E.screenColumns;
    }
    abAppend(ab, status, len);

    while (len < E.screenColumns) {
        if (E.screenColumns - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void drawEditorMessageBox(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\x1b[1m", 4);

    int msgLen = strlen(E.statusMsg);
    if (msgLen > E.screenColumns) {
        msgLen = E.screenColumns;
    }
    if (msgLen && time(NULL) - E.statusMsgTime < 5) {
        abAppend(ab, E.statusMsg, msgLen);
    }
    abAppend(ab, "\x1b[m", 3);
}

void refreshEditorScreen() {
    scrollEditor();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    drawEditorRows(&ab);
    drawEditorStatusBar(&ab);
    drawEditorMessageBox(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOffset) + 1, (E.rx - E.colOffset) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.length);

    abFree(&ab);
}

void setEditorStatusMessage(const char *message, ...) {
    va_list args;
    va_start(args, message);

    vsnprintf(E.statusMsg, sizeof(E.statusMsg), message, args);
    va_end(args);

    E.statusMsgTime = time(NULL);
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufSize = 128;
    char *buf = (char *)malloc(bufSize);

    size_t bufLen = 0;
    buf[0] = '\0';

    while(1) {
        setEditorStatusMessage(prompt, buf);
        refreshEditorScreen();

        int c = readEditorKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (bufLen != 0) {
                buf[--bufLen] = '\0';
            }
        } else if (c == '\x1b') {
            setEditorStatusMessage("");
            if (callback) {
                callback(buf, c);
            }
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (bufLen != 0) {
                setEditorStatusMessage("");
                if (callback) {
                    callback(buf, c);
                }
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (bufLen == bufSize - 1) {
                bufSize *= 2;
                buf = (char *)realloc(buf, bufSize);
            }
            buf[bufLen++] = c;
            buf[bufLen] = '\0';
        }

        if (callback) {
            callback(buf, c);
        }
    }
}

void moveEditorCursor(int key) {

    editorRow *row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];

    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cy < E.numRows) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numRows) {
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
    int rowLen = row ? row->size : 0;
    if (E.cx > rowLen) {
        E.cx = rowLen;
    }
}

void processEditorKeypress() {
    static int quitTimes = MACHO_QUIT_NUM_TIMES;
    int c = readEditorKey();

    switch (c) {
        case '\r':
            insertEditorNewline();
            break;

        case CTRL_KEY('q'):
            if (E.dirty && quitTimes > 0) {
                setEditorStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quitTimes);
                quitTimes--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'):
            saveEditor();
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if (E.cy < E.numRows) {
                E.cx = E.row[E.cy].size;
            }
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) {
                moveEditorCursor(ARROW_RIGHT);
            }
            delEditorChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    E.cy = E.rowOffset;
                } else if (c == PAGE_DOWN) {
                    E.cy = E.rowOffset + E.screenRows - 1;
                    if (E.cy > E.numRows) {
                        E.cy = E.numRows;
                    }
                }

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
            moveEditorCursor(c);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            /* TODO */
            break;

        default:
            insertEditorChar(c);
            break;
    }

    quitTimes = MACHO_QUIT_NUM_TIMES;
}

/*** init ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowOffset = 0;
    E.colOffset = 0;
    E.numRows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.fileName = NULL;
    E.statusMsg[0] = '\0';
    E.statusMsgTime = 0;
    E.syntax = NULL;

    if (getWindowSize(&E.screenRows, &E.screenColumns) == -1) {
        die("getWindowSize error");
    }
    E.screenRows -= 2;
}

int main(int argc, char *argv[]) {

    enableRawMode();
    initEditor();

    if (argc >= 2) {
        openEditor(argv[1]);
    }

    setEditorStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (1) {
        refreshEditorScreen();
        processEditorKeypress();
    }

    return 0;
}
