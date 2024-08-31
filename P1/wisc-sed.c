#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void replace(const char *search_string, const char *replacement_string, const char *file_name, int line_number, const char *output_file, int incase)
{
    // open file for reading
    FILE *file = fopen(file_name, "r");
    if (!file)
    {
        printf("wisc-sed: cannot open file\n");
        exit(1);
    }

    // Create a temporary file
    FILE *temp_file = tmpfile();
    if (!temp_file)
    {
        printf("Error creating temporary file\n");
        fclose(file);
        exit(1);
    }

    // write desired string in temp_file
    size_t len = 0;
    char *line = NULL;
    ssize_t read;
    int cnt = 0;
    while ((read = getline(&line, &len, file)) != -1)
    {
        // This was the way the world ends is->was
        cnt++;
        char *pos = line;
        char *start = line;
        char *(*fun)(const char *, const char *);
        fun = (incase) ? strcasestr : strstr;

        if (line_number == -1 || line_number == cnt)
        {
            while ((pos = fun(pos, search_string)) != NULL)
            {
                fwrite(start, sizeof(char), pos - start, temp_file);
                fputs(replacement_string, temp_file);
                pos += strlen(search_string);
                start = pos;
            }
            fputs(start, temp_file);
        }
        else
        {
            fputs(line, temp_file);
        }
    }

    free(line);
    fclose(file);

    // overwrite tempfile to dest file
    rewind(temp_file);

    file = (output_file) ? fopen(output_file, "wb") : stdout;
    if (!file)
    {
        printf("Error opening file for writing\n");
        fclose(temp_file);
        exit(1);
    }

    char buffer[1024];
    size_t n;
    while ((n = fread(buffer, sizeof(char), sizeof(buffer), temp_file)) > 0)
    {
        fwrite(buffer, sizeof(char), n, file);
    }

    fclose(file);
    fclose(temp_file);
}
int main(int argc, char **argv)
{
    char *search_string = NULL;
    char *replacement_string = NULL;
    char *file_name = NULL;
    char *output_file = NULL;
    int line_number = -1;
    int case_insensitive = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-s") == 0)
        {
            search_string = argv[++i];
        }
        else if (strcmp(argv[i], "-r") == 0)
        {
            replacement_string = argv[++i];
        }
        else if (strcmp(argv[i], "-f") == 0)
        {
            file_name = argv[++i];
        }
        else if (strcmp(argv[i], "-o") == 0)
        {
            output_file = argv[++i];
        }
        else if (strcmp(argv[i], "-n") == 0)
        {
            line_number = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-c") == 0)
        {
            case_insensitive = 1;
        }
    }
    if (!search_string || !replacement_string || !file_name)
    {
        printf("usage: wisc-sed [optional flags] -s <search string> -r <replacement string> -f <file>\n");
        return 1;
    }
    replace(search_string, replacement_string, file_name, line_number, output_file, case_insensitive);
    return 0;
}
