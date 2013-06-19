ipCompete
=========

This is my attempt to write a small tool for comparing the performance of IPv4 and IPv6 with TCP and UDP.

The tool is split into two parts

1. Client program
2. Server program


Client Program
---------------

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





Server Program
---------------

Server listens on a port using TCP socket and waits for client to connect.
The TCP port is according to the network protocol specified. After the client
connects, the server and the client program do handshake from which server
learns how much data will the client send, what transport layer protocol to use.
If transport layer protocol is UDP, server opens a new UDP socket on same port
number where he is listening. For TCP, it just uses the same connection. 
After this, the test is performed on appropriate connection and then the server
sends the information to client which includes when it received the last data
chunk and the total data it received.

	Usage: ./s_perf [port] [network protocol] 

	Where 
		network protocol can be 4 (ipv4) or 6 (ipv6)
