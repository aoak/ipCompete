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
