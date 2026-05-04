#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <curl/curl.h>
#include <openssl/evp.h>

#define mysleep(time_second) std::this_thread::sleep_for(std::chrono::seconds(time_second))

// ==================== 配置结构体 ====================
struct OTAConfig {
    std::string server_url;
    int check_interval;
    bool enabled;
    std::string boot_path;
    std::string version_file;
    std::string config_file;   // boot config.txt 路径
    std::string log_file;
    std::string ota_config_path;  // ota_config 自身路径，用于 config OTA
};

// 版本格式: YYYY.WW.N (例如 2026.18.1，同 Tesla 风格)
struct Version {
    int year;  // 年份
    int week;  // ISO 周数 (1-53)
    int seq;   // 本周构建序号
    bool valid;
};

// ==================== 全局变量 ====================
OTAConfig g_config;
const std::string IFS_A = "ifs-rpi5.bin";
const std::string IFS_B = "ifs-rpi5_B.bin";
const size_t MIN_IFS_SIZE = 10485760;  // 最小 10MB

// ==================== 版本比较函数 ====================

// 解析版本号，支持两种格式：
//   新格式 YYYY.WW.N（如 2026.18.1）
//   旧格式 整数 N（如 3），映射为 {year=0, week=0, seq=N}，始终小于任何新格式版本
Version parse_version(const std::string& v) {
    try {
        size_t p1 = v.find('.');
        if (p1 != std::string::npos) {
            size_t p2 = v.find('.', p1 + 1);
            if (p2 == std::string::npos) return {0, 0, 0, false};
            int year = std::stoi(v.substr(0, p1));
            int week = std::stoi(v.substr(p1 + 1, p2 - p1 - 1));
            int seq  = std::stoi(v.substr(p2 + 1));
            return {year, week, seq, true};
        }
        // 旧整数格式回退：year=0 week=0 保证小于任何 YYYY.WW.N 版本
        int seq = std::stoi(v);
        return {0, 0, seq, true};
    } catch (...) {
        return {0, 0, 0, false};
    }
}

// 返回 true 表示 server_v 比 local_v 新
bool is_newer_version(const std::string& server_v, const std::string& local_v) {
    Version sv = parse_version(server_v);
    Version lv = parse_version(local_v);
    if (!sv.valid || !lv.valid) return false;
    if (sv.year != lv.year) return sv.year > lv.year;
    if (sv.week != lv.week) return sv.week > lv.week;
    return sv.seq > lv.seq;
}

// ==================== 日志函数 ====================
void log_msg(const std::string& message) {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    std::string log_line = std::string("[") + time_str + "] " + message;

    std::cout << "[OTA] " << message << std::endl;

    std::ofstream log_file(g_config.log_file, std::ios_base::app);
    if (log_file.is_open()) {
        log_file << log_line << std::endl;
        log_file.close();
    }
}

// ==================== 文件操作函数 ====================

bool read_config(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        log_msg("Config file not found, using defaults: " + config_path);
        return true;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key   = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

        if (key == "OTA_SERVER") {
            g_config.server_url = value;
        } else if (key == "OTA_CHECK_INTERVAL") {
            g_config.check_interval = std::stoi(value);
        } else if (key == "OTA_ENABLED") {
            g_config.enabled = (value == "1");
        } else if (key == "BOOT_PATH") {
            g_config.boot_path = value;
            // version_file 固定跟随 boot_path，不单独配置
            g_config.version_file = value + "/ota_version";
        } else if (key == "CONFIG_FILE") {
            g_config.config_file = value;
        } else if (key == "LOG_FILE") {
            g_config.log_file = value;
        }
    }
    file.close();
    return true;
}

// 获取本地版本号，格式 YYYY.WW.N
std::string get_version() {
    std::ifstream file(g_config.version_file);
    if (!file.is_open()) {
        log_msg("Version file not found, using default 1");
        return "1";
    }

    std::string version;
    std::getline(file, version);
    file.close();

    // trim 空白和换行符
    version.erase(0, version.find_first_not_of(" \t\r\n"));
    version.erase(version.find_last_not_of(" \t\r\n") + 1);

    if (version.empty()) return "1";
    return version;
}

// 写入版本号，version_file 固定在 boot_path 下，始终可写
bool write_version(const std::string& version) {
    std::ofstream file(g_config.version_file);
    if (!file.is_open()) {
        log_msg("Failed to write version file: " + g_config.version_file);
        return false;
    }
    file << version << std::endl;
    return true;
}

bool file_exists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

// Bug fix: 文件打开失败时返回 0 而不是让 tellg() 返回 -1 被 cast 成超大 size_t
size_t get_file_size(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return 0;
    std::streampos sz = f.tellg();
    if (sz < 0) return 0;
    return static_cast<size_t>(sz);
}

bool remove_file(const std::string& path) {
    return std::remove(path.c_str()) == 0;
}

// Bug fix: trim 返回值中可能的 \r\n，防止字符串比较失败
std::string get_active_ifs() {
    std::ifstream file(g_config.config_file);
    if (!file.is_open()) {
        log_msg("Config file not found, assuming IFS_A is active");
        return IFS_A;
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find("kernel=");
        if (pos != std::string::npos) {
            std::string ifs_name = line.substr(pos + 7);  // len("kernel=") == 7
            ifs_name.erase(0, ifs_name.find_first_not_of(" \t"));
            ifs_name.erase(ifs_name.find_last_not_of(" \t\r\n") + 1);
            file.close();
            return ifs_name;
        }
    }
    file.close();
    return IFS_A;
}

// ==================== 网络函数 ====================

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* user_p) {
    ((std::string*)user_p)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

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

static int progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                              curl_off_t ultotal, curl_off_t ulnow) {
    (void)clientp; (void)ultotal; (void)ulnow;
    if (dltotal > 0) {
        int percent = (int)((dlnow * 100) / dltotal);
        if (percent % 10 == 0) {
            std::cout << "[OTA] Download progress: " << percent << "%" << std::endl;
        }
    }
    return 0;
}

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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3600L);
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

// 计算文件的 SHA256，返回小写十六进制字符串，失败返回空串
static std::string compute_sha256(const std::string& file_path) {
    FILE* fp = fopen(file_path.c_str(), "rb");
    if (!fp) return "";

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { fclose(fp); return ""; }

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        EVP_DigestUpdate(ctx, buf, n);
    fclose(fp);

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    char hex[EVP_MAX_MD_SIZE * 2 + 1] = {};
    for (unsigned int i = 0; i < hash_len; ++i)
        snprintf(hex + i * 2, 3, "%02x", hash[i]);
    return std::string(hex);
}

// 下载 .sha256 伴生文件并与本地文件哈希对比
// 伴生文件内容支持两种格式：纯哈希 或 "hash  filename"（sha256sum 输出）
bool verify_sha256(const std::string& file_path, const std::string& sha256_url) {
    log_msg("Downloading SHA256: " + sha256_url);
    std::string remote;
    if (!curl_get(sha256_url, remote)) {
        log_msg("Failed to download SHA256 sidecar");
        return false;
    }

    // 只取第一个空格前的部分（兼容 sha256sum 输出格式）
    remote.erase(0, remote.find_first_not_of(" \t\r\n"));
    size_t sp = remote.find(' ');
    if (sp != std::string::npos) remote = remote.substr(0, sp);
    remote.erase(remote.find_last_not_of(" \t\r\n") + 1);

    if (remote.size() != 64) {
        log_msg("Invalid SHA256 from server: " + remote);
        return false;
    }

    std::string actual = compute_sha256(file_path);
    if (actual.empty()) {
        log_msg("SHA256 computation failed for: " + file_path);
        return false;
    }

    if (actual != remote) {
        log_msg("SHA256 mismatch! expected=" + remote + " actual=" + actual);
        return false;
    }

    log_msg("SHA256 OK: " + actual);
    return true;
}

bool verify_ifs(const std::string& file_path) {
    log_msg("Verifying IFS file: " + file_path);

    if (!file_exists(file_path)) {
        log_msg("IFS file not found: " + file_path);
        return false;
    }

    size_t file_size = get_file_size(file_path);
    if (file_size < MIN_IFS_SIZE) {
        log_msg("IFS file too small: " + std::to_string(file_size) + " bytes");
        return false;
    }

    log_msg("IFS verified: " + file_path + " (" + std::to_string(file_size) + " bytes)");
    return true;
}

// ==================== Config OTA ====================

// 读取本地文件全部内容
static std::string read_file_content(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 检查并更新 ota_config：从服务端拉取 /ota_config，内容有变则热重载
// /etc/ 在 QNX IFS 中为只读，持久化路径按优先级降级：原路径 → /tmp/ota_config
// 无论是否能持久化，新配置都会热重载到内存
bool check_and_update_config() {
    std::string url = g_config.server_url + "/ota_config";
    std::string remote_content;

    if (!curl_get(url, remote_content)) return false;
    if (remote_content.empty()) return false;

    std::string local_content = read_file_content(g_config.ota_config_path);
    if (remote_content == local_content) return false;

    log_msg("Remote ota_config differs from local, applying update");

    // 先写到临时文件，用于热重载（不依赖原路径是否可写）
    const std::string tmp_path = "/tmp/ota_config.new";
    {
        std::ofstream tmp(tmp_path);
        if (!tmp.is_open()) {
            log_msg("Failed to write temp config, skipping update");
            return false;
        }
        tmp << remote_content;
    }

    // 尝试持久化到原路径，失败则降级到 /tmp/ota_config
    std::string persist_path = g_config.ota_config_path;
    {
        std::ofstream dst(persist_path);
        if (!dst.is_open()) {
            persist_path = "/tmp/ota_config";
            log_msg("Cannot write to " + g_config.ota_config_path +
                    " (read-only), persisting to " + persist_path);
            std::ofstream fallback(persist_path);
            if (fallback.is_open()) fallback << remote_content;
        } else {
            dst << remote_content;
            // 备份旧配置（原路径可写时才有意义）
            std::ofstream bak(g_config.ota_config_path + ".bak");
            if (bak.is_open()) bak << local_content;
        }
    }

    // 热重载：从临时文件解析新配置，立即生效
    read_config(tmp_path);
    g_config.ota_config_path = persist_path;
    log_msg("ota_config reloaded. Server: " + g_config.server_url);
    return true;
}

// ==================== 更新函数 ====================

// 获取服务器版本号，格式 YYYY.WW.N
std::string get_server_version() {
    std::string version_url = g_config.server_url + "/version.txt";
    std::string response;

    if (!curl_get(version_url, response)) {
        log_msg("Failed to check server version");
        return "";
    }

    response.erase(0, response.find_first_not_of(" \t\r\n"));
    response.erase(response.find_last_not_of(" \t\r\n") + 1);

    if (response.empty()) {
        log_msg("Empty server version response");
        return "";
    }
    return response;
}

// Bug fix: 只替换 kernel= 行，保留 config.txt 中的其他所有配置
bool switch_ifs(const std::string& new_ifs) {
    log_msg("Switching to IFS: " + new_ifs);

    // 先备份原始 config.txt
    std::string backup_file = g_config.config_file + ".bak";
    {
        std::ifstream src(g_config.config_file, std::ios::binary);
        std::ofstream dst(backup_file, std::ios::binary);
        if (src.is_open() && dst.is_open()) {
            dst << src.rdbuf();
            log_msg("Backed up config.txt to: " + backup_file);
        }
    }

    // 读取所有行，只替换 kernel= 行
    std::vector<std::string> lines;
    bool kernel_found = false;
    {
        std::ifstream f(g_config.config_file);
        if (f.is_open()) {
            std::string line;
            while (std::getline(f, line)) {
                if (line.find("kernel=") != std::string::npos) {
                    lines.push_back("kernel=" + new_ifs);
                    kernel_found = true;
                } else {
                    lines.push_back(line);
                }
            }
        }
    }
    if (!kernel_found) {
        lines.push_back("kernel=" + new_ifs);
    }

    // 写回
    std::ofstream out(g_config.config_file);
    if (!out.is_open()) {
        log_msg("Failed to open config file for writing: " + g_config.config_file);
        return false;
    }
    for (const auto& l : lines) {
        out << l << "\n";
    }
    out.close();

    log_msg("config.txt updated. System will reboot in 10 seconds.");
    mysleep(10);
    system("shutdown -v");
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
            // 先检查远端是否有新 ota_config，有则热重载后继续本轮
            check_and_update_config();

            std::string local_version  = get_version();
            std::string server_version = get_server_version();

            if (server_version.empty()) {
                log_msg("Failed to get server version, will retry");
                mysleep(g_config.check_interval);
                continue;
            }

            log_msg("Local version: " + local_version +
                    ", Server version: " + server_version);

            if (is_newer_version(server_version, local_version)) {
                log_msg("New version available: " + server_version);

                std::string active_ifs  = get_active_ifs();
                std::string target_ifs  = (active_ifs == IFS_A) ? IFS_B : IFS_A;
                std::string target_path = g_config.boot_path + "/" + target_ifs;

                // 文件名格式: ifs-rpi5_v2026.18.1.bin
                std::string ifs_filename = "ifs-rpi5_v" + server_version + ".bin";
                std::string download_url = g_config.server_url + "/" + ifs_filename;

                log_msg("Target IFS: " + target_ifs);
                log_msg("Download URL: " + download_url);

                std::string sha256_url = download_url + ".sha256";

                if (download_file(download_url, target_path)) {
                    if (!verify_sha256(target_path, sha256_url)) {
                        log_msg("SHA256 verification failed, aborting update");
                        remove_file(target_path);
                        mysleep(g_config.check_interval);
                        continue;
                    }
                    if (verify_ifs(target_path)) {
                        // Bug fix: 更新本地版本文件后再切换
                        if (!write_version(server_version)) {
                            log_msg("Failed to write version file, aborting update");
                            remove_file(target_path);
                        } else {
                            switch_ifs(target_ifs);
                            break;
                        }
                    } else {
                        log_msg("Verification failed, skipping update");
                        remove_file(target_path);
                    }
                } else {
                    log_msg("Download failed, will retry later");
                }
            }

            mysleep(g_config.check_interval);
        }
        catch (const std::exception& e) {
            log_msg("Exception in OTA loop: " + std::string(e.what()));
            mysleep(g_config.check_interval);
        }
    }
}

// ==================== 主函数 ====================
void ota_worker() {
    ota_loop();
}

bool is_mounted(const std::string& mount_point) {
    std::string line;
    system("/bin/mount > /tmp/mounts");
    std::ifstream ifs("/tmp/mounts");
    while (std::getline(ifs, line)) {
        if (line.find(mount_point) != std::string::npos)
            return true;
    }
    return false;
}

bool ensure_boot_mounted() {
    const char* mount_point = "/var/boot";
    const char* dev = "/dev/sd0t12";

    mkdir(mount_point, 0755);

    char cmd[256] = {0};
    snprintf(cmd, sizeof(cmd), "mount -t dos %s %s", dev, mount_point);
    int ret = system(cmd);

    if (ret != 0) {
        printf("mount %s to %s failed\n", dev, mount_point);
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    // 设置默认配置
    g_config.server_url      = "http://192.168.50.148:8080";
    g_config.check_interval  = 300;
    g_config.enabled         = true;
    g_config.boot_path       = "/var/boot";
    g_config.version_file    = g_config.boot_path + "/ota_version";
    g_config.config_file     = "/var/boot/config.txt";
    g_config.log_file        = "/tmp/ota_client.log";
    g_config.ota_config_path = "/etc/ota_config";

    // 确保 /var/boot 已挂载
    if (!is_mounted("/var/boot")) {
        if (!ensure_boot_mounted()) {
            std::cerr << "Failed to mount /var/boot, exiting" << std::endl;
            return 1;
        }
    }

    std::string config_file = "/etc/ota_config";
    bool daemonize = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            config_file = argv[++i];
            g_config.ota_config_path = config_file;
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

    if (!read_config(config_file)) {
        std::cerr << "Failed to read configuration file\n";
        return 1;
    }

    std::thread t(ota_worker);
    t.join();

    log_msg("OTA Client exiting");
    return 0;
}
