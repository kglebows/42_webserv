#ifndef HTTP_RESPONSE_HPP
# define HTTP_RESPONSE_HPP

#include <map>
#include <string>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include "Logger.hpp"
#include "Structs.hpp"
#include "Utils.hpp"
#include "HTTPRequest.hpp"

class HTTPResponse {
    private:
        int statusCode;
        std::map<std::string, std::string> headers;
        std::string body;
    public:
        HTTPResponse();
        ~HTTPResponse();
        
        std::string convertToString() const;
		void prepareResponse(HTTPRequest& request, const ServerConfig& ServerConfig);
		void assignResponse(int statusCode, const std::string& body, std::string contentType);
        std::string determineContentType(std::string requestURI);
        bool isMethodAllowed(const std::string& method, const std::string& uri, const ServerConfig& serverConfig);
        void serveFile(const ServerConfig& serverConfig, const std::string& uri);
        void handleRequestGET(const HTTPRequest& request, const ServerConfig& serverConfig);
        void assignPageNotFoundContent(const ServerConfig& serverConfig);
        
        /* -------------------------------------------------------------------------- */
        /*                                   Setters                                  */
        /* -------------------------------------------------------------------------- */
        void setStatusCode(int code);
        void setHeader(const std::string& key, const std::string& value);
        void setBody(const std::string& body);

};

#endif