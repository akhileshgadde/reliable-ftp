CC = gcc

LIBS =  -lsocket -lm\
	/home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a

FLAGS =  -g -O2
CFLAGS = ${FLAGS} -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib

all: server client
 
client: client.o get_ifi_info_plus.o
	${CC} ${FLAGS} -o client client.o get_ifi_info_plus.o ${LIBS}

server: server.o get_ifi_info_plus.o
	${CC} ${FLAGS} -o server server.o get_ifi_info_plus.o ${LIBS}

client.o : client.c
	${CC} ${CFLAGS} -c client.c

server.o : server.c
	${CC} ${CFLAGS} -c server.c

get_ifi_info_plus.o: /home/courses/cse533/Asgn2_code/get_ifi_info_plus.c
	${CC} ${CFLAGS} -c /home/courses/cse533/Asgn2_code/get_ifi_info_plus.c ${LIBS}

clean:
	rm get_ifi_info_plus.o server.o server client.o client

