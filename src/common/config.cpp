#include "config.h"
#include <iostream>
#include <glog/logging.h>

namespace rtcom {

ServerConfig Config::LoadDefault() {
    ServerConfig config;
    return config;
}

ServerConfig Config::LoadFromFile(const std::string& path) {
    ServerConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG(WARNING) << "Cannot open config file: " << path << ", using defaults";
        return config;
    }
    // Simple key=value parser
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        auto delim = line.find('=');
        if (delim == std::string::npos) continue;
        std::string key = line.substr(0, delim);
        std::string val = line.substr(delim + 1);
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);

        if (key == "sip_server_addr") config.sip.server_addr = val;
        else if (key == "sip_server_port") config.sip.server_port = std::stoi(val);
        else if (key == "sip_username") config.sip.username = val;
        else if (key == "sip_domain") config.sip.domain = val;
        else if (key == "sip_password") config.sip.password = val;
        else if (key == "audio_port") config.media.audio_port = std::stoi(val);
        else if (key == "video_port") config.media.video_port = std::stoi(val);
        else if (key == "max_sessions") config.max_sessions = std::stoi(val);
        else if (key == "worker_threads") config.worker_threads = std::stoi(val);
    }
    return config;
}

void Config::SaveToFile(const ServerConfig& config, const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG(ERROR) << "Cannot write config to: " << path;
        return;
    }
    file << "# RTCom Server Configuration\n";
    file << "sip_server_addr=" << config.sip.server_addr << "\n";
    file << "sip_server_port=" << config.sip.server_port << "\n";
    file << "sip_username=" << config.sip.username << "\n";
    file << "sip_domain=" << config.sip.domain << "\n";
    file << "sip_password=" << config.sip.password << "\n";
    file << "audio_port=" << config.media.audio_port << "\n";
    file << "video_port=" << config.media.video_port << "\n";
    file << "max_sessions=" << config.max_sessions << "\n";
    file << "worker_threads=" << config.worker_threads << "\n";
}

void Config::PrintConfig(const ServerConfig& config) {
    LOG(INFO) << "=== Server Configuration ===";
    LOG(INFO) << "SIP Server: " << config.sip.server_addr << ":" << config.sip.server_port;
    LOG(INFO) << "SIP User: " << config.sip.username << "@" << config.sip.domain;
    LOG(INFO) << "Audio Port: " << config.media.audio_port;
    LOG(INFO) << "Video Port: " << config.media.video_port;
    LOG(INFO) << "Audio Codec: " << CodecTypeToString(config.media.audio_codec);
    LOG(INFO) << "Video Codec: " << CodecTypeToString(config.media.video_codec);
    LOG(INFO) << "Max Sessions: " << config.max_sessions;
    LOG(INFO) << "Worker Threads: " << config.worker_threads;
}

} // namespace rtcom
