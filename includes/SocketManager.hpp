#ifndef SOCKET_MANAGER_HPP
# define SOCKET_MANAGER_HPP

#include <vector>
#include <map>
#include <unistd.h> 
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <netinet/in.h>
#include <algorithm>
#include <fstream>
#include <streambuf>

#include "Structs.hpp"
#include "HTTPRequest.hpp"
#include "HTTPResponse.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

class SocketManager {
	private:
		HTTPConfig config;
		std::vector<struct pollfd> fds;
		std::vector<int> server_fds;
		std::map<int, ClientState> clientStates;

		void acceptNewConnections(int server_fd);
		void closeConnection(int fd);
		int createAndBindSocket(int port);
		void handleClientRequest(pollfd &fd);
		void addServerFd(int fd);
		bool isServerSocket(int fd);
		ServerConfig& getCurrentServer(const HTTPRequest& request);

		void sendResponse(pollfd &fd);
		bool readClientData(int fd);
		void processRequest(int fd);
	public:
		SocketManager(const HTTPConfig& config);
		~SocketManager();

		void setupServerSockets();
		void run();
};

#endif