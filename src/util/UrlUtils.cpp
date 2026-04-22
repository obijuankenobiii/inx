/**
 * @file UrlUtils.cpp
 * @brief Definitions for UrlUtils.
 */

#include "UrlUtils.h"

namespace UrlUtils {

bool isHttpsUrl(const std::string& url) { return url.rfind("https://", 0) == 0; }

std::string ensureProtocol(const std::string& url) {
  if (url.find("://") == std::string::npos) {
    return "http://" + url;
  }
  return url;
}

std::string extractHost(const std::string& url) {
  const size_t protocolEnd = url.find("://");
  if (protocolEnd == std::string::npos) {
    
    const size_t firstSlash = url.find('/');
    return firstSlash == std::string::npos ? url : url.substr(0, firstSlash);
  }
  
  const size_t hostStart = protocolEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  return pathStart == std::string::npos ? url : url.substr(0, pathStart);
}

std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  const std::string urlWithProtocol = ensureProtocol(serverUrl);
  if (path.empty()) {
    return urlWithProtocol;
  }
  if (path[0] == '/') {
    
    return extractHost(urlWithProtocol) + path;
  }
  
  if (urlWithProtocol.back() == '/') {
    return urlWithProtocol + path;
  }
  return urlWithProtocol + "/" + path;
}

}  
