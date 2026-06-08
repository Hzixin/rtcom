#pragma once

#include "types.h"
#include <fstream>
#include <sstream>

namespace rtcom {

class Config {
public:
    static ServerConfig LoadDefault();
    static ServerConfig LoadFromFile(const std::string& path);
    static void SaveToFile(const ServerConfig& config, const std::string& path);
    static void PrintConfig(const ServerConfig& config);
};

} // namespace rtcom
