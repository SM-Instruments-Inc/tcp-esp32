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
#include <sqlite3.h>

#define PORT 3000
#define MAX_SIZE  30000
#define BUF_SIZE 1000
#define MAX_CLNT 256

int clnt_cnt = 0;
int clnt_socks[MAX_CLNT];
pthread_mutex_t mutex;

typedef struct _item_t {
    uint16_t mic;
    uint16_t gas;
}item_t;

typedef struct __circleQueue {
    char chipnumber[18];
    int rear;
    int front;
    item_t * data;
}Queue;

Queue q[MAX_SIZE];

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

static int try_receive(const int sock, item_t *data, size_t max_len)
{
    int p_len = 0;
    while(max_len != p_len)
    {
        int len = recv(sock, data + p_len, max_len, 0);
        if (len < 0) {
            if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
            len = 0;   // Not an error
            }
            if (errno == ENOTCONN) {
                printf("[sock=%d]: Connection closed", sock);
                return -2;  // Socket has been disconnected
            }
        }
        p_len += len;
    }
    return p_len;
}

static int mac_receive(const int sock, char *data, size_t max_len)
{
    int p_len = 0;
    while(max_len != p_len)
    {
        int len = recv(sock, data + p_len, max_len, 0);
        if (len < 0) {
            if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
            len = 0;   // Not an error
            }
            if (errno == ENOTCONN) {
                printf("[sock=%d]: Connection closed", sock);
                return -2;  // Socket has been disconnected
            }
        }
        p_len += len;
    }
    return p_len;
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


void push_data(int sock, item_t *data, int len)
{
    if (len > 0) 
    {
        printf("send\n");
        printf("%d\n", len);
        //printf("send start\n");
		for (int i = 0; i < len /sizeof(data[0]); i++) {
            if(data[i].gas < 4096 && data[i].mic < 4096)
            {
                pthread_mutex_lock(&mutex);
                addq(&q[sock], data[i]);
                pthread_mutex_unlock(&mutex); 
            }
		}
        //printf("Q add finsh\n");
                   // Received some data -> echo back
                    static const char *message = "OK client"; 
                    len = socket_send(sock, message, strlen(message));
                    if (len < 0) {
                        // Error occurred on write to this socket -> close it and mark invalid
                        printf("[sock=%d]: socket_send() returned %d -> closing the socket", sock, len);
                        close(sock);
                        sock = -1;
                    } else {
                        // Successfully echoed to this socket
                        printf("[sock=%d]: Written %.*s\n", sock, len, message);
                    }
    }
}

void *thread_summation(void* arg) {
	int i = 0;
    sqlite3 *db;
    char *err_msg = 0;

    int clnt_sock = *((int*)arg);

    int rc = sqlite3_open("test.db", &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
    }

    
	while(1) 
	{
		
		if(!IsEmpty(&q[clnt_sock]))
		{
            sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
            for(int i = 1; i < 1000; i++)
            {
                if(IsEmpty(&q[clnt_sock]))
                {
                    break;
                }
                pthread_mutex_lock(&mutex);
			    item_t array = deleteq(&q[clnt_sock]);
                pthread_mutex_unlock(&mutex);
                char gas[100];
                char mic[100];
                sprintf(gas, "INSERT INTO Gas_Datas (data,sock) VALUES(%d,'%s');",array.gas, q[clnt_sock].chipnumber);
                sprintf(mic, "INSERT INTO Mic_Datas (data,sock) VALUES(%d,'%s');",array.mic, q[clnt_sock].chipnumber);
                rc = sqlite3_exec(db, gas, 0, 0, &err_msg);
                rc = sqlite3_exec(db, mic, 0, 0, &err_msg);
            }
            sqlite3_exec(db, "END TRANSACTION;", NULL, NULL, NULL);
            
		}
		
	}
    sqlite3_close(db);
    
}

void* handle_clnt(void* arg)
{
    pthread_t thread;
    int clnt_sock = *((int*)arg);
    int flags = fcntl(clnt_sock, F_GETFL);
    if (fcntl(clnt_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        printf("Unable to set socket non blocking\n");
    }
    init_queue(&q[clnt_sock]);
    
    item_t *recv_buffer;
    recv_buffer = (item_t *)malloc(sizeof(item_t) * (BUF_SIZE-1));
    int len = 0, i;
    

    len = mac_receive(clnt_sock, q[clnt_sock].chipnumber, sizeof(q[clnt_sock].chipnumber));

    printf("%s\n", q[clnt_sock].chipnumber);

    int thr_id = pthread_create(&thread, NULL, thread_summation,(void*)&clnt_sock);
	if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    pthread_detach(thread);

    while((len=try_receive(clnt_sock,recv_buffer, sizeof(item_t) * (BUF_SIZE-1))) != -2)
        push_data(clnt_sock,recv_buffer, len);
        
    pthread_mutex_lock(&mutex);
    for(i = 0; i<clnt_cnt; i++)
    {
        if(clnt_sock == clnt_socks[i])
        {
            while(i<clnt_cnt-1)
            {
                clnt_socks[i] = clnt_socks[i+1];
                ++i;
            }
            break;
        }
    }
    --clnt_cnt;
    pthread_mutex_unlock(&mutex);
    close(clnt_sock);

    return NULL;
}

// void *thread_summation(void *data) {
// 	FILE *gas = NULL;
// 	FILE *mic = NULL;
//     time_t t = time(NULL);
// 	int i = 0;
// 	while(1) 
// 	{
		
// 		if(!IsEmpty(&q))
// 		{
//             gas = fopen("gas_data.txt", "a");
//             mic = fopen("mic_data.txt", "a");
// 			pthread_mutex_lock(&mutex);
// 			item_t array = deleteq(&q);
//             pthread_mutex_unlock(&mutex);
// 			fprintf(gas, "%d, ", array.gas);
// 			fprintf(mic, "%d, ", array.mic);
            
// 			i++;
// 			if(i == 1000)
// 			{
//                 t = time(NULL);
//                 struct tm tm= *localtime(&t);
// 				fprintf(gas, "\n  %d-%d-%d %d:%d:%d\n", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
//                 fprintf(mic, "\n  %d-%d-%d %d:%d:%d\n", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
// 				i = 0;
// 			}
//             fclose(gas);
// 	        fclose(mic);
// 		}
		
// 	}
    
// }



int main()
{
    sqlite3 *db;
    char *err_msg = 0;
    char *sql = "DROP TABLE IF EXISTS Gas_Datas;" 
                "DROP TABLE IF EXISTS Mic_Datas;"
                "CREATE TABLE Gas_Datas(ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,data INT,sock CHAR(18),TIME DATETIME DEFAULT (STRFTIME('%Y-%m-%d %H:%M:%f', 'NOW', 'localtime')));"
                "CREATE TABLE Mic_Datas(ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,data INT,sock CHAR(18),TIME DATETIME DEFAULT (STRFTIME('%Y-%m-%d %H:%M:%f', 'NOW', 'localtime')));";
    int rc = sqlite3_open("test.db", &db);
    
    if (rc != SQLITE_OK)
            {
                fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
                sqlite3_close(db);
            }
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
            {
                fprintf(stderr, "SQL error: %s\n", err_msg);
        
                sqlite3_free(err_msg);        
                sqlite3_close(db);
            }

    sqlite3_close(db);

	
    struct clnt_adr;
    int clnt_sock;
    socklen_t clnt_adr_sz;
    pthread_t t_id;

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
    
    if((listen_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        printf("socket error\n");

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_addr.sin_port = htons(PORT);

    if(bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == -1)
    {
        printf("bind error\n");
    }   

    if(listen(listen_sock, 1) != 0)
    {
        printf("listen error\n");
    }

    

    // thr_id = pthread_create(&thread2, NULL, thread_summation,(void *)"thread 2");
	// if (thr_id < 0)
    // {
    //     perror("thread create error : ");
    //     exit(0);
    // }

    


    while(1)
    {
        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        
        clnt_sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);

        pthread_mutex_lock(&mutex);
        clnt_socks[clnt_cnt++] = clnt_sock;
        pthread_mutex_unlock(&mutex);

        pthread_create(&t_id, NULL, handle_clnt, (void*)&clnt_sock);

        pthread_detach(t_id);
    }
}

