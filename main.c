#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <time.h>

void *handle(void *p);

struct thread_arg{
    int file_descriptor;
    int port;
    char *ipstring;
};

struct sockaddr_in addr;
int sfd;

int main(void){
    //Prep the socket like we learned in lab
    sfd = socket(PF_INET, SOCK_STREAM, 0);
    unsigned short port = 0;
    char *ipstring = NULL;
    if(sfd == -1){
	printf("Something is borked\n");
	return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(50952); //50950 - 50959
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1){
	printf("Borked\n");
	return 1;
    }

    if(listen(sfd, 10) == -1){
	printf("Too many connectiosn\n");
	return 1;
    }

    int connfd;
    struct thread_arg *arg;
    struct sockaddr_in addrs;
    int len = sizeof(addr);
    while(1){
	connfd = accept(sfd, (struct sockaddr *)&addr, &len); //accept new connection
	port = ntohs(addr.sin_port); //notohs converts from netwrok endian order to host endian order
	ipstring = inet_ntoa(addr.sin_addr); // inet_ntoa reutrns dotted strign notation of the 32 bit IP address
	if(connfd != -1){ // If a connection has been made load a struct and create a thread
	    arg = malloc(sizeof(struct thread_arg)); //Create new struct on the heap and load it
	    arg->file_descriptor = connfd;
	    arg->port = port;
	    arg->ipstring = ipstring;
	    pthread_t t;
	    if(pthread_create(&t, NULL, handle, arg)){ //Create thread
		printf("Error creating thread\n");
		return 1;
	    }
	}
    }
    //This won't ever run in our case
    close(connfd);
    close(sfd);
    return 0;
}

pthread_mutex_t mutex;

void *handle(void *d){
   char buffer[1024]; //Buffer for input
   memset(buffer, '\0', sizeof(buffer)); //Clear buffer
   char o_buffer[1024]; //Second buffer to handle telnet connections
   memset(o_buffer, '\0', sizeof(o_buffer)); //Clear o_buffer
   struct thread_arg *p = d; //saves me the trouble of castign d
   int con = 0;

   while(1){
	recv(p->file_descriptor, buffer, 1024, 0);
	if(strstr(buffer, "\r\n\r\n") != NULL)//If none telnet connection break
	    break;
	if(!strcmp(buffer, "\r\n"))
	    con += 1;
	else
	    con = 0;
	if(con >= 1)
	    break;	    
	strcat(o_buffer, buffer);
	memset(buffer, '\0', sizeof(buffer));
   }

   if(con >= 1){ //If telnet connection swap buffers
       memset(buffer, '\0', sizeof(buffer));
       strcpy(buffer, o_buffer);
   }

   char *str = strtok(buffer, " /\n");
   char *host = NULL;
   char *file = NULL;
   char *src = NULL;
   while(str != NULL){
	if(!(strcmp(str, "Host:"))){ //Sometimes host has issues where it'll end up as Host:Host:...[actual host], can't figure out why.
	    str = strtok(NULL, " /\n");
	    host = str;
	    break; //Just break since after this we don't care about the rest of the information
	}
	if(!(strcmp(str, "GET"))){ //Look for GET
	    str = strtok(NULL, " /\n"); //Get the file
	    file = str; //save the file
	    //The following block of code is adapted from stackoverflow for opening and reading afile of unknown length
	    FILE *fp = fopen(str, "r"); //open the file
 	    if(fp){
		if(fseek(fp, 0L, SEEK_END) == 0){
		    long bufsize = ftell(fp);
		    if(bufsize == -1){
			printf("Bad buffer size\n");
			return NULL;
		    }
		    src = malloc(sizeof(char) * (bufsize + 1));
		    if(fseek(fp, 0L, SEEK_SET) != 0){
			printf("Return to file start\n");
			return NULL;
		    }
		    size_t newLen = fread(src, sizeof(char), bufsize, fp);
		    if(ferror(fp) != 0){
			fputs("Error reading file", stderr);
		    }
		    else{
			src[newLen++] = '\0'; //Adds null terminator
		    }
		}
		fclose(fp);
	    }
	    //
	    else{ //If file doesn't exist deal with it
		char *tmp = "HTTP/1.1 404 Not Found"; 
		send(p->file_descriptor, tmp, strlen(tmp), 0);
		free(src);
		close(p->file_descriptor);
		pthread_detach(pthread_self());
		free(p);
		return NULL;
	    }
	}
        str = strtok(NULL, " /\n");
    }
    //stuff for time
    time_t curtime;
    struct tm *loctime;
    curtime = time(NULL);
    loctime = localtime(&curtime);

    //print stats.txt
    pthread_mutex_lock(&mutex); //I don't believe writing to a file is thread safe so mutex it
    FILE *stat = fopen("stats.txt", "ab+");
    fprintf(stat, "GET /%s HTTP/1.0\nHost: %s\nClient: %s:%d\n\n", file, host, p->ipstring, p->port);
    pthread_mutex_unlock(&mutex);

    //Construct HTML header
    char header[1024];
    strcpy(header, "HTTP/1.1 200 OK\r\nDATE: ");
    char *foo = asctime(loctime);
    foo[strlen(foo) - 1] = 0;
    strcat(header, foo);
    strcat(header, "\r\nContent-Length: ");
    char n[15];
    int u = strlen(src);
    sprintf(n, "%d\r\n", strlen(src));
    strcat(header, n);
    strcat(header, "Content-Type: text/html\r\nConnection: close\r\n\r\n");

    //Send HTML header
    send(p->file_descriptor, header, strlen(header), 0);

    //Send HTML file contents
    send(p->file_descriptor, src, strlen(src), 0);

    //clean up
    free(src);
    close(p->file_descriptor);
    free(p);
    fclose(stat);
    pthread_detach(pthread_self());
    return NULL;
}
