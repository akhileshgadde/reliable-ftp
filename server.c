#include "unpifiplus.h"
#include "unprtt.h"
#include <setjmp.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/time.h>

#define SIZE 512
#define minimum(a,b,c) (((((a)<(b))?(a):(b))) < (c)?(((a)<(b)?(a):(b))):(c))

static struct rtt_info rttinfo;
static int rttinit = 0;
struct itimerval settimer;
struct timeval persistTimer;
static ssize_t m;
static int zerowindowflag = 0;
static int fp, timer_sec, timer_usec;
static int flag = 0;
static int timeoutflag = 0;
static int head = 1;
static struct msghdr msgsend, msgrecv;	/* assumed init to 0 */
struct messagehdr
{
	int seqno;
	int advwindow;
	int retries;
	uint32_t timestamp;
	int fin;
	int ack;
	int ackcount;
}receivehdr, sendhdr[50];

static int seqno = 0;
static int retries = 0, slidingWindow = 0, cwin = 1;
static int ssthresh = 127;
static int ssthreshtemp = 127;
int num_items = 0;
static void sig_alrm(int signo);
static sigjmp_buf jmpbuf;
extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
struct ifi_info *ifi, *ifihead;

char port[10], window[10], fileData[50][SIZE];
void clearmytimer();
void setmytimer();
void readinfile(char filename[]);
void getAndBindAllInterfaces();
void checkLocal(struct sockaddr_in, int position);
int performHandshake(struct sockaddr_in cliaddr, char servaddr[], int listenfd); //returns connfd
void sendFile(char filename[], int connfd);
//ssize_t dg_send_file(int fd, const void *outbuff, size_t outbytes, void *inbuff, size_t inbytes);
ssize_t dg_send_file(int fd, int position, int windowSize, void *inbuff, size_t inbytes);
void addSegment(char dataSegment[]);
void printSegment();
int checkDatabase(struct sockaddr_in cliaddr);

struct interface
{
	int sockfd;
	char ipaddr[40];
	char netmask[40];
	char subnetaddr[40];
};

struct clientDatabase
{
    char clientIpaddr[40];
    int port;
    pid_t pid;
}cd[50];

void addToDatabase(struct clientDatabase *cd, struct sockaddr_in cliaddr, int * num_items, pid_t pid);

static int interfaceCount;
static int segmentCount;
static int count = 0;
static int advertWindow;
int minWindow = 0;
struct interface serverInterfaces[10];


int main(int argc, char **argv)
{
	int i, j, structlen, maxfdp1, listenfd, connfd;
	char recvline[MAXLINE], sendline[MAXLINE], filename[30], *token;
	struct sockaddr_in sa, cliaddr;
	fd_set rset;
	pid_t pid;
	structlen = sizeof(sa);
	
    	readinfile("server.in");
	getAndBindAllInterfaces();
	printf("\nThere are %d interfaces\n", interfaceCount);
	printf("Interface details are as follows : \n");
	//print interface details
	for(i=0; i<interfaceCount; i++)
	{
		printf("\nIp addr : %s\n", serverInterfaces[i].ipaddr);
		printf("Netmask : %s\n", serverInterfaces[i].netmask);
		printf("Subnet addr : %s\n", serverInterfaces[i].subnetaddr);
		Getsockname(serverInterfaces[i].sockfd, (SA *) &sa, &structlen);
		printf("Address Bound using Getsockname: %s\n", Sock_ntop( (SA *) &sa, structlen));
	}
	
	FD_ZERO(&rset);

	for( ; ;)
	{
		for(i=0; i<interfaceCount; i++)
			FD_SET(serverInterfaces[i].sockfd, &rset);
		maxfdp1 = serverInterfaces[interfaceCount-1].sockfd + 1;
		if(select(maxfdp1, &rset, NULL, NULL, NULL) <0 )
		{
			if(errno == EINTR)
				continue;
			else
			{
				fprintf(stdout, "Error is : %s\n", strerror(errno));
				err_quit("Error in select function");
			}
		}
		for(i=0; i<interfaceCount; i++)
		{
			if(FD_ISSET(serverInterfaces[i].sockfd, &rset))
			{
				//printf("Listenfd in parent : %d\n", serverInterfaces[i].sockfd);
				printf("Client connected to the address %s\n", serverInterfaces[i].ipaddr);
				Recvfrom(serverInterfaces[i].sockfd, recvline, MAXLINE+1, 0, (SA *) &cliaddr, &structlen);
				printf("IP address of the client %s\n", Sock_ntop_host((SA *) &cliaddr, sizeof(cliaddr)));
				token = strtok(recvline, ",");
				//printf("%s\n", token); 
				strcpy(filename, token);
				token = strtok(NULL, ",");
			//	printf("%s\n", token);
				advertWindow = atoi(token);
				printf("Advertisement window size of client is %d\n", advertWindow);
				strcpy(filename, recvline);
				printf("Filename received : %s\n", filename);
				if(checkDatabase(cliaddr))
				{
					printf("Child is available for the client\n");
					continue;
				}
				if((pid = fork()) == 0) //child process
				{
					for(j=0; j<interfaceCount; j++)
						if(i == j) listenfd = serverInterfaces[j].sockfd;
						else Close(serverInterfaces[j].sockfd);
					if(strcmp(serverInterfaces[j].ipaddr, "127.0.0.1"))
						checkLocal(cliaddr, i);
					//printf("Listenfd in child : %d\n", listenfd);
					connfd = performHandshake(cliaddr, serverInterfaces[i].ipaddr, listenfd);
					//printf("count:%d\n", count);
					printf("\nFile transfer start...\n");
					sendFile(filename, connfd);
					Close(connfd);
					exit(0);
				}
				else //parent process
				{					
					addToDatabase(cd, cliaddr, &num_items, pid);
				}
			}
		}
	}
}


void readinfile(char filename[])
{
	FILE *fp;
	int len;
	char line[10];
	fp = fopen(filename, "r");
	if(fgets (line, sizeof(line), fp ) != NULL)
	{
		strncpy(port, line, sizeof(port));
		len = strlen(port);
		if(port[len-1] == '\n')
			port[len-1] = '\0';
	}
	if(fgets (line, sizeof(line), fp ) != NULL)
	{
		strncpy(window, line, sizeof(window));
		len = strlen(window);
		if(window[len-1] == '\n')
			window[len-1] = '\0';
	}
	slidingWindow = atoi(window);
}

void getAndBindAllInterfaces()
{
	int i = 0, clientlen;
	struct sockaddr_in *ipaddr, *netmask, sa;
	struct in_addr subnetptr, netmaskptr, ipptr;
	clientlen = sizeof(sa);

	
	bzero(&sa, sizeof(sa));

	for(ifihead = ifi = Get_ifi_info_plus(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next)
	{
		serverInterfaces[interfaceCount].sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
		if ( (ipaddr = (struct sockaddr_in *) ifi->ifi_addr) != NULL)
		{
			strcpy(serverInterfaces[interfaceCount].ipaddr, Sock_ntop_host((SA *) ipaddr, sizeof(*ipaddr))); //Store ip
			inet_pton(AF_INET, serverInterfaces[interfaceCount].ipaddr, &ipptr);
		}
					
		if ( (netmask = (struct sockaddr_in *) ifi->ifi_ntmaddr) != NULL)
		{
			strcpy(serverInterfaces[interfaceCount].netmask, Sock_ntop_host((SA *) netmask, sizeof(*netmask))); 
			inet_pton(AF_INET, serverInterfaces[interfaceCount].netmask, &netmaskptr);
		}

		subnetptr.s_addr = (ipptr.s_addr) & (netmaskptr.s_addr); //Anding the ipaddr with network mask
		inet_ntop(AF_INET, &subnetptr.s_addr, serverInterfaces[interfaceCount].subnetaddr, sizeof(serverInterfaces[interfaceCount].subnetaddr));
		inet_pton(AF_INET, serverInterfaces[interfaceCount].ipaddr, &sa.sin_addr);
		sa.sin_family = AF_INET;
		sa.sin_port = htons(atoi(port));
		Bind(serverInterfaces[interfaceCount].sockfd, ((SA *) &sa), sizeof(sa));
				
		interfaceCount++;
	}
}

int checkDatabase(struct sockaddr_in cliaddr)
{
	char ipaddr[50];
	int port;
	int i;

	strcpy(ipaddr, Sock_ntop_host((SA *) &cliaddr, sizeof(cliaddr)));
	port = cliaddr.sin_port;
	//printf("Port number is %d\n", cliaddr.sin_port);
	for(i=0; i<num_items; i++)
	{
		if(!strcmp(ipaddr, cd[i].clientIpaddr))
			if(port == cd[i].port)
				return 1;
	}
	return 0;
}

void addToDatabase(struct clientDatabase *cd, struct sockaddr_in cliaddr, int * num_items, pid_t pid)
{
	
	struct clientDatabase a;
	int port;
	
	strcpy(a.clientIpaddr, Sock_ntop_host((SA *) &cliaddr, sizeof(cliaddr)));
	//sprintf(port, "%d", htons(cliaddr.sin_port));
	a.port = cliaddr.sin_port;
	//printf("Port number is %d\n", cliaddr.sin_port);
	//printf("Port %d\n", a.port)
	a.pid = pid;

	if ( *num_items < 50 )
	{
		cd[*num_items] = a;
	      	*num_items += 1;
	}
	printf("Client added to database\n");
}

void checkLocal(struct sockaddr_in cliaddr, int position)
{
	struct in_addr subnetptr, netmaskptr;
	char clientSubnet[40];
	int option;

	inet_pton(AF_INET, serverInterfaces[position].netmask, &netmaskptr);
	subnetptr.s_addr = (cliaddr.sin_addr.s_addr) & (netmaskptr.s_addr);
	inet_ntop(AF_INET, &subnetptr.s_addr, clientSubnet, sizeof(clientSubnet));
	if(!(strcmp(clientSubnet, serverInterfaces[position].subnetaddr)))
	{
		printf("Client is local to server\n");
		printf("Client subnet : %s\t Server subnet : %s\n", clientSubnet, serverInterfaces[position].subnetaddr);
		option = 1;
		Setsockopt(serverInterfaces[position].sockfd, SOL_SOCKET, SO_DONTROUTE, &option, sizeof(option));
	}
}

int performHandshake(struct sockaddr_in cliaddr, char servaddr[], int listenfd)
{
	int connfd, getsocklen, maxfdp1, i;
	char newport[10], recvline[MAXLINE];
	struct sockaddr_in sa, getsockaddr;
	struct timeval timeout;
	fd_set rset;
	FD_ZERO(&rset);

	getsocklen = sizeof(getsockaddr);
	bzero(&sa, sizeof(sa));

	timeout.tv_sec = 3;
	timeout.tv_usec = 0;

	connfd = Socket(AF_INET, SOCK_DGRAM, 0);
	//printf("connfd : %d\n", connfd);
	inet_pton(AF_INET, servaddr, &sa.sin_addr);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(0);
	Bind(connfd, ((SA *) &sa), sizeof(sa));
	Getsockname(connfd, (SA *) &getsockaddr, &getsocklen);
	printf("Connection socket bound to : %s\n", Sock_ntop( (SA *) &getsockaddr, sizeof(getsockaddr)));
	//Connect(connfd, (SA *) &cliaddr, sizeof(cliaddr));
	printf("Connected to client : %s\n", Sock_ntop( (SA *) &cliaddr, sizeof(cliaddr)));
	sprintf(newport, "%d", htons(getsockaddr.sin_port));
	//printf("New server port is %s\n", newport);
	Sendto(listenfd, newport, strlen(newport), 0, ((SA *) &cliaddr), sizeof(cliaddr));
	
	
	for( ; ; )
	{
		FD_SET(connfd, &rset);
		maxfdp1 = connfd + 1;
		if(select(maxfdp1, &rset, NULL, NULL, &timeout) <0 )
		{
			if(errno == EINTR)
				continue;
			else
			{
				fprintf(stdout, "Error is : %s\n", strerror(errno));
				err_quit("Error in select function");
			}
		}
		if(FD_ISSET(connfd, &rset))
		{
			Recvfrom(connfd, recvline, MAXLINE+1, 0, NULL, NULL);
			printf("Message from client %s\n", recvline);
			Close(listenfd);
			printf("Handshake complete\n");
			break;
		}
		Sendto(listenfd, newport, strlen(newport), 0, ((SA *) &cliaddr), sizeof(cliaddr));
		Sendto(connfd, newport, strlen(newport), 0, ((SA *) &cliaddr), sizeof(cliaddr));
	}
	//printf("Connected to client : %s\n", Sock_ntop( (SA *) &cliaddr, sizeof(cliaddr)));
	Connect(connfd, (SA *) &cliaddr, sizeof(cliaddr));
	return connfd;
//	Send(connfd, newport, strlen(newport), 0);
}

void sendFile(char filename[], int connfd)
{
	int fp, len, windowSize = 5, position = 0;
	ssize_t n;
	char inbuff[SIZE], line[SIZE];
	fp = open(filename, O_RDONLY);
	//printf("In send file : connfd %d\tfilename %s\n", connfd, filename);
	
	while((n = read (fp, line, SIZE-1)) > 0)
	{
		len = strlen(line);
		line[n] = '\0';
		addSegment(line);
		//printf("Line in file : %s\n", line);
		//printf("strlen of line is : %d\n", strlen(line));
		
	}
timeout_send:
	//printf("cwin before comaprision: %d\n", cwin);
	//printf("ssthresh before comaprision: %d\n", ssthresh);
	if(cwin >= ssthreshtemp)
	{
		cwin = ssthreshtemp;
		//cwinflag = 1;
		ssthreshtemp++;
		printf("ssthreshtemp: %d\n", ssthresh);
	}	
	printf("\ncwin is : %d\n", cwin);
	printf("ssthresh is : %d\n", ssthresh);
	minWindow = minimum(slidingWindow, advertWindow, cwin);
	if(timeoutflag == 0)	
		slidingWindow = slidingWindow - minWindow;

	//printf("minWindow is %d\n", minWindow);
	//printf("zerowindow flag: %d\n", zerowindowflag);
	if(zerowindowflag)
		n = dg_send_file(connfd, head-1, 1, inbuff, SIZE);	
	else
		n = dg_send_file(connfd, head-1, minWindow, inbuff, SIZE);
	if(n == -1) //max retries - exit program
	{
		printf("Maximum retries are done for the segment. Exiting the program\n");
		exit(0);
	}
	else if(n == -2) //timeout
	{
		timeoutflag = 1;
		ssthresh = cwin/2;
		cwin = 1;
		if (ssthresh <= 3)
			ssthresh = 2;
		ssthreshtemp = ssthresh;
		printf("Timeout happened... \n");
		//printf("cwin in timeout: %d\n", cwin);
		//printf("ssthresh in timeout: %d\n", ssthresh);
		//printf("in -2, timeout loop\n");
		goto timeout_send;
	}
	else if(n == -3)
	{
		clearmytimer();
		zerowindowflag = 1;
		if((sendhdr[head-1].retries) < 12)
		{
			persistTimer.tv_sec = 5;
			persistTimer.tv_usec = 0;
			Select(NULL, NULL, NULL, NULL, &persistTimer);
			printf("Advertisement window is zero.. Sending probe to the client..\n"); 
			//rtt_timeout1(&rttinfo);	
			//dg_send_file(connfd, head-1, minWindow, inbuff, SIZE);	
			//setmytimer();		
			//alarm(rtt_start1(&rttinfo));
			sendhdr[head-1].retries++;
			goto timeout_send;
		}
		else
		{
			printf("Maximum retries are done. Exiting the program\n");
			exit(0);
		}
	}
	else if (n ==-4) //fast retransmit - add code
	{
		clearmytimer();		
		printf("Duplicate acknowledgments received.. So, enabling fast retransmission\n");
		cwin = cwin/2;
		if (cwin <= 2)
			cwin = 1;
		ssthresh = cwin;
		if (ssthresh <= 3)
			ssthresh = 2;
		ssthreshtemp = ssthresh;
		//printf("cwin in fast retx: %d\n", cwin);
		//printf("ssthresh in fast retx: %d\n", ssthresh);		
		goto timeout_send;
	}
	else if(n > 0)
	{
		//printf("In >0 loop\n");		
		goto timeout_send;
	}		
	else if(n == 0)
	{
		//printf("Final head: %d\n", head);		
		printf("File successfully sent to the client. Exiting the connection..!! :-) \n");
		printf("Entering the parent select for accepting new connections..\n");
		exit(0);
	}
	else if (n < 0)
		err_quit("send file error");
	
	//printf("n value is %d\n", n);
	//inbuff[n] = 0;
	//printf("String is : %s\n", inbuff);
	
	//printSegment();
}

ssize_t dg_send_file(int fd, int position, int windowSize, void *inbuff, size_t inbytes)
{
	ssize_t	n= 0;
	int localWindow = windowSize;
	struct iovec iovsend[2], iovrecv[2];
	int fileflags;

	if (rttinit == 0) {
		rtt_init1(&rttinfo);
		rttinit = 1;
		rtt_d_flag = 1;
	}

	//printf("Strlen is dg_send is %d\n", outbytes);
	//sendhdr.seq++;
	//sendhdr.seq = htonl(sendhdr.seq);
	while(windowSize)
	{
		msgsend.msg_name = NULL;
		msgsend.msg_namelen = NULL;
		msgsend.msg_iov = iovsend;
		msgsend.msg_iovlen = 2;
		iovsend[0].iov_base = (char*) &(sendhdr[position]);
		iovsend[0].iov_len = sizeof(struct messagehdr);
		iovsend[1].iov_base = fileData[position];
		iovsend[1].iov_len = 512;

		//printf("In dg send func %s\n", fileData[position]);
		//printf("In dg send func %d\n", sendhdr[position].seqno);
	
		msgrecv.msg_name = NULL;
		msgrecv.msg_namelen = 0;
		msgrecv.msg_iov = iovrecv;
		msgrecv.msg_iovlen = 2;
		iovrecv[0].iov_base = (char*) &receivehdr;
		iovrecv[0].iov_len = sizeof(struct messagehdr);
		iovrecv[1].iov_base = inbuff;
		iovrecv[1].iov_len = inbytes;

		Signal(SIGALRM, sig_alrm);
		//rtt_newpack1(&rttinfo);		

		sendhdr[position].timestamp = rtt_ts1(&rttinfo);
		Sendmsg(fd, &msgsend, 0);
		/*if (sendhdr[position].fin == 1)
		{
			goto recv;
		}*/
		//if (zerowindowflag == 0)
		//	slidingWindow--;
		/*else
		{
			windowSize = 0;
			printf("dg_send_recv called due to advwindow = 0\n");
		}*/
		//printf("Sliding window: %d after sending message: %d\n", slidingWindow,sendhdr[position].seqno);
		//setmytimer();			
		//alarm(rtt_start1(&rttinfo));	

		if (sigsetjmp(jmpbuf, 1) != 0) 
		{								
			if(sendhdr[head-1].retries < 12)
			{
				sendhdr[head-1].retries++;
				clearmytimer();
				if (rtt_timeout1(&rttinfo) == 0) 
					printf("RTO in timeout: %d\n", rtt_start1(&rttinfo));				
				setmytimer();				
				return -2;
			}	
			else
			{
				err_msg("send file error: no response from client, giving up");
				rttinit = 0;	
				errno = ETIMEDOUT;
				return(-1);
			}
			
		}
		windowSize--;
		position++;
	}
	printf("Sliding window size : %d\n", slidingWindow);
		//localWindow += head; //change
		while(localWindow)
		{
			//printf("In dowhile loop\n");
			setmytimer(); //setting setitimer
			n = recvmsg(fd, &msgrecv, 0);
			if((n>0) && (receivehdr.ack == head + 1))
			{
				//alarm(rtt_start1(&rttinfo));
				clearmytimer();
				timeoutflag = 0;
				//if (cwinflag == 0)
				cwin = cwin +1;
				slidingWindow++;
				//printf("Sliding window in recvmsg %d\n", slidingWindow);
				printf("Received ack number : %d\n", receivehdr.ack);
				head = receivehdr.ack;
				//printf("head in recvmsg: %d\n", head);
				advertWindow = receivehdr.advwindow;
				zerowindowflag = 0;
				printf("Received advertisement window : %d\n", advertWindow);			
				if(receivehdr.fin)
					return 0;
				if(advertWindow == 0)
				{
					//alarm(0);
					return -3;
				}
				if(sendhdr[head-1].retries == 0)
				{
					rtt_stop1(&rttinfo, rtt_ts1(&rttinfo) - sendhdr[head-1].timestamp);
					//printf("rtt_stop calculated for %d\n", sendhdr[head-1].seqno);
				}
				
			}
			else if((n>0) && (receivehdr.ack != head + 1))
			{
				if(advertWindow == 0)
					return -3;	
				sendhdr[receivehdr.ack-1].ackcount++;
				if ((sendhdr[receivehdr.ack-1].ackcount == 4))
					return -4;
			}
			else if (n <0 && (sendhdr[head-1].fin == 1))
			{
				printf("\nLast segment sent to the client.\nClosing the connection to client");
				printf("Entering the parent select for accepting new connections..\n");
				exit(0);
			}
			else
			{	
				printf("Recvmsgerror: Client abruptly terminated in the middle of file transmission.\n Closing the connection to client.\n");		
				printf("Entering the parent select for accepting new connections..\n");
				exit(0); 	
			}
			localWindow--;
		}
		//alarm(0);
	return(n - sizeof(struct messagehdr));	
}

static void
sig_alrm(int signo)
{	
	siglongjmp(jmpbuf, 1);
}

void addSegment(char dataSegment[])
{

	sendhdr[segmentCount].seqno = segmentCount + 1;
	sendhdr[segmentCount].retries = 0;
	sendhdr[segmentCount].timestamp = 0;
	sendhdr[segmentCount].fin = 1;
	if(segmentCount != 0)
		sendhdr[segmentCount-1].fin = 0;
	strcpy(fileData[segmentCount], dataSegment);
	//printf("Data segment is \n%s\n", dataSegment);
	//printf("Array segment is \n%s\n", fileData[segmentCount]);
	segmentCount++;
}

void printSegment()
{
	int i;
	for(i=0; i<segmentCount; i++)
	{
		printf("\nSegment data : %d\n", i+1);
		printf("Retries : %d\n", sendhdr[i].retries);
		printf("Timestamp : %d\n", sendhdr[i].timestamp);
		printf("Fin flag : %d\n", sendhdr[i].fin);
		printf("i is %d\n", i);
		printf("Data in segment : %s\n", fileData[i]);
	}
}

void setmytimer()
{
		timer_sec = (rtt_start1(&rttinfo))/1000;
		timer_usec = (rtt_start1(&rttinfo)) % 1000;		
		settimer.it_interval.tv_sec = 0;      //next value
		settimer.it_interval.tv_usec = 0;
		settimer.it_value.tv_sec = timer_sec;        // current time
		settimer.it_value.tv_usec = timer_usec;
		if (setitimer(ITIMER_REAL, &settimer, NULL) < 0)
		{
			printf("setitimer error:%s\n", strerror(errno));
			exit(1);
		}
}

void clearmytimer()
{
		timer_sec = 0;
		timer_usec = 0;		
		settimer.it_interval.tv_sec = 0;      //next value
		settimer.it_interval.tv_usec = 0;
		settimer.it_value.tv_sec = timer_sec;        // current time
		settimer.it_value.tv_usec = timer_usec;
		if (setitimer(ITIMER_REAL, &settimer, NULL) < 0)
		{
			printf("setitimer error:%s\n", strerror(errno));
			exit(1);
		}
}

#define	RTT_RTOCALC(ptr) ((((ptr)->rtt_srtt) >> 3) + ((ptr)->rtt_rttvar))
//RTO = (srtt >> 3) + rttvar; 
static int
rtt_minmax1(int rto)
{
	if (rto < RTT_RXTMIN)
		rto = RTT_RXTMIN;
	else if (rto > RTT_RXTMAX)
		rto = RTT_RXTMAX;
	return(rto);
}

void
rtt_init1(struct rtt_info *ptr)
{
	struct timeval	tv;

	Gettimeofday(&tv, NULL);
	ptr->rtt_base = tv.tv_sec;		/* # sec since 1/1/1970 at start */
	ptr->rtt_rtt    = 0;
	ptr->rtt_srtt   = 0;
	ptr->rtt_rttvar = 3000;
	ptr->rtt_rto = rtt_minmax1(RTT_RTOCALC(ptr));
		/* first RTO at (srtt + (4 * rttvar)) = 3 seconds */
}
/* end rtt1 */

/*
 * Return the current timestamp.
 * Our timestamps are 32-bit integers that count milliseconds since
 * rtt_init() was called.
 */

/* include rtt_ts */
uint32_t
rtt_ts1(struct rtt_info *ptr)
{
	uint32_t		ts;
	struct timeval	tv;

	//printf("In rtt_ts\n");

	Gettimeofday(&tv, NULL);
	ts = ((tv.tv_sec - ptr->rtt_base) * 1000) + (tv.tv_usec / 1000);
	return(ts);
}

int
rtt_start1(struct rtt_info *ptr)
{
	//printf("rtt_start\n");
	return (ptr->rtt_rto);
		/* 4return value can be used as: alarm(rtt_start(&foo)) */
}
/* end rtt_ts */

/*
 * A response was received.
 * Stop the timer and update the appropriate values in the structure
 * based on this packet's RTT.  We calculate the RTT, then update the
 * estimators of the RTT and its mean deviation.
 * This function should be called right after turning off the
 * timer with alarm(0), or right after a timeout occurs.
 */

/* include rtt_stop */
void
rtt_stop1(struct rtt_info *ptr, uint32_t ms)
{
	uint32_t delta;

	ptr->rtt_rtt = ms;		/* measured RTT in seconds */

	/*
	 * Update our estimators of RTT and mean deviation of RTT.
	 * See Jacobson's SIGCOMM '88 paper, Appendix A, for the details.
	 * We use inting point here for simplicity.
	 */
	delta -= (ptr->rtt_srtt >> 3); 
 	ptr->rtt_srtt += delta; 
 	if ( delta < 0 ) 
 		delta = -delta; 
 	delta -= (ptr->rtt_rttvar >> 2); 
 	ptr->rtt_rttvar += delta; 
	ptr->rtt_rto = rtt_minmax1(RTT_RTOCALC(ptr));
	printf("RTO in rtt_stop : %d\n", ptr->rtt_rto);
}
/* end rtt_stop */

/*
 * A timeout has occurred.
 * Return -1 if it's time to give up, else return 0.
 */

/* include rtt_timeout */
int
rtt_timeout1(struct rtt_info *ptr)
{
	ptr->rtt_rto *= 2;		/* next RTO */
	ptr->rtt_rto = rtt_minmax1(ptr->rtt_rto);
	printf("RTO in rtt_timeout : %d\n", ptr->rtt_rto);
	return(0);
}
/* end rtt_timeout */

