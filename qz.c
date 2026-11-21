/*** includes ***/
#include <asm-generic/errno-base.h>
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>
#include <signal.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>

/*** defines ***/

#define QZ_VERSION "0.0.1"
#define QZ_TAB_STOP 8
#define QZ_QUIT_TIMES 3

#ifndef QZ_LINE_NUMBERS
#define QZ_LINE_NUMBERS 1
#endif

#ifndef QZ_SOFT_TABS
#define QZ_SOFT_TABS 1
#endif
#define QZ_INDENT_WIDTH 4

#ifndef QZ_AUTO_INDENT
#define QZ_AUTO_INDENT 1
#endif

#define CTRL_KEY(k) ((k) & 0x1f)

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
  PAGE_DOWN,
  CTRL_ARROW_LEFT,
  CTRL_ARROW_RIGHT,
  CTRL_HOME,
  CTRL_END,
  SHIFT_TAB
};

enum editorHighlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/*** data ***/

struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

typedef struct erow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_open_comment;
} erow;

struct editorConfig {
    int cx, cy;
    int rx;
    int wantrx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int gutter;
    int textcols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    struct timespec mtime;
    char statusmsg[80];
    time_t statusmsg_time;
    struct editorSyntax *syntax;
    struct termios orig_termios;
};

struct editorConfig E;

volatile sig_atomic_t winch_received = 0;

/*** filetypes ***/

char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", NULL
};

struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
void editorUpdateWindowSize();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
  int nread;
  unsigned char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN && errno != EINTR) die("read");
    if (winch_received) {
        winch_received = 0;
        editorUpdateWindowSize();
        editorRefreshScreen();
    }
  }
  if (c == '\x1b') {
    char seq[2];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '['){
        char params[16];
        int plen = 0;
        char final = seq[1];
        while (final >= 0x30 && final <= 0x3f) {
            if (plen < (int)sizeof(params) - 1) params[plen++] = final;
            if (read(STDIN_FILENO, &final, 1) != 1) return '\x1b';
        }
        params[plen] = '\0';

        int num = 1, mod = 1;
        sscanf(params, "%d;%d", &num, &mod);
        if (final == '^') {
            final = '~';
            mod = 5;
        }

        int key = '\x1b';

        if (final == '~') {
            switch (num) {
                case '1': key = HOME_KEY; break;
                case '3': key = DEL_KEY; break;
                case '4': key = END_KEY; break;
                case '5': key = PAGE_UP; break;
                case '6': key = PAGE_DOWN; break;
                case '7': key = HOME_KEY; break;
                case '8': key = END_KEY; break;
            }
      } else {
        switch (final) {
          case 'A': key = ARROW_UP; break;
          case 'B': key = ARROW_DOWN; break;
          case 'C': key = ARROW_RIGHT; break;
          case 'D': key = ARROW_LEFT; break;
          case 'H': key = HOME_KEY; break;
          case 'F': key = END_KEY; break;
          case 'Z': key = SHIFT_TAB; break;
        }
      }

      if (mod == 5) {
          switch (key) {
              case ARROW_RIGHT: return CTRL_ARROW_RIGHT;
              case ARROW_LEFT: return CTRL_ARROW_LEFT;
              case HOME_KEY: return CTRL_HOME;
              case END_KEY: return CTRL_END;
          }
      }
      return key;
    } else if (seq[0] == 'O') {
        switch (seq[1]){
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            case 'c': return CTRL_ARROW_RIGHT;
            case 'd': return CTRL_ARROW_LEFT;
        }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}


int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void editorUpdateWindowSize() {
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("getWindowSize");
    }
    E.screenrows -=2;
}

void handleSigWinch(int sig) {
    (void)sig;
    winch_received = 1;
}

void enableResizeSignal() {
    struct sigaction sa;
    sa.sa_handler = handleSigWinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGWINCH, &sa, NULL) == -1) die("sigaction");
}

/*** syntax highlighting ***/

int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize + 1);
  memset(row->hl, HL_NORMAL, row->rsize);

  if (E.syntax == NULL) return;

  char **keywords = E.syntax->keywords;

  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize) {
    unsigned char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if (scs_len && !in_string && !in_comment) {
        if (!strncmp(&row->render[i], scs, scs_len)) {
            memset(&row->hl[i], HL_COMMENT, row->rsize - i);
            break;
        }
    }

    if (mcs_len && mce_len && !in_string) {
        if (in_comment) {
            row->hl[i] = HL_MLCOMMENT;
            if (!strncmp(&row->render[i], mce, mce_len)) {
                memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                i += mce_len;
                in_comment = 0;
                prev_sep = 1;
                continue;
            } else {
                i++;
                continue;
            }
        } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
            memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
            i += mcs_len;
            in_comment = 1;
            continue;
        }
    }

    if(E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < row->rsize) {
                row->hl[i + 1] = HL_STRING;
                i += 2;
                continue;
            }
            if (c == in_string) in_string = 0;
            i++;
            prev_sep = 1;
            continue;
        } else {
            if (c == '"' || c == '\'') {
                in_string = c;
                row->hl[i] = HL_STRING;
                i++;
                continue;
            }
        }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    if (prev_sep) {
        int j;
        for (j = 0; keywords[j]; j++) {
            int klen = strlen(keywords[j]);
            int kw2 = keywords[j][klen - 1] == '|';
            if (kw2) klen--;

            if (!strncmp(&row->render[i], keywords[j], klen) &&
                is_separator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
        }
        if (keywords[j] != NULL) {
            prev_sep = 0;
            continue;
        }
    }

    prev_sep = is_separator(c);
    i++;
  }

  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows){
      editorUpdateSyntax(&E.row[row->idx + 1]);
  }
}

int editorSyntaxToColor(int hl){
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT: return 36;
        case HL_KEYWORD1: return 33;
        case HL_KEYWORD2: return 32;
        case HL_STRING: return 35;
        case HL_NUMBER: return 31;
        case HL_MATCH: return 34;
        default: return 37;
    }
}

void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL) return;

    char *ext = strrchr(E.filename, '.');

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(E.filename, s->filematch[i]))) {
                E.syntax = s;

                int filerow;
                for (filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }
            }
            i++;
        }
    }
}

/*** unicode ***/

int utf8SeqLen(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 0;
}

int utf8Decode(const char *s, int len, int *cp) {
    const unsigned char *u = (const unsigned char *)s;
    int n = utf8SeqLen(u[0]);
    *cp = -1;
    if (n == 0 || n > len) return 1;
    if (n == 1) {
        *cp = u[0];
        return 1;
    }

    int c = u[0] & (0x7F >> n);
    for (int i = 1; 1 < n; i++) {
        if ((u[i] & 0xC0) != 0x80) return 1;
        c = (c << 6) | (u[i] & 0x3F);
    }

    static const int min[] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (c < min[n] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 1;
    *cp = c;
    return n;
}

int utf8Next(const char *s, int len, int at) {
    int cp;
    if (at >= len) return len;
    return at + utf8Decode(&s[at], len - at, &cp);
}

int utf8Prev(const char *s, int at) {
    if (at <= 0) return 0;
    int start = at - 1;
    while (start > 0 && at - start < 4 &&
            ((unsigned char)s[start] & 0xC0) == 0x80)
        start--;
    int cp;
    if (utf8Decode(&s[start], at - start, &cp) != at - start) start = at - 1;
    return start;
}

int editorCharWidth(const char *s, int len, int *bytes) {
    int cp;
    *bytes = utf8Decode(s, len, &cp);
    int width = (cp == -1) ? -1 : wcwidth(cp);
   if (width < 0) *bytes = 1;
  return width;
}

int editorNextChar(const char *s, int len, int at) {
    int bytes;
    at = utf8Next(s, len, at);
    while (at < len && editorCharWidth(&s[at], len - at, &bytes) == 0)
        at += bytes;
    return at;
}

int editorPrevChar(const char *s, int len, int at) {
    int bytes;
    at = utf8Prev(s, at);
    while (at > 0 && editorCharWidth(&s[at], len - at, &bytes) == 0)
        at = utf8Prev(s, at);
    return at;
}

/*** row operations ***/

int editorCharColumns(const char *s, int len, int col, int *bytes) {
    if (s[0] == '\t') {
        *bytes = 1;
        return QZ_TAB_STOP - col % QZ_TAB_STOP;
    }
    int width = editorCharWidth(s, len, bytes);
    return (width < 0) ? 1 : width;
}

int editorStrWidth(const char *s, int len) {
    int cols = 0;
    int i = 0;
    int bytes;
    while (i < len) {
        cols += editorCharColumns(&s[i], len - i, cols, &bytes);
        i += bytes;
    }
    return cols;
}

int editorStrFit(const char *s, int len, int maxcols) {
    int cols = 0;
    int i = 0;
    int bytes;
    while (i < len) {
        int w = editorCharColumns(&s[i], len - 1, cols, &bytes);
        if (cols + w > maxcols) break;
        cols += w;
        i += bytes;
    }
    return i;
}

int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j = 0;
    int bytes;

    while (j < cx) {
        rx += editorCharColumns(&row->chars[j], row->size - j, rx, &bytes);
        j += bytes;
    }

    return rx;
}

int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0;
    int cx = 0;
    int bytes;
    while(cx < row->size) {
        cur_rx += editorCharColumns(&row->chars[cx], row->size - cx, cur_rx, &bytes);

        if (cur_rx > rx) return cx;
        cx += bytes;
    }
    return cx;
}

int editorRowIndent(erow *row) {
    int n = 0;
    while (n < row->size && (row->chars[n] == ' ' || row->chars[n] == '\t'))
        n++;
    return n;
}

void editorUpdateRow(erow *row){
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++){
        if (row->chars[j] == '\t') tabs++;
    }
    free(row->render);
    row->render = malloc(row->size + tabs*(QZ_TAB_STOP - 1) + 1);

    int idx = 0;
    int col = 0;
    int bytes;
    j = 0;
    while (j < row->size) {
        int cols = editorCharColumns(&row->chars[j], row->size - j, col, &bytes);
        if (row->chars[j] == '\t'){
            memset(&row->render[idx], ' ', cols);
            idx += cols;
        } else {
            memcpy(&row->render[idx], &row->chars[j], bytes);
            idx += bytes;
        }
        col += cols;
        j += bytes;
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

    E.row[at].idx=at;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
    E.numrows--;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChars(erow *row, int at, int len) {
    if (at < 0 || len < 1 || at + len > row->size) return;
    memmove(&row->chars[at], &row->chars[at + len], row->size - at - len + 1);
    row->size -= len;
    editorUpdateRow(row);
    E.dirty++;
}

/*** editor operations ***/

int isWordChar(int c) {
    return c == '_' || (c != EOF && iswalnum(c));
}

int editorRowCodePoint(erow *row, int at) {
    int cp;
    utf8Decode(&row->chars[at], row->size - at, &cp);
    return (cp == -1) ? 0xFFFD : cp;
}

void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertTab() {
    if (!QZ_SOFT_TABS) {
        editorInsertChar('\t');
        return;
    }
    int rx = (E.cy < E.numrows) ? editorRowCxToRx(&E.row[E.cy], E.cx) : 0;
    int spaces = QZ_INDENT_WIDTH - rx % QZ_INDENT_WIDTH;
    while (spaces-- > 0)
        editorInsertChar(' ');
}

void editorUnindentLine() {
    if (E.cy >= E.numrows) return;
    erow *row = &E.row[E.cy];
    int spaces = 0;
    while (spaces < row->size && row->chars[spaces] == ' ') spaces++;

    int n;
    if (spaces > 0) n = spaces - (spaces - 1) / QZ_INDENT_WIDTH * QZ_INDENT_WIDTH;
    else if (row->size > 0 && row->chars[0] == '\t') n = 1;
    else return;

    editorRowDelChars(row, 0, n);
    E.cx = (E.cx > n) ? E.cx - n : 0;
}

void editorInsertNewLine() {
    int indent = 0;
    if (QZ_AUTO_INDENT && E.cy < E.numrows) indent = editorRowIndent(&E.row[E.cy]);
    if (E.cx <= indent) {
        editorInsertRow(E.cy, "", 0);
        indent = E.cx;
    } else {
        erow *row = &E.row[E.cy];
        int restlen = row->size - E.cx;
        char *line = malloc(indent + restlen + 1);
        memcpy(line, row->chars, indent);
        memcpy(&line[indent], &row->chars[E.cx], restlen);
        editorInsertRow(E.cy + 1, line, indent + restlen);
        free(line);
        editorRowDelChars(&E.row[E.cy], E.cx, restlen);
    }
    E.cy++;
    E.cx = indent;
}

void editorDelChar() {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        int start = editorPrevChar(row->chars, row->size, E.cx);
        editorRowDelChars(row, start, E.cx - start);
        E.cx = start;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

void editorBackspace() {
    if (QZ_SOFT_TABS && E.cy < E.numrows && E.cx > 0) {
        erow *row = &E.row[E.cy];
        if (E.cx <= editorRowIndent(row)) {
            int rx = editorRowCxToRx(row, E.cx);
            int stop = (rx - 1) / QZ_INDENT_WIDTH * QZ_INDENT_WIDTH;
            int n = 0;
            while (n < E.cx && n < rx - stop && row->chars[E.cx - 1 - n] == ' ')
                n++;
            if (n > 0) {
                editorRowDelChars(row, E.cx -n, n);
                E.cx -= n;
                return;
            }
        }
    }
    editorDelChar();
}

/*** file i/o ***/

char *editorRowToString(int *buflen) {
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++){
        totlen += E.row[j].size + 1;
    }
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

ssize_t writeAll(int fd, const char *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, buf + done, len - done);
        if (n == -1) {
            if (errno = EINTR) continue;
            return -1;
        }
        done += n;
    }
    return done;
}

void fsyncParentDir(const char *path) {
    char *dir = strdup(path);
    char *slash = strrchr(dir, '/');
    if (slash == dir) slash[1] = '\0';
    else if (slash) *slash = '\0';

    int fd = open(slash ? dir : ".", O_RDONLY);
    if (fd != -1) {
        fsync(fd);
        close(fd);
    }
    free(dir);
}

void editorRememberMtime() {
    struct stat st;
    if (E.filename && stat(E.filename, &st) == 0) {
        E.mtime = st.st_mtim;
    } else {
        E.mtime.tv_sec = 0;
        E.mtime.tv_nsec = 0;
    }
}

int editorFileChangedOnDisk() {
    struct stat st;
    if (E.filename == NULL) return 0;
    if (E.mtime.tv_sec == 0 && E.mtime.tv_nsec == 0) return 0;
    if (stat(E.filename, &st) == -1) return 0;
    return st.st_mtim.tv_sec != E.mtime.tv_sec ||
           st.st_mtim.tv_nsec != E.mtime.tv_nsec;
}

void editorOpen(char *filename){
    free(E.filename);
    E.filename = strdup(filename);

    editorSelectSyntaxHighlight();

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        if (errno == ENOENT) return;
        die("fopen");
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while(linelen > 0 && (line[linelen - 1] == '\n' ||
                             line[linelen - 1] == '\r')) {
            linelen--;
        }
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
    editorRememberMtime();
}

void editorSave() {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    if (editorFileChangedOnDisk()) {
        char *answer = editorPrompt("File changed on disk! Overwrite? (y/n): %s", NULL);
        int yes = answer && (answer[0] == 'y' || answer[0] == 'Y');
        free(answer);
        if (!yes) {
            editorSetStatusMessage("Save aborted");
            return;
        }
    }

    int len;
    char *buf = editorRowToString(&len);
    char *target = realpath(E.filename, NULL);
    if (target == NULL) target = strdup(E.filename);
    char *tmpname = malloc(strlen(target) + 8);
    int fd = -1;
    int saved_errno;
    struct stat st;
    mode_t mode;

    if (stat(target, &st) == 0) {
        mode = st.st_mode & 07777;
    } else {
        mode_t mask = umask(0);
        umask(mask);
        mode = 0666 & ~mask;
    }

    sprintf(tmpname, "%s.XXXXXX", target);
    fd = mkstemp(tmpname);
    if (fd == -1) goto fail_no_tmp;
    if (fchmod(fd, mode) == -1) goto fail;

    if (writeAll(fd, buf, len) != len) goto fail;
    if (fsync(fd) == -1) goto fail;
    if (close(fd) == -1) {
        fd = -1;
        goto fail;
    }
    fd = -1;
    if (rename(tmpname, target) == -1) goto fail;
    fsyncParentDir(target);

    free(target);
    free(tmpname);
    free(buf);
    E.dirty = 0;
    editorRememberMtime();
    editorSetStatusMessage("%d bytes written to disk", len);
    return;


    fail:
        saved_errno = errno;
        if (fd != -1) close(fd);
        unlink(tmpname);
        errno = saved_errno;
    fail_no_tmp:
        editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
        free(target);
        free(tmpname);
        free(buf);
}

/*** find ***/

void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line;
  static char *saved_hl = NULL;

  if (saved_hl) {
      memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
      free(saved_hl);
      saved_hl = NULL;
  }

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }
  if (last_match == -1) direction = 1;
  int current = last_match;
  int i;

  for (i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1) current = E.numrows - 1;
    else if (current == E.numrows) current = 0;
    erow *row = &E.row[current];
    char *match = strstr(row->render, query);

    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, editorStrWidth(row->render, match - row->render));
      E.rowoff = E.numrows;

      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);

      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void editorFind() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
  if (query) {
    free(query);
  } else {
      E.cx = saved_cx;
      E.cy = saved_cy;
      E.coloff = saved_coloff;
      E.rowoff = saved_rowoff;
  }
}

/*** append buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len){
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){
    free(ab->b);
}

/*** output ***/

void editorUpdateGutter() {
    int digits = 1;
    int n = E.numrows;
    while (n >= 10) {
        n /= 10;
        digits++;
    }
    E.gutter = digits + 1;
    if (!QZ_LINE_NUMBERS || E.gutter >= E.screencols) E.gutter = 0;
    E.textcols = E.screencols - E.gutter;
}

void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numrows){
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    int width = 1;
    if (E.cy < E.numrows && E.cx < E.row[E.cy].size) {
        erow *row = &E.row[E.cy];
        int bytes;
        width = editorCharWidth(&row->chars[E.cx], row->size - E.cx,  &bytes);
        if (width < 1) width = 1;
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx + width > E.coloff + E.textcols) {
        E.coloff = E.rx + width - E.textcols;
    }
    if (E.rx < E.coloff){
        E.coloff = E.rx;
    }
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      for (int i = 0; i < E.gutter; i++) abAppend(ab, " ", 1);
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "QZeditor -- version %s", QZ_VERSION);
        if (welcomelen > E.textcols) welcomelen = E.textcols;
        int padding = (E.textcols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      if (E.gutter > 0) {
        char num[16];
        int numlen = snprintf(num, sizeof(num), "%*d ", E.gutter - 1, filerow + 1);
        if (filerow == E.cy) abAppend(ab, "\x1b[1m", 4);
        else abAppend(ab, "\x1b[90m", 5);
        abAppend(ab, num, numlen);
        abAppend(ab, "\x1b[m", 3);
      }
      char *c = E.row[filerow].render;
      unsigned char *hl = E.row[filerow].hl;
      int len = E.row[filerow].rsize;
      int current_color = -1;
      int col = 0;
      int j = 0;
      int bytes;
      int shown = 0;
      while (j < len) {
        int width = editorCharWidth(&c[j], len - j, &bytes);
        int cols = (width < 0) ? 1 : width;
        if (col + cols > E.coloff + E.textcols) break;
        int hide = (col < E.coloff) || (width == 0 && !shown);
        shown = !hide && width >= 0;
        if (hide) {
          for (int k = col; k < col + cols; k++)
              if (k >= E.coloff) abAppend(ab, " ", 1);
        } else if (width < 0) {
          char sym = ((unsigned char)c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], bytes);
        } else {
          int color = editorSyntaxToColor(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], bytes);
        }
        col += cols;
        j += bytes;
      }
      abAppend(ab, "\x1b[39m", 5);
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    char *name = E.filename ? E.filename : "[No Name]";
    int namelen = editorStrFit(name, strlen(name), 20);
    int len = snprintf(status, sizeof(status), "%.*s - %d lines %s", namelen, name, E.numrows, E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);

    if (len >= (int)sizeof(status)) len = sizeof(status) - 1;
    len = editorStrFit(status, len, E.screencols);
    int cols = editorStrWidth(status, len);
    abAppend(ab, status, len);
    while (cols < E.screencols) {
        if (E.screencols - cols == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            cols++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    msglen = editorStrFit(E.statusmsg, msglen, E.screencols);
    if (msglen && time(NULL) - E.statusmsg_time < 5) {
        abAppend(ab, E.statusmsg, msglen);
    }
}

void editorRefreshScreen() {
    editorUpdateGutter();
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + E.gutter + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) {
          buflen = editorPrevChar(buf, buflen, buflen);
          buf[buflen] = '\0';
      }
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } else if (c < 256 && (c >= 128 || !iscntrl(c))) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if (callback) callback(buf, c);
  }
}

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL: &E.row[E.cy];
    if ((key == ARROW_UP || key == ARROW_DOWN) && E.wantrx == -1) {
        E.wantrx = row ? editorRowCxToRx(row, E.cx) : 0;
    }

    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0){
                E.cx = editorPrevChar(row->chars, row->size, E.cx);
            } else if (E.cy > 0){
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size){
                E.cx = editorNextChar(row->chars, row->size, E.cx);
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0){
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows){
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL: &E.row[E.cy];
    if (row && (key == ARROW_UP || key == ARROW_DOWN)) {
        E.cx = editorRowRxToCx(row, E.wantrx);
    }
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen){
        E.cx = rowlen;
    }
}

int editorCharAtCursor() {
    if (E.cy >= E.numrows) return EOF;
    erow *row = &E.row[E.cy];
    if (E.cx < row->size) return editorRowCodePoint(row, E.cx);
    if (E.cy == E.numrows - 1) return EOF;
    return '\n';
}

void editorMoveWordRight() {
    while (editorCharAtCursor() != EOF && !isWordChar(editorCharAtCursor()))
        editorMoveCursor(ARROW_RIGHT);
    while (isWordChar(editorCharAtCursor()))
        editorMoveCursor(ARROW_RIGHT);
}

int editorCharBeforeCursor() {
    if (E.cx > 0) {
        erow *row = &E.row[E.cy];
        return editorRowCodePoint(row, editorPrevChar(row->chars, row->size, E.cx));
    }
    if (E.cy == 0) return EOF;
    return '\n';
}

void editorMoveWordLeft() {
    while (editorCharBeforeCursor() != EOF && !isWordChar(editorCharBeforeCursor()))
        editorMoveCursor(ARROW_LEFT);
    while (isWordChar(editorCharBeforeCursor()))
        editorMoveCursor(ARROW_LEFT);
}

void editorGoToLine() {
    char *answer = editorPrompt("Go to line: %s (ESC to cancel)", NULL);
    if (answer == NULL) return;

    char *end;
    errno = 0;
    long line = strtol(answer, &end, 10);
    if (end == answer || *end != '\0') {
        editorSetStatusMessage("Not a line number: %s", answer);
    } else if (errno == ERANGE || line < 1 || line > E.numrows) {
        editorSetStatusMessage("No line %s (the file has %d", answer, E.numrows);
    } else {
        E.cy = line - 1;
        E.cx = 0;
        if (E.cy < E.rowoff || E.cy >= E.rowoff + E.screenrows) {
            E.rowoff = E.cy - E.screenrows / 2;
            if (E.rowoff < 0) E.rowoff = 0;
        }
    }
    free(answer);
}

void editorProcessKeypress() {
    static int quit_times = QZ_QUIT_TIMES;
    int c = editorReadKey();

    switch (c){
        case '\r':
            editorInsertNewLine();
            break;

        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                    "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }

            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        case '\t':
            editorInsertTab();
            break;

        case SHIFT_TAB:
            editorUnindentLine();
            break;

        case HOME_KEY:
            if (E.cy < E.numrows) {
                int indent = editorRowIndent(&E.row[E.cy]);
                E.cx = (E.cx == indent) ? 0 : indent;
            }
            break;

        case END_KEY:
            if (E.cy < E.numrows) {
                E.cx = E.row[E.cy].size;
            }
            break;

        case CTRL_HOME:
            E.cy = 0;
            E.cx = 0;
            break;

        case CTRL_END:
            if (E.numrows > 0) {
                E.cy = E.numrows - 1;
                E.cx = E.row[E.cy].size;
            }
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;

        case CTRL_KEY('g'):
            editorGoToLine();
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) {
                editorMoveCursor(ARROW_RIGHT);
                editorDelChar();
            } else {
                editorBackspace();
            }
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times;
                if (c == PAGE_UP){
                    times = (E.cy - E.rowoff) + E.screencols;
                } else {
                    int bottom = E.rowoff + E.screenrows - 1;
                    if (bottom > E.numrows) bottom = E.numrows;
                    times = (bottom - E.cy) + E.screenrows;
                }
                while (times-- > 0)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;

        case CTRL_ARROW_LEFT:
            editorMoveWordLeft();
            break;

        case CTRL_ARROW_RIGHT:
            editorMoveWordRight();
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;
        default:
            editorInsertChar(c);
            break;
    }
    if (c != ARROW_UP && c != ARROW_DOWN && c != PAGE_UP && c != PAGE_DOWN) {
        E.wantrx = -1;
    }

    quit_times = QZ_QUIT_TIMES;
}

/*** init ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.wantrx = -1;
    E.rowoff = 0;
    E.coloff = 0;
    E.gutter = 0;
    E.textcols = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.mtime.tv_sec = 0;
    E.mtime.tv_nsec = 0;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL;

    editorUpdateWindowSize();
    enableResizeSignal();
}

int main(int argc, char *argv[]) {
    setlocale(LC_CTYPE, "");
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-G = go to line");

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
