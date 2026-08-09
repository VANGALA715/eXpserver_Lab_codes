#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#define PORT 8080
#define BUFF_SIZE 10000

typedef struct {
    char message[BUFF_SIZE];
    struct sockaddr_in client_addr;
    int sockfd;
    socklen_t addr_len;
} client_data_t;

void reverse(char *str)
{
    int n = strlen(str)-1;
    for(int i=0; i<n/2; i++)
    {
        char temp = str[i];
        str[i] = str[n-i-1];
        str[n-i-1] = temp;
    }
}

void *handle_client(void *arg)
{
    client_data_t* data = (client_data_t*)arg;
    printf("[CLIENT MESSAGE] %s",data->message);

    // Reverse String
    reverse(data->message);

    // Send back the reversed string
    sendto(data->sockfd, data->message, strlen(data->message), 0, (struct sockaddr *)& (data->client_addr), data->addr_len);

    free(data); // Free the allocated memory
    pthread_exit(NULL);
}
int main()
{
    int sockfd;
    char buffer[BUFF_SIZE];
    struct sockaddr_in server_addr, client_addr;
    pthread_t thread_id;

    //Create socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    // Set server address parameters
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // Bind the socket to the server address
    bind(sockfd, (struct sockaddr *)& server_addr, sizeof(server_addr));
    printf("[INFO] server listening on port %d\n", PORT);

    while(1)
    {
        socklen_t len = sizeof(client_addr);
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)& client_addr, &len);
        buffer[n] = '\0';

        //Allocate memory for client data to pass to the thread
         client_data_t* data = (client_data_t *)malloc(sizeof(client_data_t));
         strcpy(data->message, buffer);
         data->client_addr = client_addr;
         data->sockfd = sockfd;
         data->addr_len = len;

        // Create a new thread to hande the client 
         if(pthread_create(&thread_id, NULL, handle_client, (void*)data) != 0)
         {
            perror("Failed to create thread");
            free(data);
         }

         // Detach the thread to allow independent execution
         pthread_detach(thread_id);
    }
    close(sockfd);
    return 0;
}