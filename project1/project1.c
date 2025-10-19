#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>

#define MAX_PIPES 10
#define HISTORY_SIZE 100
#define MAX_ARGS 64

volatile sig_atomic_t interrupted = 0;
char *history[HISTORY_SIZE];
int hist_count = 0;
volatile pid_t foreground_pgid = 0;

void split_args(char *cmd, char **args);
void execute_pipeline(char **commands, int num_cmds);
void process_command(char *command);
void add_to_history(const char *command);
void handle_sigint(int sig);
void show_history(void);

void handle_sigint(int sig) {
    if (foreground_pgid > 0) {
        kill(-foreground_pgid, SIGKILL);
        foreground_pgid = 0;
    }
    interrupted = 1;
    write(STDOUT_FILENO, "\n", 1);
}

void add_to_history(const char *command) {
    if (hist_count < HISTORY_SIZE) {
        history[hist_count] = strdup(command);
    } else {
        free(history[0]);
        memmove(history, history+1, (HISTORY_SIZE-1)*sizeof(char*));
        history[HISTORY_SIZE-1] = strdup(command);
    }
    hist_count++;
}

void split_args(char *cmd, char **args) {
    int i = 0;
    char *token = strtok(cmd, " \t");
    while (token != NULL && i < MAX_ARGS-1) {
        args[i++] = token;
        token = strtok(NULL, " \t");
    }
    args[i] = NULL;
}

void execute_pipeline(char **commands, int num_cmds) {
    int pipefd[2 * MAX_PIPES];
    pid_t pids[MAX_PIPES];
    
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipefd + 2*i) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < num_cmds; i++) {
        if ((pids[i] = fork()) == 0) {
            setpgid(0, 0);
            if (i > 0) {
                dup2(pipefd[2*(i-1)], STDIN_FILENO);
                close(pipefd[2*(i-1)]);
            }
            if (i < num_cmds - 1) {
                dup2(pipefd[2*i+1], STDOUT_FILENO);
                close(pipefd[2*i+1]);
            }
            for (int j = 0; j < 2*(num_cmds-1); j++) {
                close(pipefd[j]);
            }
            char *args[MAX_ARGS];
            split_args(commands[i], args);
            if (args[0] == NULL) {
                fprintf(stderr, "Empty command\n");
                exit(EXIT_FAILURE);
            }
            execvp(args[0], args);
            perror("execvp");
            exit(EXIT_FAILURE);
        }
        else if (pids[i] < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        setpgid(pids[i], pids[0]);
    }

    for (int j = 0; j < 2*(num_cmds-1); j++) {
        close(pipefd[j]);
    }

    for (int i = 0; i < num_cmds; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

void show_history(void) {
    int start = hist_count > HISTORY_SIZE ? hist_count - HISTORY_SIZE : 0;
    for (int i = start; i < hist_count; i++) {
        printf("%4d: %s\n", i + 1, history[i]);
    }
}

void process_command(char *command) {
    while (isspace(*command)) command++;
    
    if (strcmp(command, "exit") == 0) exit(0);
    if (strcmp(command, "history") == 0) {
        show_history();
        return;
    }
    
    if (*command == '\0') return;

    if (strchr(command, '|')) {
        char *cmds[MAX_PIPES+1];
        int i = 0;
        char *saveptr;
        char *token = strtok_r(command, "|", &saveptr);
        
        while (token != NULL && i < MAX_PIPES) {
            while (isspace(*token)) token++;
            char *end = token + strlen(token) - 1;
            while (end > token && isspace(*end)) end--;
            *(end+1) = '\0';
            
            if (*token != '\0') {
                cmds[i++] = token;
            }
            token = strtok_r(NULL, "|", &saveptr);
        }
        
        if (i > 1) {
            execute_pipeline(cmds, i);
            return;
        }
    }

    char *and_pos = strstr(command, "&&");
    if (and_pos != NULL) {
        *and_pos = '\0';
        char *first_cmd = command;
        char *second_cmd = and_pos + 2;
        
        while (isspace(*second_cmd)) second_cmd++;
        
        process_command(first_cmd);
        if (foreground_pgid == 0) {
            process_command(second_cmd);
        }
        return;
    }

    if (strstr(command, ">>")) {
        char *append_pos = strstr(command, ">>");
        *append_pos = '\0';
        char *outfile = append_pos + 2;
        while (isspace(*outfile)) outfile++;

        pid_t pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            int fd = open(outfile, O_WRONLY|O_CREAT|O_APPEND, 0644);
            if (fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            char *args[MAX_ARGS];
            split_args(command, args);
            execvp(args[0], args);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            foreground_pgid = pid;
            signal(SIGTTOU, SIG_IGN);
            tcsetpgrp(STDIN_FILENO, pid);
            waitpid(pid, NULL, 0);
            tcsetpgrp(STDIN_FILENO, getpgrp());
            signal(SIGTTOU, SIG_DFL);
            foreground_pgid = 0;
        }
        return;
    }

    if (strchr(command, '>')) {
        char *output_pos = strchr(command, '>');
        *output_pos = '\0';
        char *outfile = output_pos + 1;
        while (isspace(*outfile)) outfile++;

        pid_t pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            int fd = open(outfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
            if (fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            char *args[MAX_ARGS];
            split_args(command, args);
            execvp(args[0], args);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            foreground_pgid = pid;
            signal(SIGTTOU, SIG_IGN);
            tcsetpgrp(STDIN_FILENO, pid);
            waitpid(pid, NULL, 0);
            tcsetpgrp(STDIN_FILENO, getpgrp());
            signal(SIGTTOU, SIG_DFL);
            foreground_pgid = 0;
        }
        return;
    }

    if (strchr(command, '<')) {
        char *input_pos = strchr(command, '<');
        *input_pos = '\0';
        char *infile = input_pos + 1;
        while (isspace(*infile)) infile++;

        pid_t pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            int fd = open(infile, O_RDONLY);
            if (fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
            char *args[MAX_ARGS];
            split_args(command, args);
            execvp(args[0], args);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            foreground_pgid = pid;
            signal(SIGTTOU, SIG_IGN);
            tcsetpgrp(STDIN_FILENO, pid);
            waitpid(pid, NULL, 0);
            tcsetpgrp(STDIN_FILENO, getpgrp());
            signal(SIGTTOU, SIG_DFL);
            foreground_pgid = 0;
        }
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        char *args[MAX_ARGS];
        split_args(command, args);
        execvp(args[0], args);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        foreground_pgid = pid;
        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(STDIN_FILENO, pid);
        waitpid(pid, NULL, 0);
        tcsetpgrp(STDIN_FILENO, getpgrp());
        signal(SIGTTOU, SIG_DFL);
        foreground_pgid = 0;
    }
}

int main() {
    char input[1024];
    struct sigaction sa;
    
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    signal(SIGTSTP, SIG_IGN);

    while (1) {
        if (!interrupted) {
            printf("sh> ");
            fflush(stdout);
        }
        interrupted = 0;
        
        if (!fgets(input, sizeof(input), stdin)) {
            if (feof(stdin)) {
                printf("\n");
                break;
            }
            continue;
        }
        
        input[strcspn(input, "\n")] = 0;
        add_to_history(input);

        char *commands[100];
        int cmd_count = 0;
        char *token = strtok(input, ";");
        while (token != NULL && cmd_count < 99) {
            commands[cmd_count++] = token;
            token = strtok(NULL, ";");
        }

        for (int i = 0; i < cmd_count; i++) {
            process_command(commands[i]);
        }
    }

    for (int i = 0; i < hist_count && i < HISTORY_SIZE; i++) {
        free(history[i]);
    }

    return 0;
}
