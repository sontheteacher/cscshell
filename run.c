/*****************************************************************************/
/*                           CSC209-24s A3 CSCSHELL                          */
/*       Copyright 2024 -- Demetres Kostas PhD (aka Darlene Heliokinde)      */
/*****************************************************************************/

#include "cscshell.h"


// COMPLETE
int cd_cscshell(const char *target_dir){
    if (target_dir == NULL) {
        char user_buff[MAX_USER_BUF];
        if (getlogin_r(user_buff, MAX_USER_BUF) != 0) {
           perror("run_command");
           return -1;
        }
        struct passwd *pw_data = getpwnam((char *)user_buff);
        if (pw_data == NULL) {
           perror("run_command");
           return -1;
        }
        target_dir = pw_data->pw_dir;
    }

    if(chdir(target_dir) < 0){
        perror("cd_cscshell");
        return -1;
    }
    return 0;
}


int *execute_line(Command *head){
    #ifdef DEBUG
    printf("\n***********************\n");
    printf("BEGIN: Executing line...\n");
    #endif

    #ifdef DEBUG
    printf("All children created\n");
    #endif

    if (head == NULL) {
        return NULL;
    }

    Command *curr = head;
    int *ret_code = (int *) malloc(sizeof(int));
    if (ret_code == NULL) {
        perror("malloc");
        return NULL;
    }
    *ret_code = 0;
    // Set up all the file descriptors for the commands
    while (curr != NULL && *ret_code != -1) {
        if (curr -> redir_in_path) {
            // Handle input redirection
            curr -> stdin_fd = open(curr -> redir_in_path, O_RDONLY);
            if (curr -> stdin_fd == -1) {
                perror("open");
                *ret_code = -1;
                free_command(head);
                return ret_code;
            }
        }

        if (curr -> next && curr -> redir_out_path) {
            // Can't have both piping and output redirection
            *ret_code = -1;
            return ret_code;
        }
        else if (curr -> next) {
            // Create a pipe
            int fd[2];
            if (pipe(fd) == -1) {
                perror("pipe");
                *ret_code = -1;
                free_command(head);
                return ret_code;
            }
            curr -> stdout_fd = fd[1];
            curr -> next -> stdin_fd = fd[0];
        }

        else if (curr -> redir_out_path) { 
            // Output redirection
            int flags = O_WRONLY | O_CREAT | (curr -> redir_append ? O_APPEND : O_TRUNC);
            curr -> stdout_fd = open(curr -> redir_out_path, flags, 0666);
            if (curr -> stdout_fd == -1) {
                perror("open");
                *ret_code = -1;
                free_command(head);
                return ret_code;
            }
        }

        *ret_code = run_command(curr);
        // Close the write end of the pipe
        // We still leave the read end open for the next command
        if (curr -> stdout_fd != STDOUT_FILENO) {
            close(curr -> stdout_fd);
        }
        curr = curr -> next;
    }
    #ifdef DEBUG
    printf("All children finished\n");
    #endif

    #ifdef DEBUG
    printf("END: Executing line...\n");
    printf("***********************\n\n");
    #endif
    free_command(head);
    return ret_code;
}


/*
** Forks a new process and execs the command
** making sure all file descriptors are set up correctly.
**
** Parent process returns -1 on error.
** Any child processes should not return.
*/
int run_command(Command *command){
    #ifdef DEBUG
    printf("Running command: %s\n", command->exec_path);
    printf("Argvs: ");
    if (command->args == NULL){
        printf("NULL\n");
    }
    else if (command->args[0] == NULL){
        printf("Empty\n");
    }
    else {
        for (int i=0; command->args[i] != NULL; i++){
            printf("%d: [%s] ", i+1, command->args[i]);
        }
    }
    printf("\n");
    printf("Redir out: %s\n Redir in: %s\n",
           command->redir_out_path, command->redir_in_path);
    printf("Stdin fd: %d | Stdout fd: %d\n",
           command->stdin_fd, command->stdout_fd);
    #endif

    // FOR CD COMMAND
    if (strcmp(command->exec_path, CD) == 0) {
        if (command->args[1] == NULL) {
            return cd_cscshell(NULL);
        }
        else {
            return cd_cscshell(command->args[1]);
        }
    }

    // We create a new process to execute the command with the arguments
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    else if (pid == 0) {
        if (command -> stdin_fd != STDIN_FILENO) {
            if (dup2(command -> stdin_fd, STDIN_FILENO) == -1) {
                perror("dup2");
                exit(-1);
            }
        }
        if (command -> stdout_fd != STDOUT_FILENO) {
            if (dup2(command -> stdout_fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                exit(-1);
            }
        }
        signal(SIGTTOU, SIG_IGN);
        execvp(command->exec_path, command->args);
        perror("execvp");
        exit(-1);
    }
    else {
        // Wait for the child process to finish
        // First close the read-end if we already read from pipe of the last command
        if (command -> stdin_fd != STDIN_FILENO) {
            close(command -> stdin_fd);
        }
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            return pid;
        }
        else if (WIFSTOPPED(status)) {
            printf("Child stopped by signal: %d\n", WSTOPSIG(status));
            return pid;
        }
        else {
            perror("waitpid");
            return -1;
        }
    }

    #ifdef DEBUG
    // printf("Parent process created child PID [%d] for %s\n", pid, command->exec_path);
    #endif
    return -1;
}

int run_script(char *file_path, Variable **root){
    FILE *stream = fopen(file_path, "r");
    if (stream == NULL){
        perror("fopen");
        return -1;
    }
    char *line = (char*)malloc(MAX_SINGLE_LINE*sizeof(char));
    if (line == NULL){
        perror("malloc");
        return -1;
    }
    size_t len = MAX_SINGLE_LINE - 1;
    int line_length;
    while ((line_length = getline(&line, &len, stream)) != -1){
        line[line_length - 1] = '\0'; // Remove the newline character
        Command *commands = parse_line(line, root);
        if (commands == (Command *) -1){
            ERR_PRINT(ERR_PARSING_LINE);
            continue;
        }
        if (commands == NULL) continue;

        int *last_ret_code_pt = execute_line(commands);
        if (*last_ret_code_pt == -1){
            ERR_PRINT(ERR_EXECUTE_LINE);
            free(last_ret_code_pt);
            return -1;
        }
        free(last_ret_code_pt);
    }
    free(line);
    fclose(stream);
    return 0;
}

void free_command(Command *command) {
    if (command == NULL){
        return;
    }
    free_command(command->next);
    if (command->exec_path != NULL) {
        free(command->exec_path);
    }
    char **args = command->args;    
    if (args != NULL) {
        for (int i=0; args[i] != NULL; i++){
            free(args[i]);
        }
        free(command->args);
    }
    if (command->redir_in_path != NULL){
        free(command->redir_in_path);
    }
    if (command->redir_out_path != NULL){
        free(command->redir_out_path);
    }
    free(command);
}