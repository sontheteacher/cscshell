/*****************************************************************************/
/*                           CSC209-24s A3 CSCSHELL                          */
/*       Copyright 2024 -- Demetres Kostas PhD (aka Darlene Heliokinde)      */
/*****************************************************************************/

#include "cscshell.h"

#define CONTINUE_SEARCH NULL 

// COMPLETE
char *resolve_executable(const char *command_name, Variable *path){

    if (command_name == NULL || path == NULL){
        return NULL;
    }

    if (strcmp(command_name, CD) == 0){
        return strdup(CD);
    }

    if (strcmp(path->name, PATH_VAR_NAME) != 0){
        ERR_PRINT(ERR_NOT_PATH);
        return NULL;
    }

    char *exec_path = NULL;

    if (strchr(command_name, '/')){
        exec_path = strdup(command_name);
        if (exec_path == NULL){
            perror("resolve_executable");
            return NULL;
        }
        return exec_path;
    }

    // we create a duplicate so that we can mess it up with strtok
    char *path_to_toke = strdup(path->value);
    if (path_to_toke == NULL){
        perror("resolve_executable");
        return NULL;
    }
    char *current_path = strtok(path_to_toke, ":");

    do {
        DIR *dir = opendir(current_path);
        if (dir == NULL){
            ERR_PRINT(ERR_BAD_PATH, current_path);
            //closedir(dir);
            continue;
        }

        struct dirent *possible_file;

        while (exec_path == NULL) {
            // rare case where we should do this -- see: man readdir
            errno = 0;
            possible_file = readdir(dir);
            if (possible_file == NULL) {
                if (errno > 0){
                    perror("resolve_executable");
                    closedir(dir);
                    goto res_ex_cleanup;
                }
                // end of files, break
                break;
            }

            if (strcmp(possible_file->d_name, command_name) == 0){
                // +1 null term, +1 possible missing '/'
                size_t buflen = strlen(current_path) +
                    strlen(command_name) + 1 + 1;
                exec_path = (char *) malloc(buflen);
                // also sets remaining buf to 0
                strncpy(exec_path, current_path, buflen);
                if (current_path[strlen(current_path)-1] != '/'){
                    strncat(exec_path, "/", 2);
                }
                strncat(exec_path, command_name, strlen(command_name)+1);
            }
        }
        closedir(dir);

        // if this isn't null, stop checking paths
        if (possible_file) break;

    } while ((current_path = strtok(CONTINUE_SEARCH, ":")));

res_ex_cleanup:
    free(path_to_toke);
    return exec_path;
}

Command *parse_line(char *line, Variable **variables){
    /**
     * Parse a line into a list of commands if connected by pipes "|"
    */

    line = strdup(line);
    if (line == NULL) {
        perror("malloc");
        return (Command *) -1;
    }
    trim_leading_white_space(line);
    
    // ptr is a general pointer we use in this function
    char *ptr = strchr(line, '#');
    if (ptr != NULL) {
        *ptr = '\0';
    }

    // If line empty return NULL
    if (strlen(line) == 0) {
        return NULL;
    }

   // Split cases, whether if this is a command or a variable assignment
    ptr = strchr(line, '=');
    
    // Potential variable assignment
    if (ptr != NULL) {
        // Can't start variable assignment w '='
        if (line[0] == '=') {
            fputs(ERR_VAR_START, stdout);
            return (Command *) -1;
        }

        // Check valid variable names
        char var_name[ptr - line + 1];
        var_name[ptr - line] = '\0';
        if (!strncpy(var_name, line, ptr - line)) {
            ERR_PRINT("strncpy");
            return (Command *) -1;
        }
        for (int i = 0; i < strlen(var_name); i++) {
            bool valid = isalpha(var_name[i]) || line[i] == '_';
            if (!valid) {
                ERR_PRINT(ERR_VAR_NAME, var_name);
                return (Command *) -1;
            }
        }
        
        // create var_val to store the value of the variable
        char var_val[strlen(line) - strlen(var_name)]; //subtract 1 for the = sign
        var_val[strlen(line) - strlen(var_name) - 1] = '\0';
        strncpy(var_val, ptr + 1, strlen(line) - strlen(var_name) - 1);
        
        // Traverse the linked list to assign the variable
        update_linked_list_variable(variables, var_name, var_val);
        free(line);
        return NULL;
    }

    // This case is a potential command
    else {
        // Trim leading and trailing whitespace
        trim_white_space(line);
        // Replace all variables with values
        char* line_replaced = replace_variables_mk_line(line, *variables);
        if (line_replaced == NULL) {
            free(line);
            return (Command *) -1;
        }
        free(line);
        if (strchr(line_replaced, '|') == NULL && strchr(line_replaced, '>') == NULL && strchr(line_replaced, '<') == NULL) {
            // We only have one command
            // First determine the command name and arguments
            char **args = (char **)malloc(sizeof(char *) * (count_word(line_replaced) + 1));
            if (args == NULL) {
                perror("malloc");
                free(line_replaced);
                return (Command *) -1;
            }
            extract_commands(args, line_replaced);
            Command *cmd = set_command(args, *variables, NULL, STDIN_FILENO, STDOUT_FILENO, NULL, NULL, 0);
            if (cmd == (Command *) -1) {
                free(args);
                free(line_replaced);
                return (Command *) -1;
            }
            free(line_replaced);
            return cmd;
        }
        else {
            // We have either piping or redirection or both
            // Linked list of commands
            Command *head = NULL;
            Command **curr = &head;
            
            // Pointer of start and end of current command
            char *start = line_replaced;
            char *end = line_replaced;

            // First, we get the list of commands with 
            // their respective arguments, but we don't
            // set the file descriptors yet
            int i = 0; // index of current character
            while (line_replaced[i] != '\0') {
                char c = line_replaced[i];
                if (c == '|') {
                    if (end - start > 0) {
                        char *cmd = (char *)malloc(end - start + 1);
                        if (cmd == NULL) {
                            perror("malloc");
                            return (Command *) -1;
                        }
                        strncpy(cmd, start, end - start);
                        cmd[end - start] = '\0';
                        trim_white_space(cmd);
                        if (strlen(cmd) == 0) {
                            free(cmd);
                            end++;
                            start = end;
                            i++;
                            continue;
                        }
                        char **args = (char **)malloc(sizeof(char *) * (count_word(cmd) + 1));
                        if (args == NULL) {
                            perror("malloc");
                            free(cmd);
                            free_command(head);
                            return (Command *) -1;
                        }
                        extract_commands(args, cmd);
                        *curr = set_command(args, *variables, NULL, fileno(stdin), fileno(stdout), NULL, NULL, 0);
                        if (*curr == (Command *) -1) {
                            return (Command *) -1;
                        }
                        curr = &((*curr) -> next);
                    }
                    // Move past the '|' character
                    // Note that end is the '|' character
                    end++;
                    start = end;
                    i++;
                }
                else if (c == '>') {
                    // We have a redirect output
                    if (end - start > 0) {
                        char *cmd = (char *)malloc(end - start + 1);
                        if (cmd == NULL) {
                            perror("malloc");
                            return (Command *) -1;
                        }
                        strncpy(cmd, start, end - start);
                        cmd[end - start] = '\0';
                        trim_white_space(cmd);
                        if (strlen(cmd) == 0) {
                            free(cmd);
                            free(line_replaced);
                            free_command(head);
                            continue;
                        }
                        char **args = (char **)malloc(sizeof(char *) * (count_word(cmd) + 1));
                        if (args == NULL) {
                            perror("malloc");
                            free(cmd);
                            free_command(head);
                            free(line_replaced);
                            return (Command *) -1;
                        }
                        extract_commands(args, cmd);
                        int name_len;
                        if (i + 1 < strlen(line_replaced) && line_replaced[i + 1] == '>') {
                            // We have a redirection as 
                            char *file_name = extract_file_name(line_replaced, i + 2);
                            name_len = strlen(file_name);
                            trim_white_space(file_name);
                            if (file_name == NULL) {
                                return (Command *) -1;
                            }
                            *curr = set_command(args, *variables, NULL, fileno(stdin), fileno(stdout), NULL, file_name, 1);
                            if (*curr == (Command *) -1) {
                                return (Command *) -1;
                            }
                            i++;
                            end++;
                        }
                        else {
                            // We have a redirection without append
                            char *file_name = extract_file_name(line_replaced, i + 1);
                            name_len = strlen(file_name);
                            trim_white_space(file_name);
                            if (file_name == NULL) {
                                return (Command *) -1;
                            }
                            *curr = set_command(args, *variables, NULL, fileno(stdin), fileno(stdout), NULL, file_name, 0);
                            if (*curr == (Command *) -1) {
                                free(file_name);
                                free(line_replaced);
                                free_command(head);
                                return (Command *) -1;
                            }
                        }
                        curr = &((*curr) -> next);
                        end += name_len + 1;
                        start = end;
                        i += name_len + 1;
                    }

                }
                else if (c == '<') {
                    if (end - start > 0) {
                        char *cmd = (char *)malloc(end - start + 1);
                        if (cmd == NULL) {
                            perror("malloc");
                            free(line_replaced);
                            free_command(head);
                            return (Command *) -1;
                        }
                        strncpy(cmd, start, end - start);
                        cmd[end - start] = '\0';
                        trim_white_space(cmd);
                        if (strlen(cmd) == 0) {
                            free(line_replaced);
                            free_command(head);
                            free(cmd);
                            continue;
                        }
                        char **args = (char **)malloc(sizeof(char *) * (count_word(cmd) + 1));
                        if (args == NULL) {
                            perror("malloc");
                            free(cmd);
                            return (Command *) -1;
                        }
                        extract_commands(args, cmd);
                        char *file_name = extract_file_name(line_replaced, i + 1);
                        int name_len = strlen(file_name);
                        trim_white_space(file_name);
                        if (file_name == NULL) {
                            free(line_replaced);
                            free_command(head);
                            free(cmd);
                            return (Command *) -1;
                        }
                        // We have an input redirection
                        *curr = set_command(args, *variables, NULL, fileno(stdin), fileno(stdout), file_name, NULL, 0);
                        if (*curr == (Command *) -1) {
                            free(line_replaced);
                            free_command(head);
                            free(cmd);
                            free(file_name);
                            return (Command *) -1;
                        }
                        curr = &((*curr) -> next);
                        end += name_len + 1;
                        start = end;
                        i += name_len + 1;
                    }
                }
                else {
                    end++;
                    i++;
                }
            }
            // Add the last cmd
            if (end - start > 0) {
                char *cmd = (char *)malloc(end - start + 1);
                if (cmd == NULL) {
                    perror("malloc");
                    free(line_replaced);
                    free_command(head);
                    return (Command *) -1;
                }
                strncpy(cmd, start, end - start);
                cmd[end - start] = '\0';
                trim_white_space(cmd);
                if (strlen(cmd) != 0) {
                    char **args = (char **)malloc(sizeof(char *) * (count_word(cmd) + 1));
                    if (args == NULL) {
                        perror("malloc");
                        free(line_replaced);
                        free_command(head);
                        free(cmd);
                        return (Command *) -1;
                    }
                    extract_commands(args, cmd);
                    *curr = set_command(args, *variables, NULL, fileno(stdin), fileno(stdout), NULL, NULL, 0);
                    if (*curr == (Command *) -1) {
                        free(line_replaced);
                        free_command(head);
                        free(cmd);
                        return (Command *) -1;
                    }
                    curr = &((*curr) -> next);
                }
                else {
                    free(cmd);
                }
            }

            // Now we have the list of commands, we need to set the file descriptors
            int mypipe[2];
            if (pipe(mypipe)) {
                perror("pipe");
                free_command(head);
                return (Command *) -1;
            }
            // setup_commands(head);
            free(line_replaced);
            return head;
        }
    }
}

void extract_commands(char **args, char *str) {
    /***
     * Extract the command name and arguments from the line
     * and store them in @param args, args[0] is the command name
     * The last element of args is NULL
     */
    int i = 0;
    char *saveptr;
    char *line = strdup(str);
    if (line == NULL) {
        perror("malloc");
        return;
    }
    char *token = strtok_r(line, " ", &saveptr);
    while (token != NULL) {
        args[i] = strdup(token);
        if (args[i] == NULL) {
            perror("malloc");
            i--;
            while (i >= 0) {
                free(args[i]);
                i--;
            }
            free(line);
            return;
        }
        token = strtok_r(NULL, " ", &saveptr);
        i++;
    }
    args[i] = NULL;
    free(line);
}

Command *set_command(char **args, Variable *path, struct Command *next, uint32_t stdin_fd, uint32_t stdout_fd,
char *redir_in_path, char *redir_out_path, uint8_t redir_append) {
    /***
     * We have one single command to handle, so return 
     * the Command* for this command
     */
    Command *cmd = (Command *)malloc(sizeof(Command));
    if (cmd == NULL) {
        perror("malloc");
        return (Command *) -1;
    }
    char *cmd_name = args[0];
    cmd -> exec_path = resolve_executable(cmd_name, path);
    if (cmd -> exec_path == NULL) {
        ERR_PRINT(ERR_NO_EXECU, cmd_name);
        free(cmd);
        return (Command *) -1;
    }
    args[0] = strdup(cmd -> exec_path);

    // Set the rest of the command
    cmd -> next = next;
    cmd -> stdin_fd = stdin_fd;
    cmd -> stdout_fd = stdout_fd;
    cmd -> redir_in_path = redir_in_path;
    cmd -> redir_out_path = redir_out_path;
    cmd -> redir_append = redir_append;
    cmd -> args = args;
    return cmd;
}

void update_linked_list_variable(Variable **variables, const char *var_name, const char *var_val) {
    /***
     * Update @param var_name in linkedlist @param variables if variable exists,
     * else append it to the end of the linked list
    */

    Variable *root = *variables;
    if (root == NULL) {
        // If the linked list is empty
        Variable *var = (Variable *)malloc(sizeof(Variable));
        if (var == NULL) {
            perror("malloc");
            return;
        }
        var -> name = strdup(var_name);
        if (var -> name == NULL) {
            perror("malloc");
            free(var);
            return;
        }
        var -> value = strdup(var_val);
        if (var -> value == NULL) {
            perror("malloc");
            free(var -> name);
            free(var);
            return;
        }
        var -> next = NULL;
        *variables = var;
        return;
    }
    if (strcmp(var_name, "PATH") == 0) {
        // Add it to beginning of the linked list
        Variable *var = (Variable *)malloc(sizeof(Variable));
        if (var == NULL) {
            perror("malloc");
            return;
        }
        var -> name = strdup(var_name);
        if (var -> name == NULL) {
            perror("malloc");
            free(var);
            return;
        }
        var -> value = strdup(var_val);
        if (var -> value == NULL) {
            perror("malloc");
            free(var -> name);
            free(var);
            return;
        }
        var -> next = root;
        *variables = var;
        return;
    }
    while (root -> next != NULL) {
        if (strcmp(root -> name, var_name) == 0) {
            free(root -> value);
            root -> value = strdup(var_val);
            if (root -> value == NULL) {
                perror("malloc");
            }
            return;
        }
        root = root -> next;
    }
    // Check the last variable
    if (strcmp(root -> name, var_name) == 0) {
        free(root -> value);
        root -> value = strdup(var_val);
        if (root -> value == NULL) {
            perror("malloc");
        }
        return;
    }

    // If not then append it to the end of the linked list
    Variable *var = (Variable *)malloc(sizeof(Variable));
    if (var == NULL) {
        perror("malloc\n");
        return;
    }
    var -> name = strdup(var_name);
    if (var -> name == NULL) {
        perror("malloc\n");
        free(var);
        return;
    }
    var -> value = strdup(var_val);
    if (var -> value == NULL) {
        perror("malloc\n");
        free(var -> name);
        free(var);
        return;
    }
    root -> next = var;
    var -> next = NULL;
}

void trim_white_space(char *str) {
    // Modify the string in place to remove leading and trailing whitespace
    if (str == NULL || *str == '\0') {
        return;
    }

    int start = 0;
    int end = strlen(str) - 1;

    // Trim leading whitespace
    while (isspace((unsigned char)str[start]) && start <= end) {
        start++;
    }

    // Trim trailing whitespace
    while (isspace((unsigned char)str[end]) && start <= end) {
        end--;
    }

    // Check if the entire string is whitespace or if it's empty
    if (start > end) {
        str[0] = '\0';
        return;
    }

    // Shift the text to the beginning of the string
    if (start > 0) {
        int length = end - start + 1;
        memmove(str, str + start, length);
        str[length] = '\0';  // Null-terminate the string
    } else {
        // Just null-terminate if no leading whitespace
        str[end + 1] = '\0';
    }
}

void trim_leading_white_space(char *str) {
    // Modify the string in place to remove leading whitespace
    if (str == NULL || *str == '\0') {
        return;
    }

    int start = 0;
    int end = strlen(str) - 1;

    // Trim leading whitespace
    while (isspace((unsigned char)str[start]) && start <= end) {
        start++;
    }

    // Shift the text to the beginning of the string
    if (start > 0) {
        int length = end - start + 1;
        memmove(str, str + start, length);
        str[length] = '\0';  // Null-terminate the string
    }
}


/*
** WARNING: this is a challenging string parsing task.
**
** Creates a new line on the heap with all named variable *usages*
** replaced with their associated values.
**
** Returns NULL if replacement parsing had an error, or (char *) -1 if
** system calls fail and the shell needs to exit.
*/
char *replace_variables_mk_line(const char *line,
                                Variable *variables){
    // NULL terminator accounted for here
    size_t new_line_length = strlen(line) + 1;

    // Commented so no warnings in starter make
    Variable *replacements = NULL;
    Variable **current = &replacements;
    const char *parse_var_st, *parse_var_end;

    // Code to determine new length
    // and list of replacements in order
    parse_var_st = strchr(line, '$');
    while (parse_var_st != NULL) {
        // Look for variable name
        parse_var_end = parse_var_st + 1;
        // Check if {} are used around variable name
        bool left_brace = parse_var_st[1] == '{';
        if (left_brace) {
            parse_var_end++;
        }

        while (isalpha((unsigned char)*parse_var_end) || *parse_var_end == '_') {
            parse_var_end++;
        }

        bool right_brace = parse_var_end[0] == '}';

        // Our var_name
        char *var_name = NULL;

        if (left_brace && right_brace) {
            var_name = (char *)malloc(parse_var_end - parse_var_st - 1);
            if (var_name == NULL) {
                perror("malloc");
                free_variable(replacements, 1);
                return (char *) -1;
            }
            new_line_length -= 2;
            strncpy(var_name, parse_var_st + 2, parse_var_end - parse_var_st - 2);
            var_name[parse_var_end - parse_var_st - 2] = '\0';
        }
        else if (!left_brace && !right_brace) {
            var_name = (char *)malloc(parse_var_end - parse_var_st);
            if (var_name == NULL) {
                perror("malloc");
                free_variable(replacements, 1);
                return (char *) -1;
            }
            strncpy(var_name, parse_var_st + 1, parse_var_end - parse_var_st - 1);
            var_name[parse_var_end - parse_var_st - 1] = '\0';
        }
        else {
            ERR_PRINT(ERR_EXECUTE_LINE);
            free_variable(replacements, 1);
            return NULL;
        }

        // Find the variable
        Variable *var = find_variable(variables, var_name);
        if (var == NULL) {
            ERR_PRINT(ERR_VAR_NOT_FOUND, var_name);
            free(var_name);
            free_variable(replacements, 1);
            return NULL;
        }
        free(var_name);
        *current = (Variable *)malloc(sizeof(Variable));
        if (*current == NULL) {
            perror("malloc");
            free_variable(replacements, 1);
            return (char *) -1;
        }
        (*current) -> name = strdup(var -> name);
        if ((*current) -> name == NULL) {
            perror("malloc");
            free(current);
            free_variable(replacements, 1);
            return (char *) -1;
        }
        (*current) -> value = strdup(var -> value);
        if ((*current) -> value == NULL) {
            perror("malloc");
            free_variable(replacements, 1);
            free((*current) -> name);
            free(*current);
            return (char *) -1;
        }
        (*current) -> next = NULL;
        current = &((*current) -> next);
        parse_var_st = strchr(parse_var_end, '$');
        new_line_length += strlen(var -> value);
        new_line_length -= strlen(var -> name) - 1;
    }

    // Now 'replacements' holds the list of found variables, in order
    char *new_line = (char *)malloc(new_line_length);
    if (new_line == NULL) {
        perror("malloc");
        free_variable(replacements, 1);
        return (char *) -1;
    }

    const char *line_ptr = line;
    char *new_line_ptr = new_line;
    Variable *current_replacement = replacements;

    // Copy the line, replacing variables
    while (*line_ptr != '\0') {
        if ((*line_ptr == '$') && current_replacement != NULL) {
            // Check for braces
            bool has_braces = *(line_ptr + 1) == '{';
            
            // Copy the replacement value
            strcpy(new_line_ptr, current_replacement->value);
            new_line_ptr += strlen(current_replacement->value);

            line_ptr++;
            if (has_braces){
                line_ptr++; // Skip the left brace
            }

            // Skip the variable name in the original line
            while (*line_ptr && (*line_ptr == '_' || isalpha((unsigned char)*line_ptr))) {
                line_ptr++;
            }

            if (has_braces && *line_ptr == '}'){
                line_ptr++; // Skip the right brace
            }

            current_replacement = current_replacement->next;
        } else {
            *new_line_ptr++ = *line_ptr++;
        }
    }

    // Free the linked list of replacements
    *new_line_ptr = '\0';
    free_variable(replacements, 1);
    return new_line;
}

void free_variable(Variable *var, uint8_t recursive){
    Variable *curr = var;
    Variable *next = NULL;
    while (curr != NULL) {
        next = curr -> next;
        free(curr -> name);
        free(curr -> value);
        free(curr);
        if (!recursive) {
            break;
        }
        curr = next;
    }
}

int count_word(const char *str) {
    /** 
     * Count the number of words in a string
     */

    char* current = strdup(str);
    char* ori = current;
    if (current == NULL) {
        perror("malloc");
        return -1;
    }

    trim_white_space(current);

    int count = 0;
    bool in_word = false;
    while (*current != '\0') {
        if (isspace((unsigned char)*current)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            count++;
        }
        current++;
    }
    free(ori);
    return count;
}

Variable *find_variable(Variable *variables, const char *var_name) {
    /***
     * Find the variable with name @param var_name in the linked list
     * @param variables
    */
    Variable *curr = variables;
    while (curr != NULL) {
        if (strcmp(curr -> name, var_name) == 0) {
            return curr;
        }
        curr = curr -> next;
    }
    return NULL;
}

char *extract_file_name(char *line, int start_index) {
    /**
     * Mainly use to extract filename after redirection
    */

    const char *sequence = "<>|";
    int i = start_index;
    while (i < strlen(line) && strchr(sequence, line[i]) == NULL) {
        i++;
    }
    char *file_name = (char *)malloc(i - start_index + 1);
    if (file_name == NULL) {
        perror("malloc");
        return NULL;
    }
    strncpy(file_name, line + start_index, i - start_index);
    file_name[i - start_index] = '\0';
    return file_name;
}