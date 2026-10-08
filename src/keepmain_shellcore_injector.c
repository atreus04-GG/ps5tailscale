#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "sdk_shim.h"

#define THREAD_NAME "tsp5_keepmain_shellcore"
#define STATUS_PATH "/data/tailscale/keepmain-shellcore-injector.status"
#define STATUS_TMP_PATH "/data/tailscale/keepmain-shellcore-injector.status.tmp"
#define CONTROL_PATH "/data/tailscale/keepmain.control"
#define READY_PATH "/data/tailscale/keepmain-shellcore-resident.ready"
#define MAX_THREADS 2048U

extern void port_outer_init_mutexes(void);
extern int find_proc_pid_by_name(const char *name);
extern int sys_proc_call_remote_func(int pid, void *elf_buf,
                                     uint64_t a3_unused,
                                     void *thread_name);
extern const uint8_t embedded_inner_start[];
extern const uint64_t embedded_inner_size;

static void write_status(const char *status)
{
    int fd = open(STATUS_TMP_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    (void)write(fd, status, strlen(status));
    (void)write(fd, "\n", 1);
    (void)fsync(fd);
    (void)close(fd);
    (void)rename(STATUS_TMP_PATH, STATUS_PATH);
}

static int wait_for_resident_ready(void)
{
    unsigned attempt;
    for (attempt = 0; attempt < 50U; attempt++) {
        int fd = open(READY_PATH, O_RDONLY);
        if (fd >= 0) {
            char ready[6] = {0};
            ssize_t n = read(fd, ready, 5);
            (void)close(fd);
            if (n == 5 && memcmp(ready, "ready", 5) == 0) return 0;
        }
        (void)usleep(100000);
    }
    return -1;
}

static int reset_control_inactive(void)
{
    int fd = open(CONTROL_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    if (write(fd, "0\n", 2) != 2) {
        (void)close(fd);
        return -1;
    }
    if (fsync(fd) != 0) {
        (void)close(fd);
        return -1;
    }
    return close(fd);
}

static int treatment_thread_present(int pid)
{
    intptr_t kproc = kernel_get_proc((pid_t)pid);
    unsigned long kthread = 0;
    unsigned count = 0;

    if (!kproc || kernel_copyout(kproc + 0x10, &kthread, sizeof(kthread)) != 0) {
        return -1;
    }
    while (kthread != 0 && count < MAX_THREADS) {
        unsigned long next = 0;
        char name[33];
        memset(name, 0, sizeof(name));
        if (kernel_copyout(kthread + 0x10, &next, sizeof(next)) != 0 ||
            kernel_copyout(kthread + 0x294, name, 32) != 0) {
            return -1;
        }
        name[32] = '\0';
        if (strcmp(name, THREAD_NAME) == 0) return 1;
        if (next == kthread) return -1;
        kthread = next;
        count++;
    }
    if (kthread != 0) return -1;
    return 0;
}

static int unlock_target_syscall_filter(int pid)
{
    intptr_t kproc = kernel_get_proc((pid_t)pid);
    intptr_t filter_addr = 0;
    unsigned long value;

    if (!kproc) return -1;
    if (kernel_copyout(kproc + 0x3e8, &filter_addr, sizeof(filter_addr)) < 0 ||
        !filter_addr) return -1;
    value = 0UL;
    if (kernel_copyin(&value, filter_addr + 0xf0, sizeof(value)) < 0) return -1;
    value = (unsigned long)-1L;
    if (kernel_copyin(&value, filter_addr + 0xf8, sizeof(value)) < 0) return -1;
    return 0;
}

int main(void)
{
    int pid;
    int census;
    int rc;

    port_outer_init_mutexes();
    pid = find_proc_pid_by_name("SceShellCore");
    if (pid < 0) {
        write_status("error:sceshellcore-not-found");
        payload_exit(1);
        return 1;
    }

    census = treatment_thread_present(pid);
    if (census > 0) {
        write_status("ok:already-resident");
        payload_exit(0);
        return 0;
    }
    if (census < 0) {
        write_status("error:thread-census-incomplete");
        payload_exit(2);
        return 2;
    }
    if (reset_control_inactive() != 0) {
        write_status("error:control-reset");
        payload_exit(3);
        return 3;
    }
    (void)unlink(READY_PATH);
    if (unlock_target_syscall_filter(pid) != 0) {
        write_status("error:syscall-filter-unlock");
        payload_exit(4);
        return 4;
    }

    rc = sys_proc_call_remote_func(
        pid, (void *)embedded_inner_start, 0,
        (void *)"libSceShareInternal.native.sprx");
    if (rc != 0) {
        write_status("error:inject-failed");
        payload_exit(5);
        return 5;
    }
    if (wait_for_resident_ready() != 0) {
        write_status("error:resident-not-ready");
        payload_exit(6);
        return 6;
    }
    write_status("ok:injected-ready");
    payload_exit(0);
    return 0;
}
