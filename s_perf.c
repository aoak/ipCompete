#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>




#define BUFF_SIZE 3000		// size of the buffer




struct test_info {

    /* First is the generic info about port numbers and ctrl and test port
	addresses etc */
	int ctrl_port;
	int test_port;

	/* We need two types of address structures. One for ipv4 and other for ipv6 */
	struct sockaddr_in ctrl4, test4, cli4;
	struct sockaddr_in6 ctrl6, test6, cli6;
	struct sockaddr * ctrl_addr;    /* pointer to server ctrl address */
	struct sockaddr * test_addr;    /* pointer to server test address */
	struct sockaddr * cli_addr;     /* pointer to client address */
	int addr_size;                  /* size of the address structure */

	/* These two are the socket descriptors */
	int testsock;
	int ctrlsock;

	/* These are test parameters */
	int n_prot;						/* This is network protocol */
	int domain;						/* AF_INET or AF_INET6 */
	int t_prot;						/* Transport protocol (TCP = 1, UDP = 0) */
	long int data_info;				/* Data to be transfered (bytes/packets) */
	} ti;



void check_input (int, char * []);
void perf_test ();
void raise_error (const char *);
void shake_hands ();
int itoa (long int, char *);
long int run_udp_test();
long int run_tcp_test();









int main (int argc, char * argv[]) {

	check_input(argc,argv);
	
	int servsock;					/* socket descriptors for server socket (we listen on this) */
	int status;
	socklen_t client_len;			/* length of client address */

	int type = SOCK_STREAM;			/* Type of control connection is TCP */
	
	ti.ctrl_port = atoi(argv[1]);		/* Convert the port from string to number */
	ti.n_prot = atoi(argv[2]);			/* Store the network protocol to be used */

	/* We are supposed to support both ipv4 and ipv6. So depending on the protocol,
	we will have to create different address structures and address family for the sockets.
	
	We have to do this for both ctrl and test socket. 
	
	On client, the switch-case is simple because of the use of getaddrinfo().
	I should try to use it here also. */

	switch (ti.n_prot) {
		case 4: ti.domain = AF_INET;
				ti.ctrl_addr = (struct sockaddr *) &ti.ctrl4;
				ti.test_addr = (struct sockaddr *) &ti.test4;
				ti.cli_addr = (struct sockaddr *) &ti.cli4;
				ti.addr_size = sizeof(ti.ctrl4);

				/* Now we have to set the fields in address structures */
				bzero( (char *) &ti.ctrl4, sizeof(ti.ctrl4) );
				bzero( (char *) &ti.test4, sizeof(ti.test4) );

				ti.ctrl4.sin_family = ti.domain;
				ti.test4.sin_family = ti.domain;

				ti.ctrl4.sin_addr.s_addr = INADDR_ANY;
				ti.test4.sin_addr.s_addr = INADDR_ANY;

				ti.ctrl4.sin_port = htons(ti.ctrl_port);
				ti.test4.sin_port = htons(ti.ctrl_port);
				break;

		case 6: ti.domain = AF_INET6;
				ti.ctrl_addr = (struct sockaddr *) &ti.ctrl6;
				ti.test_addr = (struct sockaddr *) &ti.test6;
				ti.cli_addr = (struct sockaddr *) &ti.cli6;
				ti.addr_size = sizeof(ti.ctrl6);

				/* Now we have to set the fields in address structures */
				bzero( (char *) &ti.ctrl6, sizeof(ti.ctrl6) );
				bzero( (char *) &ti.test6, sizeof(ti.test6) );

				ti.ctrl6.sin6_family = ti.domain;
				ti.test6.sin6_family = ti.domain;

				ti.ctrl6.sin6_addr = in6addr_any;
				ti.test6.sin6_addr = in6addr_any;

				ti.ctrl6.sin6_port = htons(ti.ctrl_port);
				ti.test6.sin6_port = htons(ti.ctrl_port);

				ti.ctrl6.sin6_scope_id = 1;		// hardcoding this irritating field
				ti.test6.sin6_scope_id = 1;		// hardcoding this irritating field
				break;

		default:
				fprintf(stderr,"[ERROR]: Invalid protocol %d\n",ti.n_prot);
				exit(1);
		}

	
	/* Try to create a raw socket. This will cause the socket to exist in namespace but it
	will not have an address yet */

	servsock = socket(ti.domain, type, 0);
	if (servsock < 0)
		raise_error("[ERROR]: Could not create socket");
	

	/* Now we have to set all the address structure fields and then call the bind. The 
	troublesome part of having different types of address structures with different sizes
	should be taken care by the switch-case before this. Hence this part should be clean */

	if ( bind(servsock, ti.ctrl_addr, ti.addr_size) < 0 )
		raise_error("[ERROR]: Bind failed");
	

	/* Now listen to the port and if the connection comes in, accept it */
	listen(servsock, 5);		/* 5 connections to be enqueued */
	client_len = sizeof(*ti.cli_addr);

	ti.ctrlsock = accept(servsock, ti.cli_addr, &client_len);
	printf("[INFO]: Established ctrl connection with client\n");

	if (ti.ctrlsock < 0)
		raise_error("[ERROR]: Accept failed");
	
	/* Call the function to start the tests. This function should take care of handshakes */
	perf_test();


	printf("[INFO]: Terminating server\n");
	close(servsock);
	exit(0);

	}



















/* perf_test: This function first initiates the handshake with client, then it calls
	appropriate test function. It then sends the client timestamp when we received 
	last chunk of data and total data we received */

void perf_test () {

	long int received_data = 0;
	char buff[BUFF_SIZE];
	int stat = 0;
	struct timespec end;

	/* First we need to do initial handshake with the client.*/
	
	shake_hands();

	/* Call the test function according to the transport layer protocol we are using */
	if (ti.t_prot == 1)
		received_data = run_tcp_test();
	else if (ti.t_prot == 0)
		received_data = run_udp_test();
	else
		raise_error("[ERROR]: Invalid transport layer protocol");
	
	/* We are here means that the last chunk of the data was received. Now we need to
	send the server the timestamp when we received the last chunk */

	clock_gettime(CLOCK_REALTIME, &end);

	/* Now send this as a message to the client so that it knows when the last chunk was
	received. The sending is done by converting sec and nsec numbers into strings. 
	Apparently, just copying the structure into buffer and sending it doesn't work. */

	int len = 0;
	len = itoa(end.tv_sec, buff);
	stat = write(ti.ctrlsock, buff, len+1);
	if (stat < len+1)
		raise_error("[ERROR]: Sending the end timestamp failed");
	sleep(1);


	len = itoa(end.tv_nsec, buff);
	stat = write(ti.ctrlsock, buff, len+1);
	if (stat < len+1)
		raise_error("[ERROR]: Sending the end timestamp failed");
	sleep(1);



	/* And then we have to tell the client how much data we actually received */

	bzero(buff, BUFF_SIZE);
	len = itoa(received_data,buff);
	printf("[INFO]: Received %s amount of data\n",buff);

	stat = write(ti.ctrlsock, buff, len+1);
	if (stat != len+1)
		raise_error("[ERROR]: Sending the info about received data failed");
	printf("[INFO]: Sent information about received data to client\n");

	return;
	}












/* run_tcp_test: This function keeps on receiving the data from the client
	till the time we have received the data expected to receive. It then
	returns the total number of bytes received to the caller */

long int run_tcp_test() {

	char buff[BUFF_SIZE];
	int stat = 0;
	long int received = 0;

	printf("[INFO]: Starting TCP test\n");

	while (received < ti.data_info) {
		bzero(buff,BUFF_SIZE);

		stat = read(ti.testsock, buff, BUFF_SIZE-1);
		if (stat < 0)
			raise_error("[ERROR]: Read on the socket failed");

		received += stat;
		}
	
	return received;
	}









/* run_udp_test: This function is trickier than TCP. As UDP is unreliable, we
	dont know how many messages will be dropped. We cant even rely on message
	numbers entirely because the last message itself can be dropped causing is
	to wait too long.

	Instead, here we will wait for message to arrive for 1 sec. We will do it
	as many number of times as we are expected to receive a message. We will 
	maintain a count of how many datagrams we received as well as how many
	bytes we received. Then we will return the total number of received bytes */


long int run_udp_test() {


	char buff[BUFF_SIZE];
	int stat = 0;
	long int received = 0;
	int count;
	int received_packets = 0;

	printf("[INFO]: Starting UDP test\n");

	struct timeval t;
	t.tv_sec = 1;
	t.tv_usec = 0;

	/* Set the timeout for the socket to 1 sec. If we don't receive the message
	in one sec, then probably the message is lost */
	if ( setsockopt(ti.testsock, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) < 0)
		raise_error("[ERROR]: Could not set timeout on the test socket");
	
	/* Receive messages. Note the missing error check. We dont want to exit
	on timeout error, and adding logic to check which error occurred doesnt seem
	so important (for now atleast) */
	for (count = 0; count <= ti.data_info+1; count++) {

		stat = 0;
		stat = recvfrom(ti.testsock, buff, BUFF_SIZE-1, 0, NULL, NULL);
		received += stat;
		if (stat > 0)
			received_packets++;
		}
	
	printf("[INFO]: Received %d packets\n", received_packets);
	return received;
	}









/* shake_hands: This function is to do initial handshake with the client and tell us how
	much data are we expecting.
	This should include the following:
		1. Receive indication that client is ready for handshake
		2. Receive information about the transport layer protocol
		2. Receive information about the data size
		4*. Receive confirmation that clock is synced on client (not implemented)
		5. Send ready indicator 
*/



void shake_hands () {

	int bsize = 256;
	int ssock;				/* Just in case we need to accept a UDP connection */
	char buff[256];

	int read_ele, wrote_ele;
	ti.t_prot = -1;




	/* receive "ready" from client */
	bzero(buff,bsize);

	read_ele = read(ti.ctrlsock, buff, 5);
	if (read_ele < 0)
		raise_error("[ERROR]: Read failed during handshake.");
	if (strcmp(buff,"ready") != 0)
		raise_error("[ERROR]: Handshake failed. Client not ready.");
	
	printf("[INFO]: Client ready for handshake\n");

	



	/* receive transport layer protocol */
	bzero(buff,bsize);

	read_ele = read(ti.ctrlsock, buff, 3);
	if (read_ele < 0)
		raise_error("[ERROR]: Read failed during handshake.");
	

	/* For convinience, store the transport layer protocol as int. TCP = 1, UDP = 0 */
	if (strcmp(buff,"TCP") == 0) {
		printf("[INFO]: Using TCP in transport layer\n");
		ti.t_prot = 1;
		}
	else if (strcmp(buff,"UDP") == 0) {
		printf("[INFO]: Using UDP in transport layer\n");
		ti.t_prot = 0;
		}
	else
		raise_error("[ERROR]: Received invalid transport layer protocol");
	

	/* If we are using TCP in transport layer, then receive datasize string from client. 
	This should not be more than 10 characters (we are not expecting GBs of data).
	If we are using UDP in transport layer then receive number of messages from the client
	*/

	bzero(buff,bsize);

	if (ti.t_prot == 1) {
		
		/* TCP */
		read_ele = read(ti.ctrlsock, buff, 10);
		if (read_ele < 0)
			raise_error("[ERROR]: Read failed during handshake.");
	
		ti.data_info = atoi(buff);
		if (ti.data_info <= 0)
			raise_error("[ERROR]: Invalid datasize parameter from client.");
		}
	else if (ti.t_prot == 0) {

		/* UDP */
		read_ele = read(ti.ctrlsock, buff, 10);
		if (read_ele < 0)
			raise_error("[ERROR]: Read failed during handshake.");
	
		ti.data_info = atoi(buff);
		if (ti.data_info <= 0)
			raise_error("[ERROR]: Invalid number of packets parameter from client.");
		}
	else
		raise_error("[ERROR]: Received invalid transport layer protocol");

	printf("[INFO]:	Received data size information (%ld)\n",ti.data_info);

	bzero(buff,bsize);

	/* Now we have to set up a test connection (if any)
		For UDP, we will open a new socket and accept connection there. The port number will be 
			the same port where we are listening for ctrl connection
		For TCP, we will just continue on this socket only.
	*/

	if (ti.t_prot == 1) {

		/* It is easy for TCP, we will use the same connection. So just copy the socket
		descriptor and port number (this is actually a port number where server is listening)
		The port number is to be used by client to check if we are setting up new server
		to accept UDP connection */
		ti.test_port = ti.ctrl_port;
		ti.testsock = ti.ctrlsock;
		printf("[INFO]: Ready for TCP test\n");
		}

	else if (ti.t_prot == 0) {
		
		/* For UDP, we have to create a UDP socket and bind. This is painful but important.
		We create a UDP socket on the same port number where server is listening. This is
		allowed because UDP and TCP sockets are different */
		ssock = socket(ti.domain, SOCK_DGRAM, 0);
		if (ssock < 0)
			raise_error("[ERROR]: Could not create socket for test connection");

		/* Bind with test address. This has SOCK_DGRAM type specified */
		if ( bind(ssock, ti.test_addr, ti.addr_size) < 0 )
			raise_error("[ERROR]: Could not bind for test connection");
		ti.testsock = ssock;		/* set the test socket descriptor */

		}
	else 
		raise_error("[ERROR]: Received invalid transport layer protocol");


	bzero(buff,bsize);

	/* Tell client we are ready to receive the data */
	strcpy(buff,"ready");
	wrote_ele = write(ti.ctrlsock, buff, 5);
	if (wrote_ele != 5)
		raise_error("[ERROR]: Sending the ready signal failed");
	printf("[INFO]: Server ready for test\n");
	
	return;
	}










/* check_input: This function is to check that the input to this program are proper. This
	includes checking the number of arguments and their types */


void check_input (int c, char * v[]) {

	/* Not enough args? */
	if (c < 3) {
		printf("Usage: %s [port] [protocol] \n\tWhere protocol can be 4 (ipv4) or 6 (ipv6)\n",v[0]);
		exit(1);
		}
	
	/* The port number should not be special */
	if (atoi(v[1]) < 2000) {
		fprintf(stderr,"The input port must be greater than 2000");
		exit(1);
		}
	
	/* Network protocol has to be ipv4 or ipv6 (4/6) */
	if (atoi(v[2]) != 4 && atoi(v[2]) != 6) {
		fprintf(stderr,"Invalid protocol number %d\n",atoi(v[2]));
		exit(1);
		}
	}




/* raise_error: This function is for printing a message from the program, then
	printing the message from the system and then exit with non-zero status */

void raise_error (const char * msg) {

	perror(msg);
	exit(1);
	}





/* itoa: This function converts a given integer into a string. It assumes
	that the integer will not be a negative number (which is our case).
	NOTE: In case of 0, it will generate a C-string as "0\0". Returns 
	length of the C-string excluding the null character (1 for our example) */

int itoa (long int a, char * i) {
	
	/* First we need to find out the length of the string to be. This has to
	be recursive */
	
	long int d = 10;
	int len;

	for (len = 0; a/d > 0; len++, d *= 10);

	i[len+1] = '\0';

	for (d = len; d >= 0; d--) {
		i[d] = '0' + (a % 10);
		a /= 10;
		}
	return len;
	}

