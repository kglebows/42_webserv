#include "SocketManager.hpp"

SocketManager::SocketManager(const HTTPConfig& config): config(config) {}

SocketManager::~SocketManager() {
	INFO("Closing all sockets");
	for (size_t i = 0; i < this->fds.size(); i++) {
		closeConnection(this->fds[i].fd);
	}
}

/* -------------------------------------------------------------------------- */
/*                                 Main Server                                */
/* -------------------------------------------------------------------------- */

void SocketManager::run() {
	INFO("Running poll()");
	if (this->fds.size() == 0) {
		ERROR("No servers configured");
		return;
	}

	while (true) {
		// Poll the sockets for events
		int num_elements = poll(&this->fds[0], this->fds.size(), this->config.server_timeout_time);

		if (num_elements < 0) {
			ERROR("poll() error");
			break;
		} else if (num_elements == 0) {
			// WARNING("Socket(s) timed out, trying again");
			continue;
		}

		// Iterate over fds to check which ones are ready
		for (size_t i = 0; i < this->fds.size(); i++) {
			if (this->fds[i].revents & POLLIN) { // Check if ready for reading
				if (isServerSocket(this->fds[i].fd)) {
					acceptNewConnections(this->fds[i].fd);
				} else {
					// read data from the client socket, parse into an HTTP request
					handleClient(this->fds[i].fd);
				}
			}
			// Checks if the connection is still valid
			if (this->fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				closeConnection(this->fds[i].fd);
				// adjust index after removeing an element
				i--;
			}
		}
	}
}

/* -------------------------------------------------------------------------- */
/*                               Set Up Sockets                               */
/* -------------------------------------------------------------------------- */

void SocketManager::setupServerSockets() {
	INFO("Setting up server sockets");
	for (size_t i = 0; i < this->config.serverConfigs.size(); i++) {
		int sockfd = createAndBindSocket(this->config.serverConfigs[i].listenPort);
		if (sockfd >= 0) {
			struct pollfd pfd = {sockfd, POLLIN, 0};
			this->fds.push_back(pfd);
			this->server_fds.push_back(sockfd);
		}
	}
}

int SocketManager::createAndBindSocket(int port) {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		ERROR("Failed to create socket for port: " << port);
		return -1;
	}

	// Make socket non blocking
	int flags = fcntl(sockfd, F_GETFL, 0);
	if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
		closeConnection(sockfd);
		ERROR("Failed to set to non blocking mode for socket: " << sockfd);
		return -1;
	}

	// bind
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port);
	if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		closeConnection(sockfd);
		ERROR("Failed to bind socket to port: " << port);
		return -1;
	}

	// listen
	if (listen(sockfd, SOMAXCONN) < 0) {
		closeConnection(sockfd);
		ERROR("Failed to initialize listen for socket: " << sockfd);
		return -1;
	}
	SUCCESS("Socket "<< sockfd << " is listening on port " << port);
	return sockfd;
}

/* -------------------------------------------------------------------------- */
/*                                Handle Client                               */
/* -------------------------------------------------------------------------- */

void SocketManager::acceptNewConnections(int server_fd) {
	sockaddr_in client_addr;
	socklen_t clilen = sizeof(client_addr);
	int newsockfd = accept(server_fd, (struct sockaddr*)&client_addr, &clilen);
	if (newsockfd < 0) {
		ERROR("Error accepting connection");
		return;
	}

	this->clientStates[newsockfd] = ClientState();

	// Make the new socket non-blocking
	int flags = fcntl(newsockfd, F_GETFL, 0);
	fcntl(newsockfd, F_SETFL, flags | O_NONBLOCK);

	// Add the new socket to the fds vector to monitor it with poll()
	struct pollfd new_pfd = {newsockfd, POLLIN, 0};
	this->fds.push_back(new_pfd);
	SUCCESS("Server socket " << server_fd << " Accepted new connection from " << &client_addr);
}

void SocketManager::handleClient(int fd) {
    INFO("Handling client request. FD: " << fd);
    if (!clientStates[fd].requestComplete) {
        if (readClientData(fd) && clientStates[fd].requestComplete) {
            processRequestAndRespond(fd);
        }
    } 
	// else if (!clientStates[fd].responseComplete) {
    //     // TOOD: Resend request if we didnt send everything the first time? will probably have to change this
    //     sendResponse(fd);
    // }
}

/* ----------------------------- Handle Requests ---------------------------- */

bool SocketManager::readClientData(int fd) {
	INFO("Reading client request");
    char buffer[4096]; // TODO: Change buffer size. To what? Idk lol
    ssize_t bytesRead = recv(fd, buffer, sizeof(buffer), 0);
    if (bytesRead > 0) {
        // Append data to the clients read buffer
        this->clientStates[fd].readBuffer.append(buffer, bytesRead);
        // Check if request is complete
        if (this->clientStates[fd].readBuffer.find("\r\n\r\n") != std::string::npos) {
            SUCCESS("Successfully read client request");
			this->clientStates[fd].requestComplete = true;
            return true;
        }
    } else if (bytesRead == 0) {
        WARNING("Client connection is closed");
        closeConnection(fd);
    } else {
        ERROR("Failed to read from recv()");
        closeConnection(fd);
    }
    return false;
}

/* ---------------------------- Handle Responses ---------------------------- */

void SocketManager::processRequestAndRespond(int fd) {
    HTTPRequest request(this->clientStates[fd].readBuffer);
    HTTPResponse response;

    const ServerConfig& serverConfig = getCurrentServer(request);

    // Determine the file path based on the request URI
    std::string requestURI = request.getURI();
    std::string filePath = serverConfig.rootDirectory + (requestURI == "/" ? "/index.html" : requestURI);
	DEBUG("REQUESTED URI: " << requestURI);
	DEBUG("FILE PATH: " << filePath);
    std::ifstream file(filePath.c_str());
    if (file) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        std::string contentType = "text/html";
        if (endsWith(requestURI, ".css")) {
            contentType = "text/css";
        }
        response.prepareResponse(response, 200, content, contentType);
    } else {
		ERROR("404 Not Found - The reqeusted: '" << filePath << "' was not found");
        response.prepareResponse(response, 404, "<html><body><h1>404 Not Found</h1><p>The requested file was not found.</p></body></html>", "text/html");
    }

    clientStates[fd].writeBuffer = response.convertToString();
    sendResponse(fd);
}

void SocketManager::sendResponse(int fd) {
    // Check if theres anything to write
    if (clientStates[fd].writeBuffer.empty()) {
        WARNING("Nothing to send for FD: " << fd);
        return;
    }
    ssize_t bytesWritten = send(fd, clientStates[fd].writeBuffer.c_str(), clientStates[fd].writeBuffer.size(), 0);
    if (bytesWritten > 0) {
        // Erase the sent part of the buffer, check if all data was sent
        clientStates[fd].writeBuffer.erase(0, bytesWritten);

        // Check if all data was sent
        if (clientStates[fd].writeBuffer.empty()) {
            clientStates[fd].responseComplete = true;
            // TODO: Depending on HTTP version and headers would have to not close connection potentially?? (e.g. for the Header --> Connection: keep-alive)
            SUCCESS("Response sent successfully. FD: " << fd);
            closeConnection(fd);
        } else {
            // TODO: Theres still data left to send, try to send the rest in a another poll iteration??
            WARNING("Partial data sent for FD: " << fd << ". Remaining will be attempted later.");
        }
    } else if (bytesWritten == 0) {
        WARNING("No data was sent for FD: " << fd);
    } else {
        ERROR("Failed to send response for FD: " << fd);
        closeConnection(fd);
    }
}

/* -------------------------------------------------------------------------- */
/*                              Helper Functions                              */
/* -------------------------------------------------------------------------- */

ServerConfig& SocketManager::getCurrentServer(const HTTPRequest& request) {
	std::string hostName = request.getHeader("Host");

	// Get rid of potential port number from host if present
	size_t colonPos = hostName.find(":");
	if (colonPos != std::string::npos) {
		hostName = hostName.substr(0, colonPos);
	}
	for (std::vector<ServerConfig>::iterator iter = this->config.serverConfigs.begin(); iter != this->config.serverConfigs.end(); iter++) {
		if (iter->serverName == hostName) {
			return *iter;
		}
	}
	throw std::runtime_error("Server config not found for host: " + hostName);
}

bool SocketManager::isMethodAllowed(const std::string& method, const std::string& uri, const ServerConfig& serverConfig) {
    for (std::vector<LocationConfig>::const_iterator it = serverConfig.locations.begin(); it != serverConfig.locations.end(); ++it) {
        if (uri.find(it->locationPath) == 0) {
            for (std::vector<RequestTypes>::const_iterator iter = it->allowedRequestTypes.begin(); iter != it->allowedRequestTypes.end(); ++iter) {
                if (method == requestTypeToString(*iter)) {
                    return true;
                }
            }
            // If the URI matches but the method is not allowed, return false
            return false;
        }
    }
    return false;
}

void SocketManager::closeConnection(int fd) {
    INFO("Closing socket: " << fd);
    close(fd);

    // Remove from fds vector
    for (std::vector<struct pollfd>::iterator it = this->fds.begin(); it != this->fds.end();) {
        if (it->fd == fd) {
            it = this->fds.erase(it); // Erase returns the next iterator
        } else {
            ++it;
        }
    }

    // Remove from server_fds vector
    for (std::vector<int>::iterator it = this->server_fds.begin(); it != this->server_fds.end();) {
        if (*it == fd) {
            it = this->server_fds.erase(it);
        } else {
            ++it;
        }
    }

    // Remove from clientStates map
    std::map<int, ClientState>::iterator it = this->clientStates.find(fd);
    if (it != this->clientStates.end()) {
        this->clientStates.erase(it);
    }
}

void SocketManager::addServerFd(int fd) {
	server_fds.push_back(fd);
}

bool SocketManager::isServerSocket(int fd) {
	return std::find(server_fds.begin(), server_fds.end(), fd) != server_fds.end();
}
