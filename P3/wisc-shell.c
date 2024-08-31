#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PROMPT "wish> "
#define MAX_INPUT_SIZE 512
#define ERROR_CMD_NOT_FOUND "Error: command not found\n"
#define ERROR_BATCH_FILE "Error: could not open batchfile\n"
#define ERROR_REDIRECTION "Redirection error\n"
#define ERROR_ALIAS_NOT_FOUND "Error: alias not found\n"
#define ERROR_ENV_NOT_PRESENT "unset: environment variable not present\n"

// Alias structure
typedef struct Alias
{
    char *name;
    char *value;
    struct Alias *next;
} Alias;
// tail
Alias *alias_list = NULL;

char *read_line(FILE *file)
{
    char *line = malloc(MAX_INPUT_SIZE);
    if (fgets(line, MAX_INPUT_SIZE, file) == NULL)
    {
        free(line);
        return NULL;
    }
    return line;
}

char **split_line(char *line)
{
    int bufsize = MAX_INPUT_SIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token;

    token = strtok(line, " \t\r\n\a");
    while (token != NULL)
    {
        tokens[position++] = token;
        if (position >= bufsize)
        {
            bufsize += MAX_INPUT_SIZE;
            tokens = realloc(tokens, bufsize * sizeof(char *));
        }
        token = strtok(NULL, " \t\r\n\a");
    }
    tokens[position] = NULL;
    return tokens;
}

// alias
void add_alias(char *name, char *value)
{
    Alias *current = alias_list;
    // Check if alias already exists
    while (current != NULL)
    {
        if (strcmp(current->name, name) == 0)
        {
            free(current->value);
            current->value = strdup(value);
            return;
        }
        current = current->next;
    }
    Alias *new_alias = malloc(sizeof(Alias));
    new_alias->name = strdup(name);
    new_alias->value = strdup(value);
    new_alias->next = alias_list;
    alias_list = new_alias;
}

char *get_alias(char *name)
{
    Alias *current = alias_list;
    while (current != NULL)
    {
        if (strcmp(current->name, name) == 0)
        {
            return current->value;
        }
        current = current->next;
    }
    return NULL;
}

void print_aliases()
{
    Alias *stack[1000]; // Assuming the list won't have more than 1000 elements
    int top = -1;
    Alias *current = alias_list;
    while (current != NULL)
    {
        stack[++top] = current;
        current = current->next;
    }
    // Pop elements from the stack and print them
    while (top >= 0)
    {
        current = stack[top--];
        printf("%s='%s'\n", current->name, current->value);
    }
}

void print_alias(char *name)
{
    char *value = get_alias(name);
    if (value)
    {
        printf("%s='%s'\n", name, value);
    }
    else
    {
        fprintf(stderr, ERROR_ALIAS_NOT_FOUND);
    }
}

// envir var
void set_env_var(char *name, char *value)
{

    if (setenv(name, value, 1) != 0)
    {
        perror("setenv");
    }
}
void handle_unset(char **args)
{
    for (int i = 1; args[i] != NULL; i++)
    {
        if (getenv(args[i]) == NULL)
        {
            printf(ERROR_ENV_NOT_PRESENT);
        }
        else
        {
            unsetenv(args[i]);
        }
    }
}
void replace_env_vars(char **args, int *notfound)
{
    for (int i = 0; args[i] != NULL; i++)
    {
        if (args[i][0] == '$' && args[i][1] != '\0')
        {
            char *env_var = getenv(args[i] + 1);
            if (env_var)
            {
                args[i] = env_var;
            }
            else
            {
                *notfound = 1;
                return;
            }
        }
    }
}

// Redirection
char *handle_redirection(char **args, int *redirection_error)
{
    int i = 0;
    while (args[i] != NULL)
    {
        if (strcmp(args[i], ">") == 0)
        {
            if (args[i + 1] == NULL || args[i + 2] != NULL)
            {
                *redirection_error = 1;
                return NULL;
            }
            return strdup(args[i + 1]);
        }
        i++;
    }
    return NULL;
}
void execute_command(char **args)
{
    if (args[0] == NULL)
        return; // Empty command

    if (strcmp(args[0], "exit") == 0)
    {
        exit(0);
    }
    // change $ variables
    int notfound = 0;
    replace_env_vars(args, &notfound);
    if (notfound == 1)
    {
        return;
    }

    // Check if the command is an alias
    char *alias_value = get_alias(args[0]);
    if (alias_value != NULL)
    {
        // Execute the new command array
        char *str = strdup(alias_value);
        execute_command(split_line(str));
        free(str);
        return;
    }

    if (strcmp(args[0], "alias") == 0)
    {
        if (args[1] == NULL)
        {
            print_aliases();
        }
        else if (args[2] == NULL)
        {
            print_alias(args[1]);
        }
        else
        {
            int total_length = 0;
            for (int i = 2; args[i] != NULL; i++)
            {
                total_length += strlen(args[i]) + 1; // +1 for space or null terminator
            }
            char *value = (char *)malloc(total_length);
            value[0] = '\0'; // Initialize the result string with the null terminator

            for (int i = 2; args[i] != NULL; i++)
            {
                strcat(value, args[i]);
                if (args[i + 1] != NULL)
                {
                    strcat(value, " ");
                }
            }
            add_alias(args[1], value);
            free(value);
        }
        return;
    } // enviroment variable
    else if (strcmp(args[0], "export") == 0)
    {
        if (args[1] != NULL)
        {
            char *name = strtok(args[1], "=");
            char *value = strtok(NULL, "=");
            if (name && value)
            {
                set_env_var(name, value);
            }
        }
        return;
    }
    else if (strcmp(args[0], "unset") == 0)
    {
        handle_unset(args);
        return;
    }

    // Handle redirection
    int redirection_error = 0;
    char *fileName = handle_redirection(args, &redirection_error);
    if (redirection_error)
    {
        fprintf(stderr, ERROR_REDIRECTION);
        return;
    }

    if (fileName != NULL)
    {
        // Remove redirection part from args
        int i = 0;
        while (args[i] != NULL)
        {
            if (strcmp(args[i], ">") == 0)
            {
                args[i] = NULL; // terminate the args array
                break;
            }
            i++;
        }
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        if (fileName != NULL)
        {
            int fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (fd == -1)
            {
                fprintf(stderr, ERROR_REDIRECTION);
                exit(EXIT_FAILURE);
            }
            if (dup2(fd, STDOUT_FILENO) == -1)
            {
                fprintf(stderr, ERROR_REDIRECTION);
                exit(EXIT_FAILURE);
            }
            close(fd);
        }

        if (execvp(args[0], args) == -1)
        {
            fprintf(stderr, ERROR_CMD_NOT_FOUND);
        }
        exit(EXIT_FAILURE);
    }
    else if (pid < 0)
    {
        perror("fork");
    }
    else
    {
        waitpid(pid, NULL, 0);
    }

    if (fileName != NULL)
    {
        free(fileName);
    }
}

int main(int argc, char *argv[])
{
    FILE *input = stdin;
    if (argc == 2)
    {
        input = fopen(argv[1], "r");
        if (input == NULL)
        {
            fprintf(stderr, ERROR_BATCH_FILE);
            exit(1);
        }
    }
    else if (argc > 2)
    {
        fprintf(stderr, "Usage: wish [batch-file]\n");
        exit(1);
    }

    char *line;
    char **args;
    while (1)
    {
        if (argc == 1)
        {
            write(STDOUT_FILENO, PROMPT, strlen(PROMPT));
        }
        line = read_line(input);
        if (line == NULL)
            break;
        if (argc == 2)
            printf("%s", line); // if batch Print the command before execution
        args = split_line(line);
        if (args[0] != NULL)
        {
            execute_command(args);
        }
        free(line);
        free(args);
    }

    if (argc == 2)
        fclose(input);
    return 0;
}
