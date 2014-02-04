/*
    Copyright (c) 2014, xerpi
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PSPLOAD_VER       "1"
#define PSPLOAD_PORT      4299
#define PSPLOAD_ENVVAR    "PSPLOAD"
#define PSPLOAD_CHUNK_LEN 1024
#define PSPLOAD_MAGIC_1   'L'
#define PSPLOAD_MAGIC_2   'O'
#define PSPLOAD_MAGIC_3   'A'
#define PSPLOAD_MAGIC_4   'D'

/*
  PROTOCOL:
    1) Send header
    2) Send args
    3) Send file (splitted into PSPLOAD_CHUNK_LEN bytes packet)
*/

struct pspload_header {
    uint8_t  magic[4];
    uint32_t file_size;
    int32_t  argc;
} __attribute__((packed));


static char *pspload_envvar_str = NULL;
static uint8_t pspload_buffer[PSPLOAD_CHUNK_LEN];

void print_usage();
ssize_t pspload_send(int sock, void *data, uint32_t size);

int main(int argc, char *argv[])
{
    printf("pspload version "PSPLOAD_VER" by xerpi\n");
    
    if (argc < 2) {
        print_usage();
        goto exit_error;
    }

    if (!(pspload_envvar_str = getenv(PSPLOAD_ENVVAR))) {
        printf("Set PSPLOAD\n");
        goto exit_error;
    }
    if (strncmp(pspload_envvar_str, "tcp:", 4)) {
        printf("PSPLOAD seems invalid\n");
        goto exit_error;
    }

    FILE *fp;
    if (!(fp = fopen(argv[1], "rb"))) {
        perror("Error opening file");
        goto exit_error;   
    }
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size == 0) {
        printf("Empty file?\n");
        goto exit_close_file;
    }
        
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error creating socket");
        goto exit_close_file;
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port   = htons(PSPLOAD_PORT);
    server.sin_addr.s_addr = inet_addr(&pspload_envvar_str[4]);
    
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("Error connecting socket");
        goto exit_close_file;
    }
    printf("Connected to the server\n");
    
    int pspload_argc = argc - 2;
    struct pspload_header header = {
        .magic = {
            [0] = PSPLOAD_MAGIC_1,
            [1] = PSPLOAD_MAGIC_2,
            [2] = PSPLOAD_MAGIC_3,
            [3] = PSPLOAD_MAGIC_4
        },
        .file_size = file_size,
        .argc = pspload_argc
    };
    
    printf("Sending file...\n");
    
    /* Send header*/
    pspload_send(sock, &header, sizeof(header));
    
    /* Send args */
    int i;
    for (i = 0; i < pspload_argc; ++i) {
        int argv_size = strlen(argv[2 + i])+1;
        pspload_send(sock, &argv_size, sizeof(argv_size));
        pspload_send(sock, argv[2 + i], argv_size);
    }
    
    /* Send file */
    int total_bytes_sent = 0, bytes_read = 0, bytes_sent = 0;
    for (i = 0; total_bytes_sent < file_size; ++i) {
        bytes_read = fread(pspload_buffer, 1, PSPLOAD_CHUNK_LEN, fp);
        //printf("Read %d bytes from file\n", bytes_read);
        bytes_sent = pspload_send(sock, pspload_buffer, bytes_read);
        //printf("Sent %d bytes\n", bytes_sent);
        total_bytes_sent += bytes_read;
        printf("\r%.2f%%", total_bytes_sent*(100.0f/file_size));
        fflush(stdout);
    }
    
    printf("\nFile sent!\n");
    close(sock);
    fclose(fp);
    exit(EXIT_SUCCESS);
    
//exit_close_sock:
    close(sock);
    printf("Connection closed\n");
exit_close_file:
    fclose(fp);
exit_error:
    exit(EXIT_FAILURE);
    return 0;
}

ssize_t pspload_send(int sock, void *data, uint32_t size)
{
    ssize_t ret = send(sock, data, size, 0);
    if (ret == -1) {
        perror("Error sending data");
    }
    return ret;
}


void print_usage()
{
    printf("Usage: pspload file <args>\nwill send file to the PSP\n");
}
