#include "proxyget.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define DEBUG
#define BUFSIZE 1024

int main(int argc, char ** argv) {

  if (argc < 3) {
    printf("usage (without the brackets):\n\t%s <-U> [proxyhost:port] [URL]\n", argv[0]);
    return 1;
  }
  
  int nextarg = 1;
  bool_t is_reliable = 1;
  if (!strncmp("-U", argv[nextarg], 2)) {
    is_reliable = 0;
    nextarg++;
  }

  char * hostport = argv[nextarg++];
  char * host = strtok(hostport, ":");
  char * portstring = strtok(NULL, ":");
  
  int port = (portstring == NULL ? 8080 : atoi(portstring));
#ifdef DEBUG
  printf("Connecting to %s on port %d\n", host, port);
#endif

  int socket = connectSocket(host, port, is_reliable);
  if (socket == -1) return 1;

  char * url = argv[nextarg++];
  
  char msg[strlen(url)+ 4 + 13 + 1];
  msg[0] = '\0';
  strcat(msg, "GET ");
  strcat(msg, url);
  strcat(msg, " HTTP/1.1\r\n\r\n");

#ifdef DEBUG
  printf ("Sending request %s\n", msg);
#endif
  int len = strlen(msg);
  
  if (mywrite(socket, msg, len) != len) {
    printf ("ERROR: socket write error\n");
    return  1;
  }
  
#ifdef DEBUG
  printf ("Receiving data\n");
#endif

  char buf [BUFSIZE];
  
  int read;
  do {
    read = myread(socket, buf, BUFSIZE);
//    printf("%d\n", read);
    fwrite(buf, read, sizeof(char), stdout);
  } while (read > 0);

  myclose(socket);
  
  return 0;
}

int connectSocket(char * hostname, int port, bool_t is_reliable) {
  struct sockaddr_in servAddr;  /* ECHO server address */
  unsigned long serverInaddr;   /* Binary IP address */
  struct hostent *hostentPtr;   /* used with gethostbyname() */
  int s;

    /* now initialize socket parameters */
  bzero( (char*) &servAddr, sizeof(servAddr));
  servAddr.sin_family = AF_INET;                /* Internet Address Family */
  servAddr.sin_port =  htons(port);

  /*
   * First try to convert the host name as a dotted-decimal number.
   * Only if that fails we call gethostbyname().
   */
  if ( (serverInaddr = inet_addr(hostname)) != INADDR_NONE ) {
    /* conversion succeeded */
    servAddr.sin_addr.s_addr = serverInaddr;
  }
  else {
    if ( (hostentPtr = gethostbyname(hostname)) == NULL) {
      printf("ERROR: failed to resolve host name: %s\n",
              hostname);
      return -1;
    }
    bcopy( hostentPtr->h_addr, (char *) &servAddr.sin_addr,
           hostentPtr->h_length);
  }

    
  /* create socket */
  if ( ( s = mysocket(is_reliable) ) < 0) {
    printf("ERROR: failed to open control stream socket\n");
    return -1;
  }
  
  /* connect to server */
  if( myconnect(s, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0 ) {
    printf("ERROR: failed to connect to server\n");
    return -1;
  }
  return s;

}
