#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#define MAX_CMDS 100
#define MAX_LINE_LEN 1024
#define MAX_ARGS 20

typedef struct {
    char *cmd_path;
    char *stdin_file;
    char *stdout_file;
    char **argv;
    pid_t pid;
    int active;
} process_config_t;

process_config_t processes[MAX_CMDS];
int num_processes = 0;
char *config_file = NULL;
char *log_file = "/tmp/myinit.log";
volatile sig_atomic_t need_reload = 0;

void log_message(const char *msg) {
    FILE *log = fopen(log_file, "a");
    if (log == NULL) return;
    
    time_t now = time(NULL);
    char *time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0';
    
    fprintf(log, "[%s] %s\n", time_str, msg);
    fclose(log);
}

int parse_config_line(char *line, process_config_t *proc) {
    char *token;
    char *saveptr;
    int arg_count = 0;
    
    line[strcspn(line, "\n")] = 0;
    
    if (line[0] == '\0' || line[0] == '#') return 0;
    
    proc->argv = malloc(sizeof(char*) * (MAX_ARGS + 1));
    if (!proc->argv) return -1;
    
    token = strtok_r(line, " \t", &saveptr);
    if (!token) {
        free(proc->argv);
        return -1;
    }

    if (token[0] != '/') {
        free(proc->argv);
        return -1;
    }

    proc->cmd_path = strdup(token);
    proc->argv[arg_count++] = strdup(token);
    
    while ((token = strtok_r(NULL, " \t", &saveptr)) && arg_count < MAX_ARGS - 2) {
        if (arg_count >= MAX_ARGS - 2) break;
        proc->argv[arg_count++] = strdup(token);
    }
    
    token = strtok_r(NULL, " \t", &saveptr);
    if (!token) {
        proc->stdin_file = NULL;
    } else {
        if (token[0] != '/') {
            free(proc->argv);
            return -1;
        }
        proc->stdin_file = strdup(token);
    }
    
    token = strtok_r(NULL, " \t", &saveptr);
    if (!token) {
        proc->stdout_file = NULL;
    } else {
        if (token[0] != '/') {
            free(proc->argv);
            return -1;
        }
        proc->stdout_file = strdup(token);
    }
    
    proc->argv[arg_count] = NULL;
    proc->active = 0;
    proc->pid = -1;
    
    return 1;
}

int read_config() {
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        log_message("ERROR: Cannot open config file");
        return -1;
    }
    
    char line[MAX_LINE_LEN];
    int count = 0;
    
    while (fgets(line, sizeof(line), fp) && count < MAX_CMDS) {
        int result = parse_config_line(line, &processes[count]);
        if (result == 1) {
            count++;
        }
    }
    
    fclose(fp);
    num_processes = count;
    
    char msg[256];
    sprintf(msg, "Config loaded: %d processes defined", num_processes);
    log_message(msg);
    
    return 0;
}

void start_process(int idx) {
    if (idx >= num_processes) return;
    
    pid_t pid = fork();
    
    if (pid == -1) {
        char msg[256];
        sprintf(msg, "ERROR: Fork failed for process %d", idx);
        log_message(msg);
        return;
    }
    
    if (pid == 0) {
        if (processes[idx].stdin_file) {
            int fd = open(processes[idx].stdin_file, O_RDONLY);
            if (fd == -1) {
                fd = open("/dev/null", O_RDONLY);
            }
            dup2(fd, STDIN_FILENO);
            if (fd > 2) close(fd);
        } else {
            int fd = open("/dev/null", O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        if (processes[idx].stdout_file) {
            int fd = open(processes[idx].stdout_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                fd = open("/dev/null", O_WRONLY);
            }
            dup2(fd, STDOUT_FILENO);
            if (fd > 2) close(fd);
        } else {
            int fd = open("/dev/null", O_WRONLY);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        
        execvp(processes[idx].argv[0], processes[idx].argv);
        
        char msg[256];
        sprintf(msg, "ERROR: exec failed for %s", processes[idx].cmd_path);
        log_message(msg);
        exit(1);
    }
    
    processes[idx].pid = pid;
    processes[idx].active = 1;
    
    char msg[256];
    sprintf(msg, "Started process %d: %s (pid=%d)", idx, processes[idx].cmd_path, pid);
    log_message(msg);
}

void start_all_processes() {
    for (int i = 0; i < num_processes; i++) {
        if (!processes[i].active) {
            start_process(i);
        }
    }
}

void stop_all_processes() {
    for (int i = 0; i < num_processes; i++) {
        if (processes[i].active && processes[i].pid > 0) {
            char msg[256];
            sprintf(msg, "Terminating process %d (pid=%d)", i, processes[i].pid);
            log_message(msg);
            
            kill(processes[i].pid, SIGTERM);
            
            int status;
            pid_t result = waitpid(processes[i].pid, &status, WNOHANG);
            if (result == 0) {
                usleep(100000);
                kill(processes[i].pid, SIGKILL);
                waitpid(processes[i].pid, NULL, 0);
                sprintf(msg, "Force killed process %d (pid=%d)", i, processes[i].pid);
                log_message(msg);
            } else {
                sprintf(msg, "Process %d (pid=%d) terminated", i, processes[i].pid);
                log_message(msg);
            }
            
            processes[i].active = 0;
            processes[i].pid = -1;
        }
    }
}

void cleanup_processes() {
    for (int i = 0; i < num_processes; i++) {
        if (processes[i].cmd_path) free(processes[i].cmd_path);
        if (processes[i].stdin_file) free(processes[i].stdin_file);
        if (processes[i].stdout_file) free(processes[i].stdout_file);
        if (processes[i].argv) {
            for (int j = 0; processes[i].argv[j]; j++) {
                free(processes[i].argv[j]);
            }
            free(processes[i].argv);
        }
    }
    num_processes = 0;
}

void reload_config() {
    log_message("SIGHUP received - reloading configuration");
    stop_all_processes();
    cleanup_processes();
    
    if (read_config() == 0) {
        start_all_processes();
    } else {
        log_message("ERROR: Failed to reload config");
    }
}

void sighup_handler(int sig) {
    (void)sig;  // Подавляем warning о неиспользуемом параметре
    need_reload = 1;
}

void handle_child_exit() {
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < num_processes; i++) {
            if (processes[i].active && processes[i].pid == pid) {
                char msg[256];
                if (WIFEXITED(status)) {
                    sprintf(msg, "Process %d (pid=%d) exited with status %d - restarting", 
                            i, pid, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    sprintf(msg, "Process %d (pid=%d) killed by signal %d - restarting", 
                            i, pid, WTERMSIG(status));
                } else {
                    sprintf(msg, "Process %d (pid=%d) terminated - restarting", i, pid);
                }
                log_message(msg);
                
                processes[i].active = 0;
                processes[i].pid = -1;
                start_process(i);
                break;
            }
        }
    }
}

void daemonize() {
    struct rlimit flim;
    
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    
    if (fork() != 0) {
        exit(0);
    }
    
    setsid();
    
    if (fork() != 0) {
        exit(0);
    }
    
    getrlimit(RLIMIT_NOFILE, &flim);
    for (int fd = 0; fd < (int)flim.rlim_max; fd++) {
        close(fd);
    }
    
    if (chdir("/") != 0) {
        perror("chdir failed");
        exit(1);
    }
    
    int fd = open(log_file, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd > 0) {
        close(fd);
    }
    
    umask(022);
}

int main(int argc, char **argv) {
    int opt;
    
    while ((opt = getopt(argc, argv, "c:")) != -1) {
        switch (opt) {
            case 'c':
                config_file = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s -c <config_file>\n", argv[0]);
                exit(1);
        }
    }
    
    if (!config_file) {
        fprintf(stderr, "Config file is required: -c <config_file>\n");
        exit(1);
    }
    
    daemonize();
    
    struct sigaction sa;
    sa.sa_handler = sighup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        log_message("ERROR: Cannot set SIGHUP handler");
        exit(1);
    }
    
    if (read_config() != 0) {
        log_message("ERROR: Failed to read initial config");
        exit(1);
    }
    
    start_all_processes();
    
    while (1) {
        if (need_reload) {
            need_reload = 0;
            reload_config();
        }
        
        handle_child_exit();
        usleep(100000);
    }
    
    return 0;
}