/* SPDX-License-Identifier: MPL-2.0
 * crash report for rbp. on a fatal signal the handler appends the signal,
 * the registers, /proc/self/maps and the top of the stack to RX3_CRASH_PATH,
 * then puts back the action that was there before and returns, so the fault
 * repeats under that action and the kernel can still write a core file.
 * tools/rx3_crash turns the report into symbols on the computer.
 * the handler runs inside a signal: open, read, write and sigaction only.
 */

#ifndef RX3_CORE_CRASH_H
#define RX3_CORE_CRASH_H

/* glibc's arm layouts, declared here because the hook has no libc headers. */
struct rx3_sigaction {
    void (*handler)(int, void *, void *);
    uint32_t mask[32];
    int flags;
    void (*restorer)(void);
};

struct rx3_signal_stack {
    void *base;
    int flags;
    size_t size;
};

extern int sigaction(int, const struct rx3_sigaction *, struct rx3_sigaction *);
extern int sigaltstack(const struct rx3_signal_stack *, struct rx3_signal_stack *);
extern int pipe(int[2]);

#define CRASH_SA_SIGINFO 4
#define CRASH_SA_ONSTACK 0x08000000
#define CRASH_SIGNAL_COUNT 5u
#define CRASH_ALT_STACK_BYTES 65536u
#define CRASH_STACK_DUMP_BYTES 8192u
/* ucontext: flags, link, stack_t, then sigcontext with trap_no, error_code
   and oldmask ahead of r0. r0..cpsr are 17 words, fault_address follows. */
#define CRASH_UCONTEXT_R0 32u
#define CRASH_UCONTEXT_FAULT 100u

static const int crash_signals[CRASH_SIGNAL_COUNT] = { 4, 6, 7, 8, 11 };
static const char *const crash_signal_names[CRASH_SIGNAL_COUNT] = {
    "SIGILL", "SIGABRT", "SIGBUS", "SIGFPE", "SIGSEGV"
};
static struct rx3_sigaction crash_previous[CRASH_SIGNAL_COUNT];
static char crash_path[256];
static int crash_pipe[2] = { -1, -1 };
static int crash_armed;

static void crash_text(int fd, const char *text)
{
    (void)write(fd, text, str_length(text));
}

static void crash_hex(int fd, const char *label, uint32_t value)
{
    char line[40];
    size_t length = 0;
    int shift;
    while (label[length] && length < 16u) {
        line[length] = label[length];
        length++;
    }
    line[length++] = ' ';
    line[length++] = '0';
    line[length++] = 'x';
    for (shift = 28; shift >= 0; shift -= 4)
        line[length++] = "0123456789abcdef"[(value >> shift) & 15u];
    line[length++] = '\n';
    (void)write(fd, line, length);
}

static void crash_copy_maps(int fd)
{
    char buffer[1024];
    ssize_t count;
    int maps = open("/proc/self/maps", O_RDONLY);
    if (maps < 0)
        return;
    while ((count = read(maps, buffer, sizeof(buffer))) > 0)
        (void)write(fd, buffer, (size_t)count);
    close(maps);
}

/* a word only reaches the report after the kernel read it into the pipe:
   an unmapped address fails that write with EFAULT instead of faulting here. */
static void crash_dump_stack(int fd, uint32_t sp)
{
    uint32_t offset;
    for (offset = 0; offset < CRASH_STACK_DUMP_BYTES; offset += 4u) {
        uint32_t word;
        if (write(crash_pipe[1], (const void *)(unsigned long)(sp + offset), 4u) != 4)
            break;
        if (read(crash_pipe[0], &word, 4u) != 4)
            break;
        {
            char line[24];
            size_t length = 0;
            uint32_t address = sp + offset;
            int shift;
            for (shift = 28; shift >= 0; shift -= 4)
                line[length++] = "0123456789abcdef"[(address >> shift) & 15u];
            line[length++] = ' ';
            for (shift = 28; shift >= 0; shift -= 4)
                line[length++] = "0123456789abcdef"[(word >> shift) & 15u];
            line[length++] = '\n';
            (void)write(fd, line, length);
        }
    }
}

static void crash_handler(int signal_number, void *info, void *context)
{
    static const char *const names[17] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
        "fp", "ip", "sp", "lr", "pc", "cpsr"
    };
    const uint32_t *registers =
        (const uint32_t *)((const uint8_t *)context + CRASH_UCONTEXT_R0);
    unsigned index;
    unsigned slot = 0;
    int fd;
    for (index = 0; index < CRASH_SIGNAL_COUNT; index++)
        if (crash_signals[index] == signal_number)
            slot = index;
    fd = open(crash_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        crash_text(fd, "=== rx3 crash ===\n");
        crash_text(fd, "signal ");
        crash_text(fd, crash_signal_names[slot]);
        crash_text(fd, "\n");
        crash_hex(fd, "code", (uint32_t)((const int *)info)[2]);
        crash_hex(fd, "addr", ((const uint32_t *)info)[3]);
        crash_hex(fd, "fault", *(const uint32_t *)((const uint8_t *)context +
                                                  CRASH_UCONTEXT_FAULT));
        for (index = 0; index < 17u; index++)
            crash_hex(fd, names[index], registers[index]);
        crash_text(fd, "--- maps ---\n");
        crash_copy_maps(fd);
        crash_text(fd, "--- stack ---\n");
        crash_dump_stack(fd, registers[13]);
        crash_text(fd, "=== end ===\n");
        close(fd);
    }
    (void)sigaction(signal_number, &crash_previous[slot], 0);
}

static void crash_arm(unsigned index)
{
    struct rx3_sigaction action;
    memset(&action, 0, sizeof(action));
    action.handler = crash_handler;
    action.flags = CRASH_SA_SIGINFO | CRASH_SA_ONSTACK;
    (void)sigaction(crash_signals[index], &action, &crash_previous[index]);
}

static void crash_install(void)
{
    const char *path = getenv("RX3_CRASH_PATH");
    struct rx3_signal_stack stack;
    size_t length;
    unsigned index;
    if (!path || !path[0])
        return;
    length = str_length(path);
    if (length >= sizeof(crash_path))
        return;
    memcpy(crash_path, path, length + 1u);
    if (pipe(crash_pipe))
        return;
    /* a stack overflow on the main thread still leaves room to report. */
    stack.base = mmap(0, CRASH_ALT_STACK_BYTES, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack.base != MAP_FAILED) {
        stack.flags = 0;
        stack.size = CRASH_ALT_STACK_BYTES;
        (void)sigaltstack(&stack, 0);
    }
    for (index = 0; index < CRASH_SIGNAL_COUNT; index++)
        crash_arm(index);
    crash_armed = 1;
    log_line("crash report armed");
}

/* rbp can install its own handlers after the constructor. take the signal
   back and keep theirs as the action the report hands the fault to. */
static void crash_reclaim(void)
{
    unsigned index;
    if (!crash_armed)
        return;
    for (index = 0; index < CRASH_SIGNAL_COUNT; index++) {
        struct rx3_sigaction current;
        if (sigaction(crash_signals[index], 0, &current))
            continue;
        if (current.handler == crash_handler)
            continue;
        log_line("crash report: rbp replaced a fatal-signal handler; chaining to it");
        crash_arm(index);
    }
}

#endif /* RX3_CORE_CRASH_H */
