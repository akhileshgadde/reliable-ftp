Members of the group for assignment-2 :
Akhilesh Gadde (109836849)
Pavan Maguluri (109892313)

1. Get_ifi_info_plus modifications
------------------------------------
(a) We have used the following structure to store all the interfaces of the server.
	struct interface
	{
		int sockfd;
 //stores the socket descriptor to which the interface is bound
		char ipaddr[40];
 //ipaddr of the interface
		char netmask[40];
 //network mask of the interface
		char subnetaddr[40];
 //subnetaddr of the interface
	};

We have retrieved only the ifi_addr and ifi_ntmaddr from the Get_ifi_info_plus function in order to achieve the only unicast addresses condition and then we performed a bitwise and operator to the ipaddr and the netmask address to get the subnet address of the interface. These array of structures for all the unicast addresses is being used later for comparing whether the server IP address is local to the client or not in both client and server respective array of structures.  

(b) We are also not binding the wildcard address and wildcard port number to the socket. So the server would only accept incoming connections for the IP addresses and ports for which the sockets have been bound.

(c) Professor has provided us with the Get_ifi_info_plus() function which only provides us the unicast primary or aliases(secondary) addresses. So binding the wildcard or broadcast addresses to sockets is avoided.

2. RTO changes
----------------
(a) RTO algorithm provided as part of Steven's code is modified to suit our assignment. Firstly, all the float values have been changed to integers. Also, we have changed the “RTT_STOP” algorithm to use the algorithm specified by the professor where we store 8* times the RTT value and 4* times the RTTVAR value. After performing the algorithm using the bitwise right shift operation. The macro computing the current RTO value in “rtt.c” file has also been modified accordingly.

(b) The below values are also modified:
RTT_RXTMIN as 1000 ms, RTT_RXTMAX as 3000 ms, RTT_MAXNREXMT to 12.
Also, the retransmit counter value has been moved from rttinfo structure to our segment header structure since we need to consider the retranmits count for each individual segment.

(c) When the segments are transmitted from the server child to client, the “retries” counter for the segment is set to zero. When timeout happens, the “retries” counter for the particular segment is incremented and the code checks if the “retries” counter is less than 12 or not. If yes, it would double the RTO value and set an alarm for the same using setitimer() function. If no, the server child would display a message saying that the “Maximum Retries have been reached” and stop sending any more segments. (disconnects the sending process).

(d) After the server receives acknowledgement for the normal segment (expected ack and no retransmissions for the segment), the server would calculate the new RTT using the current timestamp and timestamp stored while sending the packet. This new value would be fed into RTT_STOP function to get the new RTO value.

(e) Incase of a retransmitted segment, the server would check for the “retries” counter value and hence don't compute the new RTT value. Server would only compute for segments having “retries” count = 0.

(f) In the case of fast retransmit, the server child would clear the old setitimer() value and set a new alarm timer after retransmitting the segment. We choose not to double the RTO value in this case.  

(g) In the scenario where the received window size is zero, the server child would clear the old timer, set a new timer value starting with 5 seconds and with a maximum of 60 seconds. It would double the timer with each retry with a maximum of 12 retries before giving up on the client. If it receives an acknowledgement from the client from the client in between this process with a non-zero Advertised Window value, the server child would transmit the next segment in sliding window and reset the timer to the old RTO value before the zero advertised window scenario kicked-in.

(h) Since there is only sig_alarm handler in the program and we would not be able to find which timer has caused the SIG_ALARM signal, we have introduced additional checks in sigsetjmp() function to verify if the timeout was caused due to normal segment timeout or due to the zero advertised window scenario. 

3. ARQ Mechanisms

I. Sliding window
----------------------
(a) We have implemented an array of structures which resembles the sliding window in TCP. When a new segment is transmitted, the segment is stored in the array(sliding window) starting with zero position. The global variable head provides with the oldest segment in the sliding window. 

(b) When the acknowledgement for the oldest segment in sliding window is received, the segment would be removed from the sliding window and head incremented by 1. The timer value would be reset and a new setitimer() function called with timeout value set to new RTO value.

(c) The sliding window also hold the acknowledgement count for each segment in it so that it can call fast retransmission for the segment if it received 4 continuous ACKs from client.

II. Congestion Avoidance and Slow start 
–------------------------------------------------
(a) The initial cwnd value has been set to 1 and ssthresh set to 127 segments since the RFC specifies the value to be 65535 bytes and in our case, we are using fixed 512 byte segments. So the value of 127 has been arrived for initialization of ssthresh.

(b) At any point in the server child, the number of segments sent is minimum of cwnd, sliding window and advertised window.

(b) When timeout happens, the cwnd value is set to 1 and ssthresh set to half of old cwnd value. So after timeout, effectively only segment is sent out to client and after every successful acknowlegment, we increase the cwnd value by 1. So after the first acknowledgement, the value of cwnd would be 2 and so on.

(c) When cwnd value reaches ssthresh value, there is an additional check in our dg_send_receive() function where it verifies if the current cwnd value is greater than ssthresh value or not. If not, it would not change the cwnd value. If yes, it would change the cwnd value to ssthresh for the first time. (An additional flag introduced in the program makes sure that this happens only once during the lifetime of a connection with client.) After the cwnd reaches ssthresh value, we increment cwnd only by 1 for each set of segments transmitted from the sliding window.

(d) Incase of fast retransmission, the cwnd and ssthresh values are set to half of old cwnd value.

(e) Additonal checks were coded to make sure that the cwnd value always has a minimum of 1 and ssthresh has a minimum value of 2.

III. Fast Retransmission
------------------------------
(a) As mentioned above, an additional variable named “ackcount” is introduced for the segments in sliding window and when the ack count reaches 4 (1 original + 3 duplicates), the server child would call the fast retransmission and transmit the corresponding segment.

(b) Timeouts are adjusted as mentioned in the point 1 above.


4. Last segment handling
--------------------------

(a) We have taken a structure defined globally in the server.c file where we have defined a flag callled “FIN_FLAG”. We set this flag only for the last segment when we are sending out to the client.

(b) On the client, we have a check to identify if the “FIN_FLAG” is set for the received segment. If the received segment is the last segment, the client would send an acknowledgement and display the message that - “File is received successfully from the server”. Also, an additional check has been introduced(using thread_join() function) to make sure that the client does not exit without the consumer thread printing out the last segment on the terminal.

(c) After the consumer thread prints the last segment, the consumer thread would exit which would in turn cause the producer thread to exit. During the time, the main function would be waiting for both the producer and consumer threads to exit before the main function prints a message that “Producer and Consumer threads joined and exiting the main function”.
 
(d) An additional case is also handled where the last acknowledgement from client to server is missed, the server has an additional check where after the server times-out, it would check if the last segment transmitted had the FIN flag set. If yes, it would disconnect the connection and the control moves back to the parent select loop where it waits for new incoming connections.



