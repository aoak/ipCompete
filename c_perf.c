/* c_perf.c:

	Aniket Oak
	Created: 3-Jun-2013
	Modified: 19-Jun-2013

	Changed: 1. End timestamp receiving mechanism
			 2. Way we run UDP test (included timeout)


	This is a client part of the performance measurement tool. 

	The client resolves the server and attempts to connect on a specified
	port using TCP. On this connection, it does handshake with the server
	and conveys how much information it will send using which transport
	layer protocol. It then initializes the start timer and starts sending
	the data to the server. For TCP, same connection is used as test 
	connection, for UDP, a new connection is made using UDP socket.
	At the end, it receives the information from the server about the time
	when the last packet was received and total data received by server.
	The client the computes and displays the throughput.

	Usage: ./c_perf [server] [port] [transport protocol] [network protocol] [datasize]
			Where 
				network protocol can be 4 (ipv4) or 6 (ipv6)
				transport protocol can be TCP or UDP (case sensitive)
				datasize for TCP is number of bytes
				datasize for UDP is number of messages

	NOTE: The assumption is that the NTP daemon is running on both the machines 
		to keep the clocks in sync.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>




#define BUFF_SIZE 3000		// size of the buffer. Maybe we need two separate
							// buffers for UDP and TCP



/* This is a global structure which holds all the information about the test itself
	as well as the setup.
	It holds the socket descriptors, server address etc, along with the command line
	options as port, servername, transport and network layer protocols.

	This reduces the complexity of passing the same information here and there. */

struct test_info {

	int ctrl_port;								/* Port of server where we first attempt connection */
	char * ctrl_port_str;						/* String representation of the same port */
	int test_port;								/* Port on which test will be run (same as ctrl for TCP) */
	char * test_port_str;						/* String representation of the same port */

	int ctrlsock;								/* Socket descriptor for control connection */
	int testsock;								/* Socket descriptor for test connection (same as ctrl for TCP) */

	struct addrinfo ctrl_serv, test_serv;		/* Server info for ctrl and test connections */
	struct addrinfo * ctrl_ptr, * test_ptr;		/* Pointers for name resolution of ctrl and test serv */

	char * serv_name;							/* Input string with name of the server or its ip address */
	int n_prot;									/* Network layer protocol */
	int t_prot;									/* Transport layer protocol (TCP = 1, UDP = 0) */
	long int data_info;							/* Info about how much data to be transfered (bytes/packets) */
	int domain;									/* AF_INET or AF_INET6 depending on n_prot */
	} ti;




void check_input (int, char * []);
void perf_test ();
void raise_error (const char *);
void shake_hands ();
void calc_throughput (long int, struct timespec, struct timespec);
void itoa (long int, char *);
long int run_tcp_test();
long int run_udp_test();








int main (int argc, char * argv[]) {

	check_input(argc,argv);
	
	int status;
	struct addrinfo * s;


	int type = SOCK_STREAM;			/* ctrl socket type is TCP */
	
	ti.serv_name = argv[1];			/* Store the server name first */
	ti.ctrl_port_str = argv[2];		/* Port string */
	ti.test_port_str = argv[2];		/* In case of UDP, we use same port with UDP type socket */
	ti.ctrl_port = atoi(argv[2]);	/* Convert the port from string to number */
	ti.n_prot = atoi(argv[4]);		/* Store the network layer protocol to be used */
	ti.data_info = atol(argv[5]);	/* Size of the data to be sent */

	/* For convinience, store transport layer protocol as an int */
	if (strcmp(argv[3],"TCP") == 0)
		ti.t_prot = 1;
	else if (strcmp(argv[3],"UDP") == 0)
		ti.t_prot = 0;
	else {
		fprintf(stderr,"[ERROR]: Invalid transport layer protocol %s\n",argv[3]);
		exit(1);
		}
	

	/* We are supposed to support both ipv4 and ipv6. So depending on the protocol,
	we will have to create different address structures and address family for the socket */

	switch (ti.n_prot) {
		case 4: ti.domain = AF_INET;
				break;

		case 6: ti.domain = AF_INET6;
				break;

		default:
				fprintf(stderr,"[ERROR]: Invalid network protocol %d\n",ti.n_prot);
				exit(1);
		}


	/* Now lookup the server and populate the hostent structure

	Apparently, gethostbyname() function is now obsolete. So I will have to try and use getaddrinfo()
	which is new one.
	server = gethostbyname(serv_name); 
	
	
	getaddrinfo simplifies the matter a lot. I could remove all the ipv4/ipv6 specific things from the
	previous case statement and now getaddrinfo will take care of it
	*/

	int ret;

	/* Setting all the fields of addrinfo structure. Doing it for control as well as test connection.
	This is because of the difference in the type of the socket.
	If test is TCP test, the test addrinfo structure will be ignored */

	memset(&ti.ctrl_serv, 0, sizeof(ti.ctrl_serv));
	memset(&ti.test_serv, 0, sizeof(ti.test_serv));

	ti.ctrl_serv.ai_family = ti.domain;			/* AF_INET or AF_INET6 */
	ti.test_serv.ai_family = ti.domain;

	ti.ctrl_serv.ai_socktype = SOCK_STREAM;		/* Stream of datagram */
	ti.test_serv.ai_socktype = SOCK_DGRAM;

	ti.ctrl_serv.ai_protocol = 0;				/* TCP or UDP - let OS chose */
	ti.test_serv.ai_protocol = 0;

	ti.ctrl_serv.ai_flags = AI_CANONNAME|AI_ADDRCONFIG;				/* Flags */
	ti.test_serv.ai_flags = AI_CANONNAME|AI_ADDRCONFIG;

	/* Get address information for control connection. If needed, we will get the addr info
	for test connection later (server won't know it has to setup UDP socket before handshake */

	ret = getaddrinfo(ti.serv_name, ti.ctrl_port_str, &ti.ctrl_serv, &ti.ctrl_ptr);
	if (ret != 0)
		raise_error("[ERROR]: No such host");
	

	/* We have got a linked list of addresses. Parse through it, create the socket and try to connet
	till we succeed */

	for (s = ti.ctrl_ptr; s != NULL; s = s->ai_next) {

		/* First create a socket */
		ti.ctrlsock = socket(s->ai_family, s->ai_socktype, s->ai_protocol);
		if (ti.ctrlsock == -1)
			continue;
		if (connect(ti.ctrlsock, s->ai_addr, s->ai_addrlen) == 0)
			break;
		close(ti.ctrlsock);
		}
	

	/* Call the function to start the tests. This function should take care of handshakes */

	perf_test();


	printf("[INFO]: Terminating client\n");
	close(ti.ctrlsock);
	exit(0);

	}





/* perf_test: This function first initiates the handshake and then calls the test function
	according to transport layer protocol to be used. It then receives the timing information
	from the server and the data received by the server to calculate throughput
*/


void perf_test () {

	long int sent = 0;
	long int sent_data = 0;
	long int rcvd_data = 0;
	char buff[BUFF_SIZE];
	int stat = 0;
	struct timespec start, start1, end;

	/* First we need to do initial handshake with the server.*/
	
	shake_hands();

	/* Register the start time before we send first packet. */
	clock_gettime(CLOCK_REALTIME, &start);

	/* Call appropriate test function */
	if (ti.t_prot == 1)
		sent_data = run_tcp_test();
	else if (ti.t_prot == 0)
		sent_data = run_udp_test();
	else
		raise_error("[ERROR]: Invalid transport layer protocol");

	/* We have sent all the data. Now wait for the server to send back the time when he received
	the last chunk. 
	I was hoping to transfer the timing info by simply transferring the raw timespec structure
	from server to client. But that is not working as expected. Client always receives 0. Hence
	now we convert the longs to strings and send */

	bzero(buff, BUFF_SIZE);
	stat = read(ti.ctrlsock, buff, BUFF_SIZE-1);		/* Receive the second information */
	if (stat < 0)
		raise_error("[ERROR]: Receiving the end timestamp failed");
	end.tv_sec = atol(buff);


	bzero(buff, BUFF_SIZE);
	stat = read(ti.ctrlsock, buff, BUFF_SIZE-1);		/* Receive the nanosecond information */
	if (stat < 0)
		raise_error("[ERROR]: Receiving the end timestamp failed");
	end.tv_nsec = atol(buff);




	/* Now receive info about how much data was actually received */

	bzero(buff, BUFF_SIZE);
	stat = read(ti.ctrlsock, buff, BUFF_SIZE-1); 
	if (stat < 0)
		raise_error("[ERROR]: Receiving the info about transmitted data failed");
	
	rcvd_data = atol(buff);
	printf("Actual tranmitted data: %ld, Received data: %ld\n",sent_data, rcvd_data);


	/* Now calculate the throughput */
	calc_throughput(rcvd_data, start, end);

	return;
	}





/* run_tcp_test: This function runs the test assuming TCP socket.
	It keeps on transmitting the data until we have sent enough data.
	It then returns how much data we sent */

long int run_tcp_test () {

	char buff[BUFF_SIZE];
	int stat = 0;
	long int sent = 0;

	printf("[INFO]: Starting the perf test with TCP\n");

	while (sent < ti.data_info) {

		stat = write(ti.testsock, buff, BUFF_SIZE-1);

		if (stat < BUFF_SIZE-1)
			raise_error("[ERROR]: Write on the socket failed");

		sent += stat;
		}

	return sent;
	}








/* run_udp_test: This function runs the test assuming UDP socket.
	It keeps on transmitting the data until we have sent enough data packets.
	It then returns how much data we sent */


long int run_udp_test() {

	char buff[BUFF_SIZE];
	int stat = 0;
	int sent = 0;			/* This is packet counter */
	long int sent_data = 0;	/* This is actual data bytes */
	
	printf("[INFO]: Starting the perf test with UDP\n");

	while (sent < ti.data_info) {

		stat = sendto(ti.testsock, buff, BUFF_SIZE-1, 0, ti.test_ptr->ai_addr, ti.test_ptr->ai_addrlen);
		if (stat < BUFF_SIZE-1)
			raise_error("[ERROR]: Write on the socket failed");

		sent++;
		sent_data += stat;
		}
	
	return sent_data;
	}










/* calc_throughput: This function receives the datasize and the start and end timestamps.
	It then calculates the throughput and displays on stdout */

void calc_throughput (long int data, struct timespec s, struct timespec e) {

	long double start = s.tv_sec * 1000000000 + s.tv_nsec;
	long double end = e.tv_sec * 1000000000 + e.tv_nsec;
	long double diff = end - start;

	if (diff <= 0) {
		fprintf(stderr,"[ERROR]: Clocks on server and client seem to be out of sync.\nRun ntpd on both and try again\n");
		exit(1);
		}

	data *= 8;
	diff /= 1000000;
	long double throughput = 0;
	throughput = (data * 1000) / (1024 * diff);

	/* Sadly this fomatting is giving trouble, no matter what format specifier I use */
	printf("\n\
	+-----------------+-------------------+---------------------+\n\
	| Datasize (bits) |   Time Taken (ms) |   Throughput (Kbps) |\n\
	+-----------------+-------------------+---------------------+\n\
	|    %10ld   |     %-5.6Lf  |   %8.8Lf   |\n\
	+-----------------+-------------------+---------------------+\n", data, diff, throughput);
	}







/* shake_hands: This function is to do initial handshake with the client and tell us how
	much data are we expecting.
	This should include the following:
		1. Send indication that client is ready for handshake
		2. Send information about the transport layer protocol
		3. Send information about the data size (bytes/packets)
		4*. Send confirmation that clock is synced on client (Not implemented)
		5. Receive server ready indicator */

void shake_hands () {

	int bsize = 256;
	char buff[256];

	int read_ele, wrote_ele;

	printf("[INFO]: Starting handshake with server\n");



	/* send "ready" to server */
	bzero(buff,bsize);
	strcpy(buff,"ready");

	wrote_ele = write(ti.ctrlsock, buff, 5);
	if (wrote_ele < 0)
		raise_error("[ERROR]: Write failed during handshake.");
	printf("[INFO]: Client ready for handshake\n");
	



	/* Now we need to tell the server about the transport layer protocol */
	bzero(buff,bsize);

	if (ti.t_prot == 1) {
		printf("[INFO]: Informing server this is TCP test\n");
		strcpy(buff,"TCP");
		}
	else if (ti.t_prot == 0) {
		printf("[INFO]: Informing server this is UDP test\n");
		strcpy(buff,"UDP");
		}
	else
		raise_error("[ERROR]: Invalid transport layer protocol");


	wrote_ele = write(ti.ctrlsock, buff, 3);
	if (wrote_ele < 0)
		raise_error("[ERROR]: Write failed during handshake.");






	/* send datasize string to server. This should not be more than
	10 characters (we are not expecting GBs of data or for UDP, send
	number of datagrams we are going to transmit. */
	bzero(buff,bsize);

	itoa(ti.data_info, buff);

	wrote_ele = write(ti.ctrlsock, buff, 10);
	if (wrote_ele < 0)
		raise_error("[ERROR]: Write failed during handshake.");
	printf("[INFO]: Sent data size information (%ld)\n",ti.data_info);
	

	/* Now depending on transport layer protocol to be used, we need to set
	the test socket descriptor */

	/* In case of TCP, we already have a connection */
	if (ti.t_prot == 1) {
		ti.testsock = ti.ctrlsock;
		ti.test_port = ti.ctrl_port;
		}
	
	/* For UDP, we need to create a UDP socket */
	else if (ti.t_prot == 0) {

		/* Now here, server should ideally be waiting for us to connect on a port. So attempt
		to connect. Keep in mind that server has to create a socket, bind, listen etc. So
		sleep a little before proceeding */

		sleep(1);
		int ret;

		ret = getaddrinfo(ti.serv_name, ti.test_port_str, &ti.test_serv, &ti.test_ptr);
		if (ret != 0)
			raise_error("[ERROR]: No such host for test");
	
		struct addrinfo * s;
	
		for (s = ti.test_ptr; s != NULL; s = s->ai_next) {

			/* First create a socket */
			ti.testsock = socket(s->ai_family, s->ai_socktype, s->ai_protocol);
			if (ti.testsock != -1) {
				ti.test_ptr = s;
				break;
				}

			close(ti.testsock);
			}

		if (s == NULL)
			raise_error("[ERROR]: Could not establish test connection with server");
		else
			printf("[INFO]: Found the UDP test server\n");

		/* Else wrong t_prot */
		}
	else
		raise_error("[ERROR]: Invalid transport layer protocol\n");


	/* We are here means we got the socket 
	Now receive "ready" from server */

	bzero(buff,bsize);
	read_ele = read(ti.ctrlsock, buff, 5);

	if (read_ele <=0 || (strcmp(buff,"ready") != 0) )
		raise_error("[ERROR]: Server not ready. Handshake failed");
	printf("[INFO]: Server ready for test\n");
	
	return;
	}










/* check_input: This function is to check that the input to this program are proper. This
	includes checking the number of arguments and their types */


void check_input (int c, char * v[]) {

	/* Not enough arguments */
	if (c < 6) {
		printf("Usage: %s [server] [port] [transport protocol] [network protocol] [datasize]\n\
		Where network protocol can be 4 (ipv4) or 6 (ipv6)\n\
		and transport protocol can be TCP or UDP (case sensitive)\n",v[0]);
		exit(1);
		}
	
	/* Port should not be a special port */
	if (atoi(v[2]) < 2000) {
		fprintf(stderr,"The input port must be greater than 2000");
		exit(1);
		}
	
	/* Transport layer protocol can be "UDP" or "TCP" */
	if ( strcmp(v[3],"UDP") != 0 && strcmp(v[3],"TCP") != 0) {
		fprintf(stderr,"Invalid transport layer protocol %s\n",v[3]);
		exit(1);
		}
	
	/* Network protocol as to be ipv4 or ipv6 (4/6) */
	if (atoi(v[4]) != 4 && atoi(v[4]) != 6) {
		fprintf(stderr,"Invalid network protocol number %d\n",atoi(v[4]));
		exit(1);
		}
	
	/* This constraint on datasize is maybe not needed */
	if (atoi(v[5]) <= 0 || atoi(v[5]) > 100000000) {
		fprintf(stderr,"Datasize should be between 1 byte to 100 Mb\n");
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
	NOTE: In case of 0, it will generate a C-string as "0\0". Returns the
	length of the C-String (1 for "0\0") */

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

