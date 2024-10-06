/*
This is the source file for the C++ ostream class for writing a file on a remote system via an IP socket connection.
Details are explained on GitHub: https://github.com/viktor-nikolov/lwIP-file-via-socket

Tested (and ready for compilation) on FreeRTOS on AMD Xilinx Zynq SoC (Vitis 2023.1 toolchain),
Windows 11 (MinGW toolchain) and Ubuntu 22.04 (gcc toolchain).

BSD 2-Clause License:

Copyright (c) 2024 Viktor Nikolov

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "FileViaSocket.h"
#include <cstring>

#ifdef __WIN32__
#   include <winsock2.h>
#   define SHUTDOWN_HOW_BOTH SD_BOTH   // We pass this as a parameter to function shutdown()
#elif defined(__linux__)
#   include <sys/socket.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#   define SHUTDOWN_HOW_BOTH SHUT_RDWR // We pass this as a parameter to function shutdown()
#else // If not Windows nor Linux, we assume FreeRTOS with lwIP
/* When using lwIP and ostream together, we face an issue with two different definitions of errno.
 * lwIP defines errno as a global variable in lwip/errno.h. lwIP functions set this global variable.
 * However, when we include header ostream it takes with it sys/errno.h
 * (for example Xilinx/Vitis/2023.1/gnu/aarch32/nt/gcc-arm-none-eabi/aarch32-xilinx-eabi/usr/include/errno.h),
 * which defines errno differently (as an attribute of global reentrancy structure, returned by function).
 * By un-defining macro "errno" defined in sys/errno.h, we get access to the global variable errno defined
 * in  lwip/errno.h. */
#undef errno

/* Following macros are defined in lwip/errno.h differently from sys/errno.h. We are un-defining
 * them to avoid compiler warning (there is no other way to disable this warning).
 * sys/errno.h was included from within header ostream. */
#   undef ENAMETOOLONG
#   undef EDEADLK
#   undef ENOLCK
#   undef ENOSYS
#   undef ENOTEMPTY
#   undef ELOOP
#   undef ENOMSG
#   undef EIDRM
#   undef EMULTIHOP
#   undef EBADMSG
#   undef EOVERFLOW
#   undef EILSEQ
#   undef ENOTSOCK
#   undef EDESTADDRREQ
#   undef EMSGSIZE
#   undef EPROTOTYPE
#   undef EPROTONOSUPPORT
#   undef EAFNOSUPPORT
#   undef EADDRINUSE
#   undef EADDRNOTAVAIL
#   undef ENETDOWN
#   undef ENETUNREACH
#   undef ENETRESET
#   undef ECONNABORTED
#   undef EISCONN
#   undef ENOTCONN
#   undef ETOOMANYREFS
#   undef ETIMEDOUT
#   undef EHOSTDOWN
#   undef EHOSTUNREACH
#   undef EALREADY
#   undef EINPROGRESS
#   undef ESTALE
#   undef EDQUOT
#   undef ENOPROTOOPT

#   include "lwip/sockets.h"
#   define SHUTDOWN_HOW_BOTH SHUT_RDWR  // We pass this as a parameter to function shutdown()
#   define INADDR_NONE IPADDR_NONE      // lwIP doesn't provide macro INADDR_NONE
#   undef close  // Macro "close" defined in lwip/sockets.h messes with our methods named "close"
#endif

void SocketBuffer::open( const std::string &serverIP, unsigned short port )
{
	if( Socket >= 0 ) { // We still have an open socket from before
		close();
		Socket = -1;
	}

	// Create socket
	if( (Socket = socket(AF_INET, SOCK_STREAM, 0 )) < 0 )
#ifdef __WIN32__
		throw FileViaSocket::SocketCreationErrorExc( WSAGetLastError() );
#else
		throw FileViaSocket::SocketCreationErrorExc( errno );
#endif

	struct sockaddr_in serv_addr = {};
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	// Convert IPv4 and IPv6 addresses from text to binary form
	serv_addr.sin_addr.s_addr = inet_addr( serverIP.c_str() );
	if( serv_addr.sin_addr.s_addr == INADDR_NONE ) {
		std::string m{"Server IP was provided in a wrong format '" + serverIP + "'!"};
		throw FileViaSocket::WrongServerIPFormatExc( m );
	}

	// Connect to the server
	if( connect(Socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0 )
#ifdef __WIN32__
		throw FileViaSocket::SocketConnectionErrorExc( WSAGetLastError() );
#else
		throw FileViaSocket::SocketConnectionErrorExc( errno );
#endif
} // SocketBuffer::open

void SocketBuffer::close()
{
	if( Socket >= 0 ) {
		sync(); // Write remaining data from the buffer to the socket
		shutdown(Socket, SHUTDOWN_HOW_BOTH); // Gracefully closing the socket

#ifdef __WIN32__
		closesocket( Socket ); // Calling Winsock2 function for closing the socket
#elif defined(__linux__)
		::close( Socket );     // Calling the global library function for closing the file descriptor
#else // If not Windows nor Linux, we assume FreeRTOS with lwIP
		lwip_close( Socket );
#endif
		Socket = -1;
	}
} // SocketBuffer::close

int SocketBuffer::overflow( int c ) {
	if( Socket < 0 )
		return traits_type::eof();

	if ( c == traits_type::eof() ) {
		sync();
		return 1; // Success
	}

	if( bytesInBuffer == SOCKET_BUFF_SIZE-1 ) { // This character fills the buffer
		buffer[ bytesInBuffer ] = char(c);

		if( send(Socket, buffer, SOCKET_BUFF_SIZE, 0) != SOCKET_BUFF_SIZE )
			return traits_type::eof(); // Failure
		bytesInBuffer = 0;
	}
	else { // There is space in the buffer for more characters
		buffer[ bytesInBuffer ] = char(c);
		bytesInBuffer++;
	}

	return 1; // Success
} // SocketBuffer::overflow

std::streamsize SocketBuffer::xsputn( const char_type* s, std::streamsize n )
{
	if( Socket < 0 )
		return 0; // Failure

	if( bytesInBuffer + n >= SOCKET_BUFF_SIZE ) { // Data won't fit in the buffer; we need to send data to the socket
		std::streamsize bytesConsumed{0}; // Number of bytes we already consumed form s

		// If we have data in the buffer, we fill the buffer to be full and send it
		if( bytesInBuffer > 0 ) {
			memcpy( buffer + bytesInBuffer, s, SOCKET_BUFF_SIZE - bytesInBuffer );
			bytesConsumed = SOCKET_BUFF_SIZE - bytesInBuffer;
			if( send(Socket, buffer, SOCKET_BUFF_SIZE, 0) != SOCKET_BUFF_SIZE )
				return 0; // Failure
		}

		// Now send all data from s, which would not fit in the buffer
		std::streamsize n2 = ( n - bytesConsumed ) - ( n - bytesConsumed ) % SOCKET_BUFF_SIZE;
		if( n2 > 0 ) { // Is there something to send?
			if( send(Socket, s + bytesConsumed, n2, 0) != n2 )
				return bytesConsumed; // Failure
			bytesConsumed += n2;
		}

		if( bytesConsumed < n ) { // We store data, which are still remaining, in the buffer
			memcpy( buffer, s + bytesConsumed, n - bytesConsumed );
			bytesInBuffer = n - bytesConsumed;
		} else {
			bytesInBuffer = 0; // Otherwise, we are left with an empty buffer
		}
	}
	else { // Data still fit in the buffer
		memcpy( buffer + bytesInBuffer, s, n );
		bytesInBuffer += n;
	}
	return n; // Success
} // SocketBuffer::xsputn

int SocketBuffer::sync()
{
	if( bytesInBuffer == 0 ) // No data to send
		return 0; // Success

	if( Socket < 0 )
		return -1; // Failure

	if( send(Socket, buffer, bytesInBuffer, 0) != bytesInBuffer )
		return -1; // Failure
	bytesInBuffer = 0;

	return 0; // Success
} // SocketBuffer::sync

FileViaSocket::SocketCreationErrorExc::SocketCreationErrorExc( int errCode )
{
#ifdef __WIN32__
	message = "Socket creation error! WSAGetLastError() == " + std::to_string(errCode);
#else
	message = "Socket creation error! errno == " + std::to_string(errno);
#endif
} // FileViaSocket::SocketCreationErrorExc

FileViaSocket::SocketConnectionErrorExc::SocketConnectionErrorExc( int errCode )
{
#ifdef __WIN32__
	message = "Socket connection error! WSAGetLastError() == " + std::to_string(errCode);
	if( errCode == WSAECONNREFUSED )
		message += " (connection refused; is server running?)";
	else if( errCode == WSAETIMEDOUT )
		message += " (connection timed out; is server accessible?)";
#else
	message = "Socket connection error! errno == " + std::to_string(errno);

	switch (errCode) {
		case ECONNREFUSED: // On Linux this error is raised when server is not running
			               // on the target host.
			message += " (connection refused; is server running?)";
			break;
		case ETIMEDOUT:
			message += " (connection timed out; is server accessible?)";
			break;
		case ECONNRESET:   // On lwIP this error is raised when server is not running
			               // on the target host.
			message += " (connection reset by peer; is server running?)";
			break;
		case ECONNABORTED: // On lwIP this error is raised instead of ETIMEDOUT
			message += " (SW caused connection abort; is server accessible?)";
			break;
	}
#endif
} // FileViaSocket::SocketConnectionErrorExc
