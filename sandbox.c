#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define MAX_CMD_LEN 256

void set_limits(int cpu_time, int mem_limit_mb) {
    struct rlimit rl;
    rl.rlim_cur = rl.rlim_max = cpu_time;
    setrlimit(RLIMIT_CPU, &rl);
    rl.rlim_cur = rl.rlim_max = (rlim_t)mem_limit_mb * 1024 * 1024;
    setrlimit(RLIMIT_AS, &rl);
}

int run_with_limits(char *cmd[], int cpu_time, int mem_limit_mb) {
    pid_t pid = fork();
    if (pid == 0) {
        if (!freopen("stdout.txt", "w", stdout)) {
            perror("stdout redirect failed");
            exit(1);
        }
        if (!freopen("stderr.txt", "w", stderr)) {
            perror("stderr redirect failed");
            exit(1);
        }
        set_limits(cpu_time, mem_limit_mb);
        execvp(cmd[0], cmd);
        perror("execvp failed");
        exit(1);
    } else if (pid > 0) {
        int status;
        struct rusage usage;
        clock_t start = clock();
        wait4(pid, &status, 0, &usage);
        clock_t end = clock();
        double time_used = (double)(end - start) / CLOCKS_PER_SEC;
        printf("\n=== Sandbox Report ===\n");
        if (WIFEXITED(status))
            printf("Exit Code: %d\n", WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            printf("Terminated by Signal: %d\n", WTERMSIG(status));
        printf("User Time: %ld.%06lds\n", usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
        printf("System Time: %ld.%06lds\n", usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);
        printf("Max Memory: %ld KB\n", usage.ru_maxrss);
        printf("Total Time: %.2fs\n", time_used);
        return WEXITSTATUS(status);
    } else {
        perror("fork failed");
        return -1;
    }
}

const char* detect_language(const char *filename) {
    if (strstr(filename, ".c")) return "c";
    if (strstr(filename, ".cpp")) return "cpp";
    if (strstr(filename, ".py")) return "python";
    if (strstr(filename, ".java")) return "java";
    return "unknown";
}

int compile_program(const char *filename, const char *lang) {
    char cmd[MAX_CMD_LEN];
    if (strcmp(lang, "c") == 0)
        snprintf(cmd, sizeof(cmd), "gcc %s -o program.out 2> compile_error.txt", filename);
    else if (strcmp(lang, "cpp") == 0)
        snprintf(cmd, sizeof(cmd), "g++ %s -o program.out 2> compile_error.txt", filename);
    else if (strcmp(lang, "java") == 0)
        snprintf(cmd, sizeof(cmd), "javac %s 2> compile_error.txt", filename);
    else
        return 0;
    return system(cmd);
}

void execute_program(const char *filename, const char *lang) {
    char *cmd[5];
    if (strcmp(lang, "python") == 0) {
        cmd[0] = "python3";
        cmd[1] = (char *)filename;
        cmd[2] = NULL;
    } else if (strcmp(lang, "java") == 0) {
        cmd[0] = "java";
        cmd[1] = "Main";
        cmd[2] = NULL;
    } else {
        cmd[0] = "./program.out";
        cmd[1] = NULL;
    }
    run_with_limits(cmd, 2, 64);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./sandbox <source_file>\n");
        return 1;
    }
    const char *filename = argv[1];
    const char *lang = detect_language(filename);
    if (strcmp(lang, "unknown") == 0) {
        printf("Unsupported file type.\n");
        return 1;
    }
    printf("Detected Language: %s\n", lang);
    if (strcmp(lang, "python") == 0) {
        execute_program(filename, lang);
        return 0;
    }
    int compile_status = compile_program(filename, lang);
    if (compile_status != 0) {
        printf("Compilation failed. Check compile_error.txt.\n");
        return 1;
    }
    execute_program(filename, lang);
    return 0;
}
