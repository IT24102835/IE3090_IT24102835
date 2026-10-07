#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h> // NEW: Required for non-blocking UDP checks

#define PORT 9410

volatile int controller_monitoring = 0;
int udp_listen_sock = -1;
pthread_t udp_tid;

void *controller_udp_thread(void *arg) {
    int port = *(int*)arg;
    free(arg);

    udp_listen_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    int opt = 1;
    setsockopt(udp_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(udp_listen_sock, (const struct sockaddr *)&servaddr, sizeof(servaddr));

    char buffer[1024];
    fd_set readfds;
    struct timeval tv;

    // NEW: Safely loop and check for packets without permanently blocking
    while (controller_monitoring) {
        FD_ZERO(&readfds);
        FD_SET(udp_listen_sock, &readfds);

        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 0.5 second timeout

        int activity = select(udp_listen_sock + 1, &readfds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(udp_listen_sock, &readfds)) {
            int n = recvfrom(udp_listen_sock, buffer, sizeof(buffer)-1, 0, NULL, NULL);
            if (n > 0) {
                buffer[n] = '\0';
                printf("\r[UDP MONITOR] %s\n> ", buffer); 
                fflush(stdout);
            }
        }
    }
    
    if (udp_listen_sock != -1) {
        close(udp_listen_sock);
        udp_listen_sock = -1;
    }
    return NULL;
}

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
        else if (strncmp(input, "MONITOR START ", 14) == 0) {
            int port;
            if (sscanf(input + 14, "%d", &port) == 1) {
                char cmd_buf[1050]; 
                snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", input);
                send(sock, cmd_buf, strlen(cmd_buf), 0);
                
                memset(buffer, 0, sizeof(buffer));
                if (recv(sock, buffer, sizeof(buffer) - 1, 0) > 0) {
                    printf("Agent: %s\n", buffer);
                    if (strncmp(buffer, "OK MONITOR_STARTED", 18) == 0) {
                        controller_monitoring = 1;
                        int *p_port = malloc(sizeof(int));
                        *p_port = port;
                        pthread_create(&udp_tid, NULL, controller_udp_thread, p_port);
                    }
                } else break;
                continue;
            }
        }
        else if (strcmp(input, "MONITOR STOP") == 0) {
            char cmd_buf[1050]; 
            snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", input);
            send(sock, cmd_buf, strlen(cmd_buf), 0);
            
            memset(buffer, 0, sizeof(buffer));
            if (recv(sock, buffer, sizeof(buffer) - 1, 0) > 0) {
                printf("Agent: %s\n", buffer);
                if (strncmp(buffer, "OK MONITOR_STOPPED", 18) == 0) {
                    controller_monitoring = 0;
                    pthread_join(udp_tid, NULL);
                }
            } else break;
            continue;
        }
        else if (strcmp(input, "QUIT") == 0) {
            char cmd_buf[1050];
            snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", input);
            send(sock, cmd_buf, strlen(cmd_buf), 0);
            
            memset(buffer, 0, sizeof(buffer));
            if (recv(sock, buffer, sizeof(buffer) - 1, 0) > 0) {
                printf("Agent: %s\n", buffer);
                if (strncmp(buffer, "OK BYE", 6) == 0) {
                    if (controller_monitoring) {
                        controller_monitoring = 0;
                        pthread_join(udp_tid, NULL);
                    }
                    break; 
                }
            } else break;
            continue;
        }
        else {
            char cmd_buf[1050];
            snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", input);
            send(sock, cmd_buf, strlen(cmd_buf), 0);
        }

        memset(buffer, 0, sizeof(buffer));
        int valread = recv(sock, buffer, sizeof(buffer) - 1, 0);
        
        if (valread > 0) {
            if (strncmp(buffer, "OK FILE_SEND ", 13) == 0) {
                char rx_filename[256];
                int rx_filesize;
                char sid[256];
                
                if (sscanf(buffer, "OK FILE_SEND %255s %d %255s", rx_filename, &rx_filesize, sid) >= 2) {
                    printf("Agent: %s\n", buffer);
                    char save_path[512];
                    snprintf(save_path, sizeof(save_path), "downloaded_%s", rx_filename);
                    printf("Saving to %s...\n", save_path);
                    
                    FILE *dl_fp = fopen(save_path, "wb");
                    if (dl_fp) {
                        int remaining = rx_filesize;
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
