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

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) return -1;
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) return -1;

    printf("Connected to Agent on port %d.\n", PORT);
    printf("Type commands (e.g., AUTH OPS-2835) and press Enter. Type QUIT to exit.\n");

    char input[1024];
    while (1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;

        input[strcspn(input, "\n")] = 0; 
        if (strlen(input) == 0) continue;

        // PUT Logic
        if (strncmp(input, "PUT ", 4) == 0) {
            char filename[256];
            if (sscanf(input + 4, "%255s", filename) == 1) {
                FILE *fp = fopen(filename, "rb");
                if (fp == NULL) {
                    printf("Error: Could not open local file '%s'\n", filename);
                    continue;
                }
                fseek(fp, 0L, SEEK_END);
                long filesize = ftell(fp);
                rewind(fp);

                char cmd_buf[512];
                snprintf(cmd_buf, sizeof(cmd_buf), "PUT %s %ld\r\n", filename, filesize);
                send(sock, cmd_buf, strlen(cmd_buf), 0);

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
        } 
        // Standard text commands
        else {
            char cmd_buf[1050];
            snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", input);
            send(sock, cmd_buf, strlen(cmd_buf), 0);
        }

        // Wait for Agent's response
        memset(buffer, 0, sizeof(buffer));
        int valread = recv(sock, buffer, sizeof(buffer) - 1, 0);
        
        if (valread > 0) {
            // GET Logic (Intercept File Downloads)
            if (strncmp(buffer, "OK FILE_SEND ", 13) == 0) {
                char rx_filename[256];
                int rx_filesize;
                char sid[256];
                
                if (sscanf(buffer, "OK FILE_SEND %255s %d %255s", rx_filename, &rx_filesize, sid) >= 2) {
                    printf("Agent: %s\n", buffer);
                    
                    // We save it as "downloaded_filename" so it doesn't overwrite your original local file
                    char save_path[512];
                    snprintf(save_path, sizeof(save_path), "downloaded_%s", rx_filename);
                    printf("Saving to %s...\n", save_path);
                    
                    FILE *dl_fp = fopen(save_path, "wb");
                    if (dl_fp) {
                        int remaining = rx_filesize;
                        
                        // Check for data piggybacked on the network packet
                        char *header_end = strstr(buffer, "\r\n");
                        if (header_end) {
                            header_end += 2;
                            int header_len = header_end - buffer;
                            int piggyback = valread - header_len;
                            if (piggyback > 0) {
                                int to_write = (piggyback < remaining) ? piggyback : remaining;
                                fwrite(header_end, 1, to_write, dl_fp);
                                remaining -= to_write;
                            }
                        }
                        
                        // Read the rest of the file stream
                        char file_buf[4096];
                        while (remaining > 0) {
                            int to_read = (remaining < sizeof(file_buf)) ? remaining : sizeof(file_buf);
                            int recvd = recv(sock, file_buf, to_read, 0);
                            if (recvd <= 0) break;
                            fwrite(file_buf, 1, recvd, dl_fp);
                            remaining -= recvd;
                        }
                        fclose(dl_fp);
                        printf("Download complete!\n");
                    }
                }
            } else {
                printf("Agent: %s\n", buffer);
            }
        } else {
            printf("Agent disconnected.\n");
            break;
        }
    }

    close(sock);
    return 0;
}
