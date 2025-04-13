/* Start Header
*****************************************************************/
/*!
\file client_template.cpp
\author Neo Hui Zong (neo.h@digipen.edu)
\par Assignment 3
\date 17 Feb 2025
\brief
This file implements the client file which will be used to implement a
simple echo server.
Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

/*******************************************************************************
 * A simple TCP/IP client application
 ******************************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows.h"		// Entire Win32 API...
#include "winsock2.h"		// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()

 // Tell the Visual Studio linker to include the following library in linking.
 // Alternatively, we could add this file to the linker command-line parameters,
 // but including it in the source code simplifies the configuration.
#pragma comment(lib, "ws2_32.lib")

#include <iostream>			// cout, cerr
#include <string>			// string
#include <vector>
#include <map>
#include <thread>
#include <memory>
#include <fstream>
#include <queue>
#include <filesystem>

//#define WINSOCK_VERSION     2
#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1500 // Max text field length for my program
#define BUFFER				6 // Buffer which includes (1 bit for command, 4 bit for text field size, 1 bit for null terminator)
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

SOCKET UDP_socket;

std::string file_name;

enum CMDID {
	UNKNOWN = (unsigned char)0x0, // Not in use
	REQ_QUIT = (unsigned char)0x1,
	REQ_DOWNLOAD = (unsigned char)0x2,
	RSP_DOWNLOAD = (unsigned char)0x3,
	REQ_LISTFILES = (unsigned char)0x4,
	RSP_LISTFILES = (unsigned char)0x5,
	CMD_TEST = (unsigned char)0x20, // Not in use
	DOWNLOAD_ERROR = (unsigned char)0x30
};

enum message_id {
	DATA = (unsigned char)0x0,
	ACK = (unsigned char)0x1
};

bool execute(SOCKET clientSocket);
std::vector<std::pair<unsigned int, unsigned short>> ip_connected; // For user req
std::map<std::string, SOCKET> users_connected; // For echo req

std::vector<std::string> list_of_files; // list of files

struct file_data {
	std::string file_name;
	std::shared_ptr<char> data_pointer; // Pointing to actual file data
	size_t data_size; // Size of the data
};

std::map<std::string, file_data> file_repo; // Files to be downloaded

struct SocketFileDownload {
	SOCKET udp_socket;
	std::string file_to_be_downloaded;
	std::shared_ptr<char> data_pointer; // Contain actual file data
	int data_size; // Size of the data 
	ULONG destination_ip_net;
	USHORT destination_port_net;
	ULONG session_id;
};

std::vector<SocketFileDownload> list_of_socketfiledownload; // List of download sessions - 1 session is 1 download
std::queue<int> finish_queue; // Add the finished session ids in here

struct cmp_pair_seq_timeout {
	bool operator()(std::pair<ULONG, std::chrono::steady_clock::time_point> const& lhs, std::pair<ULONG, std::chrono::steady_clock::time_point> const& rhs) {
		return lhs.second < rhs.second;
	}
};

struct sender_state {
	int window; // How many packets to be sent
	int seq_base; // The start of the packet up to window - 1
	int seq_sent;
	std::queue <std::chrono::steady_clock::time_point> time_out_queue;
	//std::priority_queue <std::pair<ULONG, std::chrono::steady_clock::time_point>, std::vector<std::pair<ULONG, std::chrono::steady_clock::time_point>>, cmp_pair_seq_timeout()> time_out_queue;

};

struct receiver_state {
	int window; // How many packets to be sent
	int seq_base; // The start of the packet up to window - 1

};

struct SocketFileDownloadSender : public SocketFileDownload, sender_state {
	SocketFileDownloadSender(SOCKET udp, std::string file, std::shared_ptr<char> data_ptr, size_t data_size_var, ULONG dest_ip, USHORT dest_port, ULONG sess_id, int window_var) {
		udp_socket = udp;
		file_to_be_downloaded = file;
		data_pointer = data_ptr;
		data_size = data_size_var;
		destination_ip_net = dest_ip;
		destination_port_net = dest_port;
		session_id = sess_id;
		window = window_var;
	};

	constexpr static size_t BUFFER_SIZE = 1000;
	constexpr static int TIME_OUT = 100;
	char buffer[BUFFER_SIZE];

	// Session ID > Flag > Seq number > Data length > Data
	void send_seq(ULONG seq_to_send) {
		ULONG packet_size;
		ULONG remaining_data_size;

		ULONG session_id_net = htonl(session_id);
		buffer[4] = message_id::DATA; // Send data

		ULONG seq_to_send_net = htonl(seq_to_send);

		if (((seq_to_send + 1) * (BUFFER_SIZE - 13)) > data_size) {
			remaining_data_size = data_size - (seq_to_send * (BUFFER_SIZE - 13));
			ULONG remaining_data_size_net = htonl(remaining_data_size);
			memcpy(buffer + 9, &remaining_data_size_net, sizeof(ULONG));
			memcpy(buffer + 13, data_pointer.get() + (seq_to_send * (BUFFER_SIZE - 13)), remaining_data_size);
			packet_size = remaining_data_size + 13;
		}
		else {
			packet_size = BUFFER_SIZE;
			data_size = packet_size - 13;
			ULONG data_size_net = htonl(data_size);
			memcpy(buffer + 9, &data_size_net, sizeof(ULONG));
			memcpy(buffer + 13, data_pointer.get() + (seq_to_send * (BUFFER_SIZE - 13)), data_size);
		}

		memcpy(buffer, &session_id_net, sizeof(ULONG));
		memcpy(buffer + 5, &seq_to_send_net, sizeof(ULONG));

		sockaddr_in destination;
		destination.sin_family = AF_INET;
		destination.sin_addr.S_un.S_addr = destination_ip_net;
		destination.sin_port = destination_port_net;

		sendto(udp_socket, buffer, packet_size, 0, (sockaddr*)(&destination), sizeof(destination));

		time_out_queue.push(std::chrono::steady_clock::now());
	};

	ULONG receive_ack() {
		sockaddr_in destination;
		destination.sin_family = AF_INET;
		destination.sin_addr.S_un.S_addr = destination_ip_net;
		destination.sin_port = destination_port_net;

		int size_of_dest = sizeof(destination);

		int err = recvfrom(udp_socket, buffer, BUFFER_SIZE, 0, (sockaddr*)(&destination), &size_of_dest);

		if (err <= 0) {
			if (WSAGetLastError() == WSAEWOULDBLOCK) {
				return -2;
			}
			return -3;
		}

		if (buffer[4] == message_id::ACK) {
			ULONG seq = ntohl(*(ULONG*)(buffer + 5));
			return seq;
		}
		return -1;
	}

	void send() {
		ULONG max_seq_size = (data_size / (BUFFER_SIZE - 13)) + 1;
		seq_sent = 0;
		while (seq_base < max_seq_size) {
			for (; seq_sent < seq_base + window; ++seq_sent) {
				send_seq(seq_sent);
			}
			ULONG seq_ack = receive_ack();
			if (seq_ack > seq_base && seq_ack < ULONG(-3)) {
				for (; seq_base < seq_ack; seq_base++) {
					time_out_queue.pop();
				}
			}
			if (!time_out_queue.empty()) {
				auto time_first = time_out_queue.front();
				auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - time_first);
				if (time_diff.count() >= TIME_OUT) {
					seq_sent = seq_base;
					for (; !time_out_queue.empty(); ) {
						time_out_queue.pop();
					}
				}
			}

		};
	}
};

//struct SocketFileDownloadReceiver : public SocketFileDownload, receiver_state {
//	SocketFileDownloadReceiver(SOCKET udp, std::string file, std::shared_ptr<char> data_ptr, size_t data_size_var, ULONG source_ip, USHORT source_port, ULONG sess_id, int window_var) {
//		udp_socket = udp;
//		file_to_be_downloaded = file;
//		data_pointer = data_ptr;
//		data_size = data_size_var;
//		destination_ip_net = source_ip;
//		destination_port_net = source_port;
//		session_id = sess_id;
//
//		window = window_var;
//		seq_base = 0;
//	};
//
//	constexpr static size_t BUFFER_SIZE = 1000;
//	constexpr static int TIME_OUT = 100;
//	char buffer[BUFFER_SIZE];
//
//	void send_ack() {
//
//		ULONG session_id_net = htonl(session_id);
//		buffer[4] = message_id::ACK; // Check for ack
//
//		ULONG seq_base_net = htonl(seq_base);
//
//		memcpy(buffer, &session_id_net, sizeof(ULONG));
//		memcpy(buffer + 5, &seq_base_net, sizeof(ULONG));
//
//		sockaddr_in destination;
//		destination.sin_family = AF_INET;
//		destination.sin_addr.S_un.S_addr = destination_ip_net;
//		destination.sin_port = destination_port_net;
//
//		// Session ID (4) + Flag (1) + Sequence Number (4) = 9 
//		sendto(udp_socket, buffer, 9, 0, (sockaddr*)(&destination), sizeof(destination));
//	}
//
//	// Receive and process packets
//	bool receive_packet() {
//		sockaddr_in destination;
//		destination.sin_family = AF_INET;
//		destination.sin_addr.S_un.S_addr = destination_ip_net;
//		destination.sin_port = destination_port_net;
//
//		int size = sizeof(destination);
//
//		recvfrom(udp_socket, buffer, BUFFER_SIZE, 0, (sockaddr*)(&destination), &size);
//
//		ULONG seq_num = ntohl(*(ULONG*)(buffer + 5));
//
//		// If same sequence, extract message size and the message
//		if (seq_num == seq_base) {
//			ULONG message_size = ntohl(*(ULONG*)(buffer + 9));
//			memcpy(data_pointer.get() + (seq_num * (BUFFER_SIZE - 13)), buffer + 13, message_size);
//			seq_base++;
//			return true; // Return true if packet in order
//		}
//
//		return false;
//	}
//
//	//void receive() {
//	//	ULONG max_seq_size = (data_size / (BUFFER_SIZE - 13)) + 1;
//	//	while (seq_base < max_seq_size) {
//	//		receive_packet();
//	//		send_ack();
//	//	}
//
//	//	std::ofstream output_stream(file_to_be_downloaded, std::ios::binary);
//	//	output_stream.write(data_pointer.get(), data_size);
//	//}
//};


struct SocketFileDownloadReceiver : public SocketFileDownload, receiver_state {
	SocketFileDownloadReceiver(SOCKET udp, std::string file, std::shared_ptr<char> data_ptr, size_t data_size_var, ULONG source_ip, USHORT source_port, ULONG sess_id, int window_var) {
		udp_socket = udp;
		file_to_be_downloaded = file;
		data_pointer = data_ptr;
		data_size = data_size_var;
		destination_ip_net = source_ip;
		destination_port_net = source_port;
		session_id = sess_id;

		window = window_var;
		seq_base = 0;
		last_successful_receive_time = std::chrono::steady_clock::now();
	};

	constexpr static size_t BUFFER_SIZE = 1000;
	constexpr static int TIME_OUT = 100;
	constexpr static int MAX_RETRY_COUNT = 5;
	char buffer[BUFFER_SIZE];
	std::chrono::steady_clock::time_point last_successful_receive_time;
	int retry_count = 0;

	void send_ack() {
		// Clear buffer first to prevent data leakage
		memset(buffer, 0, 9); // Only clear what we need to send

		// Format ACK packet
		ULONG session_id_net = htonl(session_id);
		ULONG seq_base_net = htonl(seq_base);

		// Copy data safely
		memcpy(buffer, &session_id_net, sizeof(ULONG));
		buffer[4] = message_id::ACK;
		memcpy(buffer + 5, &seq_base_net, sizeof(ULONG));

		// Set up destination
		sockaddr_in destination;
		memset(&destination, 0, sizeof(destination));
		destination.sin_family = AF_INET;
		destination.sin_addr.S_un.S_addr = destination_ip_net;
		destination.sin_port = destination_port_net;

		// Send ACK with retry logic for reliability
		int send_result = sendto(udp_socket, buffer, 9, 0, (sockaddr*)(&destination), sizeof(destination));
		if (send_result == SOCKET_ERROR) {
			int error = WSAGetLastError();
			if (error != WSAEWOULDBLOCK) {
				std::cerr << "Send ACK failed with error: " << error << std::endl;
			}
		}
	}

	int receive_packet() {
		// Check for timeout since last successful receive

		auto current_time = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
			current_time - last_successful_receive_time).count();


		// If we haven't received anything for too long, try to recover
		if (elapsed > 3 && retry_count < MAX_RETRY_COUNT) {
			// Reset the socket if we've timed out
			std::cout << "Resetting connection due to timeout..." << std::endl;

			// Close and recreate the socket
			//closesocket(udp_socket);

			udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (udp_socket == INVALID_SOCKET) {
				std::cerr << "Failed to recreate socket: " << WSAGetLastError() << std::endl;
				return false;
			}

			// Bind to any available port
			sockaddr_in local_addr;
			memset(&local_addr, 0, sizeof(local_addr));
			local_addr.sin_family = AF_INET;
			local_addr.sin_addr.s_addr = INADDR_ANY;
			local_addr.sin_port = 0;  // Let system choose port

			if (bind(udp_socket, (sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
				std::cerr << "Failed to bind socket: " << WSAGetLastError() << std::endl;
				closesocket(udp_socket);
				return false;
			}

			// Set socket to non-blocking mode
			unsigned long non_blocking = 1;
			ioctlsocket(udp_socket, FIONBIO, &non_blocking);

			last_successful_receive_time = current_time;
			retry_count++;

			std::cout << "Socket reset complete. Retry " << retry_count << " of " << MAX_RETRY_COUNT << std::endl;
			return false;
		}

		// Clear buffer before receiving
		memset(buffer, 0, BUFFER_SIZE);

		// Set up source address
		sockaddr_in source;
		memset(&source, 0, sizeof(source));
		source.sin_family = AF_INET;
		int source_size = sizeof(source);

		// Receive data with error handling
		int received = recvfrom(udp_socket, buffer, BUFFER_SIZE, 0, (sockaddr*)(&source), &source_size);

		if (received <= 0) {
			int error = WSAGetLastError();
			if (error == WSAECONNRESET) {
				// Connection reset by peer - this is common in UDP when the remote host can't be reached
				// Just log it once and continue
				static bool logged_reset = false;
				if (!logged_reset) {
					std::cerr << "Connection reset detected. Will retry..." << std::endl;
					logged_reset = true;
				}
				return 1;
			}
			else if (error != WSAEWOULDBLOCK) {
				// Only log non-blocking errors once
				static int last_error = 0;
				if (error != last_error) {
					std::cerr << "recvfrom error: " << error << std::endl;
					last_error = error;
				}
			}
			else
			{
				return WSAEWOULDBLOCK;
			}
			return 1;
		}

		// Update the last successful receive time
		last_successful_receive_time = std::chrono::steady_clock::now();
		retry_count = 0;  // Reset retry counter on successful receive

		// Check if this packet is from our expected source
		// We'll be permissive here and accept packets from any source
		destination_ip_net = source.sin_addr.S_un.S_addr;
		destination_port_net = source.sin_port;

		// Basic validation
		if (received < 13) {
			return 1;
		}

		// Extract session ID and sequence number
		ULONG received_session_id = 0;
		ULONG seq_num = 0;

		memcpy(&received_session_id, buffer, sizeof(ULONG));
		received_session_id = ntohl(received_session_id);

		// Check if the session ID matches - be a bit permissive after resets
		if (received_session_id != session_id && retry_count == 0) {
			return 1;
		}

		// Update session ID if it changed after reset
		if (received_session_id != session_id && retry_count > 0) {
			session_id = received_session_id;
		}

		memcpy(&seq_num, buffer + 5, sizeof(ULONG));
		seq_num = ntohl(seq_num);

		// Check if this is the packet we're expecting
		if (seq_num == seq_base) {
			ULONG message_size = 0;
			memcpy(&message_size, buffer + 9, sizeof(ULONG));
			message_size = ntohl(message_size);

			// Limit message size to what we actually received
			size_t actual_message_size = (received - 13 < message_size) ? received - 13 : message_size;

			// Calculate destination offset
			size_t dest_offset = seq_num * (BUFFER_SIZE - 13);

			// Ensure we don't write past the allocated buffer
			if (dest_offset + actual_message_size <= data_size) {
				memcpy(data_pointer.get() + dest_offset, buffer + 13, actual_message_size);
				seq_base++;
				return 0;
			}
			else {
				std::cerr << "Buffer overflow prevented. Offset: " << dest_offset
					<< " Size: " << actual_message_size
					<< " Total: " << data_size << std::endl;
			}
		}

		return 1;
	}
};
constexpr size_t BUFFER_SIZE = 1000;
char buffer[BUFFER_SIZE];
bool close = false;

std::atomic<bool> running = true;
std::atomic<bool> stop_thread = false;
//void asyncInputHandler(SOCKET clientSocket) {
//	std::string input;
//	unsigned long long_pointer = 1;
//	// Receive commands
//	ioctlsocket(clientSocket, FIONBIO, &long_pointer); // Prevent recv from blocking
//	while (!stop_thread) {
//		int bytesReceived = recv(
//			clientSocket,
//			buffer,
//			MAX_STR_LEN - 1,
//			0);
//
//		if (buffer[0] == CMDID::RSP_DOWNLOAD)
//		{
//			//char clientIPAddr[MAX_STR_LEN];
//
//			//unsigned int client_addr_dest = *(reinterpret_cast<unsigned int*>(buffer + 1));
//			//unsigned short client_port_dest = ntohs(*(reinterpret_cast<unsigned short*>(buffer + 5)));
//			//uint32_t little_endian_length = ntohl(*(reinterpret_cast<u_long*>(&buffer[7])));
//			//inet_ntop(AF_INET, &client_addr_dest, clientIPAddr, MAX_STR_LEN);
//
//			//std::cout << "==========RECV START==========" << std::endl;
//			//std::cout << clientIPAddr << ":" << client_port_dest << std::endl;
//			//std::cout << std::string(buffer + 11, little_endian_length) << std::endl;
//			//std::cout << "==========RECV END==========" << std::endl;
//
//			//continue;
//
//			// Extract IP, Port, Session id
//			uint32_t ip = *(ULONG*)(buffer + 1);
//			uint16_t port = ntohs(*(USHORT*)(buffer + 5));
//			uint32_t session_id = ntohl(*(ULONG*)(buffer + 7));
//
//			// Extract File length
//			uint32_t file_length = ntohl(*(ULONG*)(buffer + 11));
//
//			// Construct the file details
//			file_data file_details;
//			file_details.file_name = file_name;
//			file_details.data_size = file_length;
//			file_details.data_pointer = std::shared_ptr<char>(new char[file_details.data_size]);
//			file_repo[file_name] = file_details;
//
//			SocketFileDownloadReceiver receiver(UDP_socket, file_details.file_name, file_details.data_pointer, file_details.data_size, ip, port, session_id, 200);
//
//			ULONG max_seq_size = (file_details.data_size / (BUFFER_SIZE - 13)) + 1;
//			while (receiver.seq_base < max_seq_size - 1) {
//				receiver.receive_packet();
//				std::cout << receiver.seq_base << '/' << max_seq_size << std::endl;
//				receiver.send_ack();
//			}
//
//			std::ofstream output_stream("a.txt", std::ios::binary);
//
//			if (output_stream) {
//				output_stream.write(file_details.data_pointer.get(), file_details.data_size);
//			}
//			
//
//			memset(buffer, 0, MAX_STR_LEN);
//			//uint32_t filename_length = ntohl(*(ULONG*)(buffer + 7));
//
//			//std::string file_name(buffer + 11, filename_length);
//		}
//
//		if (bytesReceived < 0) {
//			int error_code = WSAGetLastError();
//			//std::cout << error_code << std::endl;
//			if (error_code == WSAEWOULDBLOCK || error_code == 183) {
//				continue;
//			}
//			std::cout << error_code << std::endl;
//			stop_thread = true;
//			break;
//		}
//
//		if (buffer[0] == CMDID::RSP_LISTFILES) {
//			uint16_t num_of_files;
//			uint32_t total_length;
//
//			memcpy(&num_of_files, buffer + 1, sizeof(uint16_t));
//			num_of_files = ntohs(num_of_files);
//
//			memcpy(&total_length, buffer + 3, sizeof(uint32_t));
//			total_length = ntohl(total_length);
//
//			std::vector<std::string> file_list;
//			int offset = 7; // Start parsing filenames after header
//
//			for (int i = 0; i < num_of_files; ++i) {
//				uint32_t file_length;
//				memcpy(&file_length, buffer + offset, sizeof(uint32_t));
//				file_length = ntohl(file_length);
//				offset += 4;
//
//				std::string filename(buffer + offset, file_length);
//				file_list.push_back(filename);
//				offset += filename.length();
//			}
//
//			std::cout << "Available Files:" << std::endl;
//			for (const auto& file : file_list) {
//				std::cout << " - " << file << std::endl;
//			}
//
//		}
//
//		if (buffer[0] == CMDID::DOWNLOAD_ERROR) {
//			//break or continue;
//			std::cout << "File invalid" << std::endl;
//			continue;
//		}
//	}
//
//	if (stop_thread) {
//		std::cout << "disconection..." << std::endl;
//	}
//}
// Replace the asyncInputHandler function with this safer version
void asyncInputHandler(SOCKET clientSocket) {
	unsigned long non_blocking = 1;
	// Make socket non-blocking
	ioctlsocket(clientSocket, FIONBIO, &non_blocking);

	while (!stop_thread) {
		// Clear buffer before receiving new data
		memset(buffer, 0, BUFFER_SIZE); // init to 0

		int bytesReceived = recv(
			clientSocket,
			buffer,
			BUFFER_SIZE - 1,
			0); // TCP Recv

		if (bytesReceived < 0) {
			int error_code = WSAGetLastError();
			if (error_code == WSAEWOULDBLOCK || error_code == 183) {
				// Non-blocking socket would block, this is normal
				std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Wait for 10ms after socket receive no response
				continue;
			}
			std::cerr << "Socket error: " << error_code << std::endl; // Print error code
			stop_thread = true;
			break;
		}

		if (bytesReceived == 0) { // Graceful disconnection
			// Connection closed by server
			std::cout << "Server disconnected" << std::endl;
			stop_thread = true;
			break;
		}

		if (bytesReceived > 0) {
			// Process only if we received actual data

			// Handle RSP_DOWNLOAD
			if (buffer[0] == CMDID::RSP_DOWNLOAD) {
				// Validate minimum message size
				if (bytesReceived < 15) { // Minimum size for RSP_DOWNLOAD header
					std::cerr << "Received incomplete RSP_DOWNLOAD packet" << std::endl;
					continue;
				}

				try {
					// Extract IP, Port, Session id
					uint32_t ip = 0;
					uint16_t port = 0;
					uint32_t session_id = 0;
					uint32_t file_length = 0;

					// Copy data with proper bounds checking
					if (bytesReceived >= 5)
						memcpy(&ip, buffer + 1, sizeof(uint32_t));

					if (bytesReceived >= 7)
						port = ntohs(*(uint16_t*)(buffer + 5));

					if (bytesReceived >= 11)
						session_id = ntohl(*(uint32_t*)(buffer + 7));

					if (bytesReceived >= 15)
						file_length = ntohl(*(uint32_t*)(buffer + 11));

					std::cout << "Starting download of file: " << file_name << std::endl;
					std::cout << "File size: " << file_length << " bytes" << std::endl;

					// Construct the file details
					file_data file_details;
					file_details.file_name = file_name;
					file_details.data_size = file_length;

					// Allocate memory safely
					try {
						char* data_buffer = new char[file_length];
						// Zero the memory
						memset(data_buffer, 0, file_length);
						file_details.data_pointer = std::shared_ptr<char>(data_buffer, std::default_delete<char[]>());
					}
					catch (const std::bad_alloc& e) {
						std::cerr << "Memory allocation failed: " << e.what() << std::endl;
						continue;
					}

					// Store in the file repository
					file_repo[file_name] = file_details;

					// Set up the receiver with proper error handling
					// Set UDP socket to non-blocking mode
					ioctlsocket(UDP_socket, FIONBIO, &non_blocking);

					SocketFileDownloadReceiver receiver(UDP_socket, file_details.file_name,
						file_details.data_pointer, file_details.data_size,
						ip, port, session_id, 200);

					// Calculate total packets
					ULONG max_seq_size = (file_details.data_size / (BUFFER_SIZE - 13)) + 1;
					std::cout << "Total packets expected: " << max_seq_size << std::endl;

					// Set timeout for download
					auto start_time = std::chrono::steady_clock::now();
					const auto timeout_duration = std::chrono::seconds(60); // Increase timeout

					// Download loop with retry mechanism
					int consecutive_failures = 0;
					int retry_count = 0;
					const int MAX_RETRIES = 5;

					while (receiver.seq_base < max_seq_size && retry_count < MAX_RETRIES) {
						int received = receiver.receive_packet();
						if (received == 0) {
							consecutive_failures = 0;
							if (receiver.seq_base % 10 == 0 || receiver.seq_base == max_seq_size - 1) {
								std::cout << "Received packet " << receiver.seq_base << "/" << max_seq_size
									<< " (" << (receiver.seq_base * 100 / max_seq_size) << "%)" << std::endl;
							}
						}
						else if (received == WSAEWOULDBLOCK)
						{
							continue;
						}
						else {
							consecutive_failures++;

							// Send ACK more frequently during failures to ensure server gets it
							if (consecutive_failures % 5 == 0) {
								receiver.send_ack();
							}

							// If too many consecutive failures, try to reset the connection
							if (consecutive_failures > 100) {
								std::cout << "Too many consecutive failures. Resetting connection..." << std::endl;

								// Close and reopen the UDP socket
								closesocket(UDP_socket);

								UDP_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
								if (UDP_socket == INVALID_SOCKET) {
									std::cerr << "Failed to recreate UDP socket: " << WSAGetLastError() << std::endl;
									break;
								}

								// Rebind to the same local address
								sockaddr_in local_addr;
								memset(&local_addr, 0, sizeof(local_addr));
								local_addr.sin_family = AF_INET;
								local_addr.sin_addr.s_addr = INADDR_ANY;
								local_addr.sin_port = htons(0); // Let system assign port

								if (bind(UDP_socket, (sockaddr*)&local_addr, sizeof(local_addr)) != 0) {
									std::cerr << "Failed to bind UDP socket: " << WSAGetLastError() << std::endl;
									break;
								}

								// Set to non-blocking
								ioctlsocket(UDP_socket, FIONBIO, &non_blocking);

								// Update the receiver with the new socket
								receiver.udp_socket = UDP_socket;

								// Send ACK immediately after reset
								receiver.send_ack();

								consecutive_failures = 0;
								retry_count++;

								std::cout << "Connection reset. Retry " << retry_count << " of " << MAX_RETRIES << std::endl;
								std::this_thread::sleep_for(std::chrono::seconds(1));
							}
						}

						// Send ACK periodically (every 20 packets)
						if (receiver.seq_base % 20 == 0) {
							receiver.send_ack();
						}

						// Check for timeout
						auto current_time = std::chrono::steady_clock::now();
						if (current_time - start_time > timeout_duration) {
							std::cout << "Download timeout. Attempting retry..." << std::endl;
							retry_count++;
							if (retry_count < MAX_RETRIES) {
								// Reset timeout
								start_time = std::chrono::steady_clock::now();
								std::cout << "Retry " << retry_count << " of " << MAX_RETRIES << std::endl;
							}
							else {
								std::cout << "Maximum retries reached. Aborting download." << std::endl;
								break;
							}
						}

						// Brief sleep to prevent CPU thrashing
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}

					receiver.send_ack();

					// Check if download completed
					if (receiver.seq_base >= max_seq_size) {
						std::cout << "Download completed successfully." << std::endl;

						// Save file
						try {
							std::string output_file = file_name; // Use actual filename
							std::ofstream output_stream(output_file, std::ios::binary);

							if (output_stream) {
								output_stream.write(file_details.data_pointer.get(), file_details.data_size);
								output_stream.close();
								std::cout << "Downloaded file saved as: " << output_file << std::endl;
							}
							else {
								std::cerr << "Failed to create output file: " << output_file << std::endl;
							}
						}
						catch (const std::exception& e) {
							std::cerr << "Error saving file: " << e.what() << std::endl;
						}
					}
					else {
						std::cout << "Download incomplete. Received " << receiver.seq_base << " of "
							<< max_seq_size << " packets." << std::endl;
					}

				}
				catch (const std::exception& e) {
					std::cerr << "Error during file download: " << e.what() << std::endl;
				}
			}
			else if (buffer[0] == CMDID::RSP_LISTFILES && bytesReceived >= 7) {
				// Process file listing response (unchanged)
				try {
					uint16_t num_of_files = 0;
					uint32_t total_length = 0;

					memcpy(&num_of_files, buffer + 1, sizeof(uint16_t));
					num_of_files = ntohs(num_of_files);

					memcpy(&total_length, buffer + 3, sizeof(uint32_t));
					total_length = ntohl(total_length);

					std::vector<std::string> file_list;
					int offset = 7; // Start parsing filenames after header

					for (int i = 0; i < num_of_files && offset + 4 <= bytesReceived; ++i) {
						uint32_t file_length = 0;
						memcpy(&file_length, buffer + offset, sizeof(uint32_t));
						file_length = ntohl(file_length);
						offset += 4;

						// Ensure we don't read past the buffer
						if (offset + file_length > bytesReceived) {
							std::cerr << "File entry extends beyond received data" << std::endl;
							break;
						}

						std::string filename(buffer + offset, file_length);
						file_list.push_back(filename);
						offset += file_length;
					}

					std::cout << "Available Files:" << std::endl;
					for (const auto& file : file_list) {
						std::cout << " - " << file << std::endl;
					}
				}
				catch (const std::exception& e) {
					std::cerr << "Error processing file list: " << e.what() << std::endl;
				}
			}
			else if (buffer[0] == CMDID::DOWNLOAD_ERROR) {
				std::cout << "File download error: File invalid or not found" << std::endl;
			}
		}
	}

	std::cout << "Input handler thread exiting" << std::endl;
}

// This program requires one extra command-line parameter: a server hostname.
int main(int argc, char** argv)
{
	constexpr uint16_t port = 2048;

	// Get IP Address
	std::string host{};
	std::cout << "Server IP Address: ";
	std::getline(std::cin, host);

	std::cout << std::endl;

	// Get Port Number
	std::string portNumber;
	std::cout << "Server Port Number: ";
	std::getline(std::cin, portNumber);

	std::cout << std::endl;

	std::string portString = portNumber;

	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------

	// This object holds the information about the version of Winsock that we
	// are using, which is not necessarily the version that we requested.
	WSADATA wsaData{};
	SecureZeroMemory(&wsaData, sizeof(wsaData));

	// Initialize Winsock. You must call WSACleanup when you are finished.
	// As this function uses a reference counter, for each call to WSAStartup,
	// you must call WSACleanup or suffer memory issues.
	int errorCode = WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}

	// -------------------------------------------------------------------------
	// Resolve a server host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------

	// Object hints indicates which protocols to use to fill in the info.
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery
	// Could be 0 to autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP

	addrinfo* info = nullptr;
	errorCode = getaddrinfo(host.c_str(), portString.c_str(), &hints, &info);
	if ((NO_ERROR != errorCode) || (nullptr == info))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}

	// -------------------------------------------------------------------------
	// Create a socket and attempt to connect to the first resolved address.
	//
	// socket()
	// connect()
	// -------------------------------------------------------------------------

	SOCKET clientSocket = socket(
		info->ai_family,
		info->ai_socktype,
		info->ai_protocol);
	if (INVALID_SOCKET == clientSocket)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return RETURN_CODE_2;
	}

	errorCode = connect(
		clientSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (SOCKET_ERROR == errorCode)
	{
		std::cerr << "connect() failed." << std::endl;
		freeaddrinfo(info);
		closesocket(clientSocket);

	}

	char clientIPAddr[MAX_STR_LEN];
	struct sockaddr_in* clientAddress = reinterpret_cast<struct sockaddr_in*> (info->ai_addr);
	int len = sizeof(sockaddr_in);
	inet_ntop(AF_INET, &(clientAddress->sin_addr), clientIPAddr, INET_ADDRSTRLEN);
	getsockname(clientSocket, (sockaddr*)clientAddress, &len);

	UDP_socket = socket(
		hints.ai_family,
		SOCK_DGRAM,
		IPPROTO_UDP);

	errorCode = bind(
		UDP_socket,
		(sockaddr*)clientAddress,
		static_cast<int>(len));
	if (errorCode != NO_ERROR)
	{
		auto ec = WSAGetLastError();
		std::cerr << "bind() failed. EC: " << ec << std::endl;
		closesocket(UDP_socket);
		UDP_socket = INVALID_SOCKET;
	}

	freeaddrinfo(info);

	if (clientSocket == INVALID_SOCKET)
	{
		std::cerr << "bind() failed." << std::endl;
		WSACleanup();
		return 2;
	}

	std::thread inputThread(asyncInputHandler, clientSocket);

	// Get Command
	std::string command{};
	BYTE command_id = 0;
	while (std::getline(std::cin, command)) {


		// Quit if no slash
		if (command[0] != '/') {
			break;
		}

		// For request quit
		if (command == "/q") {
			command_id = CMDID::REQ_QUIT;
		}

		if (command_id == 1) {
			char arr[MAX_STR_LEN + BUFFER];
			arr[0] = command_id;
			uint32_t big_endian_length = htonl((unsigned long)(command.length()));
			memcpy(arr + 1, &big_endian_length, sizeof(uint32_t));
			send(clientSocket, arr, (int)(command.length() + 5), NULL);
			break;
		}

		// For request list files
		std::string command_l;
		if ((command_l = command.substr(0, 2)) == "/l") {
			command_id = CMDID::REQ_LISTFILES;
			send(clientSocket, (const char*)(&command_id), 1, 0);
		}


		// /d
		std::string command_d;
		if ((command_l = command.substr(0, 2)) == "/d") {
			command_id = CMDID::REQ_DOWNLOAD; // REQ_LISTUSERS
			// command = "/d <IP>:<PORT> <FILENAME>"
			size_t space_pos = command.find(' ', 3);
			if (space_pos == std::string::npos) {
				std::cerr << "Invalid command format. Expected: /d <IP>:<PORT> <FILENAME>" << std::endl;
				return 0;
			}
			std::string address_port = command.substr(3, space_pos - 3);
			file_name = command.substr(space_pos + 1);


			size_t colon_pos = address_port.find(':');
			std::string ip = address_port.substr(0, colon_pos);
			int port = std::stoi(address_port.substr(colon_pos + 1));

			buffer[0] = CMDID::REQ_DOWNLOAD;

			// Convert IP Address
			struct in_addr ip_addr;
			inet_pton(AF_INET, ip.c_str(), &ip_addr);
			// 4 bytes for IP
			memcpy(buffer + 1, &ip_addr, 4);

			// Convert port to network byte order
			uint16_t net_port = htons(port);
			memcpy(buffer + 5, &net_port, 2); // 2 bytes for port

			// Filename length
			uint32_t filename_length = htonl(file_name.length());
			memcpy(buffer + 7, &filename_length, 4); // 4 bytes for filename length

			// Filename
			memcpy(buffer + 11, file_name.c_str(), file_name.length()); // Variable length file name

			// Send completed message
			size_t message_size = 11 + file_name.length();
			send(clientSocket, (const char*)buffer, message_size, 0);

		}
	}
	stop_thread = true;
	inputThread.join();

	errorCode = shutdown(clientSocket, SD_SEND);
	if (SOCKET_ERROR == errorCode)
	{
		std::cerr << "shutdown() failed." << std::endl;
	}
	closesocket(clientSocket);

	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------

	WSACleanup();
	return 0;
}
