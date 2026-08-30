#include<arpa/inet.h>
#include<netdb.h>
#include<netinet/in.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<unistd.h>
#include <sys/epoll.h>
#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10
#define UPSTREAM_PORT 3000
#define MAX_SOCKS 10
int listen_sock_fd, epoll_fd;
struct epoll_event events[MAX_EPOLL_EVENTS];
/*
 * route_table[i][0] = client connection socket
 * route_table[i][1] = upstream socket
 */
int route_table[MAX_SOCKS][2];
int route_table_size = 0;
 int create_loop()
{
    int fd = epoll_create1(0);

    if (fd == -1)
    {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    return fd;
}
void loop_attach(int epoll_fd, int fd, int events)
{
   /* attach fd to epoll */
   struct epoll_event e;
   memset(&e, 0, sizeof(e));
   e.events = events;
   e.data.fd = fd;
   if( epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &e) == -1) 
   {
    perror("epoll_ctl");
    exit(EXIT_FAILURE);
   }
}
// Create the listening server socket
int create_server()
{
   /* create listening socket and return it */
   int listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
   
   // Setting socket opt reuse addr
    int enable = 1;
    setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

    // Creating an object of struct socktaddr_in
    struct sockaddr_in server_addr;

  // Setting up server addr
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

  // Binding listening sock to port
  bind(listen_sock_fd, (struct sockaddr *)& server_addr, sizeof(server_addr));
  
  // Starting to listen 
   listen(listen_sock_fd, MAX_ACCEPT_BACKLOG);
   printf("[INFO] Server listening on port %d\n", PORT);
  
   return listen_sock_fd;
}

void handle_client(int conn_sock_fd) {

  char buff[BUFF_SIZE];
  memset(buff, 0, BUFF_SIZE);
  int read_n = recv(conn_sock_fd, buff, BUFF_SIZE, 0); 

  // client closed connection or error occurred
  if (read_n <= 0) {
    close(conn_sock_fd);
    return;
  }

  /* print client message (helpful for Milestone #2) */
  printf("[CLIENT MESSAGE] %s\n", buff);
  /* find the right upstream socket from the route table */
  int upstream_sock_fd = -1;
  for(int i=0; i<route_table_size; i++)
  {
    if(route_table[i][0] == conn_sock_fd)
     {
      upstream_sock_fd = route_table[i][1];
      break;
     }
  }
  if(upstream_sock_fd == -1)
  {
    printf("[ERROR] Upstream socket FD not found\n");
    return;
  }

  // sending client message to upstream
  int bytes_written = 0;
  int message_len = read_n;
  printf("[DEBUG] Sending %d bytes from client fd=%d to upstream fd=%d\n",
       message_len, conn_sock_fd, upstream_sock_fd);
  while (bytes_written < message_len) {
    int n = send(upstream_sock_fd, buff + bytes_written, message_len - bytes_written, 0);
    bytes_written += n;
  }

}
void handle_upstream(int upstream_sock_fd) {
   char buff[BUFF_SIZE];
   memset(buff, 0, BUFF_SIZE);
   int read_n = recv(upstream_sock_fd, buff, BUFF_SIZE, 0);

  // Upstream closed connection or error occurred
  if (read_n <= 0) {
    close(upstream_sock_fd);
    return;
  }
  printf("[UPSTREAM MESSAGE] %.*s\n", read_n, buff);

  /* find the right client socket from the route table */
  int client_sock_fd = -1;
  for(int i=0; i<route_table_size; i++)
  {
    if(route_table[i][1] == upstream_sock_fd)
    {
       client_sock_fd = route_table[i][0];
       break;
    }
  }
  if(client_sock_fd == -1)
  {
    printf("[ERROR] Client socket FD not found\n");
    return;
  }
   printf("[DEBUG] Upstream fd=%d -> client fd=%d\n",
           upstream_sock_fd,
           client_sock_fd);

  /* send upstream message to client */
  int bytes_written = 0;
  int msg_len =read_n;
  while(bytes_written < msg_len)
  {
     int n = send(client_sock_fd, buff + bytes_written, msg_len - bytes_written, 0);
     bytes_written += n;
  }
}

int connect_upstream()
{
   int upstream_sock_fd;
   struct sockaddr_in upstream_addr;
   memset(&upstream_addr, 0, sizeof(upstream_addr));

   //create TCP socket
   upstream_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (upstream_sock_fd == -1) 
   {
        perror("socket upstream");
        exit(EXIT_FAILURE);
   }
    upstream_addr.sin_family = AF_INET;
    upstream_addr.sin_port = htons(UPSTREAM_PORT);
    upstream_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if( connect(upstream_sock_fd, (struct sockaddr*)&upstream_addr, sizeof(upstream_addr)) == -1)
    {
      perror("connect upstream");
      close(upstream_sock_fd);
      exit(1);
    }
    printf("[INFO] Connected to upstream server on port %d\n", UPSTREAM_PORT);
    return upstream_sock_fd;
}
void accept_connection(int listen_sock_fd)
{
  int conn_sock_fd;
  int upstream_sock_fd;

  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  // accept client
  conn_sock_fd = accept(listen_sock_fd, (struct sockaddr *)& client_addr, &client_addr_len);

  if(conn_sock_fd == -1)
  {
    perror("accept");
    return;
  }
  printf("[INFO] Client conected: fd = %d\n", conn_sock_fd);
  
   // check routetable capacity
  if(route_table_size >= MAX_SOCKS)
  {
    fprintf(stderr, "[ERROR] Maximum number of connections reached\n");
    close(conn_sock_fd);
    return;
  }
   // Add client connection socket to epoll
   loop_attach(epoll_fd, conn_sock_fd, EPOLLIN);

   // create connection to upstream server.
   upstream_sock_fd = connect_upstream();

   // add upstream socket to epoll
   loop_attach(epoll_fd, upstream_sock_fd, EPOLLIN);
  
   //Add client/upstream pair to routing table
   route_table[route_table_size][0] = conn_sock_fd;
   route_table[route_table_size][1] = upstream_sock_fd;
   route_table_size++;
   // remove it later
   printf("[INFO] Route created: client fd=%d <-> upstream fd=%d\n", conn_sock_fd, upstream_sock_fd);
}
int is_client_sock(int fd)
{
  for(int i=0; i<route_table_size; i++)
  {
     if(route_table[i][0] == fd)
     return 1;
  }
  return 0;
}
int is_upstream_sock(int fd)
{
   for(int i=0; i<route_table_size; i++)
   {
    if(route_table[i][1] == fd)
     return 1;
   }
  return 0;
}
void loop_run(int epoll_fd) {
    int event_count;
    /* infinite loop and processing epoll events */ 
    while(1)
    {
       printf("[DEBUG] Epoll wait\n");
       // wait for events
       event_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
      
       for(int i=0; i<event_count; i++)
       {
        int fd = events[i].data.fd;
        if(fd == listen_sock_fd)
        {
          accept_connection(listen_sock_fd);
        }
        else if(is_client_sock(fd))
        {  
          handle_client(fd);
        }
        else if(is_upstream_sock(fd))
        {  
          handle_upstream(fd);
        }
       }
    }
}

int main()
{
   //create listening socket
   listen_sock_fd = create_server();

   //create epoll instance
   epoll_fd = create_loop();

   //Attach listening socket to epoll
   loop_attach(epoll_fd, listen_sock_fd, EPOLLIN);
   
    //Start event loop
    loop_run(epoll_fd);

    close(listen_sock_fd);
    close(epoll_fd);
    return 0;
}