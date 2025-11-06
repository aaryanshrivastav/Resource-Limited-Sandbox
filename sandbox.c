#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define MAX_CMD_LEN 256

// Apply CPU and memory limits
void set_limits(int cpu_time, int mem_limit_mb) {
    struct rlimit rl;

    rl.rlim_cur = rl.rlim_max = cpu_time;
    setrlimit(RLIMIT_CPU, &rl);

    rl.rlim_cur = rl.rlim_max = (rlim_t)mem_limit_mb * 1024 * 1024;
    setrlimit(RLIMIT_AS, &rl);
}

// Display file content if it exists
void print_file(const char *filename, const char *label) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    if (size > 0) {
        printf("\n--- %s ---\n", label);
        char c;
        while ((c = fgetc(fp)) != EOF) putchar(c);
        printf("\n");
    }
    fclose(fp);
}

// Run a command with resource limits
int run_with_limits(char *cmd[], int cpu_time, int mem_limit_mb) {
    pid_t pid = fork();

    if (pid == 0) {
        // Child process
        if (!freopen("stdout.txt", "w", stdout)) {
	    perror("Failed to redirect stdout");
	    exit(1);
	}

	if (!freopen("stderr.txt", "w", stderr)) {
	    perror("Failed to redirect stderr");
	    exit(1);
	}


        set_limits(cpu_time, mem_limit_mb);

        execvp(cmd[0], cmd);
        perror("execvp failed");
        exit(127); // if exec fails
    } 
    else if (pid > 0) {
        // Parent process
        int status;
        struct rusage usage;
        clock_t start = clock();

        wait4(pid, &status, 0, &usage);
        clock_t end = clock();
        double time_used = (double)(end - start) / CLOCKS_PER_SEC;

        // Display output
        print_file("stdout.txt", "Program Output");
        print_file("stderr.txt", "Program Errors");

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
    } 
    else {
        perror("fork failed");
        return -1;
    }
}

// Detect language from file extension
const char* detect_language(const char *filename) {
    if (strstr(filename, ".cpp")) return "cpp";
    if (strstr(filename, ".c")) return "c";
    if (strstr(filename, ".py")) return "python";
    if (strstr(filename, ".java")) return "java";
    return "unknown";
}

// Compile if required
int compile_program(const char *filename, const char *lang) {
    char cmd[MAX_CMD_LEN];

    if (strcmp(lang, "c") == 0)
        snprintf(cmd, sizeof(cmd), "gcc %s -o program.out 2> compile_error.txt", filename);
    else if (strcmp(lang, "cpp") == 0)
        snprintf(cmd, sizeof(cmd), "g++ %s -o program.out 2> compile_error.txt", filename);
    else if (strcmp(lang, "java") == 0)
        snprintf(cmd, sizeof(cmd), "javac %s 2> compile_error.txt", filename);
    else
        return 0; // no compile step for Python

    return system(cmd);
}

// Execute compiled/interpreted program
void execute_program(const char *filename, const char *lang) {
    char *cmd[5];

    if (strcmp(lang, "python") == 0) {
        cmd[0] = "python3";
        cmd[1] = (char *)filename;
        cmd[2] = NULL;
    } 
    else if (strcmp(lang, "java") == 0) {
        static char classname[128];
        strncpy(classname, filename, sizeof(classname) - 1);
        classname[sizeof(classname) - 1] = '\0';
        char *dot = strrchr(classname, '.');
        if (dot) *dot = '\0';

        cmd[0] = "java";
	cmd[1] = "-Xmx512m";
	cmd[2] = "-XX:-UseCompressedClassPointers";
	cmd[3] = classname;
	cmd[4] = NULL;
    } 
    else {
        cmd[0] = "./program.out";
        cmd[1] = NULL;
    }

    // JVM needs more memory than C/Python
    int mem_limit = strcmp(lang, "java") == 0 ? 2048 : 64;
    run_with_limits(cmd, 2, mem_limit);
}

// Entry point
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
        print_file("compile_error.txt", "Compilation Errors");
        return 1;
    }

    execute_program(filename, lang);
    return 0;
}
