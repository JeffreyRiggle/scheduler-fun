#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const char *get_response = 
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/html; charset=utf-8\r\n"
  "Content-Length: 191\r\n"
  "Connection: close\r\n"
  "\r\n"
  "<html><body><form action=\"file\" method=\"post\"><label for=\"fscript\">Create a script</label><input type=\"text\" name=\"fscript\" required /><input type=\"submit\" value=\"send\"/></form></body></html>";

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        exit(EXIT_FAILURE);
    }
}

// TODO make this easier to read
int main() {
  int server_fd, kqueue_fd;
  struct sockaddr_in server_addr;
  struct kevent change_event, event_list[32];

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    fprintf(stderr, "Failed to socket for TCP server\n");
    return -1;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(3000);

  int bind_res = bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (bind_res < 0) {
    fprintf(stderr, "Failed to bind server to socket %d. Closing server socket!\n", bind_res);
    close(server_fd);
    return -1;
  }


  int listen_res = listen(server_fd, SOMAXCONN);
  if (listen_res < 0) {
    fprintf(stderr, "Failed to listen on TCP server %d. Closing server socket!\n", listen_res);
    close(server_fd);
    return -1;
  }

  set_nonblocking(server_fd);
  printf("Server is listenting on port 3000...\n");

  kqueue_fd = kqueue();
  if (kqueue_fd == -1) {
    fprintf(stderr, "failed to create queue. Closing server socket!\n");
    close(server_fd);
    return -1;
  }

  EV_SET(&change_event, server_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
  if (kevent(kqueue_fd, &change_event, 1, NULL, 0, NULL) == -1) {
    fprintf(stderr, "Failed to register socket closing server socket!\n");
    close(server_fd);
    return -1;
  }

  while (1) {
    // DO the work
    int event_count = kevent(kqueue_fd, NULL, 0, event_list, 32, NULL);
    if (event_count == -1) {
      fprintf(stderr, "Failed to wait on kqueue\n");
      break;
    }

    for (int i = 0; i < event_count; i++) {
      int target_fd = event_list[i].ident;

      if (event_list[i].flags & EV_EOF) {
        printf("Client has disconnected closing fd %d\n", target_fd);
        close(target_fd);
        continue;
      }

      if (target_fd == server_fd) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd == -1) {
          fprintf(stderr, "Failed to accept connection!\n");
          continue;
        }

        set_nonblocking(client_fd);

        EV_SET(&change_event, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
        if (kevent(kqueue_fd, &change_event, 1, NULL, 0, NULL) == -1) {
          fprintf(stderr, "Failed to register client to a new socket\n");
          close(client_fd);
        }
      }
      else if (event_list[i].filter == EVFILT_READ) {
        char buffer[1024];
        memset(buffer, 0, 1024);
        ssize_t bytes_read = read(target_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
          // TODO actually handle all the same crap as before
          printf("Got data %s\n", buffer);
          write(target_fd, get_response, bytes_read);
        } else if (bytes_read == 0) {
          printf("Unable to read data killing connection or something\n");
          close(target_fd);
        } else {
          printf("Failed to read data\n");
        }
      }
    }
  }

  close(server_fd);
  close(kqueue_fd);
  return 0;
}
