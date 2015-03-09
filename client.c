#include "unpifiplus.h"
#include "unprtt.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <setjmp.h>
# include <stdio.h>
# include "unpthread.h"
# include "unp.h"
# include <pthread.h>
# include <math.h>
# include <stdlib.h>
#include<time.h>

#define SIZE 512

void* producer (void *arg);
void* consumer (void *arg);
pthread_mutex_t lock;

int stricmp (const char *p1, const char *p2);
int compareip (const char *q1, const char *q2);
//ssize_t recvfile(int fd);
extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
extern void free_ifi_info_plus(struct ifi_info *ifihead);
int drop();
static pthread_t  producer_tid, consumer_tid; //thread ids
static struct rtt_info   rttinfo;
static int	rttinit = 0;
//struct itimerval settimer;
static struct msghdr msgsend, msgrecv;	/* assumed init to 0 */

static struct messagehdr
{
	int seqno;
	int advwindow;
	int retries;
	uint32_t timestamp;
	int fin;
	int ack;
	int ackcount;
}sendheader, recvhdr;


static struct buffer
{
	int b_seqno;
	char b_advertWindow[512];
	int fin_flag;
}buff[100];


static void sig_alrm(int signo);
static sigjmp_buf jmpbuf;
static int fin_flag = 0;

struct interface 
{
	//int sockfd;
	char *ipaddr;
	char *netmask;
	char subnetaddr[40];
};

struct my_interface
{
	//int sockfd;
	char ipaddr[40];
	char netmask[40];
	char subnetaddr[40];
};

static int advertWindowSize;
char advertWindow[50][SIZE];
int head = 0, acknowledge = 1;
char input[10][100];

int main(int argc, char **argv)
{
	FILE *fp;
	char line[100], charip[100], serverip[MAXLINE], prefixipaddr[50], netmaskprefix[50];
	char subnetprev[50], subnetcurr[50], inputsubnet[50], prefixor[50], sendline[MAXLINE], recvline[MAXLINE+1];
	size_t len = 0;
	socklen_t clientlen;
	ssize_t read;
	struct interface intdetails[10];
	struct my_interface myint[10];	
	const int on = 1;
	pid_t pid;
	fd_set rset;
	int i=0, j=0, n, subnetflag = 1, longprefixflag = 0, sockfd, option, optlen, maxfdp1, localflag = 0;
	int consumersleep = 0, bindflag = 0;
	char str[INET_ADDRSTRLEN], serverinput[30], outbuff[SIZE], inbuff[SIZE];
	struct ifi_info *ifi, *ifihead;
	struct sockaddr_in *ipaddr, *netmask, cliaddr, servaddr, wilddr, finalipclient, finalipserver;
	struct in_addr ipptr, netmaskptr, subnetptr, netmaskprefixptr, prefix;
	sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
	//printf("sockfd after Socket: %d\n", sockfd);
	fp = fopen("client.in", "r");
	struct timeval timeout;
	bzero(&finalipclient, sizeof(finalipclient));
	bzero(&finalipserver, sizeof(finalipserver));
	while(fgets (line, sizeof(line), fp ) != NULL)
	{
		strncpy(input[i], line, sizeof(input[i])); //input[0] is for server Ip
		len = strlen(input[i]);
		if(input[i][len-1] == '\n')
			input[i][len-1] = '\0';
		//printf("%d %s\n", i, input[i]);
		i++;
	}
	i = 0;

	for(ifihead = ifi = Get_ifi_info_plus(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next)
	{
		//intdetails[i].sockfd = Socket(AF_INET,top SOCK_DGRAM, 0);//Create UDP socket
		//myint[i].sockfd = intdetails[i].sockfd;

		//Setsockopt(intdetails[i].sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)); 
			
		if ( (ipaddr = (struct sockaddr_in *) ifi->ifi_addr) != NULL)
		{
			intdetails[i].ipaddr = Sock_ntop_host((SA *) ipaddr, sizeof(*ipaddr)); //Store ip
			printf("\nIP addr: %s, i=%d\n", intdetails[i].ipaddr,i);
			strcpy(myint[i].ipaddr, intdetails[i].ipaddr);
			//printf("myint ip addr: %s, i=%d\n", myint[i].ipaddr, i);
			inet_pton(AF_INET, intdetails[i].ipaddr, &ipptr);
			//printf("Ip addr again: %s\n", intdetails[i].ipaddr);
		}

		if ( (netmask = (struct sockaddr_in *) ifi->ifi_ntmaddr) != NULL)
		{
			intdetails[i].netmask = Sock_ntop_host((SA *) netmask, sizeof(*netmask)); //Store netmask
			printf("Network mask: %s\n", intdetails[i].netmask);
			strcpy(myint[i].netmask, intdetails[i].netmask);
			inet_pton(AF_INET, intdetails[i].netmask, &netmaskptr);
		}
						
		subnetptr.s_addr = (ipptr.s_addr) & (netmaskptr.s_addr); //Anding the ipaddr with network mask
		inet_ntop(AF_INET, &subnetptr.s_addr, intdetails[i].subnetaddr, sizeof(intdetails[i].subnetaddr));
		strcpy(myint[i].subnetaddr, intdetails[i].subnetaddr);
		printf("Subnet : %s\n", intdetails[i].subnetaddr);
		
		ipaddr->sin_family = AF_INET;
		i++; //Increment the array of structure
		//printf("Ip addr again: %s\n", intdetails[i].ipaddr);
	}
	//printf("Outside loop\n");
	printf("IPServer IP address from client.in file: %s\n", input[0]); 
	//verify if the server IP is local or not - check for exact match
	printf("Comparing serverIP with client's loopback IP address\n"); 	
	//printf("value of i before comparision loop: %d\n", i);
	for (j=0; j<i; j++)
	{
		//printf("Entered loop for exact IP match comparision\n");
		//printf("myint ip addr: %s\n", myint[j].ipaddr);
		//printf("myint ip subnet: %s\n", myint[j].subnetaddr);
		//printf("myint ip netmask: %s\n", myint[j].netmask);
		//printf("Input IP address: %s\n", input[0]);
		//printf("Comapring with Ip addr: %s\n", myint[j].ipaddr);
		//strcpy (serverinput, input[0]);
		//printf("serverinput: %s\n", serverinput);
		n= (strcmp(input[0], myint[j].ipaddr));
		//n = compareip(input[0], myint[j].ipaddr);
		if (n == 0)  //loopback
		{
			printf("Server IP address is local.\nSetting the ServerIP to loopback address- 127.0.0.1\n");
			strcpy (serverip, "127.0.0.1");
			inet_pton(AF_INET, serverip, &finalipclient.sin_addr);
			inet_pton(AF_INET, serverip, &finalipserver.sin_addr);
			option = 1;
			Setsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE, &option, sizeof(option));
			finalipclient.sin_family = AF_INET;
			finalipclient.sin_port = htons(0);
			Bind(sockfd, (SA *) &finalipclient, sizeof(finalipclient));
			//sockfd = Socket(AF_INET, SOCK_DGRAM, 0)
			subnetflag = 0;
			localflag = 1;
			break;
		}
		//else if (n==1)
		//	printf("not equal ip, value of n:%d\n\n",n);
					
	}
	
	if(subnetflag == 1)
	{
		//printf("In subnet flag\n");
		for (j=0; j<i; j++)
		{
			//printf("Entered loop for exact IP match comparision\n");
			//printf("myint ip addr: %s\n", myint[j].ipaddr);
			//printf("myint ip subnet: %s\n", myint[j].subnetaddr);
			//printf("myint ip netmask: %s\n", myint[j].netmask);
			//printf("Input: %s\n", input[0]);
			//printf("Comparing with Ip addr: %s\n", myint[j].ipaddr);
			//strcpy (serverinput, input[0]);
			//printf("serverinput: %s\n", serverinput);
			n= (strcmp(input[0], myint[j].ipaddr));
			inet_pton(AF_INET, input[0], &ipptr);
			inet_pton(AF_INET, myint[j].netmask, &netmaskptr);
			subnetptr.s_addr = (ipptr.s_addr) & (netmaskptr.s_addr);
			inet_ntop(AF_INET, &subnetptr.s_addr, inputsubnet, sizeof(inputsubnet));
			//printf("inputsubnet: %s\n", inputsubnet);
			n = strcmp(inputsubnet, myint[j].subnetaddr);
			//n = compareip(input[0], myint[j].ipaddr);
			if (n == 0) //same subnet
			{	
				bindflag = 1;
				//printf("%d\n", longprefixflag);
				if(longprefixflag == 0)
				{
					strcpy(netmaskprefix, myint[j].netmask); //netmask
					strcpy(prefixipaddr, myint[j].ipaddr); //ipaddr
					//printf("netmaskprefix : %s\nprefixipaddr : %s\n", netmaskprefix, prefixipaddr);
					localflag = 1;
					inet_pton(AF_INET, myint[j].ipaddr, &finalipclient.sin_addr);				
					option = 1;
					Setsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE, &option, sizeof(option));
					//inet_pton(AF_INET, input[0], &finalipserver.sin_addr);
					longprefixflag = 1;
					break;
				}
				else
				{
					inet_pton(AF_INET,netmaskprefix, &netmaskprefixptr.s_addr);
					inet_pton(AF_INET,myint[j].netmask, &netmaskptr.s_addr);
					prefix.s_addr = ((netmaskprefixptr.s_addr) | (netmaskptr.s_addr));
					inet_ntop(AF_INET, &prefix.s_addr, prefixor, sizeof(prefixor));
					//printf("netmaskprefixptr: %s\n",inet_ntop(AF_INET,&netmaskprefixptr.s_addr,str,sizeof(str)));
					inet_ntop(AF_INET,&netmaskprefixptr.s_addr, subnetprev, sizeof(subnetprev));
					inet_ntop(AF_INET, &netmaskptr.s_addr, subnetcurr, sizeof(subnetcurr));
					//printf("prefixor : %s\nsubnetprev : %s\nsubnetcurr : %s\n", prefixor, subnetprev, subnetcurr);
					if(!strcmp(prefixor, subnetprev))
						strcpy(netmaskprefix, subnetprev);
					if(!strcmp(prefixor, subnetcurr))
					{
						strcpy(netmaskprefix, subnetcurr);
						strcpy(prefixipaddr, myint[j].ipaddr);
						printf("Ipaddr in strcmp: %s\n", prefixipaddr);
					}
					
				}							
			}		
		}
		//printf("Longest prefix : %s\n", netmaskprefix);
		//printf("Matched ip : %s\n", prefixipaddr);
		//inet_pton(AF_INET, prefixipaddr, &finalipclient.sin_addr);
		
		//setting SO_DONTROUTE option for same subnet IP
	} //ifsubnetflag
	if (bindflag == 1) //local subnet
	{
		finalipclient.sin_family = AF_INET;
		finalipclient.sin_port = htons(0);		
		inet_pton(AF_INET, prefixipaddr, &finalipclient.sin_addr);
		printf("Client is local to server\n");
		Bind(sockfd, (SA *) &finalipclient, sizeof(finalipclient));
		inet_pton(AF_INET, input[0], &finalipserver.sin_addr);
	}
	if((!localflag) && (bindflag == 0)) // external IP 
	{
		//printf("My interface is %s\n", myint[1].ipaddr);
		inet_pton(AF_INET, myint[1].ipaddr, &finalipclient.sin_addr);
		finalipclient.sin_family = AF_INET;
		finalipclient.sin_port = htons(0);
		printf("The server IP is not local to the client.\n");
		//printf("Messages on the socket will be routed.\n");
		inet_pton(AF_INET, input[0], &finalipserver.sin_addr);
	}
	//create UDP socket and call bind
	//finalipclient.sin_family = AF_INET;
	//finalipclient.sin_port = htons(0);
	finalipserver.sin_family = AF_INET;
	finalipserver.sin_port = htons(atoi(input[1]));
	//Bind(sockfd, (SA *) &finalipclient, sizeof(finalipclient));
	Connect(sockfd, (SA *) &finalipserver, sizeof(finalipserver));
	//printf("sockfd after Connect: %d\n", sockfd);
	printf("Now connected to socket %s\n", Sock_ntop( (SA *) &finalipserver, sizeof(finalipserver)));
	/*{
		//inet_ntop (AF_INET, & (finalipserver), serverip, sizeof(serverip));
		clientlen = sizeof(cliaddr);
		//Getsockname(sockfd, (SA *) &cliaddr, &clientlen);
		//Getpeername(sockfd, (SA *) &servaddr, &clientlen);
		printf("Connect Error: Server IP %s is not reachable\n", Sock_ntop( (SA *) &finalipserver, clientlen));
		printf("Exiting the program. Goodbye!\n");
		exit(1);
	}*/
	clientlen = sizeof(cliaddr);
	Getsockname(sockfd, (SA *) &cliaddr, &clientlen);
	printf("IPClient address using Getsockname: %s\n", Sock_ntop( (SA *) &cliaddr, clientlen));	
	Getpeername(sockfd, (SA *) &servaddr, &clientlen);
	printf("IPServer address using Getpeername: %s\n", Sock_ntop( (SA *) &servaddr, clientlen));
	option = 0;
	optlen = sizeof(option);
	Getsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE, &option, &optlen);
	//printf("Option: %d\n", option);
	if (option == 0)
		printf("SO_DONTROUTE option not set for socket to Server:%s.\n", Sock_ntop( (SA *) &servaddr, clientlen));
	else
		printf("SO_DONTROUTE option set for socket to server:%s.\n", Sock_ntop( (SA *) &servaddr, clientlen));
	advertWindowSize = atoi(input[3]);
	strcat(input[2], ",");
	strcat(input[2], input[3]);
	strcpy(sendline, input[2]); //input[2] is filename
	//Sendto(sockfd, sendline, strlen(sendline), 0, NULL, NULL); 
	//printf("Writing to socket: %s\n", sendline);
	//printf("sockfd before send:%d\n", sockfd);
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	if(drop())
		send(sockfd, sendline, strlen(sendline), 0);
	else
		printf("1st Handshake from client dropped due to drop probability\n");
	//Signal(SIGALRM, sig_alrm);
	FD_ZERO(&rset);
	for( ; ;)
	{
		FD_SET(sockfd, &rset);
		maxfdp1 = sockfd + 1;
		if(select(maxfdp1, &rset, NULL, NULL, &timeout) <0 )
		{
			if(errno == EINTR)
				continue;
			else
			{
				fprintf(stdout, "Select error: %s\n", strerror(errno));
				err_quit("Error in select function");
			}
		}
		if(FD_ISSET(sockfd, &rset)) 
		{
			//printf("sockfd before receive:%d\n", sockfd);
			//printf("sockfd before recv(2nd handshake):%d\n", sockfd);
			n = Recvfrom(sockfd, recvline, MAXLINE+1, 0, (SA *) &servaddr, &clientlen);
			printf("Received New Port from server child: %s\n", recvline);
			finalipserver.sin_port = htons(atoi(recvline));
			Connect(sockfd, (SA *) &finalipserver, sizeof(finalipserver));
			//printf("After reconnect\n");
			//printf("sockfd after reconnect:%d\n", sockfd);
			printf("After reconnecting to the new port.\nNew IPClient address: %s\n", Sock_ntop( (SA *) &cliaddr, clientlen));	
			printf("New Ipserver address: %s\n", Sock_ntop( (SA *) &finalipserver, clientlen));
			strcpy(sendline, "Now connected to new port");
			Getpeername(sockfd, (SA *) &servaddr, &clientlen);
			//printf("IPServer address: %s\n",  Sock_ntop( (SA *) &servaddr, clientlen));
			Send(sockfd, sendline, strlen(sendline), 0);
			//printf("Sent to server after reconnecting\n");
			break;
		}
		
			printf("Timer expired.. Sending the 1st handshake again\n");
              		send(sockfd, sendline, strlen(sendline), 0);
	}
	consumersleep = atoi(input[6]);
	printf("File transfer start in client..\n\n");
	//printf("sockfd before passing to producer: %d\n", sockfd);
	Pthread_create(&producer_tid, NULL, (void *)&producer, (void *) &sockfd);
	Pthread_create(&consumer_tid, NULL, (void *)&consumer, (void *) &consumersleep);

	//waiting for threads to exit before exiting main thread
	Pthread_join(producer_tid, NULL);
    	Pthread_join(consumer_tid, NULL);
	printf("Producer and consumer threads joined and exiting the main function\n");
	//recvfile(sockfd);
	//Getpeername(sockfd, (SA *) &servaddr, &clientlen);
	//printf("IPServer address: %s\n",  Sock_ntop( (SA *) &servaddr, clientlen));
	/*n = Recvfrom(sockfd, recvline, MAXLINE+1, 0, (SA *) &servaddr, &clientlen);
	printf("Message from server after reconnect: %s\n", recvline);*/

fclose(fp);
Close(sockfd);
exit(0);
}

void* producer(void *arg)
{
	ssize_t	n;
	int fd, Drop;
	fd = *((int *) arg);
	free(arg);
	struct iovec iovsend[2], iovrecv[2];
	char inbuff[SIZE], outbuff[SIZE];
	struct sockaddr_in servaddr;
	int clientlen, h, receivedFlag = 0;

	msgsend.msg_name = NULL;
	msgsend.msg_namelen = 0;
	msgsend.msg_iov = iovsend;
	msgsend.msg_iovlen = 2;

	msgrecv.msg_name = NULL;
	msgrecv.msg_namelen = 0;
	msgrecv.msg_iov = iovrecv;
	msgrecv.msg_iovlen = 2;
	iovrecv[0].iov_base = (char*) &recvhdr;
	iovrecv[0].iov_len = sizeof(struct messagehdr);
	iovrecv[1].iov_base = inbuff;
	iovrecv[1].iov_len = SIZE;
	//printf("sockfd before recvmsg: %d\n", fd);
	//printf("Before recvmsg\n");
	for( ; ;)
	{
		//printf("\nNow in producer loop\n");
		//sleep(1);
		n = Recvmsg(fd, &msgrecv, 0);
		inbuff[n] = '\0';
		receivedFlag = 0;
		printf("\nExpected segment number is %d\n", acknowledge);
		printf("Received segment number is %d\n", recvhdr.seqno);
		pthread_mutex_lock(&lock);
		if((acknowledge == recvhdr.seqno) && (advertWindowSize >0))
		{
			receivedFlag = 1;
			sendheader.seqno = recvhdr.seqno;
			if (recvhdr.fin == 1)
			{
				buff[head].fin_flag = 1;
				//pthread_mutex_unlock(&lock);
				fin_flag = 1;
			}
			sendheader.fin = recvhdr.fin;
			//printf("Received segment number : %d\n", recvhdr.seqno);
			buff[head].b_seqno = recvhdr.seqno;
			strcpy(buff[head].b_advertWindow, inbuff);	
			//printf("buff[head].b_seqno: %d\n", buff[head].b_seqno);
			//printf("buff[head].b_advertWindow: %s\n", buff[head].b_advertWindow);
			/*if(sendheader.seqno == 2)
				sendheader.seqno = 1;*/
			sendheader.ack = sendheader.seqno + 1;
			sendheader.advwindow = --advertWindowSize;
			printf("Size of the advertisement window is : %d\n", advertWindowSize);
			strcpy(outbuff, "Ack sent");
		}
		else if (advertWindowSize == 0)
			printf("AdvertWindowSize is still zero in producer thread.\n");
		pthread_mutex_unlock(&lock);
		/*if(head == 2)
		{
			for(h=0; h<head; h++)
			{
				printf("%s\n", advertWindow[h]);
				memset(advertWindow[h], 0, SIZE);
			}
			head = 0;
			advertWindowSize = atoi(input[3]);
		}*/
//		sendheader.timestamp = 0;
		printf("sendheader ackno: %d\n", sendheader.ack);
		sendheader.advwindow = advertWindowSize; // new added advwindowsize incase of not expected ack.
		//printf("send seqno: %d\n", sendheader.seqno);
		printf("sendheader adwindow in ack being sent: %d\n", sendheader.advwindow);
		iovsend[0].iov_base = (char*) &sendheader;
		iovsend[0].iov_len = sizeof(struct messagehdr);
		iovsend[1].iov_base = outbuff;
		iovsend[1].iov_len = SIZE;
		
		//if(sendheader.seqno == 6)
		//	sleep(5);	
		//{

		Drop = drop();
		//printf("dontDrop probability : %d\n", Drop);
		if(Drop)
		{
			//if(sendheader.ack != 14)
			//{
			if(sendmsg(fd,&msgsend, 0) < 0)
			{
				if(fin_flag == 1)
				{
					printf("File received successfully from FTP server. Exiting the program.\nGoodbye..!!\n");
					Pthread_join(consumer_tid, NULL);
				}			
				printf("sendmsg error:%s\n", strerror(errno));
				exit(1);
			}
			else
			{
			
				pthread_mutex_lock(&lock);
				if((advertWindowSize != 0) && (receivedFlag == 1))			
					head++;
				//printf("Head is now at %d in producer after sending ack\n", head);
				acknowledge = sendheader.ack;
				printf("Ack sent to server %d\n", sendheader.ack);
				pthread_mutex_unlock(&lock);
			}
			//}
		}
		else printf("Acknowledgment dropped.\n");
			
		if(fin_flag == 1)
		{
			//Pthread_join(consumer_tid, NULL);
			printf("Producer Thread: File received successfully from FTP server..!!\n");
			return (NULL);
		}	
		//sleep(4);
		/*if(sendmsg(fd, &msgsend, 0) < 0)
		{
			printf("sendmsg error:%s\n", strerror(errno));
			exit(1);
		}*/
		
	}
}

void* consumer(void *arg)
{
	int m, i=0, h, seqnumber=0;
	m = *((int *) arg);
	free(arg);
	uint32_t timer_sec, timer_usec, t, random;
	for (;;)
	{			
		srand48(2);
		t = -1 * m * log(((float)rand()/RAND_MAX));
		//printf("\n\nValue of t: %u\n\n", t);
		//printf("RANDMAX: %u\n",RAND_MAX); 
		timer_sec = t / 1000; //millisec to sec
		timer_usec = t * 1000;	//microsec to millisec	
		//settimer.it_interval.tv_sec = 0;      //next value
		//settimer.it_interval.tv_usec = 0;
		//settimer.it_value.tv_sec = timer_sec;        // current time
		//settimer.it_value.tv_usec = timer_usec;
		/*if (setitimer(ITIMER_REAL, &settimer, NULL) < 0)
		{
			printf("setitimer error:%s\n", strerror(errno));
			exit(1);
		}
		if ((sigsetjmp(jmpbuf, 1)) != 0) 
		{*/
		//sleep(t);
		usleep(timer_usec);	
		pthread_mutex_lock(&lock);
		//printf("\n\nHead before loop: %d\n", head);
		for(h=0; h<=head; h++)
		{
			if(seqnumber < 	buff[h].b_seqno)
			{		
				//printf("\n\nH Value in consumer thread loop:%d\n", h);
				printf("%s\n", buff[h].b_advertWindow);
				printf("Buffer sequence number is %d\n", buff[h].b_seqno);
				seqnumber++;
				advertWindowSize++;
				//printf("New Advertwindowsize after printing in consumer: %d\n", advertWindowSize);
				memset(buff[h].b_advertWindow, 0, SIZE);
				if(buff[h].fin_flag == 1)
				{
					//Pthread_join(producer_tid, NULL);
					printf("\nFile displayed completely. Exiting the consumer thread.\nGoodbye!!\n");
					pthread_mutex_unlock(&lock);
					return (NULL);
				}
			}
		}
		//pthread_mutex_lock(&lock);
		//printf("Value of head seqno in consumer before assigning to local seqno: %d\n", buff[head].b_seqno);
		//seqnumber = buff[head].b_seqno;
		//printf("Sequence number at the end is %d\n", seqnumber);
		head = 0;
		//advertWindowSize = atoi(input[3]);
		//printf("advertWindowSize at end of consumer: %d\n", advertWindowSize);
		pthread_mutex_unlock(&lock);
	}
	
}
	


/*static void sig_alrm(int signo)
{
	siglongjmp(jmpbuf, 1);
}*/

int drop()
{
	int dontDrop;
	float prob, dropProbability;
	dropProbability = atof(input[5]);
	//printf("Drop probability is %f\n", dropProbability);

	srand48(atoi(input[4]));
	prob = (float)rand()/RAND_MAX;
	
	if(prob < dropProbability)
		dontDrop = 0;
	else dontDrop = 1;

	return dontDrop;
}
