// QNX + C++11：须在系统头之前打开扩展，否则 open/fsync/fork 等无声明
#ifndef _QNX_SOURCE
#define _QNX_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <thread>
#include <chrono>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include <process.h>
#include <sys/procmgr.h>

#include <curl/curl.h>
#include <openssl/evp.h>

#define mysleep(time_second) std::this_thread::sleep_for(std::chrono::seconds(time_second))

// ==================== 配置 ====================
struct OTAConfig {
    std::string server_url;
    int check_interval;
    bool enabled;
    std::string boot_path;
    std::string version_file;
    std::string config_file;
    std::string log_file;
    std::string ota_config_path;
};

// 版本: YYYY.WW.N；旧整数 N → {0,0,N}
struct Version {
    int year;
    int week;
    int seq;
    bool valid;
};

OTAConfig g_config;
const std::string IFS_A = "ifs-rpi5.bin";
const std::string IFS_B = "ifs-rpi5_B.bin";
const size_t MIN_IFS_SIZE = 10485760;  // 10MB

// ==================== 小工具 ====================

static void trim_inplace(std::string& s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    if (s.empty()) return;
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
}

static std::string trimmed(std::string s) {
    trim_inplace(s);
    return s;
}

// 刷路径对应文件到存储（FAT 上 OTA 写 config/IFS 后需要）
static bool fsync_path(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    int rc = fsync(fd);
    close(fd);
    return rc == 0;
}

static bool file_exists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

static size_t get_file_size(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return 0;
    std::streampos sz = f.tellg();
    return (sz < 0) ? 0 : static_cast<size_t>(sz);
}

static bool remove_file(const std::string& path) {
    return std::remove(path.c_str()) == 0;
}

static std::string read_file_content(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string pending_path() {
    return g_config.boot_path + "/ota_pending";
}

// ==================== 版本 ====================

Version parse_version(const std::string& v) {
    try {
        size_t p1 = v.find('.');
        if (p1 != std::string::npos) {
            size_t p2 = v.find('.', p1 + 1);
            if (p2 == std::string::npos) return {0, 0, 0, false};
            return {
                std::stoi(v.substr(0, p1)),
                std::stoi(v.substr(p1 + 1, p2 - p1 - 1)),
                std::stoi(v.substr(p2 + 1)),
                true
            };
        }
        return {0, 0, std::stoi(v), true};
    } catch (...) {
        return {0, 0, 0, false};
    }
}

bool is_newer_version(const std::string& server_v, const std::string& local_v) {
    Version sv = parse_version(server_v);
    Version lv = parse_version(local_v);
    if (!sv.valid || !lv.valid) return false;
    if (sv.year != lv.year) return sv.year > lv.year;
    if (sv.week != lv.week) return sv.week > lv.week;
    return sv.seq > lv.seq;
}

// ==================== 日志 ====================

void log_msg(const std::string& message) {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    std::cout << "[OTA] " << message << std::endl;

    std::ofstream log_file(g_config.log_file, std::ios_base::app);
    if (log_file.is_open())
        log_file << "[" << time_str << "] " << message << std::endl;
}

// ==================== 配置读写 ====================

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

        std::string key   = trimmed(line.substr(0, pos));
        std::string value = trimmed(line.substr(pos + 1));

        if (key == "OTA_SERVER") {
            g_config.server_url = value;
        } else if (key == "OTA_CHECK_INTERVAL") {
            try {
                int n = std::stoi(value);
                if (n > 0) g_config.check_interval = n;
                else log_msg("Ignoring non-positive OTA_CHECK_INTERVAL: " + value);
            } catch (...) {
                log_msg("Invalid OTA_CHECK_INTERVAL: " + value);
            }
        } else if (key == "OTA_ENABLED") {
            g_config.enabled = (value == "1");
        } else if (key == "BOOT_PATH") {
            g_config.boot_path = value;
            g_config.version_file = value + "/ota_version";
        } else if (key == "CONFIG_FILE") {
            g_config.config_file = value;
        } else if (key == "LOG_FILE") {
            g_config.log_file = value;
        }
    }
    return true;
}

std::string get_version() {
    std::ifstream file(g_config.version_file);
    if (!file.is_open()) {
        log_msg("Version file not found, using default 1");
        return "1";
    }
    std::string version;
    std::getline(file, version);
    version = trimmed(version);
    return version.empty() ? "1" : version;
}

bool write_version(const std::string& version) {
    std::ofstream file(g_config.version_file);
    if (!file.is_open()) {
        log_msg("Failed to write version file: " + g_config.version_file);
        return false;
    }
    file << version << std::endl;
    file.close();
    if (!fsync_path(g_config.version_file))
        log_msg("Warning: fsync failed for " + g_config.version_file);
    return true;
}

// ==================== config.txt / kernel= ====================

// 解析非注释 kernel= 行；成功返回 IFS 名，否则空串
static std::string kernel_ifs_from_line(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] == '#') return "";
    if (line.compare(i, 7, "kernel=") != 0) return "";

    std::string name = trimmed(line.substr(i + 7));
    size_t hash = name.find('#');
    if (hash != std::string::npos)
        name = trimmed(name.substr(0, hash));
    return name;
}

std::string get_active_ifs() {
    std::ifstream file(g_config.config_file);
    if (!file.is_open()) {
        log_msg("Config file not found, assuming IFS_A is active");
        return IFS_A;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::string name = kernel_ifs_from_line(line);
        if (!name.empty()) return name;
    }
    return IFS_A;
}

// 从 config.txt.bak 恢复（写 kernel 失败时用，避免 kernel= 已改但镜像被删）
static bool restore_config_bak() {
    const std::string bak = g_config.config_file + ".bak";
    if (!file_exists(bak)) {
        log_msg("No config.bak to restore: " + bak);
        return false;
    }
    std::ifstream src(bak, std::ios::binary);
    std::ofstream dst(g_config.config_file, std::ios::binary | std::ios::trunc);
    if (!src.is_open() || !dst.is_open()) {
        log_msg("Failed to restore config from bak");
        return false;
    }
    dst << src.rdbuf();
    dst.close();
    src.close();
    fsync_path(g_config.config_file);
    log_msg("Restored config.txt from bak");
    return true;
}

// 只改 kernel=，不重启。写盘/fsync 失败时尽量恢复 bak，保证不留下「指向已删镜像」的 kernel=。
bool set_active_ifs(const std::string& new_ifs) {
    log_msg("Setting active IFS: " + new_ifs);

    const std::string bak = g_config.config_file + ".bak";
    {
        std::ifstream src(g_config.config_file, std::ios::binary);
        std::ofstream dst(bak, std::ios::binary);
        if (src.is_open() && dst.is_open())
            dst << src.rdbuf();
        else
            log_msg("Warning: could not backup config.txt");
    }

    std::vector<std::string> lines;
    bool found = false;
    {
        std::ifstream f(g_config.config_file);
        std::string line;
        while (f.is_open() && std::getline(f, line)) {
            if (!kernel_ifs_from_line(line).empty()) {
                lines.push_back("kernel=" + new_ifs);
                found = true;
            } else {
                lines.push_back(line);
            }
        }
    }
    if (!found) lines.push_back("kernel=" + new_ifs);

    {
        std::ofstream out(g_config.config_file);
        if (!out.is_open()) {
            log_msg("Failed to write config: " + g_config.config_file);
            return false;
        }
        for (const auto& l : lines) out << l << "\n";
    }

    if (!fsync_path(g_config.config_file)) {
        log_msg("fsync failed for config.txt, restoring bak");
        restore_config_bak();
        return false;
    }
    return true;
}

// 保持原写法
static void request_reboot() {
    log_msg("config.txt updated. System will reboot in 10 seconds.");
    mysleep(10);
    system("shutdown -v");
}

// ==================== HTTP ====================

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* user_p) {
    ((std::string*)user_p)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static bool curl_http_ok(CURL* curl, const std::string& url) {
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if (code >= 200 && code < 300) return true;
    log_msg("HTTP error " + std::to_string(code) + " for " + url);
    return false;
}

bool curl_get(const std::string& url, std::string& response) {
    response.clear();
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
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    CURLcode res = curl_easy_perform(curl);
    bool ok = (res == CURLE_OK) && curl_http_ok(curl, url);
    if (res != CURLE_OK)
        log_msg("CURL error: " + std::string(curl_easy_strerror(res)));
    curl_easy_cleanup(curl);
    if (!ok) response.clear();
    return ok;
}

static int progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t, curl_off_t) {
    int* last = static_cast<int*>(clientp);
    if (dltotal <= 0 || !last) return 0;
    int percent = (int)((dlnow * 100) / dltotal);
    int next = (*last < 0) ? 0 : (*last + 10);
    while (next <= percent && next <= 100) {
        std::cout << "[OTA] Download progress: " << next << "%" << std::endl;
        *last = next;
        next += 10;
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

    int last_progress = -1;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3600L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &last_progress);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    bool ok = (res == CURLE_OK) && curl_http_ok(curl, url);
    fflush(fp);
    {
        int fd = fileno(fp);
        if (fd >= 0) fsync(fd);
    }
    fclose(fp);
    curl_easy_cleanup(curl);

    if (!ok) {
        if (res != CURLE_OK)
            log_msg("Download failed: " + std::string(curl_easy_strerror(res)));
        remove_file(dest_file);
        return false;
    }
    log_msg("Download completed: " + dest_file);
    return true;
}

// ==================== 校验 ====================

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

bool verify_sha256(const std::string& file_path, const std::string& sha256_url) {
    log_msg("Downloading SHA256: " + sha256_url);
    std::string remote;
    if (!curl_get(sha256_url, remote)) {
        log_msg("Failed to download SHA256 sidecar");
        return false;
    }

    remote = trimmed(remote);
    size_t sp = remote.find_first_of(" \t\r\n");
    if (sp != std::string::npos) remote = remote.substr(0, sp);
    for (char& c : remote) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    }

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
    if (!file_exists(file_path)) {
        log_msg("IFS file not found: " + file_path);
        return false;
    }
    size_t n = get_file_size(file_path);
    if (n < MIN_IFS_SIZE) {
        log_msg("IFS file too small: " + std::to_string(n) + " bytes");
        return false;
    }
    log_msg("IFS verified: " + file_path + " (" + std::to_string(n) + " bytes)");
    return true;
}

// ==================== Config OTA ====================

static bool looks_like_ota_config(const std::string& content) {
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i < line.size() && line.compare(i, 11, "OTA_SERVER=") == 0)
            return true;
    }
    return false;
}

void check_and_update_config() {
    std::string remote;
    if (!curl_get(g_config.server_url + "/ota_config", remote) || remote.empty())
        return;
    if (!looks_like_ota_config(remote)) {
        log_msg("Remote /ota_config rejected (missing OTA_SERVER=)");
        return;
    }
    if (remote == read_file_content(g_config.ota_config_path))
        return;

    log_msg("Remote ota_config differs, applying update");
    const std::string tmp_path = "/tmp/ota_config.new";
    {
        std::ofstream tmp(tmp_path);
        if (!tmp.is_open()) {
            log_msg("Failed to write temp config");
            return;
        }
        tmp << remote;
    }

    std::string persist = g_config.ota_config_path;
    std::string local = read_file_content(g_config.ota_config_path);
    {
        std::ofstream dst(persist);
        if (!dst.is_open()) {
            persist = "/tmp/ota_config";
            log_msg("Cannot write " + g_config.ota_config_path + ", using " + persist);
            std::ofstream fb(persist);
            if (fb.is_open()) fb << remote;
        } else {
            dst << remote;
            std::ofstream bak(g_config.ota_config_path + ".bak");
            if (bak.is_open()) bak << local;
        }
    }

    read_config(tmp_path);
    g_config.ota_config_path = persist;
    log_msg("ota_config reloaded. Server: " + g_config.server_url);
}

// ==================== Pending 事务 ====================
//
// download+verify → write pending(槽,版本) → set kernel → reboot
//
// 版本提交规则（修假 commit）：
//   - 仅进程冷启动时 settle：active==expected → write_version（说明已用新镜像启动）
//   - 同会话内 config 已切、shutdown 未重启 → 不 commit，只阻塞新升级并等待重启
//
// pending 两行: IFS 名 \n 版本号

struct PendingInfo {
    std::string expected_ifs;
    std::string version;
    bool valid;
};

static bool write_pending(const std::string& target_ifs, const std::string& version) {
    const std::string path = pending_path();
    std::ofstream pf(path);
    if (!pf.is_open()) {
        log_msg("Failed to write pending: " + path);
        return false;
    }
    pf << target_ifs << "\n" << version << "\n";
    pf.close();
    if (!fsync_path(path))
        log_msg("Warning: fsync failed for " + path);
    return true;
}

static void clear_pending() {
    std::remove(pending_path().c_str());
}

static PendingInfo read_pending() {
    PendingInfo p = {"", "", false};
    std::ifstream pf(pending_path());
    if (!pf.is_open()) return p;
    std::getline(pf, p.expected_ifs);
    std::getline(pf, p.version);
    trim_inplace(p.expected_ifs);
    trim_inplace(p.version);
    p.valid = !p.expected_ifs.empty();
    return p;
}

// 冷启动调用一次：已用 pending 目标槽启动则提交版本
// 返回 true = 可进入主循环升级逻辑
bool settle_pending_on_boot() {
    PendingInfo p = read_pending();
    if (!p.valid) return true;

    std::string active = get_active_ifs();
    log_msg("Boot pending: expected=" + p.expected_ifs + " active=" + active +
            (p.version.empty() ? "" : (" version=" + p.version)));

    if (active == p.expected_ifs) {
        // 新槽已启动（OTA 进程能跑起来即基本证明可引导）
        if (!p.version.empty() && !write_version(p.version)) {
            log_msg("Commit version failed, will retry next boot");
            return false;  // 保留 pending
        }
        if (!p.version.empty())
            log_msg("Version committed after cold start: " + p.version);
        clear_pending();
        log_msg("OTA boot verified OK: " + active);
        return true;
    }

    // config 未指向目标：切换失败或被改回 → 清 pending，可重试
    std::string path = g_config.boot_path + "/" + p.expected_ifs;
    if (file_exists(path)) {
        log_msg("Removing unused IFS after failed switch: " + path);
        remove_file(path);
    }
    clear_pending();
    log_msg("Boot pending aborted, version unchanged");
    return true;
}

// 主循环：若 pending 仍在且 config 已是目标槽，说明同会话已切槽、尚未冷启动确认
// 不得 write_version；返回 true 表示应阻塞新升级
bool pending_blocks_update() {
    PendingInfo p = read_pending();
    if (!p.valid) return false;

    std::string active = get_active_ifs();
    if (active == p.expected_ifs) {
        log_msg("Pending awaits reboot (kernel already " + active +
                "); NOT committing version until cold start");
        return true;
    }

    // config 又不一致：清掉脏 pending，允许重试
    std::string path = g_config.boot_path + "/" + p.expected_ifs;
    if (file_exists(path)) {
        log_msg("Removing unused IFS: " + path);
        remove_file(path);
    }
    clear_pending();
    log_msg("Stale pending cleared");
    return false;
}

// ==================== 一次升级尝试 ====================

std::string get_server_version() {
    std::string response;
    if (!curl_get(g_config.server_url + "/version.txt", response)) {
        log_msg("Failed to check server version");
        return "";
    }
    response = trimmed(response);
    if (response.empty()) {
        log_msg("Empty server version response");
        return "";
    }
    return response;
}

// 下载 → 校验 → pending → 切槽 → 请求重启
// 返回 true = 已切换并请求重启；false = 本轮失败
bool try_apply_update(const std::string& server_version) {
    std::string active = get_active_ifs();
    std::string target = (active == IFS_A) ? IFS_B : IFS_A;
    std::string target_path = g_config.boot_path + "/" + target;
    std::string url = g_config.server_url + "/ifs-rpi5_v" + server_version + ".bin";

    log_msg("New version " + server_version + ", target=" + target);
    log_msg("Download URL: " + url);

    if (!download_file(url, target_path)) {
        log_msg("Download failed, will retry later");
        return false;
    }

    auto abort_before_switch = [&](const std::string& why) {
        log_msg(why);
        remove_file(target_path);
        clear_pending();
    };

    if (!verify_sha256(target_path, url + ".sha256")) {
        abort_before_switch("SHA256 verification failed");
        return false;
    }
    if (!verify_ifs(target_path)) {
        abort_before_switch("IFS size verification failed");
        return false;
    }
    if (!write_pending(target, server_version)) {
        abort_before_switch("Failed to write pending marker");
        return false;
    }

    // 写 kernel 失败：set_active_ifs 会 restore bak，可安全删未激活镜像
    if (!set_active_ifs(target)) {
        log_msg("Failed to update config.txt (restored if possible)");
        remove_file(target_path);
        clear_pending();
        return false;
    }

    // 此后禁止删 target；版本等冷启动后再 commit
    request_reboot();
    mysleep(60);
    if (file_exists(pending_path())) {
        log_msg("Still running after reboot request; version NOT committed until next cold start");
    }
    return true;
}

// ==================== 主循环 ====================

void ota_loop() {
    log_msg("OTA Client started");
    log_msg("Server: " + g_config.server_url);
    log_msg("Check interval: " + std::to_string(g_config.check_interval) + "s");

    if (!g_config.enabled) {
        log_msg("OTA is disabled in configuration");
        return;
    }

    // 仅冷启动时提交版本（禁止同会话假 commit）
    settle_pending_on_boot();

    while (true) {
        try {
            if (pending_blocks_update()) {
                // 已切槽、等重启：周期性再请求 reboot，仍不 write_version
                log_msg("Re-requesting reboot while pending...");
                request_reboot();
                mysleep(g_config.check_interval);
                continue;
            }

            check_and_update_config();

            std::string local  = get_version();
            std::string server = get_server_version();
            if (server.empty()) {
                mysleep(g_config.check_interval);
                continue;
            }

            log_msg("Local version: " + local + ", Server version: " + server);

            if (is_newer_version(server, local))
                try_apply_update(server);

            mysleep(g_config.check_interval);
        } catch (const std::exception& e) {
            log_msg("Exception in OTA loop: " + std::string(e.what()));
            mysleep(g_config.check_interval);
        }
    }
}

// ==================== 挂载 / main ====================

bool is_mounted(const std::string& mount_point) {
    auto scan = [&](std::istream& in) -> bool {
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            std::string dev, mp;
            if (ss >> dev >> mp && mp == mount_point)
                return true;
            const std::string token = " on " + mount_point;
            size_t p = line.find(token);
            if (p == std::string::npos) continue;
            size_t after = p + token.size();
            if (after == line.size() || line[after] == ' ' || line[after] == '\t')
                return true;
        }
        return false;
    };

    std::ifstream proc("/proc/mounts");
    if (proc.is_open() && scan(proc)) return true;

    system("mount > /tmp/mounts 2>/dev/null");
    std::ifstream fallback("/tmp/mounts");
    return fallback.is_open() && scan(fallback);
}

bool ensure_boot_mounted() {
    mkdir("/var/boot", 0755);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mount -t dos /dev/sd0t12 /var/boot");
    if (system(cmd) != 0) {
        printf("mount /dev/sd0t12 to /var/boot failed\n");
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    g_config.server_url      = "http://192.168.50.148:8080";
    g_config.check_interval  = 300;
    g_config.enabled         = true;
    g_config.boot_path       = "/var/boot";
    g_config.version_file    = g_config.boot_path + "/ota_version";
    g_config.config_file     = "/var/boot/config.txt";
    g_config.log_file        = "/tmp/ota_client.log";
    g_config.ota_config_path = "/etc/ota_config";

    if (!is_mounted("/var/boot")) {
        bool mounted = false;
        for (int i = 1; i <= 5; ++i) {
            log_msg("Mounting /var/boot, attempt " + std::to_string(i) + "/5");
            if (ensure_boot_mounted()) { mounted = true; break; }
            if (i < 5) mysleep(3);
        }
        if (!mounted) {
            log_msg("Failed to mount /var/boot, exiting");
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
            std::cout << "Usage: ota_client [-c file] [-d] [-h]\n";
            return 0;
        }
    }

    if (!read_config(config_file)) {
        std::cerr << "Failed to read configuration file\n";
        return 1;
    }

    if (daemonize) {
        // QNX 原生守护进程化（避免 fork/setsid 在部分工具链下声明缺失）
        // NOCLOSE：保留 net_start.sh 重定向的 stdout/stderr 日志
        if (procmgr_daemon(EXIT_SUCCESS,
                           PROCMGR_DAEMON_NOCLOSE | PROCMGR_DAEMON_NODEVNULL) == -1) {
            log_msg(std::string("procmgr_daemon failed: ") + strerror(errno) +
                    ", continuing in foreground");
        }
    }

    ota_loop();
    log_msg("OTA Client exiting");
    return 0;
}
