#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sched.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h> // <-- WEEK 8: Folder banane ke liye zaroori

#define STACK_SIZE (1024 * 1024)
static char child_stack[STACK_SIZE];

pid_t global_child_pid = -1;
int pipe_fd[2];

void handle_signal(int sig) {
    printf("\n[BoxForge] Warning: Signal %d received! Cleaning up...\n", sig);
    if (global_child_pid > 0) kill(global_child_pid, SIGTERM);
}

// --- WEEK 8: CGROUP HELPER FUNCTIONS ---
void write_cg_file(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s", value);
        fclose(f);
    } else {
        perror("Cgroup write failed (Are you running with sudo?)");
    }
}

void setup_cgroup() {
    printf("Container: Setting up Cgroups (RAM Limit: 50MB)...\n");
    
    // Linux kernel ke virtual cgroup folder mein apna ek naya folder banao
    mkdir("/sys/fs/cgroup/boxforge", 0755);
    
    // 50 MB limit set karo (50000000 bytes)
    write_cg_file("/sys/fs/cgroup/boxforge/memory.max", "50000000"); 
    
    // Kernel ko batao ki humare current process (container) par yeh limit lagani hai
    char pid[32];
    sprintf(pid, "%d", getpid());
    write_cg_file("/sys/fs/cgroup/boxforge/cgroup.procs", pid);
}
// ---------------------------------------

int child_main(void *arg) {
    char **argv = (char **)arg;
    
    close(pipe_fd[0]);
    dup2(pipe_fd[1], STDOUT_FILENO);
    dup2(pipe_fd[1], STDERR_FILENO);

    // --- WEEK 8: CALL CGROUP SETUP ---
    // Isolation start hone se thik pehle RAM limit set karo
    setup_cgroup();
    // ---------------------------------

    sethostname("boxforge-os", 11);
    clearenv();
    setenv("TERM", "xterm-256color", 1);
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
    setenv("HOME", "/root", 1);

    if (chroot("rootfs") != 0) return -1;
    chdir("/"); 

    if (execvp(argv[1], &argv[1]) == -1) return -1;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("[BoxForge] Starting container with Memory Limits...\n");

    signal(SIGINT, handle_signal);  
    signal(SIGTERM, handle_signal); 

    if (pipe(pipe_fd) == -1) exit(EXIT_FAILURE);

    int flags = CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD;
    pid_t child_pid = clone(child_main, child_stack + STACK_SIZE, flags, argv);

    if (child_pid == -1) exit(EXIT_FAILURE);
    global_child_pid = child_pid; 

    close(pipe_fd[1]);
    
    char buffer[256];
    int bytes_read;
    printf("[BoxForge] Capturing container logs:\n");
    printf("------------------------------------\n");
    
    while ((bytes_read = read(pipe_fd[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("[LOG] %s", buffer);
    }
    printf("------------------------------------\n");

    waitpid(child_pid, NULL, 0);
    
    // --- WEEK 8: CLEANUP CGROUP ---
    // Container band hone ke baad cgroup folder delete kar do
    rmdir("/sys/fs/cgroup/boxforge");
    // ------------------------------
    
    printf("[BoxForge] Container successfully exited.\n");
    return 0;
}
