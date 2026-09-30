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
#include <ctime>
#include <time.h>
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
// 用 O_RDWR 打开以确保 FAT 驱动正确刷盘
static bool fsync_path(const std::string& path) {
    int fd = open(path.c_str(), O_RDWR);
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

// 解析 config.txt 的 kernel= 行。
// 失败（读不到 / 无 kernel= 行）返回 false：此时无法知道哪个槽在运行，
// 调用方必须中止升级而不是猜测——猜错会把新镜像写进正在运行的 IFS。
bool get_active_ifs(std::string* out) {
    if (out) out->clear();
    std::ifstream file(g_config.config_file);
    if (!file.is_open()) {
        log_msg("Config file not found: " + g_config.config_file);
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::string name = kernel_ifs_from_line(line);
        if (!name.empty()) {
            if (out) *out = name;
            return true;
        }
    }
    log_msg("No kernel= line in " + g_config.config_file);
    return false;
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

    // 原子替换：先写 .tmp，fsync，再 rename（FAT 支持 rename）
    const std::string tmp = g_config.config_file + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out.is_open()) {
            log_msg("Failed to write temp config: " + tmp);
            return false;
        }
        for (const auto& l : lines) out << l << "\n";
        // close 会 flush 到内核 page cache
    }
    if (!fsync_path(tmp)) {
        log_msg("fsync failed for temp config, aborting update");
        remove_file(tmp.c_str());
        restore_config_bak();
        return false;
    }

    if (rename(tmp.c_str(), g_config.config_file.c_str()) != 0) {
        log_msg("rename failed for config.txt, restoring bak");
        remove_file(tmp.c_str());
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
// download+verify → write pending(槽,版本,切槽时uptime,切槽时墙钟,见证位) → set kernel → reboot
//
// 版本提交规则（修假 commit）：只有系统真正重启过才 settle（active==expected 且
// 确认重启过）→ write_version。判据按可靠性排序：
//
//   1) 见证文件（硬判据）：切槽时在 /tmp/ota_switch_witness 留一个文件。
//      /tmp 是 rpi5.build 的 [type=link] /dev/shmem，即 procnto 的内核内存
//      命名空间，重启必然清空：文件还在 = 同一个 boot（绝不 commit），
//      文件没了 = 重启过。不依赖时钟，不受校时影响。创建失败时见证位记 0，
//      退回下面两个时钟判据。
//   2) CLOCK_MONOTONIC 回绕（当前 uptime < 切槽时 uptime）：QNX 文档明确
//      CLOCK_MONOTONIC 的 tv_sec 是 seconds since system boot 且不可调整，
//      重启归零（板上实测 5.5h 开机对应 monotonic=19904s）。
//   3) boot 时刻推进：墙钟减 uptime 就是内核记录的 UTC seconds when machine
//      booted（板上实测与 qtime->boot_time 一致）。切槽到下次内核启动之间必有
//      request_reboot() 的 10s 等待 + 关机 + 固件重启，实测约 12-16s；仅进程
//      重启时该值约 0，阈值取 5s 两侧留余量。这条补 2) 的盲区：切槽若发生在
//      开机后很早（记录 uptime 很小），重启后到 settle 的 uptime 可能仍
//      ≥ 记录值，只看回绕会误判未重启而反复请求重启。
//
// pending 五行: IFS 名 / 版本号 / 切槽时 uptime 秒 / 切槽时墙钟秒 / 见证位(0|1)
// 旧格式（缺后三行）按旧行为视为已重启，避免升级后卡死。

struct PendingInfo {
    std::string expected_ifs;
    std::string version;
    int64_t switch_uptime_sec;   // -1 = 旧格式无此字段
    int64_t switch_wall_sec;     //  0 = 旧格式无此字段
    bool witness_ok;             // 切槽时见证文件创建成功
    bool valid;
};

// /dev/shmem 命名空间里的文件：重启必被清空，是“重启过”的硬证据
static const char* kRebootWitnessPath = "/tmp/ota_switch_witness";

static int64_t uptime_sec_now() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return (int64_t)time(nullptr);   // 退化为墙钟（启动早期两者近似）
    return (int64_t)ts.tv_sec;
}

static bool write_pending(const std::string& target_ifs, const std::string& version) {
    // 见证文件先写：只要它存在，就说明系统自切槽起没重启过
    bool witness_ok = false;
    {
        std::ofstream wf(kRebootWitnessPath);
        if (wf.is_open()) {
            wf << target_ifs << " " << version << "\n";
            wf.close();
            witness_ok = true;
        } else {
            log_msg("Warning: cannot create reboot witness " +
                    std::string(kRebootWitnessPath) + "; will use clock checks");
        }
    }

    const std::string path = pending_path();
    std::ofstream pf(path);
    if (!pf.is_open()) {
        log_msg("Failed to write pending: " + path);
        return false;
    }
    pf << target_ifs << "\n" << version << "\n"
       << uptime_sec_now() << "\n" << (int64_t)time(nullptr) << "\n"
       << (witness_ok ? 1 : 0) << "\n";
    pf.close();
    if (!fsync_path(path))
        log_msg("Warning: fsync failed for " + path);
    return true;
}

static void clear_pending() {
    std::remove(pending_path().c_str());
    // 事务结束：没重启的路径（abort/陈旧 pending）下见证文件还在，一并清掉
    std::remove(kRebootWitnessPath);
}

static PendingInfo read_pending() {
    PendingInfo p = {"", "", -1, 0, false, false};
    std::ifstream pf(pending_path());
    if (!pf.is_open()) return p;
    std::getline(pf, p.expected_ifs);
    std::getline(pf, p.version);
    std::string up, wall, wit;
    std::getline(pf, up);
    std::getline(pf, wall);
    std::getline(pf, wit);
    trim_inplace(p.expected_ifs);
    trim_inplace(p.version);
    trim_inplace(up);
    trim_inplace(wall);
    trim_inplace(wit);
    if (!up.empty()) {
        try { p.switch_uptime_sec = std::stoll(up); } catch (...) {}
    }
    if (!wall.empty()) {
        try { p.switch_wall_sec = std::stoll(wall); } catch (...) {}
    }
    p.witness_ok = (wit == "1");
    p.valid = !p.expected_ifs.empty();
    return p;
}

// “写下 pending 之后系统是否真的重启过”
static bool system_rebooted_since(const PendingInfo& p) {
    if (p.witness_ok)
        return !file_exists(kRebootWitnessPath);   // 判据 1：内存 fs 被重启清空
    if (p.switch_uptime_sec < 0 || p.switch_wall_sec <= 0)
        return true;                               // 旧格式：按旧行为视为已重启
    const int64_t u_now = uptime_sec_now();
    if (u_now < p.switch_uptime_sec)
        return true;                               // 判据 2：单调钟回绕
    const int64_t boot_ts_then = p.switch_wall_sec - p.switch_uptime_sec;
    const int64_t boot_ts_now  = (int64_t)time(nullptr) - u_now;
    return boot_ts_now > boot_ts_then + 5;         // 判据 3：boot 时刻推进 >5s
}

// 系统真正重启后调用一次：已用 pending 目标槽启动且确认发生过重启则提交版本
// 返回 true = 可进入主循环升级逻辑
bool settle_pending_on_boot() {
    PendingInfo p = read_pending();
    if (!p.valid) return true;

    std::string active;
    if (!get_active_ifs(&active)) {
        // 连活跃槽都读不出来：不动 pending、不删镜像，等下一轮再试。
        // 保守处理，绝不在此状态下做任何破坏性动作。
        log_msg("Boot pending: cannot read active IFS, deferring settle");
        return false;
    }
    log_msg("Boot pending: expected=" + p.expected_ifs + " active=" + active +
            (p.version.empty() ? "" : (" version=" + p.version)));

    if (active == p.expected_ifs) {
        if (!system_rebooted_since(p)) {
            // 只是 ota_client 进程重启、系统没重启（shutdown 失败/进程被杀后拉起）：
            // 绝不提交版本。保持 pending，主循环会继续强制请求重启。
            log_msg("Pending awaits true cold start (no reboot evidence); "
                    "NOT committing version");
            return false;
        }
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

// 主循环：若 pending 仍在且 config 已是目标槽，说明尚未冷启动确认
// 不得 write_version；返回 true 表示应阻塞新升级。
// out_need_reboot=false 用于“读不出 config.txt”的情形：此时既不能确认槽位、
// 也不能靠重启获得任何新信息，若仍周期请求重启会变成重启循环。
bool pending_blocks_update(bool* out_need_reboot) {
    if (out_need_reboot) *out_need_reboot = true;
    PendingInfo p = read_pending();
    if (!p.valid) return false;

    std::string active;
    if (!get_active_ifs(&active)) {
        // 读不出活跃槽：只阻塞，不清 pending/不删镜像/不请求重启——
        // 此状态下任何猜测都可能指错槽。恢复需人工修好 config.txt。
        log_msg("Pending present but active IFS unreadable; blocking updates "
                "(not requesting reboot)");
        if (out_need_reboot) *out_need_reboot = false;
        return true;
    }
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
// 检查 /var/boot (FAT) 剩余空间是否足够存放 IFS + 元数据
static bool check_boot_space(size_t required_bytes) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "df -k %s | tail -1 | awk '{print $4}'", g_config.boot_path.c_str());
    FILE* f = popen(cmd, "r");
    if (!f) {
        log_msg("check_boot_space: popen df failed");
        return false;
    }
    long free_kb = 0;
    if (fscanf(f, "%ld", &free_kb) != 1 || free_kb <= 0) {
        pclose(f);
        log_msg("check_boot_space: df returned invalid value");
        return false;
    }
    pclose(f);

    // 需要 IFS 大小 + 1MB 余量（FAT 元数据更新）
    long required_kb = (long)(required_bytes / 1024) + 1024;
    if (free_kb < required_kb) {
        log_msg("check_boot_space: only " + std::to_string(free_kb) + " KB free, need " + std::to_string(required_kb) + " KB");
        return false;
    }
    log_msg("check_boot_space: OK (" + std::to_string(free_kb) + " KB free)");
    return true;
}

bool try_apply_update(const std::string& server_version) {
    if (!check_boot_space(MIN_IFS_SIZE)) {
        log_msg("Insufficient space on /var/boot, skipping update this cycle");
        return false;
    }
    std::string active;
    if (!get_active_ifs(&active)) {
        // 无法确认当前运行的槽：宁可放弃本轮，也不能猜。
        // 猜错会把新镜像写进正在运行的 IFS（违反 A/B 不变量）。
        log_msg("Cannot determine active IFS, skipping update this cycle");
        return false;
    }
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
            bool need_reboot = true;
            if (pending_blocks_update(&need_reboot)) {
                // 已切槽、等重启：周期性再请求 reboot，仍不 write_version
                if (need_reboot) {
                    log_msg("Re-requesting reboot while pending...");
                    request_reboot();
                } else {
                    log_msg("Pending blocked on unreadable config; no reboot requested");
                }
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
