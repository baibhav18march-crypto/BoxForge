#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>

#define STACK_SIZE (1024 * 1024)
static char child_stack[STACK_SIZE];

pid_t global_child_pid = -1;
int pipe_fd[2];

void write_file(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s", value);
        fclose(f);
    }
}

void handle_signal(int sig) {
    if (global_child_pid > 0) {
        kill(global_child_pid, SIGTERM);
    }
}

int child_main(void *arg) {
    char **argv = (char **)arg;
    usleep(50000);

    close(pipe_fd[0]);
    dup2(pipe_fd[1], STDOUT_FILENO);
    dup2(pipe_fd[1], STDERR_FILENO);
    close(pipe_fd[1]);

    sethostname("boxforge-os", 11);
    clearenv();
    setenv("TERM", "xterm-256color", 1);
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
    setenv("HOME", "/root", 1);

    // --- WEEK 9: OVERLAYFS MOUNT ---
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

    if (mount("overlay", "merged", "overlay", 0, "lowerdir=rootfs,upperdir=upper,workdir=work") != 0) {
        perror("OverlayFS mount failed");
        return -1;
    }

    if (chroot("merged") != 0 || chdir("/") != 0) {
        perror("chroot/chdir failed");
        return -1;
    }

    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {}

    if (execvp(argv[0], argv) == -1) {
        perror("execvp failed");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // --- WEEK 12: AUTOMATED SETUP ---
    umount("merged");
    system("rm -rf upper work merged");
    mkdir("upper", 0755);
    mkdir("work", 0755);
    mkdir("merged", 0755);

    if (pipe(pipe_fd) == -1) {
        exit(EXIT_FAILURE);
    }

    int flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | CLONE_NEWNET | SIGCHLD;
    pid_t child_pid = clone(child_main, child_stack + STACK_SIZE, flags, &argv[1]);
    global_child_pid = child_pid;

    mkdir("/sys/fs/cgroup/boxforge", 0755);
    write_file("/sys/fs/cgroup/boxforge/memory.max", "50000000");
    write_file("/sys/fs/cgroup/boxforge/memory.swap.max", "0");
    char pid_str[32];
    sprintf(pid_str, "%d", child_pid);
    write_file("/sys/fs/cgroup/boxforge/cgroup.procs", pid_str);

    close(pipe_fd[1]);
    char buffer[256];
    int bytes_read;
    while ((bytes_read = read(pipe_fd[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
    }

    // --- WEEK 11: TEARDOWN & CLEANUP ---
    waitpid(child_pid, NULL, 0);
    umount("merged");
    rmdir("/sys/fs/cgroup/boxforge");

    printf("[BoxForge Manager] Container stopped. Resources cleaned up successfully. Exiting cleanly.\n");
    return 0;
}
