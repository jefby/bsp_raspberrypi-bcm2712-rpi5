#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <curl/curl.h>

// ==================== 配置结构体 ====================
struct OTAConfig {
    std::string server_url;
    int check_interval;
    bool enabled;
    std::string boot_path;
    std::string version_file;
    std::string config_file;
    std::string log_file;
};

// ==================== 全局变量 ====================
OTAConfig g_config;
const std::string IFS_A = "ifs-rpi5.bin";
const std::string IFS_B = "ifs-rpi5_B.bin";
const size_t MIN_IFS_SIZE = 10485760;  // 最小 10MB

// ==================== 日志函数 ====================
void log_msg(const std::string& message) {
    // 获取当前时间
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    std::string log_line = std::string("[") + time_str + "] " + message;
    
    // 打印到控制台
    std::cout << "[OTA] " << message << std::endl;
    
    // 写入日志文件
    std::ofstream log_file(g_config.log_file, std::ios_base::app);
    if (log_file.is_open()) {
        log_file << log_line << std::endl;
        log_file.close();
    }
}

// ==================== 文件操作函数 ====================

// 读取配置文件
bool read_config(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        log_msg("Failed to open config file: " + config_path);
        // 使用默认配置
        g_config.server_url = "http://192.168.1.100:8080";
        g_config.check_interval = 300;
        g_config.enabled = true;
        g_config.boot_path = "/proc/boot";
        g_config.version_file = "/etc/ota_version";
        g_config.config_file = "/proc/boot/config.txt";
        g_config.log_file = "/tmp/ota_client.log";
        return true;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            // 去除前后空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            if (key == "OTA_SERVER") {
                g_config.server_url = value;
            } else if (key == "OTA_CHECK_INTERVAL") {
                g_config.check_interval = std::stoi(value);
            } else if (key == "OTA_ENABLED") {
                g_config.enabled = (value == "1");
            } else if (key == "BOOT_PATH") {
                g_config.boot_path = value;
            } else if (key == "VERSION_FILE") {
                g_config.version_file = value;
            } else if (key == "CONFIG_FILE") {
                g_config.config_file = value;
            } else if (key == "LOG_FILE") {
                g_config.log_file = value;
            }
        }
    }
    file.close();
    return true;
}

// 获取当前版本
int get_version() {
    std::ifstream file(g_config.version_file);
    if (!file.is_open()) {
        log_msg("Version file not found, using default version 1");
        return 1;
    }
    
    int version = 1;
    file >> version;
    file.close();
    return version;
}

// 检查文件是否存在
bool file_exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// 获取文件大小
off_t get_file_size(const std::string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) == 0) {
        return buffer.st_size;
    }
    return -1;
}

// 删除文件
bool remove_file(const std::string& path) {
    return (unlink(path.c_str()) == 0);
}

// 获取活跃的 IFS 名称
std::string get_active_ifs() {
    std::ifstream file(g_config.config_file);
    if (!file.is_open()) {
        log_msg("Config file not found, assuming IFS_A is active");
        return IFS_A;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("kernel=") != std::string::npos) {
            size_t pos = line.find('=');
            std::string ifs_name = line.substr(pos + 1);
            file.close();
            return ifs_name;
        }
    }
    file.close();
    return IFS_A;
}

// ==================== 网络函数 ====================

// CURL 回调函数，用于处理下载数据
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* user_p) {
    ((std::string*)user_p)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// 从 URL 获取数据
bool curl_get(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_msg("Failed to initialize CURL");
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        log_msg("CURL error: " + std::string(curl_easy_strerror(res)));
        return false;
    }
    return true;
}

// CURL 下载进度回调
static int progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                              curl_off_t ultotal, curl_off_t ulnow) {
    if (dltotal > 0) {
        int percent = (int)((dlnow * 100) / dltotal);
        if (percent % 10 == 0) {
            std::cout << "[OTA] Download progress: " << percent << "%" << std::endl;
        }
    }
    return 0;
}

// 下载文件
bool download_file(const std::string& url, const std::string& dest_file) {
    log_msg("Downloading IFS from: " + url);
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_msg("Failed to initialize CURL");
        return false;
    }
    
    FILE* fp = fopen(dest_file.c_str(), "wb");
    if (!fp) {
        log_msg("Failed to open destination file: " + dest_file);
        curl_easy_cleanup(curl);
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3600L);  // 1小时超时
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, nullptr);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    
    fclose(fp);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        log_msg("Download failed: " + std::string(curl_easy_strerror(res)));
        remove_file(dest_file);
        return false;
    }
    
    log_msg("Download completed: " + dest_file);
    return true;
}

// ==================== 验证函数 ====================

// 验证 IFS 文件
bool verify_ifs(const std::string& file_path) {
    log_msg("Verifying IFS file: " + file_path);
    
    if (!file_exists(file_path)) {
        log_msg("IFS file not found: " + file_path);
        return false;
    }
    
    off_t file_size = get_file_size(file_path);
    if (file_size < (off_t)MIN_IFS_SIZE) {
        log_msg("IFS file too small: " + std::to_string(file_size) + " bytes");
        return false;
    }
    
    log_msg("IFS verified: " + file_path + " (" + std::to_string(file_size) + " bytes)");
    return true;
}

// ==================== 更新函数 ====================

// 获取服务器版本
int get_server_version() {
    std::string version_url = g_config.server_url + "/version.txt";
    std::string response;
    
    if (!curl_get(version_url, response)) {
        log_msg("Failed to check server version");
        return -1;
    }
    
    // 解析版本号
    try {
        int version = std::stoi(response);
        return version;
    } catch (...) {
        log_msg("Invalid server version response: " + response);
        return -1;
    }
}

// 切换 IFS
bool switch_ifs(const std::string& new_ifs) {
    log_msg("Switching to IFS: " + new_ifs);
    
    // 备份现有 config.txt
    std::string backup_file = g_config.config_file + ".bak";
    std::ifstream src(g_config.config_file, std::ios::binary);
    std::ofstream dst(backup_file, std::ios::binary);
    if (src.is_open() && dst.is_open()) {
        dst << src.rdbuf();
        src.close();
        dst.close();
        log_msg("Backed up config.txt to: " + backup_file);
    }
    
    // 更新 config.txt
    std::ofstream config_file(g_config.config_file);
    if (!config_file.is_open()) {
        log_msg("Failed to open config file for writing: " + g_config.config_file);
        return false;
    }
    
    config_file << "kernel=" << new_ifs << std::endl;
    config_file.close();
    
    log_msg("config.txt updated. System will reboot in 10 seconds.");
    sleep(10);
    
    // 执行重启命令
    system("shutdown -r now");
    return true;
}

// ==================== 更新版本文件 ====================

bool update_version_file(int version) {
    std::ofstream file(g_config.version_file);
    if (!file.is_open()) {
        log_msg("Failed to update version file: " + g_config.version_file);
        return false;
    }
    
    file << version << std::endl;
    file.close();
    log_msg("Version file updated to: " + std::to_string(version));
    return true;
}

// ==================== OTA 主循环 ====================

void ota_loop() {
    log_msg("OTA Client started");
    log_msg("Server: " + g_config.server_url);
    log_msg("Check interval: " + std::to_string(g_config.check_interval) + " seconds");
    
    if (!g_config.enabled) {
        log_msg("OTA is disabled in configuration");
        return;
    }
    
    while (true) {
        try {
            int current_version = get_version();
            int server_version = get_server_version();
            
            if (server_version < 0) {
                log_msg("Failed to get server version, will retry");
                sleep(g_config.check_interval);
                continue;
            }
            
            log_msg("Current version: " + std::to_string(current_version) +
                   ", Server version: " + std::to_string(server_version));
            
            if (server_version > current_version) {
                log_msg("New version available!");
                
                // 确定目标 IFS
                std::string active_ifs = get_active_ifs();
                std::string target_ifs = (active_ifs == IFS_A) ? IFS_B : IFS_A;
                std::string target_path = g_config.boot_path + "/" + target_ifs;
                
                // 构建下载 URL
                std::string ifs_filename = "ifs-rpi5_v" + std::to_string(server_version) + ".bin";
                std::string download_url = g_config.server_url + "/" + ifs_filename;
                
                log_msg("Target IFS: " + target_ifs);
                log_msg("Download URL: " + download_url);
                
                // 下载
                if (download_file(download_url, target_path)) {
                    // 验证
                    if (verify_ifs(target_path)) {
                        // 更新版本文件
                        if (update_version_file(server_version)) {
                            // 切换并重启
                            switch_ifs(target_ifs);
                            break;  // 重启后不会执行到这里
                        }
                    } else {
                        log_msg("Verification failed, skipping update");
                        remove_file(target_path);
                    }
                } else {
                    log_msg("Download failed, will retry later");
                }
            }
            
            sleep(g_config.check_interval);
        }
        catch (const std::exception& e) {
            log_msg("Exception in OTA loop: " + std::string(e.what()));
            sleep(g_config.check_interval);
        }
    }
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    // 设置默认配置
    g_config.server_url = "http://192.168.1.100:8080";
    g_config.check_interval = 300;
    g_config.enabled = true;
    g_config.boot_path = "/proc/boot";
    g_config.version_file = "/etc/ota_version";
    g_config.config_file = "/proc/boot/config.txt";
    g_config.log_file = "/tmp/ota_client.log";
    
    // 解析命令行参数
    std::string config_file = "/etc/ota_config";
    bool daemonize = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "-d") {
            daemonize = true;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: ota_client [OPTIONS]\n"
                      << "  -c <file>  Configuration file (default: /etc/ota_config)\n"
                      << "  -d         Run as daemon\n"
                      << "  -h         Show this help message\n";
            return 0;
        }
    }
    
    // 读取配置文件
    if (!read_config(config_file)) {
        std::cerr << "Failed to read configuration file\n";
        return 1;
    }
    
    // 后台运行
    if (daemonize) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork() failed");
            return 1;
        }
        if (pid > 0) {
            // 父进程退出
            return 0;
        }
        // 子进程继续
        setsid();
    }
    
    // 运行 OTA 循环
    ota_loop();
    
    return 0;
}
