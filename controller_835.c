#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    char input[1024];

    // 1. Create the client socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 2. Convert localhost address (127.0.0.1) to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // 3. Connect to the Agent
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed. Is the Agent running?\n");
        return -1;
    }
    
    printf("Connected to Agent on port %d.\n", PORT);
    printf("Type commands (e.g., AUTH OPS-2835) and press Enter. Type QUIT to exit.\n");

    // 4. Main loop to send commands and receive responses
    while(1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        
        // Remove the newline character that fgets adds
        input[strcspn(input, "\n")] = 0;
        
        if (strlen(input) == 0) continue;

        // Send the text command to the Agent
        send(sock, input, strlen(input), 0);
        
        if (strcmp(input, "QUIT") == 0) {
            break;
        }

        // Wait for and print the Agent's response
        int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("Agent: %s\n", buffer);
        } else {
            printf("Connection closed by agent.\n");
            break;
        }
    }

    close(sock);
    return 0;
}
