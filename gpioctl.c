/*
 * gpioctl.c - Raspberry Pi 5 GPIO control using PHYSICAL header pin numbers.
 * Backend: `pinctrl` (preinstalled on Raspberry Pi OS Bookworm).
 *
 * Build:  gcc -O2 -Wall -o gpioctl gpioctl.c      (or simply: make)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "gpio_pins.h"

#ifndef GPIOCTL_VERSION
#define GPIOCTL_VERSION "1.0.0"
#endif

#define OUTBUF 512
#define DASH   "\xE2\x80\x94"   /* UTF-8 em dash "—" */

static volatile sig_atomic_t g_stop = 0;

/* ---------- helpers ---------- */
static void die(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "Error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* Sleep that can be interrupted by Ctrl+C. Returns -1 if interrupted. */
static int sleep_sec(double s)
{
    struct timespec ts;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, &ts) == -1) {
        if (errno != EINTR) break;
        if (g_stop) return -1;
    }
    return g_stop ? -1 : 0;
}

static int lookup_bcm(int pin)
{
    for (size_t i = 0; i < PIN_MAP_LEN; i++)
        if (PIN_MAP[i].pin == pin) return PIN_MAP[i].bcm;
    return -1;
}

static int to_bcm(int pin)
{
    for (size_t i = 0; i < POWER_LEN; i++)
        if (POWER_GND[i].pin == pin)
            die("Physical pin %d is %s, not a GPIO.", pin, POWER_GND[i].name);
    int g = lookup_bcm(pin);
    if (g < 0) die("Invalid physical pin %d. Use 1-40.", pin);
    if (pin == 27 || pin == 28)
        fprintf(stderr, "Warning: pins 27/28 (GPIO0/1) are reserved for HAT EEPROM.\n");
    return g;
}

static int parse_pin(const char *s)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || *end || v < 1 || v > HEADER_PINS)
        die("Invalid physical pin '%s'. Use 1-40.", s);
    return (int)v;
}

static double parse_num(const char *s, const char *name)
{
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || *end || v < 0) die("Invalid value for %s: '%s'", name, s);
    return v;
}

/*
 * Run pinctrl with argv, capture stdout+stderr into out.
 * Returns the exit status (0 = success, 127 = pinctrl not found, -1 = abnormal).
 */
static int try_pinctrl(char *out, size_t n, char *const argv[])
{
    int fd[2];
    if (pipe(fd) < 0) die("pipe: %s", strerror(errno));

    pid_t pid = fork();
    if (pid < 0) die("fork: %s", strerror(errno));
    if (pid == 0) {
        dup2(fd[1], STDOUT_FILENO);
        dup2(fd[1], STDERR_FILENO);
        close(fd[0]);
        close(fd[1]);
        execvp("pinctrl", argv);
        _exit(127);
    }
    close(fd[1]);

    size_t total = 0;
    ssize_t r;
    while (total < n - 1) {
        r = read(fd[0], out + total, n - 1 - total);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        total += (size_t)r;
    }
    out[total] = '\0';

    char junk[256];   /* drain any remaining output */
    while ((r = read(fd[0], junk, sizeof junk)) > 0 || (r < 0 && errno == EINTR))
        ;
    close(fd[0]);

    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) die("waitpid: %s", strerror(errno));

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Same as try_pinctrl() but dies with a helpful message on failure. */
static void run_pinctrl(char *out, size_t n, char *const argv[])
{
    char local[OUTBUF];
    if (!out) { out = local; n = sizeof local; }

    int rc = try_pinctrl(out, n, argv);
    if (rc == 0) return;
    if (rc == 127)
        die("`pinctrl` not found. Install with: sudo apt install raspi-utils");
    out[strcspn(out, "\n")] = '\0';
    die("%s", *out ? out : "pinctrl failed (try sudo?)");
}

/* ---------- GPIO operations ---------- */
static void set_output(int g, int high)
{
    char gs[8];
    snprintf(gs, sizeof gs, "%d", g);
    char *argv[] = {"pinctrl", "set", gs, "op", high ? "dh" : "dl", NULL};
    run_pinctrl(NULL, 0, argv);
}

static void set_input(int g, const char *pull_code)
{
    char gs[8];
    snprintf(gs, sizeof gs, "%d", g);
    char *argv[] = {"pinctrl", "set", gs, "ip", (char *)pull_code, NULL};
    run_pinctrl(NULL, 0, argv);
}

/* Parse e.g. "17: op dh pd | hi // GPIO17 = output" into mode + level. */
static void parse_info(const char *out, char *mode, size_t mlen, int *level)
{
    if (level) *level = strstr(out, "| hi") != NULL;
    if (mode) {
        const char *c = strchr(out, ':');
        if (!c || sscanf(c + 1, "%7s", mode) != 1)
            snprintf(mode, mlen, "?");
    }
}

static void get_info(int g, char *mode, size_t mlen, int *level)
{
    char gs[8], out[OUTBUF];
    snprintf(gs, sizeof gs, "%d", g);
    char *argv[] = {"pinctrl", "get", gs, NULL};
    run_pinctrl(out, sizeof out, argv);
    parse_info(out, mode, mlen, level);
}

/* Non-fatal variant: returns 0 on success, otherwise the pinctrl exit code. */
static int try_get_info(int g, char *mode, size_t mlen, int *level)
{
    char gs[8], out[OUTBUF];
    snprintf(gs, sizeof gs, "%d", g);
    char *argv[] = {"pinctrl", "get", gs, NULL};
    int rc = try_pinctrl(out, sizeof out, argv);
    if (rc == 0) parse_info(out, mode, mlen, level);
    return rc;
}

static int read_level(int g)
{
    int lv;
    get_info(g, NULL, 0, &lv);
    return lv;
}

/* ---------- options ---------- */
struct opts {
    double duration;
    int has_duration;
    double delay;
    int state_on;
    long repeat;
    double interval;
    int watch;
    const char *pull;      /* "up" | "down" | "none" */
};

static void usage(int code)
{
    FILE *f = code ? stderr : stdout;
    fprintf(f,
        "gpioctl - Raspberry Pi 5 GPIO control by PHYSICAL pin number\n\n"
        "Usage:\n"
        "  gpioctl on <pin>\n"
        "  gpioctl off <pin>\n"
        "  gpioctl toggle <pin>\n"
        "  gpioctl mode <pin> in|out [--pull up|down|none]\n"
        "  gpioctl read <pin> [-w|--watch]\n"
        "  gpioctl status [pin]\n"
        "  gpioctl list\n"
        "  gpioctl timer <pin> -d <sec> [--delay <sec>] [--state on|off]\n"
        "                      [-r <count, 0=forever>] [-i <sec>]\n"
        "  gpioctl --version\n");
    exit(code);
}

/* ---------- commands ---------- */
static void cmd_onoff(int pin, int high)
{
    int g = to_bcm(pin);
    set_output(g, high);
    printf("Pin %d (GPIO%d) -> %s\n", pin, g, high ? "ON" : "OFF");
}

static void cmd_toggle(int pin)
{
    int g = to_bcm(pin);
    int nv = !read_level(g);
    set_output(g, nv);
    printf("Pin %d (GPIO%d) -> %s\n", pin, g, nv ? "ON" : "OFF");
}

static void cmd_mode(int pin, const char *dir, const char *pull)
{
    int g = to_bcm(pin);
    if (strcmp(dir, "out") == 0) {
        set_output(g, 0);
        printf("Pin %d (GPIO%d) set as OUTPUT (initial OFF)\n", pin, g);
    } else if (strcmp(dir, "in") == 0) {
        const char *code;
        if (strcmp(pull, "up") == 0)        code = "pu";
        else if (strcmp(pull, "down") == 0) code = "pd";
        else if (strcmp(pull, "none") == 0) code = "pn";
        else die("--pull must be up, down or none");
        set_input(g, code);
        printf("Pin %d (GPIO%d) set as INPUT (pull %s)\n", pin, g, pull);
    } else {
        die("Direction must be 'in' or 'out'");
    }
}

static void cmd_read(int pin, int watch)
{
    int g = to_bcm(pin);
    if (!watch) {
        printf("%d\n", read_level(g));
        return;
    }
    install_signals();
    printf("Watching pin %d (GPIO%d). Ctrl+C to stop.\n", pin, g);
    int last = -1;
    while (!g_stop) {
        int v = read_level(g);
        if (v != last) {
            char ts[16];
            time_t t = time(NULL);
            strftime(ts, sizeof ts, "%H:%M:%S", localtime(&t));
            printf("%s  Pin %d = %d\n", ts, pin, v);
            last = v;
        }
        sleep_sec(0.05);
    }
    printf("\n");
}

static void print_status_row(int pin, int g)
{
    char mode[8];
    int lv;
    char gname[12];
    get_info(g, mode, sizeof mode, &lv);
    snprintf(gname, sizeof gname, "GPIO%d", g);
    printf("%4d %6s %5s %6s\n", pin, gname, mode, lv ? "HIGH" : "LOW");
}

static void cmd_status(int pin)
{
    printf("%4s %6s %5s %6s\n", "PIN", "GPIO", "MODE", "LEVEL");
    if (pin > 0) {
        print_status_row(pin, to_bcm(pin));
        return;
    }
    for (int p = 1; p <= HEADER_PINS; p++) {
        int g = lookup_bcm(p);
        if (g >= 0) print_status_row(p, g);
    }
}

/* Print s left-aligned in a field of `width` display columns (UTF-8 aware). */
static void print_col(const char *s, int width)
{
    int cols = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80) cols++;     /* count code points, not bytes */
    fputs(s, stdout);
    for (; cols < width; cols++) putchar(' ');
}

/* list: all 40 header pins with type, alt function and live mode/level. */
static void cmd_list(void)
{
    enum { W_PIN = 5, W_GPIO = 8, W_TYPE = 6, W_FUNC = 17, W_MODE = 6 };
    int pinctrl_ok = 1;

    print_col("PIN", W_PIN);   print_col("GPIO", W_GPIO);
    print_col("TYPE", W_TYPE); print_col("FUNCTION", W_FUNC);
    print_col("MODE", W_MODE); puts("LEVEL");
    puts("---- ------- ----- ---------------- ----- -----");

    for (size_t i = 0; i < PIN_INFO_LEN; i++) {
        const struct pin_info *pi = &PIN_INFO[i];
        char buf[24], mode[8] = "?";
        const char *level = "?";

        snprintf(buf, sizeof buf, "%d", pi->pin);
        print_col(buf, W_PIN);

        if (pi->bcm >= 0) {
            snprintf(buf, sizeof buf, "GPIO%d", pi->bcm);
            print_col(buf, W_GPIO);
        } else {
            print_col(DASH, W_GPIO);
        }

        print_col(pi->type, W_TYPE);
        print_col(*pi->func ? pi->func : DASH, W_FUNC);

        if (pi->bcm < 0) {
            print_col(DASH, W_MODE);
            puts(DASH);
            continue;
        }

        if (pinctrl_ok) {
            int lv;
            int rc = try_get_info(pi->bcm, mode, sizeof mode, &lv);
            if (rc == 0)
                level = lv ? "HIGH" : "LOW";
            else if (rc == 127)
                pinctrl_ok = 0;     /* not installed: stop trying */
        }
        print_col(mode, W_MODE);
        puts(level);
    }

    if (!pinctrl_ok)
        fprintf(stderr, "\nNote: `pinctrl` not found; live MODE/LEVEL unavailable. "
                        "Install with: sudo apt install raspi-utils\n");
}

static void cmd_timer(int pin, const struct opts *o)
{
    int g = to_bcm(pin);
    int active = o->state_on;
    const char *on_s  = active ? "ON" : "OFF";
    const char *off_s = active ? "OFF" : "ON";

    install_signals();

    if (o->delay > 0) {
        printf("Waiting %gs before trigger...\n", o->delay);
        if (sleep_sec(o->delay) < 0) goto interrupted;
    }

    long count = 0;
    while (o->repeat == 0 || count < o->repeat) {
        set_output(g, active);
        printf("Pin %d (GPIO%d) -> %s for %gs\n", pin, g, on_s, o->duration);
        if (sleep_sec(o->duration) < 0) goto interrupted;

        set_output(g, !active);
        count++;
        printf("Pin %d -> %s  (cycle %ld)\n", pin, off_s, count);

        if (o->repeat == 0 || count < o->repeat)
            if (sleep_sec(o->interval) < 0) goto interrupted;
    }
    printf("Timer finished.\n");
    return;

interrupted:
    set_output(g, !active);
    printf("\nInterrupted. Pin %d -> %s\n", pin, off_s);
}

/* ---------- main ---------- */
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 2) usage(1);
    const char *cmd = argv[1];
    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help"))
        usage(0);
    if (!strcmp(cmd, "-V") || !strcmp(cmd, "--version") || !strcmp(cmd, "version")) {
        printf("gpioctl %s\n", GPIOCTL_VERSION);
        return 0;
    }

    struct opts o = { .delay = 0, .state_on = 1, .repeat = 1,
                      .interval = 1, .pull = "none" };

    static const struct option lo[] = {
        {"duration", required_argument, 0, 'd'},
        {"delay",    required_argument, 0, 'D'},
        {"state",    required_argument, 0, 's'},
        {"repeat",   required_argument, 0, 'r'},
        {"interval", required_argument, 0, 'i'},
        {"watch",    no_argument,       0, 'w'},
        {"pull",     required_argument, 0, 'p'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    /* Parse options after the subcommand */
    int c;
    optind = 1;
    while ((c = getopt_long(argc - 1, argv + 1, "d:r:i:wh", lo, NULL)) != -1) {
        switch (c) {
        case 'd': o.duration = parse_num(optarg, "--duration"); o.has_duration = 1; break;
        case 'D': o.delay    = parse_num(optarg, "--delay"); break;
        case 'i': o.interval = parse_num(optarg, "--interval"); break;
        case 'r': o.repeat   = (long)parse_num(optarg, "--repeat"); break;
        case 'w': o.watch    = 1; break;
        case 'p': o.pull     = optarg; break;
        case 's':
            if (!strcmp(optarg, "on"))       o.state_on = 1;
            else if (!strcmp(optarg, "off")) o.state_on = 0;
            else die("--state must be 'on' or 'off'");
            break;
        case 'h': usage(0); break;
        default:  usage(1);
        }
    }

    char **pos = argv + 1 + optind;          /* positional args */
    int npos = argc - 1 - optind;

    if (!strcmp(cmd, "on") || !strcmp(cmd, "off")) {
        if (npos != 1) usage(1);
        cmd_onoff(parse_pin(pos[0]), !strcmp(cmd, "on"));
    } else if (!strcmp(cmd, "toggle")) {
        if (npos != 1) usage(1);
        cmd_toggle(parse_pin(pos[0]));
    } else if (!strcmp(cmd, "mode")) {
        if (npos != 2) usage(1);
        cmd_mode(parse_pin(pos[0]), pos[1], o.pull);
    } else if (!strcmp(cmd, "read")) {
        if (npos != 1) usage(1);
        cmd_read(parse_pin(pos[0]), o.watch);
    } else if (!strcmp(cmd, "status")) {
        if (npos > 1) usage(1);
        cmd_status(npos == 1 ? parse_pin(pos[0]) : 0);
    } else if (!strcmp(cmd, "list")) {
        if (npos != 0) usage(1);
        cmd_list();
    } else if (!strcmp(cmd, "timer")) {
        if (npos != 1) usage(1);
        if (!o.has_duration) die("timer requires -d/--duration");
        cmd_timer(parse_pin(pos[0]), &o);
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", cmd);
        usage(1);
    }
    return 0;
}
