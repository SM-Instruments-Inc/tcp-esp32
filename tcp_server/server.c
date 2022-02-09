#include<stdio.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<pthread.h>
#include<sys/socket.h>
#include<unistd.h>
#include<string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#define PORT 3000
#define MAX_SIZE  100000
#define BUF_SIZE 1000
pthread_mutex_t mutex;

typedef struct _item_t {
    uint16_t mic;
    uint16_t gas;
} item_t;

typedef struct __circleQueue {
    int rear;
    int front;
    item_t * data;
}Queue;

Queue q;

void init_queue(Queue * q) {
    q -> front = 0;
    q -> rear = 0;
    q -> data = (item_t *)malloc(sizeof(item_t) * MAX_SIZE);
}

int IsEmpty(Queue * q) {
    if (q -> front == q -> rear) //front와 rear가 같으면 큐는 비어있는 상태
        return 1;
    else 
        return 0;
    }
int IsFull(Queue * q) {
    if ((q -> rear + 1) % MAX_SIZE == q -> front) {
        return 1;
    }
    return 0;
}
void addq(Queue * q, item_t value) {
    if (IsFull(q)) {
        printf("Queue is Full.\n");
        return;
    } else {
        q -> rear = (q -> rear + 1) % MAX_SIZE;
        q -> data[q -> rear] = value;
    }
    return;

}

item_t deleteq(Queue * q) {

    q -> front = (q -> front + 1) % MAX_SIZE;
    return q->data[q->front];
}

void *thread_summation(void *data) {
	FILE *gas = NULL;
	FILE *mic = NULL;
    time_t t = time(NULL);
	int i = 0;
	while(1) 
	{
		
		if(!IsEmpty(&q))
		{
            gas = fopen("gas_data.txt", "a");
            mic = fopen("mic_data.txt", "a");
			//pthread_mutex_lock(&mutex);
			item_t array = deleteq(&q);
            //pthread_mutex_unlock(&mutex);
			fprintf(gas, "%d, ", array.gas);
			fprintf(mic, "%d, ", array.mic);
            
			i++;
			if(i == 1000)
			{
                t = time(NULL);
                struct tm tm= *localtime(&t);
				fprintf(gas, "\n  %d-%d-%d %d:%d:%d\n", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                fprintf(mic, "\n  %d-%d-%d %d:%d:%d\n", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
				i = 0;
			}
            fclose(gas);
	        fclose(mic);
		}
		
	}
    
}


static int try_receive(const int sock, item_t *buffer, size_t max_len)
{
    int len = recv(sock, buffer, sizeof(item_t) * (BUF_SIZE-1), 0);
    if (len < 0) {
        printf("%s\n",strerror(errno));
        if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;   // Not an error
        }
        if (errno == ENOTCONN) {
            printf("[sock=%d]: Connection closed", sock);
            return -2;  // Socket has been disconnected
        }
        printf("Error occurred during receiving");
        return -1;
    }

    return len;
}

static int socket_send(const int sock, const char * data, const size_t len)
{
    int to_write = len;
    while (to_write > 0) {
        int written = send(sock, data + (len - to_write), to_write, 0);
        if (written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("Error occurred during sending");
            return -1;
        }
        to_write -= written;
    }
    return len;
}


int main(void) 
{
    pthread_t thread1, thread2;

	init_queue(&q);

    int recv_len;
    static const char *message = "OK client"; 
    int count = 0;

    pthread_mutex_init(&mutex, NULL);

    item_t *recv_buffer;
    recv_buffer = (item_t *)malloc(sizeof(item_t) * (BUF_SIZE-1));
	
    int listen_sock = -1;
    const size_t max_socks = 100;
    static int sock[100];

	struct sockaddr_in listen_addr;


    for (int i=0; i<max_socks; ++i) {
        sock[i] = -1;
    }
    
    if((listen_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        printf("socket error\n");

    int flags = fcntl(listen_sock, F_GETFL);
    if (fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        printf("Unable to set socket non blocking");
        goto error;
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_addr.sin_port = htons(PORT);

    if(bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == -1)
    {
        printf("bind error\n");
        goto error;
    }   

    if(listen(listen_sock, 1) != 0)
    {
        printf("listen error\n");
        goto error;
    }

    int thr_id = pthread_create(&thread1, NULL, thread_summation,(void *)"thread 1");
	if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    
    }

    // Main loop for accepting new connections and serving all connected clients
    while (1) {
        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);

        // Find a free socket
        int new_sock_index = 0;
        for (new_sock_index=0; new_sock_index<max_socks; ++new_sock_index) {
            if (sock[new_sock_index] == -1) {
                break;
            }
        }

        // We accept a new connection only if we have a free socket
        if (new_sock_index < max_socks) {
            // Try to accept a new connections
            sock[new_sock_index] = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);

            if (sock[new_sock_index] < 0) {
                if (errno == EWOULDBLOCK) { // The listener socket did not accepts any connection
                                            // continue to serve open connections and try to accept again upon the next iteration
                    //printf("No pending connections...\n");
                } else {
                    printf("Error when accepting connection");
                    goto error;
                }
            } else {
                // We have a new client connected -> print it's address
                // ...and set the client's socket non-blocking
                printf("connections\n");
                flags = fcntl(sock[new_sock_index], F_GETFL);
                if (fcntl(sock[new_sock_index], F_SETFL, flags | O_NONBLOCK) == -1) {
                    printf("Unable to set socket non blocking");
                    goto error;
                }
            }
        }

        // We serve all the connected clients in this loop
        for (int i=0; i<max_socks; ++i) {
            if (sock[i] != -1) {
                // This is an open socket -> try to serve it
                int len = try_receive(sock[i], recv_buffer, sizeof(item_t) * (BUF_SIZE-1));
                if (len < 0) {
                    // Error occurred within this client's socket -> close and mark invalid
                    printf("[sock=%d]: try_receive() returned %d -> closing the socket", sock[i], len);
                    close(sock[i]);
                    sock[i] = -1;
                } else if (len > 0) {
                    printf("send\n");
                    printf("%d\n", len);
                    printf("send start\n");
		            for (int i = 0; i < len /sizeof(recv_buffer[0]); i++) {
                        if(recv_buffer[i].gas < 4096 && recv_buffer[i].mic < 4096)
                        {
                            //pthread_mutex_lock(&mutex);
                            addq(&q, recv_buffer[i]);
                            //pthread_mutex_unlock(&mutex); 
                        }
		            }
                    printf("Q add finsh\n");
                    //Received some data -> echo back
                    // len = socket_send(sock[i], message, strlen(message));
                    // if (len < 0) {
                    //     // Error occurred on write to this socket -> close it and mark invalid
                    //     printf("[sock=%d]: socket_send() returned %d -> closing the socket", sock[i], len);
                    //     close(sock[i]);
                    //     sock[i] = -1;
                    // } else {
                    //     // Successfully echoed to this socket
                    //     printf("[sock=%d]: Written %.*s\n", sock[i], len, message);
                    //}
                }
                
            } // one client's socket
        } // for all sockets
    }

error:
    if (listen_sock != -1) {
        close(listen_sock);
    }

    for (int i=0; i<max_socks; ++i) {
        if (sock[i] != -1) {
            close(sock[i]);
        }
    }
}
		


