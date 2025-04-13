/* Start Header
*****************************************************************/
/*!
\file Client.cpp
\author Edwin Lee Zirui (edwinzirui.lee\@digipen.edu)
\par CSD2161 - Computer Networks
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
#include <mutex>

//#define WINSOCK_VERSION     2
#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1500 // Max text field length for my program
#define BUFFER				6 // Buffer which includes (1 bit for command, 4 bit for text field size, 1 bit for null terminator)
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

SOCKET UDP_socket;

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
	DATA = (unsigned char)0x1,
	ACK = (unsigned char)0x2
};

std::mutex filename_mutex;
std::string file_name;


//bool execute(SOCKET clientSocket);
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
	size_t data_size; // Size of the data 
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
	ULONG seq_base; // The start of the packet up to window - 1
	ULONG seq_sent;
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

	

	
};

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
	};

	constexpr static size_t BUFFER_SIZE = 1400;
	constexpr static int TIME_OUT = 100;
	char buffer[BUFFER_SIZE];

	void send_ack() {

		ULONG session_id_net = htonl(session_id);
		SecureZeroMemory(buffer, BUFFER_SIZE); // Clear buffer before sending ack
		buffer[0] = message_id::ACK;

		ULONG seq_base_net = htonl(seq_base-1);

		memcpy(buffer+1, &session_id_net, sizeof(ULONG));
		memcpy(buffer + 5, &seq_base_net, sizeof(ULONG));
		uint32_t data = 0;
		memcpy(buffer + 9, &data, sizeof(ULONG));
		memcpy(buffer + 13, &data, sizeof(ULONG));

		sockaddr_in destination;
		destination.sin_family = AF_INET;
		destination.sin_addr.S_un.S_addr = destination_ip_net;
		char IPVerification[2050];
		inet_ntop(AF_INET, &destination_ip_net, IPVerification, sizeof(IPVerification));
		// Session ID (4) + Flag (1) + Sequence Number (4) = 9
		u_short pV = ntohs(destination_port_net);
		destination.sin_port = pV;
		std::cout << "Sending ACK to " << IPVerification << ":" << ntohs(destination.sin_port) << " with seq " << seq_base-1 << std::endl;

		int status = sendto(udp_socket, buffer, 17, 0, (sockaddr*)(&destination), sizeof(destination));
		if (status < 0)
		{
			std::cout << "Send Error: " << WSAGetLastError() << std::endl;
		}
	}

	// Receive and process packets
	bool receive_packet() {
		sockaddr_in destination;
		destination.sin_family = AF_INET;
		destination.sin_addr.S_un.S_addr = destination_ip_net;
		destination.sin_port = destination_port_net;

		int size = sizeof(destination);

		int bytesReceived = recvfrom(udp_socket, buffer, BUFFER_SIZE, MSG_PEEK, (sockaddr*)(&destination), &size);

		// Handle errors and empty packets
		if (bytesReceived <= 0) {
			int error = WSAGetLastError();
			if (error == WSAEWOULDBLOCK) {
				// No data available - not an error
				return false;
			}

			// Log actual errors
			if (error != WSAEWOULDBLOCK) {
				std::cerr << "Error receiving packet: " << error << std::endl;
			}
			return false;
		}

		// Check if packet is too small to contain header
		if (bytesReceived < 13) {
			std::cerr << "Received packet too small: " << bytesReceived << " bytes" << std::endl;
			return false;
		}

		// Verify session ID
		ULONG received_session_id = ntohl(*(ULONG*)(buffer+1));
		if (received_session_id != session_id) {
			// Not our session
			return false;
		}

		SecureZeroMemory(buffer, BUFFER_SIZE); // Clear buffer before receiving actual data
		int actualBytesRecv = recvfrom(udp_socket, buffer, BUFFER_SIZE, 0, (sockaddr*)(&destination), &size);

		

		// Check if it's a DATA packet
		if (buffer[0] != message_id::DATA) {
			// Not a data packet
			return false;
		}
		// Extract sequence number
		ULONG seq_num = ntohl(*(ULONG*)(buffer + 5));
		std::cout << "Received packet with seq_num: " << seq_num << std::endl;

		// If same sequence as expected, extract message size and data
		if (seq_num == seq_base) {
			ULONG message_size = ntohl(*(ULONG*)(buffer + 9));

			// Validate message size
			if (message_size > (BUFFER_SIZE - 13) || message_size > static_cast<ULONG>(bytesReceived - 13)) {
				std::cerr << "Invalid message size: " << message_size << std::endl;
				return false;
			}

			// Copy data to the right position in our buffer
			memcpy(data_pointer.get() + (seq_num * (BUFFER_SIZE - 17)), buffer + 17, message_size);
			seq_base++;
			return true; // Return true if packet in order
		}

		return false;
	}

	void receive() {
		ULONG max_seq_size = static_cast<ULONG>((data_size / (BUFFER_SIZE - 17)) + 1);

		// Add a timeout mechanism for the entire download
		auto start_time = std::chrono::steady_clock::now();
		const std::chrono::seconds timeout(60); // 60 second timeout

		while (static_cast<ULONG>(seq_base) < max_seq_size) {
			// Check for timeout
			auto current_time = std::chrono::steady_clock::now();
			if (current_time - start_time > timeout) {
				std::cerr << "Download timed out after 60 seconds" << std::endl;
				return;
			}

			bool received = receive_packet();
			if (received) {
				send_ack();
				// Reset start_time on successful packet
				start_time = std::chrono::steady_clock::now();
			}
			else {
				// Small sleep to prevent CPU hogging
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		// Save file to disk
		try {
			std::ofstream output_stream(file_to_be_downloaded, std::ios::binary);
			if (!output_stream) {
				std::cerr << "Failed to open file for writing: " << file_to_be_downloaded << std::endl;
				return;
			}
			output_stream.write(data_pointer.get(), data_size);
			output_stream.close();

			std::cout << "File downloaded successfully: " << file_to_be_downloaded << std::endl;
		}
		catch (const std::exception& e) {
			std::cerr << "Error writing file: " << e.what() << std::endl;
		}
	}
};

constexpr size_t BUFFER_SIZE = 2000;
char buffer[BUFFER_SIZE];
bool close = false;

std::atomic<bool> running = true;
std::atomic<bool> stop_thread = false;

void asyncInputHandler(SOCKET clientSocket, const std::string& downloadPath) {
	std::string input;
	unsigned long long_pointer = 1;
	// Receive commands
	ioctlsocket(clientSocket, FIONBIO, &long_pointer); // Prevent recv from blocking
	while (!stop_thread) {
		int bytesReceived = recv(
			clientSocket,
			buffer,
			MAX_STR_LEN - 1,
			0);

		if (bytesReceived <= 0) {
			int error_code = WSAGetLastError();
			if (error_code == WSAEWOULDBLOCK) {
				continue;
			}
			stop_thread = true;
			break;
		}

		if (buffer[0] == CMDID::RSP_DOWNLOAD) {
			// Extract server information
			uint32_t server_ip = *(reinterpret_cast<uint32_t*>(buffer + 1));
			uint16_t server_port = ntohs(*(reinterpret_cast<uint16_t*>(buffer + 5)));
			uint32_t session_id = ntohl(*(reinterpret_cast<uint32_t*>(buffer + 7)));
			uint32_t file_size = ntohl(*(reinterpret_cast<uint32_t*>(buffer + 11)));

			char serverIPAddr[MAX_STR_LEN];
			inet_ntop(AF_INET, &server_ip, serverIPAddr, MAX_STR_LEN);

			std::cout << "==========DOWNLOAD INFO==========" << std::endl;
			std::cout << "Server IP: " << serverIPAddr << std::endl;
			std::cout << "Server Port: " << server_port << std::endl;
			std::cout << "Session ID: " << session_id << std::endl;
			std::cout << "File Size: " << file_size << " bytes" << std::endl;
			std::cout << "====================================" << std::endl;

			// Allocate memory for the file
			std::shared_ptr<char> fileData(new char[file_size], std::default_delete<char[]>());
			std::string filename;
			{
				std::lock_guard<std::mutex> lock(filename_mutex);
				filename = file_name;
			}
			std::string fullFilePath = downloadPath + "/" + filename;

			// Create a receiver to handle the file download
			SocketFileDownloadReceiver receiver(UDP_socket, fullFilePath, fileData, file_size,
				server_ip, server_port, session_id, 64);

			// Start the download process in a separate thread
			std::thread downloadThread(&SocketFileDownloadReceiver::receive, receiver);
			downloadThread.detach();  // Let it run independently

			std::cout << "Download started..." << std::endl;
		}

		if (buffer[0] == CMDID::RSP_LISTFILES) {
			uint16_t num_of_files;
			uint32_t total_length;

			memcpy(&num_of_files, buffer + 1, sizeof(uint16_t));
			num_of_files = ntohs(num_of_files);

			memcpy(&total_length, buffer + 3, sizeof(uint32_t));
			total_length = ntohl(total_length);

			std::vector<std::string> file_list;
			int offset = 7; // Start parsing filenames after header

			for (int i = 0; i < num_of_files; ++i) {
				uint32_t file_length;
				memcpy(&file_length, buffer + offset, sizeof(uint32_t));
				file_length = ntohl(file_length);
				offset += 4;

				std::string filename(buffer + offset, file_length);
				file_list.push_back(filename);
				offset += static_cast<int>(filename.length());
			}

			std::cout << "Available Files:" << std::endl;
			for (const auto& file : file_list) {
				std::cout << " - " << file << std::endl;
			}

		}

		if (buffer[0] == CMDID::DOWNLOAD_ERROR) {
			//break or continue;
			std::cout << "File invalid" << std::endl;
			continue;
		}
	}

	if (stop_thread) {
		std::cout << "disconection..." << std::endl;
	}
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

	// Get UDP port number for receiving
	std::string serverUdpPortStr;
	std::cout << "Server UDP Port Number: ";
	std::getline(std::cin, serverUdpPortStr);



	// Get download path
	std::string downloadPath;
	std::cout << "Path: ";
	std::getline(std::cin, downloadPath);

	if (downloadPath.empty() || !std::filesystem::exists(downloadPath)) {
		std::cerr << "Invalid path" << std::endl;
		return RETURN_CODE_1;
	}

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
		WSACleanup();
		return RETURN_CODE_3;
	}

	// Create UDP socket
	UDP_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (UDP_socket == INVALID_SOCKET) {
		std::cerr << "Failed to create UDP socket" << std::endl;
		freeaddrinfo(info);
		closesocket(clientSocket);
		WSACleanup();
		return RETURN_CODE_2;
	}

	// Set up UDP socket address
	sockaddr_in udpAddr;
	SecureZeroMemory(&udpAddr, sizeof(udpAddr));
	udpAddr.sin_family = AF_INET;
	inet_pton(AF_INET, host.c_str(), &udpAddr.sin_addr);
	udpAddr.sin_port = INADDR_ANY;

	// Bind UDP socket
	/*if (bind(UDP_socket, (sockaddr*)&udpAddr, sizeof(udpAddr)) == SOCKET_ERROR) {
		std::cerr << "Failed to bind UDP socket" << std::endl;
		closesocket(UDP_socket);
		freeaddrinfo(info);
		closesocket(clientSocket);
		WSACleanup();
		return RETURN_CODE_3;
	}*/

	// Make UDP socket non-blocking
	unsigned long nonBlocking = 1;
	ioctlsocket(UDP_socket, FIONBIO, &nonBlocking);

	// Now modify the thread creation to pass the downloadPath
	std::thread inputThread(asyncInputHandler, clientSocket, std::ref(downloadPath));

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
				return 1;
			}
			std::string address_port = command.substr(3, space_pos - 3);
			{
				std::lock_guard<std::mutex> lock(filename_mutex);
				file_name = command.substr(space_pos + 1);
			}


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

			udpAddr.sin_port = net_port;
			udpAddr.sin_addr = ip_addr;

			static bool binded = false;
			if (!binded) {
				if (bind(UDP_socket, (sockaddr*)&udpAddr, sizeof(udpAddr)) == SOCKET_ERROR) {
					std::cerr << "Failed to bind UDP socket" << std::endl;
					closesocket(UDP_socket);
					freeaddrinfo(info);
					closesocket(clientSocket);
					WSACleanup();
					return RETURN_CODE_3;
				}
				binded = true;
			}

			// Filename length
			uint32_t filename_length = htonl(static_cast<uint32_t>(file_name.length()));
			memcpy(buffer + 7, &filename_length, 4); // 4 bytes for file_name length

			// Filename
			memcpy(buffer + 11, file_name.c_str(), file_name.length()); // Variable length file name

			// Send completed message
			size_t message_size = static_cast<int>(11 + file_name.length());
			send(clientSocket, (const char*)buffer, static_cast<int>(message_size), 0);
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
	closesocket(UDP_socket);


	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------

	WSACleanup();
}
