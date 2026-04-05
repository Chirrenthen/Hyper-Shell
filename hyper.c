// =============================================================
//  HYPER SHELL v2.0  -  Windows Advanced Shell
//  Pure ASCII edition: no UTF-8 box chars, no emoji
//  Compiles with: gcc hyper.c -o hyper.exe -lm
// =============================================================

// Windows Native Advanced Shell
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <windows.h>

// ANSI VT flag (missing in old MinGW wincon.h)
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#  define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

// ARM64 constant (missing in old MinGW SDK)
#ifndef PROCESSOR_ARCHITECTURE_ARM64
#  define PROCESSOR_ARCHITECTURE_ARM64 12
#endif

// Console font structs loaded at runtime - avoids missing header issues
typedef struct {
    ULONG cbSize;
    DWORD nFont;
    COORD dwFontSize;
    UINT  FontFamily;
    UINT  FontWeight;
    WCHAR FaceName[32];
} HYPER_FONT_INFOEX;
typedef BOOL (WINAPI *PFN_GetFontEx)(HANDLE, BOOL, HYPER_FONT_INFOEX*);
typedef BOOL (WINAPI *PFN_SetFontEx)(HANDLE, BOOL, HYPER_FONT_INFOEX*);

// =============================================================
//  CONSTANTS
// =============================================================

#define MAX_INPUT  2048
#define MAX_ARGS   128
#define MAX_HIST   100
#define MAX_ALIAS  32
#define VERSION    "2.0"

// =============================================================
//  ANSI COLOR MACROS  (pure 7-bit escape sequences)
// =============================================================

#define RST        "\033[0m"
#define BOLD       "\033[1m"
#define DIM        "\033[2m"

#define FG_RED     "\033[91m"
#define FG_GREEN   "\033[92m"
#define FG_YELLOW  "\033[93m"
#define FG_BLUE    "\033[94m"
#define FG_MAGENTA "\033[95m"
#define FG_CYAN    "\033[96m"
#define FG_WHITE   "\033[97m"
#define FG_DGRAY   "\033[90m"
#define FG_DGREEN  "\033[32m"
#define FG_DCYAN   "\033[36m"
#define FG_DYELLOW "\033[33m"

#define CLEAR_LINE "\033[2K"

// Status printers - pure ASCII symbols only
#define OK(msg)   printf("  " FG_GREEN  BOLD "[OK]  " RST FG_WHITE  "%s" RST "\n", msg)
#define ERR(msg)  printf("  " FG_RED    BOLD "[!!]  " RST FG_RED    "%s" RST "\n", msg)
#define WARN(msg) printf("  " FG_YELLOW BOLD "[**]  " RST FG_YELLOW "%s" RST "\n", msg)
#define INFO(msg) printf("  " FG_CYAN   BOLD "[--]  " RST FG_CYAN   "%s" RST "\n", msg)

// =============================================================
//  GLOBAL STATE
// =============================================================

int    running      = 1;
HANDLE serialHandle = INVALID_HANDLE_VALUE;

char   history[MAX_HIST][MAX_INPUT];
int    hist_count = 0;

typedef struct { char name[64]; char value[256]; } Alias;
Alias  aliases[MAX_ALIAS];
int    alias_count = 0;

char   cwd[MAX_PATH];

// =============================================================
//  CONSOLE SETUP
// =============================================================

void enable_ansi() {
    // Force UTF-8 codepage
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    // Enable ANSI/VT escape processing
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }

    SetConsoleTitleA("HYPER SHELL v" VERSION);

    // Load font API at runtime - safe on all MinGW versions
    HMODULE hK = GetModuleHandleA("kernel32.dll");
    if (hK) {
        PFN_GetFontEx pfnGet = (PFN_GetFontEx)
            GetProcAddress(hK, "GetCurrentConsoleFontEx");
        PFN_SetFontEx pfnSet = (PFN_SetFontEx)
            GetProcAddress(hK, "SetCurrentConsoleFontEx");
        if (pfnGet && pfnSet) {
            HYPER_FONT_INFOEX cfi;
            memset(&cfi, 0, sizeof(cfi));
            cfi.cbSize = sizeof(cfi);
            pfnGet(hOut, FALSE, &cfi);
            cfi.dwFontSize.Y = 18;
            wcscpy(cfi.FaceName, L"Consolas");
            pfnSet(hOut, FALSE, &cfi);
        }
    }
}

// =============================================================
//  BANNER  (pure ASCII - no box-drawing or unicode)
// =============================================================

void banner() {
    printf(FG_CYAN BOLD
    "\n"
    "  ##  ##  ##  ##  #####   #####  ####  \n"
    "  ##  ##  ##  ##  ##  ##  ##     ##  ##\n"
    "  ######   ####   #####   ####   ####  \n"
    "  ##  ##    ##    ##      ##     ##  ##\n"
    "  ##  ##    ##    ##      #####  ##  ##\n"
    RST
    FG_DGRAY
    "  Windows Advanced Shell  v" VERSION
    RST "\n\n");
}

// =============================================================
//  BOOT SEQUENCE
// =============================================================

void boot() {
    system("cls");
    banner();

    const char *steps[]  = {
        "Booting kernel   ",
        "Loading modules  ",
        "Init network     ",
        "Starting engine  "
    };
    const char *colors[] = {
        FG_DCYAN, FG_DGREEN, FG_DYELLOW, FG_MAGENTA
    };

    for (int i = 0; i < 4; i++) {
        printf("  %s%s" RST, colors[i], steps[i]);
        fflush(stdout);
        Sleep(300);
        printf(FG_GREEN BOLD " [OK]\n" RST);
    }

    printf("\n"
           FG_CYAN BOLD "  >> HYPER SHELL READY" RST
           FG_DGRAY " (type 'help' to get started)\n\n" RST);
}

// =============================================================
//  PROMPT
// =============================================================

void show_prompt() {
    GetCurrentDirectoryA(MAX_PATH, cwd);

    char user[128] = "user";
    DWORD usz = sizeof(user);
    GetUserNameA(user, &usz);

    SYSTEMTIME st;
    GetLocalTime(&st);

    printf(
        FG_DGRAY "[" FG_YELLOW "%02d:%02d:%02d" FG_DGRAY "] "
        FG_GREEN BOLD "%s" RST
        FG_DGRAY "@" RST
        FG_CYAN BOLD "HYPER" RST
        FG_DGRAY ":" RST
        FG_BLUE BOLD "%s" RST "\n"
        FG_CYAN BOLD ">>> " RST,
        st.wHour, st.wMinute, st.wSecond,
        user, cwd
    );
    fflush(stdout);
}

// =============================================================
//  PARSER
// =============================================================

void parse(char *input, char **args) {
    int i = 0;
    args[i] = strtok(input, " \t\n");
    while (args[i] && i < MAX_ARGS - 1)
        args[++i] = strtok(NULL, " \t\n");
    args[i] = NULL;
}

// =============================================================
//  HISTORY
// =============================================================

void hist_add(const char *cmd) {
    char buf[MAX_INPUT];
    strncpy(buf, cmd, MAX_INPUT - 1);
    buf[strcspn(buf, "\n")] = '\0';
    if (!buf[0]) return;
    if (hist_count < MAX_HIST)
        strncpy(history[hist_count++], buf, MAX_INPUT - 1);
    else {
        memmove(history[0], history[1],
                sizeof(char) * MAX_INPUT * (MAX_HIST - 1));
        strncpy(history[MAX_HIST - 1], buf, MAX_INPUT - 1);
    }
}

void cmd_history() {
    printf(FG_CYAN BOLD "\n  -- Command History --\n\n" RST);
    for (int i = 0; i < hist_count; i++)
        printf("  " FG_DGRAY "%3d  " RST FG_WHITE "%s\n" RST,
               i + 1, history[i]);
    if (!hist_count) WARN("No history yet.");
    printf("\n");
}

// =============================================================
//  ALIAS
// =============================================================

const char* alias_get(const char *name) {
    for (int i = 0; i < alias_count; i++)
        if (strcmp(aliases[i].name, name) == 0)
            return aliases[i].value;
    return NULL;
}

void cmd_alias(char **args) {
    if (!args[1]) {
        printf(FG_CYAN BOLD "\n  -- Aliases --\n\n" RST);
        for (int i = 0; i < alias_count; i++)
            printf("  " FG_YELLOW "%-16s" RST " = "
                   FG_GREEN "%s\n" RST,
                   aliases[i].name, aliases[i].value);
        if (!alias_count) WARN("No aliases defined.");
        printf("\n");
        return;
    }
    if (!args[2]) { ERR("Usage: alias <name> <command>"); return; }
    if (alias_count >= MAX_ALIAS) { ERR("Alias table full."); return; }
    strncpy(aliases[alias_count].name, args[1], 63);
    char val[256] = "";
    for (int i = 2; args[i]; i++) {
        if (i > 2) strncat(val, " ", 255);
        strncat(val, args[i], 255);
    }
    strncpy(aliases[alias_count].value, val, 255);
    alias_count++;
    printf(FG_GREEN "  [OK] Alias '" FG_YELLOW "%s"
           FG_GREEN "' -> '" FG_YELLOW "%s"
           FG_GREEN "'\n" RST, args[1], val);
}

// =============================================================
//  ANIMATIONS  (pure ASCII spinner + block progress bar)
// =============================================================

void animate_spinner(int frames, int delay_ms, const char *msg) {
    const char spin[] = "|/-\\";
    for (int i = 0; i < frames; i++) {
        printf("\r  " FG_CYAN "%c" RST "  %s", spin[i % 4], msg);
        fflush(stdout);
        Sleep(delay_ms);
    }
    printf("\r" CLEAR_LINE "\n");
}

void animate_progress(int steps, const char *label) {
    int width = 38;
    printf("\n  " FG_CYAN "%s\n" RST, label);
    for (int s = 0; s <= steps; s++) {
        int filled = (s * width) / steps;
        printf("\r  " FG_DGRAY "[" RST);
        for (int i = 0; i < width; i++) {
            if (i < filled)
                printf(FG_CYAN "#" RST);
            else
                printf(FG_DGRAY "." RST);
        }
        printf(FG_DGRAY "]" RST FG_YELLOW " %3d%%", (s * 100) / steps);
        fflush(stdout);
        Sleep(40);
    }
    printf(RST "\n  " FG_GREEN BOLD "[OK] Done\n\n" RST);
}

// =============================================================
//  NETWORK
// =============================================================

void cmd_ping(char **args) {
    if (!args[1]) { ERR("Usage: ping <host>"); return; }
    char cmd[512];
    printf(FG_CYAN "\n  Pinging " BOLD "%s" RST FG_CYAN "...\n\n" RST,
           args[1]);
    snprintf(cmd, sizeof(cmd), "ping -n 4 %s", args[1]);
    system(cmd);
}

void cmd_check(char **args) {
    if (!args[1]) { ERR("Usage: check <url>"); return; }
    char cmd[1024];
    printf(FG_CYAN "\n  Checking " BOLD "%s" RST FG_CYAN "...\n\n" RST,
           args[1]);
    snprintf(cmd, sizeof(cmd), "curl -Is --max-time 5 %s", args[1]);
    system(cmd);
}

void cmd_wget(char **args) {
    if (!args[1]) { ERR("Usage: wget <url>"); return; }
    char cmd[1024];
    printf(FG_CYAN "\n  Downloading " BOLD "%s" RST FG_CYAN "...\n\n" RST,
           args[1]);
    snprintf(cmd, sizeof(cmd), "curl -L -O \"%s\"", args[1]);
    system(cmd);
}

void cmd_ipinfo() {
    printf(FG_CYAN "\n  Fetching public IP info...\n\n" RST);
    system("curl -s https://ipinfo.io");
    printf("\n\n");
}

// =============================================================
//  SYSINFO
// =============================================================

void cmd_sysinfo() {
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    char computer[256] = "unknown";
    DWORD csz = sizeof(computer);
    GetComputerNameA(computer, &csz);

    char user[128] = "unknown";
    DWORD usz = sizeof(user);
    GetUserNameA(user, &usz);

    /* GetTickCount64 is unreliable on old MinGW toolchains;
       GetTickCount (32-bit ms, wraps ~49 days) is sufficient
       for displaying shell uptime.                          */
    ULONGLONG upms = (ULONGLONG)GetTickCount();
    int uph = (int)(upms / 3600000ULL);
    int upm = (int)((upms % 3600000ULL) / 60000ULL);

    ULONGLONG ram_total = ms.ullTotalPhys / (1024ULL * 1024ULL);
    ULONGLONG ram_avail = ms.ullAvailPhys / (1024ULL * 1024ULL);
    ULONGLONG ram_used  = ram_total - ram_avail;
    int       ram_pct   = (ram_total > 0)
                          ? (int)((ram_used * 100ULL) / ram_total)
                          : 0;

    const char *arch =
        (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
            ? "x64 (AMD64)"
        : (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
            ? "ARM64"
        : "x86";

    printf(FG_CYAN BOLD
           "\n  +----------------------------------+\n"
           "  |      SYSTEM  INFORMATION         |\n"
           "  +----------------------------------+\n\n" RST);

    #define ROW(k, fmt, ...) \
        printf("  " FG_DGRAY "%-14s" RST FG_WHITE fmt RST "\n", \
               k, ##__VA_ARGS__)

    ROW("Computer:",  "%s",  computer);
    ROW("User:",      "%s",  user);
    ROW("CPU Cores:", "%lu", si.dwNumberOfProcessors);
    ROW("Arch:",      "%s",  arch);
    ROW("Uptime:",    "%dh %dm", uph, upm);

    // RAM bar
    int bar  = 32;
    int fill = (ram_pct * bar) / 100;
    printf("  " FG_DGRAY "%-14s" RST FG_DGRAY "[" RST, "RAM:");
    for (int i = 0; i < bar; i++) {
        if (i < fill)
            printf(ram_pct > 80 ? FG_RED "#" RST : FG_GREEN "#" RST);
        else
            printf(FG_DGRAY "." RST);
    }
    printf(FG_DGRAY "]" RST
           FG_YELLOW " %llu" RST "/" FG_WHITE "%llu MB"
           RST " (%d%%)\n\n",
           ram_used, ram_total, ram_pct);

    #undef ROW
}

// =============================================================
//  FILE LISTING  (colored by type)
// =============================================================

void cmd_ls(char **args) {
    char path[MAX_PATH] = ".";
    if (args[1]) strncpy(path, args[1], MAX_PATH - 1);

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        ERR("Cannot list directory.");
        return;
    }

    printf(FG_CYAN BOLD "\n  -- %s --\n\n" RST, path);

    int col = 0;
    do {
        const char *name = fd.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            printf("  " FG_BLUE BOLD "%-24s" RST, name);
        } else {
            const char *ext = strrchr(name, '.');
            if (ext) {
                if (!strcmp(ext,".exe")||!strcmp(ext,".bat")||!strcmp(ext,".cmd"))
                    printf("  " FG_GREEN  "%-24s" RST, name);
                else if (!strcmp(ext,".c")||!strcmp(ext,".cpp")||
                         !strcmp(ext,".h")||!strcmp(ext,".py"))
                    printf("  " FG_YELLOW "%-24s" RST, name);
                else if (!strcmp(ext,".txt")||!strcmp(ext,".md")||
                         !strcmp(ext,".log"))
                    printf("  " FG_WHITE  "%-24s" RST, name);
                else if (!strcmp(ext,".zip")||!strcmp(ext,".tar")||
                         !strcmp(ext,".gz")||!strcmp(ext,".rar"))
                    printf("  " FG_RED    "%-24s" RST, name);
                else
                    printf("  " FG_DGRAY  "%-24s" RST, name);
            } else {
                printf("  " FG_DGRAY "%-24s" RST, name);
            }
        }
        if (++col % 3 == 0) printf("\n");
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    if (col % 3 != 0) printf("\n");
    printf("\n"
        "  " FG_BLUE  "[dir]=Blue  " RST
        FG_GREEN  "[exe]=Green  " RST
        FG_YELLOW "[src]=Yellow  " RST
        FG_WHITE  "[txt]=White  " RST
        FG_RED    "[arc]=Red\n\n" RST);
}

// =============================================================
//  CD BUILT-IN
// =============================================================

void cmd_cd(char **args) {
    if (!args[1]) {
        const char *home = getenv("USERPROFILE");
        if (home) SetCurrentDirectoryA(home);
        return;
    }
    if (!SetCurrentDirectoryA(args[1]))
        ERR("Directory not found.");
}

// =============================================================
//  ECHO WITH OPTIONAL COLOR FLAG
// =============================================================

void cmd_echo(char **args) {
    int start = 1;
    const char *color = FG_WHITE;
    if (args[1]) {
        if      (!strcmp(args[1],"--red"))     { color=FG_RED;     start=2; }
        else if (!strcmp(args[1],"--green"))   { color=FG_GREEN;   start=2; }
        else if (!strcmp(args[1],"--yellow"))  { color=FG_YELLOW;  start=2; }
        else if (!strcmp(args[1],"--cyan"))    { color=FG_CYAN;    start=2; }
        else if (!strcmp(args[1],"--magenta")) { color=FG_MAGENTA; start=2; }
        else if (!strcmp(args[1],"--bold"))    { color=BOLD;       start=2; }
    }
    printf("  %s", color);
    for (int i = start; args[i]; i++) {
        if (i > start) printf(" ");
        printf("%s", args[i]);
    }
    printf(RST "\n");
}

// =============================================================
//  CALC  (recursive descent: +, -, *, /, ^, unary -, parens)
// =============================================================

static const char *cp;

static double c_expr();
static double c_term();

static void   c_skip() { while (*cp == ' ') cp++; }

static double c_factor() {
    c_skip();
    if (*cp == '(') {
        cp++;
        double v = c_expr();
        c_skip();
        if (*cp == ')') cp++;
        return v;
    }
    if (*cp == '-') { cp++; return -c_factor(); }
    char *end;
    double v = strtod(cp, &end);
    cp = end;
    return v;
}

static double c_power() {
    double b = c_factor();
    c_skip();
    if (*cp == '^') { cp++; return pow(b, c_power()); }
    return b;
}

static double c_term() {
    double v = c_power();
    c_skip();
    while (*cp == '*' || *cp == '/') {
        char op = *cp++;
        double r = c_power();
        v = (op == '*') ? v * r : (r != 0.0 ? v / r : 0.0);
    }
    return v;
}

static double c_expr() {
    double v = c_term();
    c_skip();
    while (*cp == '+' || *cp == '-') {
        char op = *cp++;
        double r = c_term();
        v = (op == '+') ? v + r : v - r;
    }
    return v;
}

void cmd_calc(char **args) {
    if (!args[1]) { ERR("Usage: calc <expression>"); return; }
    char expr[512] = "";
    for (int i = 1; args[i]; i++) {
        if (i > 1) strncat(expr, " ", 511);
        strncat(expr, args[i], 511);
    }
    cp = expr;
    double result = c_expr();
    printf("\n  " FG_DGRAY "%s" RST " = "
           FG_CYAN BOLD "%.10g\n\n" RST, expr, result);
}

// =============================================================
//  CAT - print file with line numbers
// =============================================================

void cmd_cat(char **args) {
    if (!args[1]) { ERR("Usage: cat <file>"); return; }
    FILE *f = fopen(args[1], "r");
    if (!f) { ERR("File not found."); return; }
    char line[1024];
    int  n = 1;
    printf(FG_CYAN BOLD "\n  -- %s --\n\n" RST, args[1]);
    while (fgets(line, sizeof(line), f))
        printf("  " FG_DGRAY "%4d | " RST FG_WHITE "%s" RST, n++, line);
    printf("\n");
    fclose(f);
}

// =============================================================
//  TOUCH
// =============================================================

void cmd_touch(char **args) {
    if (!args[1]) { ERR("Usage: touch <file>"); return; }
    FILE *f = fopen(args[1], "a");
    if (!f) { ERR("Cannot create file."); return; }
    fclose(f);
    printf("  " FG_GREEN "[OK] Created: " FG_YELLOW "%s\n" RST, args[1]);
}

// =============================================================
//  PROCESSES
// =============================================================

void cmd_ps() {
    printf(FG_CYAN "\n  Listing processes...\n\n" RST);
    system("tasklist /FO TABLE");
    printf("\n");
}

void cmd_kill(char **args) {
    if (!args[1]) { ERR("Usage: kill <pid|name>"); return; }
    char cmd[256];
    if (isdigit((unsigned char)args[1][0]))
        snprintf(cmd, sizeof(cmd), "taskkill /PID %s /F", args[1]);
    else
        snprintf(cmd, sizeof(cmd), "taskkill /IM \"%s\" /F", args[1]);
    system(cmd);
}

// =============================================================
//  SERIAL PORT
// =============================================================

void open_serial(const char *port, int baud) {
    serialHandle = CreateFileA(port,
        GENERIC_READ | GENERIC_WRITE, 0, 0,
        OPEN_EXISTING, 0, 0);

    if (serialHandle == INVALID_HANDLE_VALUE) {
        ERR("Failed to open serial port. Check port and permissions.");
        return;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    GetCommState(serialHandle, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    SetCommState(serialHandle, &dcb);

    COMMTIMEOUTS to;
    memset(&to, 0, sizeof(to));
    to.ReadIntervalTimeout        = 50;
    to.ReadTotalTimeoutConstant   = 200;
    to.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(serialHandle, &to);

    printf(FG_GREEN BOLD "\n  [OK] Connected: " RST
           FG_CYAN "%s" RST FG_DGRAY " @ %d baud\n\n" RST, port, baud);
}

void serial_mode_loop() {
    char  input[512];
    char  buffer[512];
    DWORD bytesRead, bytesWritten;

    printf(FG_CYAN BOLD "  [SERIAL MODE]" RST
           FG_DGRAY "  type 'exit' to return\n\n" RST);

    while (1) {
        printf(FG_MAGENTA "  (serial)> " RST);
        fgets(input, sizeof(input), stdin);

        if (strncmp(input, "exit", 4) == 0) {
            CloseHandle(serialHandle);
            serialHandle = INVALID_HANDLE_VALUE;
            OK("Serial connection closed.");
            printf("\n");
            break;
        }

        WriteFile(serialHandle, input, (DWORD)strlen(input),
                  &bytesWritten, NULL);
        Sleep(100);

        memset(buffer, 0, sizeof(buffer));
        if (ReadFile(serialHandle, buffer, sizeof(buffer) - 1,
                     &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            printf("  " FG_GREEN "[RX] " RST FG_WHITE "%s\n" RST, buffer);
        }
    }
}

// =============================================================
//  RUN WINDOWS COMMAND (fallback)
// =============================================================

void run_command(char *input) {
    input[strcspn(input, "\n")] = '\0';

    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, input, NULL, NULL,
                        TRUE, 0, NULL, NULL, &si, &pi)) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            printf("  " FG_RED BOLD "[!!]" RST FG_RED
                   "  Not found: " FG_YELLOW "%s\n" RST, input);
        else
            printf("  " FG_RED BOLD "[!!]" RST FG_RED
                   "  Error %lu\n" RST, e);
        return;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

// =============================================================
//  HELP
// =============================================================

void cmd_help() {
    printf(FG_CYAN BOLD
        "\n  +====================================================+\n"
        "  |         HYPER SHELL  -  COMMAND REFERENCE         |\n"
        "  +====================================================+\n\n"
        RST);

    typedef struct { const char *cmd, *arg, *desc; } H;
    H e[] = {
        {"cd",      "<dir>",          "Change directory  (blank = home)"},
        {"ls",      "[dir]",          "List files with color coding"},
        {"cat",     "<file>",         "Print file with line numbers"},
        {"touch",   "<file>",         "Create empty file"},
        {"sysinfo", "",               "RAM bar, CPU, arch, uptime"},
        {"history", "",               "Show command history"},
        {"ps",      "",               "List running processes"},
        {"kill",    "<pid|name>",     "Kill a process by PID or name"},
        {"ping",    "<host>",         "Ping a host  (4 packets)"},
        {"check",   "<url>",          "HTTP HEAD check a URL"},
        {"wget",    "<url>",          "Download file via curl"},
        {"ipinfo",  "",               "Show public IP and location"},
        {"serial",  "<COMx> [baud]",  "Open serial port  (default 9600)"},
        {"echo",    "[--color] txt",  "Print text  (--red/green/yellow/cyan/magenta/bold)"},
        {"calc",    "<expr>",         "Calculator  (+,-,*,/,^,parens)"},
        {"animate", "",               "Spinner and progress bar demo"},
        {"alias",   "[name value]",   "Set or list aliases"},
        {"clear",   "",               "Clear screen"},
        {"banner",  "",               "Re-show ASCII banner"},
        {"help",    "",               "This reference screen"},
        {"exit",    "",               "Exit HYPER shell"},
    };

    const char *secs[] = {
        "Navigation","Navigation","Navigation","Navigation",
        "Info","Info","Info","Info",
        "Network","Network","Network","Network",
        "Serial",
        "Utilities","Utilities","Utilities","Utilities",
        "Utilities","Utilities","Utilities","Utilities"
    };

    const char *cur = "";
    int n = (int)(sizeof(e) / sizeof(e[0]));
    for (int i = 0; i < n; i++) {
        if (strcmp(secs[i], cur) != 0) {
            cur = secs[i];
            printf("\n  " FG_YELLOW BOLD "%s\n" RST, cur);
        }
        printf("    " FG_CYAN "%-10s" RST
               " " FG_DGRAY "%-24s" RST
               "  " FG_WHITE "%s\n" RST,
               e[i].cmd, e[i].arg, e[i].desc);
    }

    printf("\n  " FG_DGRAY
           "Unknown commands are passed to the Windows shell.\n\n" RST);
}

// =============================================================
//  EXECUTE DISPATCHER
// =============================================================

void execute(char **args, char *raw) {
    if (!args[0]) return;

    // Alias substitution
    const char *av = alias_get(args[0]);
    if (av) {
        char expanded[MAX_INPUT];
        strncpy(expanded, av, MAX_INPUT - 1);
        char *ea[MAX_ARGS];
        parse(expanded, ea);
        execute(ea, expanded);
        return;
    }

    if (!strcmp(args[0],"exit"))    { running = 0;          return; }
    if (!strcmp(args[0],"clear"))   { system("cls");        return; }
    if (!strcmp(args[0],"banner"))  { banner();             return; }
    if (!strcmp(args[0],"help"))    { cmd_help();           return; }
    if (!strcmp(args[0],"history")) { cmd_history();        return; }
    if (!strcmp(args[0],"sysinfo")) { cmd_sysinfo();        return; }
    if (!strcmp(args[0],"ps"))      { cmd_ps();             return; }
    if (!strcmp(args[0],"ipinfo"))  { cmd_ipinfo();         return; }

    if (!strcmp(args[0],"animate")) {
        animate_spinner(28, 80, "Processing...");
        animate_progress(40, "Loading data");
        return;
    }

    if (!strcmp(args[0],"ls") || !strcmp(args[0],"dir")) {
                                    { cmd_ls(args);  return; } }
    if (!strcmp(args[0],"cd"))      { cmd_cd(args);         return; }
    if (!strcmp(args[0],"cat"))     { cmd_cat(args);        return; }
    if (!strcmp(args[0],"touch"))   { cmd_touch(args);      return; }
    if (!strcmp(args[0],"echo"))    { cmd_echo(args);       return; }
    if (!strcmp(args[0],"calc"))    { cmd_calc(args);       return; }
    if (!strcmp(args[0],"alias"))   { cmd_alias(args);      return; }
    if (!strcmp(args[0],"ping"))    { cmd_ping(args);       return; }
    if (!strcmp(args[0],"check"))   { cmd_check(args);      return; }
    if (!strcmp(args[0],"wget"))    { cmd_wget(args);       return; }
    if (!strcmp(args[0],"kill"))    { cmd_kill(args);       return; }

    if (!strcmp(args[0],"serial")) {
        if (!args[1]) { ERR("Usage: serial <COMx> [baud]"); return; }
        int baud = args[2] ? atoi(args[2]) : 9600;
        open_serial(args[1], baud);
        if (serialHandle != INVALID_HANDLE_VALUE)
            serial_mode_loop();
        return;
    }

    // Fallback: pass to Windows shell
    run_command(raw);
}

// =============================================================
//  MAIN
// =============================================================

int main() {
    enable_ansi();
    boot();

    char  input[MAX_INPUT];
    char  raw[MAX_INPUT];
    char *args[MAX_ARGS];

    while (running) {
        show_prompt();

        if (!fgets(input, MAX_INPUT, stdin)) break;
        if (input[0] == '\n') continue;

        strncpy(raw, input, MAX_INPUT - 1);
        hist_add(raw);
        parse(input, args);
        execute(args, raw);
    }

    printf(FG_CYAN "\n  Shutting down...\n" RST);
    Sleep(400);
    animate_spinner(10, 60, "Goodbye!");
    printf(FG_DGRAY "\n  Session ended.\n\n" RST);
    return 0;
}