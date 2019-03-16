# CS118 Project 2

## Makefile

This provides a couple make targets for things.
By default (all target), it makes the `server` and `client` executables.

It provides a `clean` target, and `tarball` target to create the submission file as well.

You will need to modify the `Makefile` to add your userid for the `.tar.gz` turn-in at the top of the file.

## Provided Files

`server.cpp` and `client.cpp` are the entry points for the server and client part of the project.

## Academic Integrity Note

You are encouraged to host your code in private repositories on [GitHub](https://github.com/), [GitLab](https://gitlab.com), or other places.  At the same time, you are PROHIBITED to make your code for the class project public during the class or any time after the class.  If you do so, you will be violating academic honestly policy that you have signed, as well as the student code of conduct and be subject to serious sanctions.

## Wireshark dissector

For debugging purposes, you can use the wireshark dissector from `tcp.lua`. The dissector requires
at least version 1.12.6 of Wireshark with LUA support enabled.

To enable the dissector for Wireshark session, use `-X` command line option, specifying the full
path to the `tcp.lua` script:

    wireshark -X lua_script:./confundo.lua

To dissect tcpdump-recorded file, you can use `-r <pcapfile>` option. For example:

    wireshark -X lua_script:./confundo.lua -r confundo.pcap

## Project Report

Nathan March\
UID: 404827938

Kevin Liu\
UID: 504862375

John Kujawski\
UID: 604835032

------

### High Level Design
#### Server
Server begins by parsing arguments and setting up signal handlers for SIGQUIT and SIGTERM signals. The server then binds to the designated port and begins listening (with the recvfrom function) to incoming packets on that port. It conducts a handshake and can begin saving the incoming file from a given client.

#### Client
Client begin my taking that command arguments and parsing them for correctness as well as establishing a socket to be used for sending. After this has occurred, it starts to handle the data transfer to the server indicated. Client uses a couple timeval structs to keep track of current time and last time that a packet was received to implement a 10 second timeout for receiveing any data throught the socket. A receive timeout is also placed on all recvfrom() calls which prevents them from blocking for more than 0.5 seconds. When the 3 way handshake is established, the client enters a series of while loops which handle sending the actual data. Afterwards, the connection breakdown establishes and the client waits 2 seconds before gracefully exiting.

### Problems
In the beginning, we really struggled with getting a basic UDP connection due to pointer errors.
When trying to implement the Packet class, we ran into issues with the payload being incorrectly copied. Adding additional amperands solved the issue. Another minor issue was that we originally stored the payload as uint32_t and did not notice for quite some time. Changing it to uint8_t solved the problem.
A major problem we had was towards the final stages of development, we noticed the congestion window was being increment incorrectly. The packets were sending correctly; however, cwnd was being incremented too soon.

### Additional Libraries
None, other than standard C libraries and a custom packet class.
<queue>
"packet.h"

### Sources
Man pages for all functions used.

Beej's Guide to Network Programming
	[https://beej.us/guide/bgnet/html/multi/htonsman.html?fbclid=IwAR0MA7zwVFbi6KdzleiZoqhvy7PnexC5uG7U8MQAR52LqWfsk-PRygXokSM]
Constructor Initializer Lists
	[https://www.geeksforgeeks.org/when-do-we-use-initializer-list-in-c/]
Timeout for receive
	[https://stackoverflow.com/questions/16163260/setting-timeout-for-recv-fcn-of-a-udp-socket/16163536]
UDP Server-Client Implementation
	[https://www.geeksforgeeks.org/udp-server-client-implementation-c/?fbclid=IwAR16efaltlerlprx7PjPCW6aBcAf7ohXXZQglfqQMatpsUL_61q8a1e7Ibg]
UDP Sockets
	[https://www.cs.rutgers.edu/~pxk/417/notes/sockets/udp.html]