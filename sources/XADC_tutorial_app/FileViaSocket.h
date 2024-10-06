/*
This is the header file for the C++ ostream class for writing a file on a remote system via an IP socket connection.
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
#ifndef FILEVIASOCKET_H
#define FILEVIASOCKET_H

#include <ostream>
#include <exception>

/* SocketBuffer is the streambuf class which is used by the FileViaSocket ostream class.
 * All logic of sending data over an IP socket is implemented in this class. */
class SocketBuffer : public std::streambuf {
public:
	/* SOCKET_BUFF_SIZE is length of the array we use as buffer before sending the data via the socket.
	 * Ideally it should be equal to the max. number of bytes sent in a TCP packet.
	 * I tested using Wireshark that on FreeRTOS on Xilinx Zynq (using lwIP 2.1.3) 1446 bytes of data are sent
	 * in one TCP packet. On Ubuntu 22.04 it's 1448 bytes and on Windows 11 it's 1460 bytes of data. */
#ifdef __WIN32__
	static int const SOCKET_BUFF_SIZE = 1460;
#elif defined(__linux__)
	static int const SOCKET_BUFF_SIZE = 1448;
#else // If not Windows nor Linux, we assume FreeRTOS with lwIP
	static int const SOCKET_BUFF_SIZE = 1446;
#endif

	SocketBuffer() : std::streambuf() {}
	SocketBuffer( const std::string &serverIP, unsigned short port ) : std::streambuf() {
		open( serverIP, port );
	}
	~SocketBuffer() override { close(); }

	void open( const std::string &ip, unsigned short port );
	void close();

protected:
	/* This method is called when ostream wants to write one character
	 * or to explicitly flush the buffer.*/
	int overflow( int c ) override;

	/* This method is called when ostream wants to write a sequence of characters. */
	std::streamsize xsputn( const char_type* s, std::streamsize n ) override;

	/* This method is called when ostream wants to explicitly flush the buffer. */
	int sync() override;

private:
	int Socket = -1;  // The IP socket file descriptor; value <0 means that the socket is closed
	char buffer[SOCKET_BUFF_SIZE] = {}; // Buffer for writes to the socket
	int bytesInBuffer{0};               // Number of bytes stored in the buffer
}; //class SocketBuffer

/* FileViaSocket is a simple descendant of ostream.
 * It is set to use our SocketBuffer streambuf. */
class FileViaSocket : public std::ostream {
public:
	FileViaSocket() : std::ostream( &Buff ) {}
	FileViaSocket( const std::string &serverIP, unsigned short port ) : std::ostream( &Buff ) {
		Buff.open( serverIP, port );
	}

	void open( const std::string &ip, unsigned short port ) {
		Buff.open( ip, port );
	}
	void close() {
		Buff.close();
	}

protected:
	SocketBuffer Buff;

public:
	/***** Definition of exceptions specific to the FileViaSocket *****/
	class WrongServerIPFormatExc : public std::exception {
	public:
		WrongServerIPFormatExc() : message("Server IP was provided in a wrong format!") {}
		explicit WrongServerIPFormatExc( std::string &m ) : message(m) {}

		// Override the what() method to return a custom error message
		[[nodiscard]] const char* what() const noexcept override {
			return message.c_str();
		}
	private:
		std::string message;
	};
	class SocketCreationErrorExc : public std::exception {
	public:
		explicit SocketCreationErrorExc( int errCode );
		[[nodiscard]] const char* what() const noexcept override {
			return message.c_str();
		}
	private:
		std::string message;
	};
	class SocketConnectionErrorExc : public std::exception {
	public:
		explicit SocketConnectionErrorExc( int errCode );
		[[nodiscard]] const char* what() const noexcept override {
			return message.c_str();
		}
	private:
		std::string message;
	};
}; //class FileViaSocket


#endif //FILEVIASOCKET_H
