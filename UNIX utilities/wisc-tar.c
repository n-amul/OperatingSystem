#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void write_header(FILE *tar_file, const char *filename)
{
    char str_filename[120];

    /* Add padding to the file name */
    for (int i = 0; i < 120; i++)
    {
        if (i <= strlen(filename))
        {
            str_filename[i] = filename[i];
        }
        else
        {
            str_filename[i] = 0;
        }
    }

    fwrite(str_filename, 120, 1, tar_file);
}

void write_file_content(FILE *tar_file, FILE *input_file, size_t file_size)
{
    size_t buffer_size = 512;
    char buffer[buffer_size];

    while (file_size > 0)
    {
        size_t bytes_to_read = (file_size < buffer_size) ? file_size : buffer_size;
        size_t bytes_read = fread(buffer, 1, bytes_to_read, input_file);
        if (bytes_read != bytes_to_read)
        {
            printf("Error reading input file");
            return;
        }
        fwrite(buffer, 1, bytes_read, tar_file);

        // If the read bytes are less than 512, pad the rest with zeros
        if (bytes_read < buffer_size)
        {
            memset(buffer + bytes_read, 0, buffer_size - bytes_read);
            fwrite(buffer + bytes_read, 1, buffer_size - bytes_read, tar_file);
        }

        file_size -= bytes_read;
    }

    // Pad the file to be a multiple of 512 bytes if necessary
    size_t padding_size = (512 - (file_size % 512)) % 512;
    if (padding_size > 0)
    {
        memset(buffer, 0, padding_size);
        fwrite(buffer, 1, padding_size, tar_file);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("[wisc-tar: tar-file file1 [...]]");
        return 1;
    }

    const char *output_filename = argv[1];
    FILE *tar_file = fopen(output_filename, "wb");
    if (tar_file == NULL)
    {
        printf("Error opening output file");
        return 1;
    }

    for (int i = 2; i < argc; ++i)
    {
        const char *input_filename = argv[i];
        FILE *input_file = fopen(input_filename, "rb");
        if (input_file == NULL)
        {
            printf("Error opening input file");
            fclose(tar_file);
            return 1;
        }

        // Get the size of the input file
        struct stat file_info;
        if (stat(input_filename, &file_info) != 0)
        {
            printf("Error getting file size");
            fclose(input_file);
            fclose(tar_file);
            return 1;
        }
        size_t file_size = file_info.st_size;

        // Write the header and file content
        write_header(tar_file, input_filename);
        fwrite(&file_info.st_size, 8, 1, tar_file);
        write_file_content(tar_file, input_file, file_size);
        fclose(input_file);
    }

    fclose(tar_file);
    return 0;
}
