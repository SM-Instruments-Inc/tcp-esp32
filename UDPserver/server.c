#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8888
#define MAX_SIZE  500

pthread_mutex_t mutex;

typedef struct _item_t {
    uint16_t mic;
    uint16_t gas;
} item_t;

typedef struct __circleQueue {
    int rear;
    int front;
    item_t * data;
    item_t * arrayToSend;
}Queue;

Queue q;

void init_queue(Queue * q) {
    q -> front = 0;
    q -> rear = 0;
    q -> data = (item_t *)malloc(sizeof(item_t) * MAX_SIZE);
    q -> arrayToSend = (item_t *)malloc(sizeof(item_t) * (MAX_SIZE-1));
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

void allpopq(Queue * q) {
    //for(int i = 0; i < MAX_SIZE; i++){
    //    printf("%d, ", q->data[i]);
    //}
    //printf("\n");
    for(int i = 0; i < MAX_SIZE-1; i++)
    {
        q->arrayToSend[i] = deleteq(q);
    }
    
}

void *thread_summation(void *data) {
	FILE *gas = NULL;
	FILE *mic = NULL;
	int i = 0;
	gas = fopen("gas_data.txt", "a");
    mic = fopen("mic_data.txt", "a");
	while(1) 
	{
		
		if(!IsEmpty(&q))
		{
			pthread_mutex_lock(&mutex);
			item_t array = deleteq(&q);
			pthread_mutex_unlock(&mutex);
			fprintf(gas, "%d", array.gas);
			fprintf(gas, ",");
			fprintf(mic, "%d", array.mic);
			fprintf(mic, ",");
			
			i++;
			if(i%1000 == 0)
			{
				fprintf(gas, "\n");
				fprintf(mic, "\n");
				i = 0;
				//printf("write\n");
			}
		}
		
	}

			fclose(gas);
			fclose(mic);
			
}

int main(void)
{

    int sock;
    struct sockaddr_in addr, client_addr;
	int count = 0;
	pthread_t thread1;
	init_queue(&q);
	pthread_mutex_init(&mutex, NULL);
    item_t *recv_buffer;
    recv_buffer = (item_t *)malloc(sizeof(item_t) * (MAX_SIZE));

    int recv_len;
    int addr_len;

	while(1){
    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        perror("socket ");
        return 1;
    }

    memset(&addr, 0x00, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        perror("bind");
        return 1;
    }

	int thr_id = pthread_create(&thread1, NULL, thread_summation,(void *)"thread 1");
	if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
	struct timeval tv;
    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));


    while(1){

    addr_len = sizeof(client_addr);
    if((recv_len = recvfrom(sock, recv_buffer, sizeof(item_t) * (MAX_SIZE), 0, (struct sockaddr *)&client_addr, &addr_len)) < 0)
    {
        //perror("recvfrom ");
        break;
    }
    printf("send %d\n", count++);
		pthread_mutex_lock(&mutex);
		printf("%d\n ", recv_buffer[recv_len / sizeof(recv_buffer[0])-1].mic);
		for (int i = 0; i < recv_len / sizeof(recv_buffer[0]); i++) {
			addq(&q, recv_buffer[i]);
		}
		pthread_mutex_unlock(&mutex);
    }

    close(sock);
	}



}