#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9410

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[2048] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    printf("Connected to Agent on port %d.\n", PORT);
    printf("Type commands (e.g., AUTH OPS-2835) and press Enter. Type QUIT to exit.\n");

    char input[1024];
    while (1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;

        input[strcspn(input, "\n")] = 0; // Remove trailing newline
        if (strlen(input) == 0) continue;

        // Intercept PUT command to handle file uploading
        if (strncmp(input, "PUT ", 4) == 0) {
            char filename[256];
            
            // Extract the filename (ignoring any size the user might have manually typed)
            if (sscanf(input + 4, "%255s", filename) == 1) {
                FILE *fp = fopen(filename, "rb");
                if (fp == NULL) {
                    printf("Error: Could not open local file '%s'\n", filename);
                    continue;
                }

                // Automatically calculate the exact file size
                fseek(fp, 0L, SEEK_END);
                long filesize = ftell(fp);
                rewind(fp);

                // Send the properly formatted command: PUT <filename> <size>
                char cmd_buf[512];
                snprintf(cmd_buf, sizeof(cmd_buf), "PUT %s %ld\r\n", filename, filesize);
                send(sock, cmd_buf, strlen(cmd_buf), 0);

                // Read the file from the hard drive and stream it to the Agent
                char file_buf[4096];
                size_t bytes_read;
                while ((bytes_read = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
                    send(sock, file_buf, bytes_read, 0);
                }
                fclose(fp);
            } else {
                printf("Usage: PUT <filename>\n");
                continue;
            }
        } else {
            // Send standard text commands exactly as typed
            char cmd_buf[1050];
            snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", input);
            send(sock, cmd_buf, strlen(cmd_buf), 0);
        }

        // Read the Agent's response
        memset(buffer, 0, sizeof(buffer));
        int valread = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (valread > 0) {
            printf("Agent: %s\n", buffer);
        } else {
            printf("Agent disconnected.\n");
            break;
        }
    }

    close(sock);
    return 0;
}
