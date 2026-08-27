#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <uxtheme.h>
#include <winver.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kWindowClass[] = L"OMGNetworkMonitorWindow";
constexpr wchar_t kAppName[] = L"OMG Network Monitor";
constexpr UINT WM_SNAPSHOT_READY = WM_APP + 1;
constexpr UINT WM_TRAY = WM_APP + 2;
constexpr UINT WM_FIREWALL_RESULT = WM_APP + 3;
constexpr UINT TIMER_POLL = 1;
constexpr UINT TIMER_ANIMATION = 2;
constexpr UINT ID_SEARCH = 100;
constexpr UINT ID_PAUSE = 101;
constexpr UINT ID_EXPORT = 102;
constexpr UINT ID_LOGS = 103;
constexpr UINT ID_CONFIG = 104;
constexpr UINT ID_RELOAD = 105;
constexpr UINT ID_COPY = 106;
constexpr UINT ID_EXIT = 107;
constexpr UINT ID_LIST = 120;
constexpr UINT ID_TRAY_SHOW = 201;
constexpr UINT ID_TRAY_EXIT = 202;
constexpr wchar_t kMaterialButtonClass[] = L"OMGMaterial3Button";

struct Config {
    bool startWithWindows = true;
    int pollIntervalMs = 1000;
    int keepClosedSeconds = 90;
    int retentionDays = 30;
    double uploadAlertMbps = 20.0;
    bool logConnectionEvents = true;
    bool logMinuteMetrics = true;
    bool startMinimizedWithWindows = true;
};

struct AdapterStat {
    std::wstring name;
    unsigned long long received = 0;
    unsigned long long sent = 0;
};

struct RawConnection {
    std::wstring key;
    std::wstring protocol;
    DWORD pid = 0;
    std::wstring process;
    std::wstring processPath;
    std::wstring local;
    std::wstring remote;
    std::wstring state;
    bool review = false;
};

struct Snapshot {
    std::vector<RawConnection> connections;
    std::vector<AdapterStat> adapters;
    unsigned long long totalReceived = 0;
    unsigned long long totalSent = 0;
    double downloadBps = 0;
    double uploadBps = 0;
};

struct DisplayConnection : RawConnection {
    std::chrono::system_clock::time_point firstSeen;
    std::chrono::system_clock::time_point lastSeen;
    bool active = true;
};

struct FirewallResult {
    std::wstring key;
    bool blocked = false;
    bool success = false;
};

struct MaterialButtonState {
    bool hover = false;
    bool down = false;
    bool rippling = false;
    POINT rippleOrigin{};
    DWORD rippleStarted = 0;
};

HWND g_window = nullptr;
HWND g_search = nullptr;
HWND g_list = nullptr;
std::vector<HWND> g_buttons;
std::unordered_map<HWND, MaterialButtonState> g_buttonStates;
HFONT g_font = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_valueFont = nullptr;
HBRUSH g_editBrush = nullptr;
HICON g_appIcon = nullptr;
HIMAGELIST g_rowHeightImages = nullptr;
ULONG_PTR g_gdiplusToken = 0;
HDC g_graphBufferDc = nullptr;
HBITMAP g_graphBufferBitmap = nullptr;
HGDIOBJ g_graphBufferOldBitmap = nullptr;
int g_graphBufferWidth = 0;
int g_graphBufferHeight = 0;
int g_dpi = 96;
Config g_config;
fs::path g_exePath;
fs::path g_configPath;
fs::path g_dataDir;
fs::path g_logDir;
std::map<std::wstring, DisplayConnection> g_connections;
std::vector<double> g_downHistory(120, 0.0);
std::vector<double> g_upHistory(120, 0.0);
ULONGLONG g_sampleAnimationStarted = 0;
double g_graphMaximum = 1761.28;
double g_graphScaleStart = 1761.28;
double g_graphScaleTarget = 1761.28;
std::atomic<bool> g_captureBusy{false};
bool g_paused = false;
bool g_exiting = false;
bool g_trayAdded = false;
bool g_closeHintShown = false;
bool g_searchPlaceholder = true;
UINT g_showMessage = 0;
double g_downloadBps = 0;
double g_uploadBps = 0;
unsigned long long g_totalReceived = 0;
unsigned long long g_totalSent = 0;
std::vector<AdapterStat> g_adapters;
std::wstring g_status = L"Starting system-wide monitoring…";
std::chrono::steady_clock::time_point g_statusUntil;
long long g_lastMetricMinute = -1;
std::set<std::wstring> g_blockedRules;
std::wstring g_rowRippleKey;
DWORD g_rowRippleStarted = 0;
POINT g_rowRippleOrigin{};

int S(int value) { return MulDiv(value, g_dpi, 96); }

std::wstring trim(std::wstring text) {
    const wchar_t* ws = L" \t\r\n";
    const auto first = text.find_first_not_of(ws);
    if (first == std::wstring::npos) return L"";
    return text.substr(first, text.find_last_not_of(ws) - first + 1);
}

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), towlower);
    return text;
}

bool parseBool(const std::wstring& value, bool fallback) {
    const auto v = lower(trim(value));
    if (v == L"true" || v == L"yes" || v == L"1") return true;
    if (v == L"false" || v == L"no" || v == L"0") return false;
    return fallback;
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring fromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring friendlyProcessName(const DisplayConnection& connection) {
    std::wstring name = connection.process;
    if (name.rfind(L"Unknown", 0) == 0) return L"Unknown process";
    const auto dot = lower(name).rfind(L".exe");
    if (dot != std::wstring::npos && dot + 4 == name.size()) name.resize(dot);
    const auto normalized = lower(name);
    if (normalized == L"wechat" || normalized == L"weixin" || normalized == L"wechatappex") return L"Weixin";
    return name.empty() ? L"Unknown process" : name;
}

std::wstring fileDescription(const std::wstring& path) {
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!bytes) return L"";
    std::vector<BYTE> data(bytes);
    if (!GetFileVersionInfoW(path.c_str(), 0, bytes, data.data())) return L"";
    struct LanguageCode { WORD language; WORD codePage; };
    LanguageCode* translations = nullptr;
    UINT translationBytes = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &translationBytes) ||
        translationBytes < sizeof(LanguageCode)) return L"";
    for (UINT i = 0; i < translationBytes / sizeof(LanguageCode); ++i) {
        wchar_t query[96]{};
        swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[i].language, translations[i].codePage);
        wchar_t* value = nullptr;
        UINT length = 0;
        if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &length) && value && length > 1) return trim(value);
    }
    return L"";
}

bool splitEndpoint(const std::wstring& endpoint, std::wstring& host, int& port) {
    host.clear();
    port = 0;
    if (endpoint.empty() || endpoint == L"—") return false;
    try {
        if (endpoint.front() == L'[') {
            const auto close = endpoint.find(L']');
            if (close == std::wstring::npos) return false;
            host = endpoint.substr(1, close - 1);
            if (close + 2 < endpoint.size()) port = std::stoi(endpoint.substr(close + 2));
        } else {
            const auto colon = endpoint.rfind(L':');
            if (colon == std::wstring::npos) return false;
            host = endpoint.substr(0, colon);
            port = std::stoi(endpoint.substr(colon + 1));
        }
    } catch (...) { return false; }
    return !host.empty();
}

std::wstring firewallKey(const DisplayConnection& connection) {
    std::wstring host;
    int port = 0;
    splitEndpoint(connection.remote, host, port);
    return lower(connection.processPath) + L"|" + lower(host);
}

std::wstring firewallRuleName(const std::wstring& key) {
    unsigned long long hash = 1469598103934665603ULL;
    for (wchar_t c : key) { hash ^= static_cast<unsigned short>(c); hash *= 1099511628211ULL; }
    std::wostringstream out;
    out << L"OMG_NetworkMonitor_" << std::hex << std::setw(16) << std::setfill(L'0') << hash;
    return out.str();
}

bool isBlocked(const DisplayConnection& connection) {
    return g_blockedRules.count(firewallKey(connection)) != 0;
}

fs::path blocklistPath() { return g_dataDir / L"blocked-connections.txt"; }

void loadBlocklist() {
    g_blockedRules.clear();
    std::ifstream input(blocklistPath(), std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) g_blockedRules.insert(fromUtf8(line));
    }
}

void saveBlocklist() {
    std::error_code ec;
    fs::create_directories(g_dataDir, ec);
    std::ofstream output(blocklistPath(), std::ios::binary | std::ios::trunc);
    for (const auto& key : g_blockedRules) output << utf8(key) << "\n";
}

std::wstring jsonEscape(const std::wstring& value) {
    std::wstring out;
    for (wchar_t c : value) {
        switch (c) {
            case L'\\': out += L"\\\\"; break;
            case L'\"': out += L"\\\""; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::wstring isoTime(std::chrono::system_clock::time_point point = std::chrono::system_clock::now()) {
    const auto value = std::chrono::system_clock::to_time_t(point);
    tm local{};
    localtime_s(&local, &value);
    wchar_t buffer[40]{};
    wcsftime(buffer, std::size(buffer), L"%Y-%m-%dT%H:%M:%S", &local);
    return buffer;
}

std::wstring shortTime(std::chrono::system_clock::time_point point) {
    const auto value = std::chrono::system_clock::to_time_t(point);
    tm local{};
    localtime_s(&local, &value);
    wchar_t buffer[16]{};
    wcsftime(buffer, std::size(buffer), L"%H:%M:%S", &local);
    return buffer;
}

std::wstring dateStamp() {
    const auto value = std::time(nullptr);
    tm local{};
    localtime_s(&local, &value);
    wchar_t buffer[20]{};
    wcsftime(buffer, std::size(buffer), L"%Y-%m-%d", &local);
    return buffer;
}

std::wstring fileStamp() {
    const auto value = std::time(nullptr);
    tm local{};
    localtime_s(&local, &value);
    wchar_t buffer[32]{};
    wcsftime(buffer, std::size(buffer), L"%Y%m%d-%H%M%S", &local);
    return buffer;
}

std::wstring formatBytes(double bytes, bool perSecond = false) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    int unit = 0;
    while (bytes >= 1024.0 && unit < 4) { bytes /= 1024.0; ++unit; }
    std::wostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : (bytes >= 100 ? 0 : 1)) << bytes << L" " << units[unit];
    if (perSecond) out << L"/s";
    return out.str();
}

void setStatus(const std::wstring& text, int seconds = 5) {
    g_status = text;
    g_statusUntil = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    if (g_window) InvalidateRect(g_window, nullptr, FALSE);
}

Config loadConfig() {
    Config cfg;
    std::wifstream input(g_configPath);
    std::wstring line;
    while (std::getline(input, line)) {
        const auto comment = line.find(L'#');
        if (comment != std::wstring::npos) line.resize(comment);
        const auto colon = line.find(L':');
        if (colon == std::wstring::npos) continue;
        const auto key = lower(trim(line.substr(0, colon)));
        const auto value = trim(line.substr(colon + 1));
        try {
            if (key == L"start_with_windows") cfg.startWithWindows = parseBool(value, cfg.startWithWindows);
            else if (key == L"poll_interval_ms") cfg.pollIntervalMs = std::clamp(std::stoi(value), 250, 10000);
            else if (key == L"keep_closed_seconds") cfg.keepClosedSeconds = std::clamp(std::stoi(value), 0, 3600);
            else if (key == L"retention_days") cfg.retentionDays = std::clamp(std::stoi(value), 1, 3650);
            else if (key == L"upload_alert_mbps") cfg.uploadAlertMbps = std::max(0.0, std::stod(value));
            else if (key == L"log_connection_events") cfg.logConnectionEvents = parseBool(value, cfg.logConnectionEvents);
            else if (key == L"log_minute_metrics") cfg.logMinuteMetrics = parseBool(value, cfg.logMinuteMetrics);
            else if (key == L"start_minimized_with_windows") cfg.startMinimizedWithWindows = parseBool(value, cfg.startMinimizedWithWindows);
        } catch (...) {}
    }
    return cfg;
}

void updateAutostart() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    if (g_config.startWithWindows) {
        const std::wstring command = L"\"" + g_exePath.wstring() + L"\" --autostart";
        RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kAppName);
    }
    RegCloseKey(key);
}

void cleanupLogs() {
    std::error_code ec;
    if (!fs::exists(g_logDir, ec)) return;
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24LL * g_config.retentionDays);
    for (const auto& entry : fs::directory_iterator(g_logDir, ec)) {
        if (entry.is_regular_file(ec) && entry.last_write_time(ec) < cutoff) fs::remove(entry.path(), ec);
    }
}

void appendLog(const std::wstring& json) {
    std::error_code ec;
    fs::create_directories(g_logDir, ec);
    std::ofstream output(g_logDir / (L"events-" + dateStamp() + L".jsonl"), std::ios::app | std::ios::binary);
    output << utf8(json) << "\n";
}

void logConnection(const wchar_t* event, const DisplayConnection& c) {
    if (!g_config.logConnectionEvents) return;
    std::wostringstream out;
    out << L"{\"time\":\"" << isoTime() << L"\",\"event\":\"" << event
        << L"\",\"protocol\":\"" << c.protocol << L"\",\"pid\":" << c.pid
        << L",\"process\":\"" << jsonEscape(c.process) << L"\",\"process_path\":\"" << jsonEscape(c.processPath)
        << L"\",\"local\":\"" << jsonEscape(c.local) << L"\",\"remote\":\"" << jsonEscape(c.remote)
        << L"\",\"state\":\"" << jsonEscape(c.state) << L"\",\"review\":" << (c.review ? L"true" : L"false") << L"}";
    appendLog(out.str());
}

std::wstring tcpState(DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED: return L"CLOSED";
        case MIB_TCP_STATE_LISTEN: return L"LISTEN";
        case MIB_TCP_STATE_SYN_SENT: return L"SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD: return L"SYN_RCVD";
        case MIB_TCP_STATE_ESTAB: return L"ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1: return L"FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2: return L"FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT: return L"CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING: return L"CLOSING";
        case MIB_TCP_STATE_LAST_ACK: return L"LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT: return L"TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return L"DELETE_TCB";
        default: return L"UNKNOWN";
    }
}

std::wstring baseName(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring snapshotProcessName(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return L"";
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::wstring result;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) { result = entry.szExeFile; break; }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

std::pair<std::wstring, std::wstring> processInfo(DWORD pid) {
    static std::unordered_map<DWORD, std::pair<std::wstring, std::wstring>> cache;
    if (pid == 0) return {L"System Idle", L""};
    if (pid == 4) return {L"System", L""};
    if (const auto found = cache.find(pid); found != cache.end()) return found->second;
    std::wstring path;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        std::vector<wchar_t> buffer(32768);
        DWORD size = static_cast<DWORD>(buffer.size());
        if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) path.assign(buffer.data(), size);
        CloseHandle(process);
    }
    std::wstring displayName;
    if (!path.empty()) {
        displayName = fileDescription(path);
        if (displayName.empty()) displayName = baseName(path);
        const auto executable = lower(baseName(path));
        if (executable == L"wechat.exe" || executable == L"weixin.exe" || executable == L"wechatappex.exe") displayName = L"Weixin";
    }
    const std::wstring snapshotName = path.empty() ? snapshotProcessName(pid) : L"";
    std::pair<std::wstring, std::wstring> result = path.empty()
        ? std::make_pair(snapshotName.empty() ? L"Unknown" : snapshotName, L"")
        : std::make_pair(displayName, path);
    cache[pid] = result;
    return result;
}

std::wstring endpoint4(DWORD address, DWORD port) {
    IN_ADDR addr{};
    addr.S_un.S_addr = address;
    wchar_t text[INET_ADDRSTRLEN]{};
    InetNtopW(AF_INET, &addr, text, std::size(text));
    return std::wstring(text) + L":" + std::to_wstring(ntohs(static_cast<u_short>(port)));
}

std::wstring endpoint6(const UCHAR address[16], DWORD scope, DWORD port) {
    IN6_ADDR addr{};
    memcpy(&addr, address, 16);
    wchar_t text[INET6_ADDRSTRLEN]{};
    InetNtopW(AF_INET6, &addr, text, std::size(text));
    std::wstring result = L"[" + std::wstring(text);
    if (scope) result += L"%" + std::to_wstring(scope);
    return result + L"]:" + std::to_wstring(ntohs(static_cast<u_short>(port)));
}

bool reviewPort(DWORD networkPort) {
    const int port = ntohs(static_cast<u_short>(networkPort));
    return port == 1337 || port == 4444 || port == 5555 || port == 6667 || port == 31337;
}

void addConnection(std::vector<RawConnection>& list, const std::wstring& protocol, DWORD pid,
                   const std::wstring& local, const std::wstring& remote, const std::wstring& state, bool review) {
    const auto info = processInfo(pid);
    RawConnection c;
    c.protocol = protocol;
    c.pid = pid;
    c.process = info.first;
    c.processPath = info.second;
    c.local = local;
    c.remote = remote;
    c.state = state;
    c.review = review || (info.second.empty() && pid > 4 && remote != L"—" && state == L"ESTABLISHED");
    c.key = protocol + L"|" + std::to_wstring(pid) + L"|" + local + L"|" + remote;
    list.push_back(std::move(c));
}

template <typename T, typename C>
std::vector<unsigned char> queryTable(T function, int family, C tableClass) {
    ULONG size = 0;
    function(nullptr, &size, FALSE, family, tableClass, 0);
    std::vector<unsigned char> buffer(size);
    if (size && function(buffer.data(), &size, FALSE, family, tableClass, 0) == NO_ERROR) return buffer;
    return {};
}

void captureConnections(std::vector<RawConnection>& list) {
    {
        auto buffer = queryTable(GetExtendedTcpTable, AF_INET, TCP_TABLE_OWNER_PID_ALL);
        if (!buffer.empty()) {
            auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& r = table->table[i];
                const bool noRemote = r.dwState == MIB_TCP_STATE_LISTEN;
                addConnection(list, L"TCP4", r.dwOwningPid, endpoint4(r.dwLocalAddr, r.dwLocalPort),
                              noRemote ? L"—" : endpoint4(r.dwRemoteAddr, r.dwRemotePort), tcpState(r.dwState),
                              !noRemote && reviewPort(r.dwRemotePort));
            }
        }
    }
    {
        auto buffer = queryTable(GetExtendedTcpTable, AF_INET6, TCP_TABLE_OWNER_PID_ALL);
        if (!buffer.empty()) {
            auto* table = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& r = table->table[i];
                const bool noRemote = r.dwState == MIB_TCP_STATE_LISTEN;
                addConnection(list, L"TCP6", r.dwOwningPid, endpoint6(r.ucLocalAddr, r.dwLocalScopeId, r.dwLocalPort),
                              noRemote ? L"—" : endpoint6(r.ucRemoteAddr, r.dwRemoteScopeId, r.dwRemotePort), tcpState(r.dwState),
                              !noRemote && reviewPort(r.dwRemotePort));
            }
        }
    }
    {
        auto buffer = queryTable(GetExtendedUdpTable, AF_INET, UDP_TABLE_OWNER_PID);
        if (!buffer.empty()) {
            auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& r = table->table[i];
                addConnection(list, L"UDP4", r.dwOwningPid, endpoint4(r.dwLocalAddr, r.dwLocalPort), L"—", L"BOUND", false);
            }
        }
    }
    {
        auto buffer = queryTable(GetExtendedUdpTable, AF_INET6, UDP_TABLE_OWNER_PID);
        if (!buffer.empty()) {
            auto* table = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& r = table->table[i];
                addConnection(list, L"UDP6", r.dwOwningPid, endpoint6(r.ucLocalAddr, r.dwLocalScopeId, r.dwLocalPort), L"—", L"BOUND", false);
            }
        }
    }
}

Snapshot* captureSnapshot() {
    static unsigned long long previousReceived = 0;
    static unsigned long long previousSent = 0;
    static auto previousTime = std::chrono::steady_clock::now();
    auto* snapshot = new Snapshot;
    captureConnections(snapshot->connections);

    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) == NO_ERROR && table) {
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const auto& row = table->Table[i];
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) continue;
            AdapterStat adapter;
            adapter.name = row.Alias[0] ? row.Alias : row.Description;
            adapter.received = row.InOctets;
            adapter.sent = row.OutOctets;
            snapshot->adapters.push_back(adapter);
            snapshot->totalReceived += row.InOctets;
            snapshot->totalSent += row.OutOctets;
        }
        FreeMibTable(table);
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - previousTime).count();
    if (previousReceived && elapsed > 0.05) {
        if (snapshot->totalReceived >= previousReceived) snapshot->downloadBps = (snapshot->totalReceived - previousReceived) / elapsed;
        if (snapshot->totalSent >= previousSent) snapshot->uploadBps = (snapshot->totalSent - previousSent) / elapsed;
    }
    previousReceived = snapshot->totalReceived;
    previousSent = snapshot->totalSent;
    previousTime = now;
    return snapshot;
}

void startCapture() {
    if (g_paused || g_captureBusy.exchange(true)) return;
    std::thread([] {
        Snapshot* snapshot = captureSnapshot();
        if (!PostMessageW(g_window, WM_SNAPSHOT_READY, 0, reinterpret_cast<LPARAM>(snapshot))) delete snapshot;
        g_captureBusy = false;
    }).detach();
}

void logMetrics() {
    if (!g_config.logMinuteMetrics) return;
    std::wostringstream out;
    out << L"{\"time\":\"" << isoTime() << L"\",\"event\":\"minute_metrics\",\"download_bps\":"
        << static_cast<unsigned long long>(g_downloadBps) << L",\"upload_bps\":" << static_cast<unsigned long long>(g_uploadBps)
        << L",\"total_received\":" << g_totalReceived << L",\"total_sent\":" << g_totalSent << L",\"adapters\":[";
    for (size_t i = 0; i < g_adapters.size(); ++i) {
        if (i) out << L",";
        out << L"{\"name\":\"" << jsonEscape(g_adapters[i].name) << L"\",\"received\":" << g_adapters[i].received
            << L",\"sent\":" << g_adapters[i].sent << L"}";
    }
    out << L"]}";
    appendLog(out.str());
}

void rebuildList();

void applySnapshot(std::unique_ptr<Snapshot> snapshot) {
    const auto now = std::chrono::system_clock::now();
    std::set<std::wstring> current;
    for (auto& raw : snapshot->connections) {
        current.insert(raw.key);
        auto found = g_connections.find(raw.key);
        if (found == g_connections.end()) {
            DisplayConnection display;
            static_cast<RawConnection&>(display) = std::move(raw);
            display.firstSeen = display.lastSeen = now;
            display.active = true;
            auto inserted = g_connections.emplace(display.key, std::move(display));
            logConnection(L"connection_open", inserted.first->second);
        } else {
            const auto first = found->second.firstSeen;
            static_cast<RawConnection&>(found->second) = std::move(raw);
            found->second.firstSeen = first;
            found->second.lastSeen = now;
            found->second.active = true;
        }
    }

    for (auto& [key, connection] : g_connections) {
        if (connection.active && !current.count(key)) {
            connection.active = false;
            connection.lastSeen = now;
            logConnection(L"connection_close", connection);
        }
    }
    const auto cutoff = now - std::chrono::seconds(g_config.keepClosedSeconds);
    for (auto it = g_connections.begin(); it != g_connections.end();) {
        if (!it->second.active && it->second.lastSeen < cutoff) it = g_connections.erase(it);
        else ++it;
    }

    g_downloadBps = snapshot->downloadBps;
    g_uploadBps = snapshot->uploadBps;
    g_totalReceived = snapshot->totalReceived;
    g_totalSent = snapshot->totalSent;
    g_adapters = std::move(snapshot->adapters);
    g_downHistory.erase(g_downHistory.begin());
    g_downHistory.push_back(g_downloadBps);
    g_upHistory.erase(g_upHistory.begin());
    g_upHistory.push_back(g_uploadBps);
    g_graphScaleStart = g_graphMaximum;
    double graphPeak = 1024.0;
    for (double value : g_downHistory) graphPeak = std::max(graphPeak, value);
    for (double value : g_upHistory) graphPeak = std::max(graphPeak, value);
    g_graphScaleTarget = graphPeak * 1.72;
    g_sampleAnimationStarted = GetTickCount64();

    const long long minute = std::chrono::duration_cast<std::chrono::minutes>(now.time_since_epoch()).count();
    if (g_lastMetricMinute != minute) {
        g_lastMetricMinute = minute;
        logMetrics();
    }
    rebuildList();
    InvalidateRect(g_window, nullptr, FALSE);
}

std::wstring getWindowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(length, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    return text;
}

void setListText(int row, int column, const std::wstring& value) {
    ListView_SetItemText(g_list, row, column, const_cast<wchar_t*>(value.c_str()));
}

void rebuildList() {
    if (!g_list) return;
    auto needle = lower(trim(getWindowText(g_search)));
    if (g_searchPlaceholder) needle.clear();
    std::vector<const DisplayConnection*> rows;
    rows.reserve(g_connections.size());
    for (const auto& [key, c] : g_connections) {
        const std::wstring searchable = lower(c.process + L" " + std::to_wstring(c.pid) + L" " + c.protocol + L" " +
                                              c.local + L" " + c.remote + L" " + c.state + L" " + c.processPath);
        if (needle.empty() || searchable.find(needle) != std::wstring::npos) rows.push_back(&c);
    }
    std::sort(rows.begin(), rows.end(), [](const auto* a, const auto* b) {
        if (a->active != b->active) return a->active > b->active;
        if (a->review != b->review) return a->review > b->review;
        return a->lastSeen > b->lastSeen;
    });

    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_list);
    int row = 0;
    for (const auto* c : rows) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        std::wstring name = c->review ? L"⚠  " + friendlyProcessName(*c) : friendlyProcessName(*c);
        item.pszText = name.data();
        item.iItem = row;
        item.lParam = reinterpret_cast<LPARAM>(c);
        ListView_InsertItem(g_list, &item);
        setListText(row, 1, c->protocol);
        setListText(row, 2, c->local);
        setListText(row, 3, c->remote);
        setListText(row, 4, c->active ? c->state : L"CLOSED");
        setListText(row, 5, shortTime(c->firstSeen));
        setListText(row, 6, isBlocked(*c) ? L"ALLOW" : L"BLOCK");
        ++row;
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, TRUE);
}

const DisplayConnection* selectedConnection() {
    const int index = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (index < 0) return nullptr;
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = index;
    if (!ListView_GetItem(g_list, &item)) return nullptr;
    return reinterpret_cast<const DisplayConnection*>(item.lParam);
}

void copySelected() {
    const auto* c = selectedConnection();
    if (!c) { setStatus(L"Select a connection first"); return; }
    const std::wstring text = L"Process: " + friendlyProcessName(*c) + L" (PID " + std::to_wstring(c->pid) + L")\r\nPath: " +
        (c->processPath.empty() ? L"Unavailable" : c->processPath) + L"\r\nProtocol: " + c->protocol + L"\r\nLocal: " + c->local +
        L"\r\nRemote: " + c->remote + L"\r\nState: " + (c->active ? c->state : L"CLOSED") + L"\r\nFirst seen: " + isoTime(c->firstSeen);
    if (!OpenClipboard(g_window)) return;
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
    setStatus(L"Connection details copied");
}

std::string csvQuote(const std::wstring& value) {
    std::string source = utf8(value);
    std::string out = "\"";
    for (char c : source) { if (c == '\"') out += '\"'; out += c; }
    return out + "\"";
}

void exportCsv() {
    wchar_t path[MAX_PATH]{};
    const std::wstring defaultName = L"network-snapshot-" + fileStamp() + L".csv";
    wcsncpy_s(path, defaultName.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) return;
    std::ofstream output(fs::path(path), std::ios::binary);
    output << "\xEF\xBB\xBFprocess,pid,protocol,local,remote,state,first_seen,last_seen,process_path,review\r\n";
    for (const auto& [key, c] : g_connections) {
        output << csvQuote(c.process) << ',' << c.pid << ',' << csvQuote(c.protocol) << ',' << csvQuote(c.local) << ','
               << csvQuote(c.remote) << ',' << csvQuote(c.active ? c.state : L"CLOSED") << ',' << csvQuote(isoTime(c.firstSeen))
               << ',' << csvQuote(isoTime(c.lastSeen)) << ',' << csvQuote(c.processPath) << ',' << (c.review ? "true" : "false") << "\r\n";
    }
    setStatus(L"CSV exported: " + std::wstring(path), 8);
}

void openPath(const fs::path& path) {
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(g_window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) setStatus(L"Could not open: " + path.wstring());
}

std::wstring quoteArgument(const std::wstring& value) {
    std::wstring escaped = L"\"";
    for (wchar_t c : value) {
        if (c == L'\"') escaped += L'\\';
        escaped += c;
    }
    return escaped + L"\"";
}

DWORD runHiddenProcess(const std::wstring& file, const std::wstring& parameters) {
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpFile = file.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) return ERROR_CANCELLED;
    WaitForSingleObject(info.hProcess, 120000);
    DWORD code = ERROR_GEN_FAILURE;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return code;
}

void terminateIpv4Tcp(const std::wstring& local, const std::wstring& remote) {
    std::wstring localHost, remoteHost;
    int localPort = 0, remotePort = 0;
    if (!splitEndpoint(local, localHost, localPort) || !splitEndpoint(remote, remoteHost, remotePort)) return;
    IN_ADDR localAddress{}, remoteAddress{};
    if (InetPtonW(AF_INET, localHost.c_str(), &localAddress) != 1 ||
        InetPtonW(AF_INET, remoteHost.c_str(), &remoteAddress) != 1) return;
    MIB_TCPROW row{};
    row.dwState = MIB_TCP_STATE_DELETE_TCB;
    row.dwLocalAddr = localAddress.S_un.S_addr;
    row.dwLocalPort = htons(static_cast<u_short>(localPort));
    row.dwRemoteAddr = remoteAddress.S_un.S_addr;
    row.dwRemotePort = htons(static_cast<u_short>(remotePort));
    SetTcpEntry(&row);
}

int runFirewallHelperIfRequested() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 2 || std::wstring(argv[1]) != L"--firewall-helper") {
        if (argv) LocalFree(argv);
        return -1;
    }
    if (argc < 9) { LocalFree(argv); return ERROR_INVALID_PARAMETER; }
    const std::wstring action = argv[2];
    const std::wstring ruleName = argv[3];
    const std::wstring processPath = argv[4];
    const std::wstring remoteHost = argv[5];
    const std::wstring localEndpoint = argv[6];
    const std::wstring remoteEndpoint = argv[7];
    const std::wstring protocol = argv[8];
    std::wstring parameters;
    if (action == L"block") {
        parameters = L"advfirewall firewall add rule name=" + quoteArgument(ruleName) +
                     L" dir=out action=block enable=yes profile=any protocol=any";
        if (!processPath.empty()) parameters += L" program=" + quoteArgument(processPath);
        if (!remoteHost.empty()) parameters += L" remoteip=" + quoteArgument(remoteHost);
    } else {
        parameters = L"advfirewall firewall delete rule name=" + quoteArgument(ruleName);
    }
    const DWORD result = runHiddenProcess(L"netsh.exe", parameters);
    if (result == 0 && action == L"block" && protocol == L"TCP4") terminateIpv4Tcp(localEndpoint, remoteEndpoint);
    LocalFree(argv);
    return static_cast<int>(result);
}

void toggleFirewall(const DisplayConnection& connection) {
    std::wstring host;
    int port = 0;
    splitEndpoint(connection.remote, host, port);
    if (connection.processPath.empty() && host.empty()) {
        setStatus(L"A process path or remote address is required for a precise firewall rule");
        return;
    }
    const std::wstring key = firewallKey(connection);
    const bool targetBlocked = !isBlocked(connection);
    const std::wstring ruleName = firewallRuleName(key);
    const std::wstring processPath = connection.processPath;
    const std::wstring local = connection.local;
    const std::wstring remote = connection.remote;
    const std::wstring protocol = connection.protocol;
    setStatus(targetBlocked ? L"Requesting permission to block this connection…" : L"Requesting permission to allow this connection…", 120);
    std::thread([key, targetBlocked, ruleName, processPath, host, local, remote, protocol] {
        const std::wstring params = L"--firewall-helper " + std::wstring(targetBlocked ? L"block " : L"allow ") +
            quoteArgument(ruleName) + L" " + quoteArgument(processPath) + L" " + quoteArgument(host) + L" " +
            quoteArgument(local) + L" " + quoteArgument(remote) + L" " + quoteArgument(protocol);
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = L"runas";
        info.lpFile = g_exePath.c_str();
        info.lpParameters = params.c_str();
        info.nShow = SW_HIDE;
        bool success = false;
        if (ShellExecuteExW(&info) && info.hProcess) {
            WaitForSingleObject(info.hProcess, 120000);
            DWORD exitCode = ERROR_GEN_FAILURE;
            GetExitCodeProcess(info.hProcess, &exitCode);
            CloseHandle(info.hProcess);
            success = exitCode == 0;
        }
        auto* result = new FirewallResult{key, targetBlocked, success};
        if (!PostMessageW(g_window, WM_FIREWALL_RESULT, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }).detach();
}

void openRemoteAddress(const DisplayConnection& connection) {
    std::wstring host;
    int port = 0;
    if (!splitEndpoint(connection.remote, host, port)) {
        setStatus(L"This row has no remote address to open");
        return;
    }
    const bool ipv6 = host.find(L':') != std::wstring::npos;
    const std::wstring displayHost = ipv6 ? L"[" + host + L"]" : host;
    std::wstring url = port == 443 ? L"https://" : L"http://";
    url += displayHost;
    if (port != 80 && port != 443 && port > 0) url += L":" + std::to_wstring(port);
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(g_window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) setStatus(L"The default browser could not open: " + url);
}

void reloadConfig() {
    g_config = loadConfig();
    updateAutostart();
    SetTimer(g_window, TIMER_POLL, static_cast<UINT>(g_config.pollIntervalMs), nullptr);
    cleanupLogs();
    setStatus(g_config.startWithWindows ? L"Configuration reloaded · Windows startup enabled" : L"Configuration reloaded · Windows startup disabled");
}

void addTrayIcon() {
    if (g_trayAdded) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = WM_TRAY;
    data.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_INFORMATION);
    wcscpy_s(data.szTip, kAppName);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void removeTrayIcon() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
    g_trayAdded = false;
}

void showFromTray() {
    ShowWindow(g_window, SW_SHOW);
    ShowWindow(g_window, SW_RESTORE);
    SetForegroundWindow(g_window);
}

void hideToTray() {
    addTrayIcon();
    ShowWindow(g_window, SW_HIDE);
    if (!g_closeHintShown) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = g_window;
        data.uID = 1;
        data.uFlags = NIF_INFO;
        wcscpy_s(data.szInfoTitle, kAppName);
        wcscpy_s(data.szInfo, L"Monitoring continues in the background. Double-click the tray icon to reopen.");
        data.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &data);
        g_closeHintShown = true;
    }
}

// Material Design 3 dark roles generated around a cool-blue seed.
COLORREF kBg = RGB(17, 19, 24);                  // background / surface
COLORREF kCard = RGB(29, 32, 36);                // surfaceContainer
COLORREF kCard2 = RGB(40, 42, 47);               // surfaceContainerHigh
COLORREF kText = RGB(226, 226, 233);              // onSurface
COLORREF kMuted = RGB(197, 198, 208);             // onSurfaceVariant
COLORREF kBlue = RGB(173, 198, 255);              // primary
COLORREF kPrimaryContainer = RGB(20, 69, 127);    // primaryContainer
COLORREF kGreen = RGB(136, 213, 182);             // positive semantic role
COLORREF kOrange = RGB(221, 188, 224);            // tertiary
COLORREF kError = RGB(255, 180, 171);              // error
COLORREF kErrorContainer = RGB(140, 29, 24);       // errorContainer
COLORREF kOutline = RGB(72, 74, 82);               // outlineVariant

Gdiplus::Color gpColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void roundedPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    const float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

void fillRound(HDC dc, const RECT& rect, COLORREF color, int radius = 12) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, S(radius), S(radius));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void drawText(HDC dc, const std::wstring& text, RECT rect, COLORREF color, HFONT font, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const auto old = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), -1, &rect, format);
    SelectObject(dc, old);
}

void drawCard(HDC dc, RECT rect, const std::wstring& label, const std::wstring& value, COLORREF accent) {
    fillRound(dc, rect, kCard);
    HBRUSH dot = CreateSolidBrush(accent);
    const RECT dotRect{rect.left + S(18), rect.top + S(18), rect.left + S(25), rect.top + S(25)};
    const auto oldBrush = SelectObject(dc, dot);
    const auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(dot);
    RECT labelRect{rect.left + S(32), rect.top + S(9), rect.right - S(14), rect.top + S(37)};
    RECT valueRect{rect.left + S(18), rect.top + S(38), rect.right - S(12), rect.bottom - S(8)};
    drawText(dc, label, labelRect, kMuted, g_font);
    drawText(dc, value, valueRect, kText, g_valueFont ? g_valueFont : g_titleFont);
}

[[maybe_unused]] void drawGraphLegacy(HDC dc, RECT rect) {
    fillRound(dc, rect, kCard);
    RECT title{rect.left + S(16), rect.top + S(8), rect.right - S(16), rect.top + S(32)};
    drawText(dc, L"Network activity  ·  120 second window", title, kText, g_font);
    RECT legend{rect.left + S(16), rect.top + S(31), rect.right - S(16), rect.top + S(53)};
    drawText(dc, L"↓ 下载  " + formatBytes(g_downloadBps, true) + L"     ↑ 上传  " + formatBytes(g_uploadBps, true), legend, kMuted, g_font);
    RECT plot{rect.left + S(16), rect.top + S(57), rect.right - S(16), rect.bottom - S(12)};
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(37, 47, 63));
    const auto oldPen = SelectObject(dc, grid);
    for (int i = 0; i <= 3; ++i) {
        const int y = plot.top + (plot.bottom - plot.top) * i / 3;
        MoveToEx(dc, plot.left, y, nullptr); LineTo(dc, plot.right, y);
    }
    double maximum = 1024.0;
    for (double value : g_downHistory) maximum = std::max(maximum, value);
    for (double value : g_upHistory) maximum = std::max(maximum, value);
    auto drawLine = [&](const std::vector<double>& data, COLORREF color) {
        HPEN pen = CreatePen(PS_SOLID, S(2), color);
        SelectObject(dc, pen);
        for (size_t i = 0; i < data.size(); ++i) {
            const int x = plot.left + static_cast<int>((plot.right - plot.left) * i / (data.size() - 1));
            const int y = plot.bottom - static_cast<int>((plot.bottom - plot.top) * data[i] / maximum);
            if (i == 0) MoveToEx(dc, x, y, nullptr); else LineTo(dc, x, y);
        }
        DeleteObject(SelectObject(dc, grid));
    };
    drawLine(g_downHistory, kBlue);
    drawLine(g_upHistory, kGreen);
    SelectObject(dc, oldPen);
    DeleteObject(grid);
}

void drawGraph(HDC dc, RECT rect) {
    fillRound(dc, rect, kCard);
    RECT title{rect.left + S(20), rect.top + S(10), rect.right - S(20), rect.top + S(35)};
    drawText(dc, L"Network activity  ·  120 second window", title, kText, g_font);
    RECT legend{rect.left + S(20), rect.top + S(36), rect.right - S(20), rect.top + S(60)};
    drawText(dc, L"DOWNLOAD  " + formatBytes(g_downloadBps, true) + L"      UPLOAD  " + formatBytes(g_uploadBps, true), legend, kMuted, g_font);
    RECT plot{rect.left + S(20), rect.top + S(68), rect.right - S(20), rect.bottom - S(20)};

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::Pen gridPen(gpColor(kOutline, 100), 1.0f);
    for (int i = 0; i <= 3; ++i) {
        const float y = static_cast<float>(plot.top + (plot.bottom - plot.top) * i / 3);
        graphics.DrawLine(&gridPen, static_cast<float>(plot.left), y, static_cast<float>(plot.right), y);
    }

    const double rawProgress = g_sampleAnimationStarted == 0 ? 1.0 :
        std::clamp((GetTickCount64() - g_sampleAnimationStarted) / static_cast<double>(std::max(250, g_config.pollIntervalMs)), 0.0, 1.0);
    const float progress = static_cast<float>(rawProgress); // uniform distribution across the full scan interval
    g_graphMaximum = g_graphScaleStart + (g_graphScaleTarget - g_graphScaleStart) * rawProgress;
    const double maximum = std::max(1024.0, g_graphMaximum);
    const float step = static_cast<float>(plot.right - plot.left) / static_cast<float>(g_downHistory.size() - 1);
    auto valueY = [&](double value) {
        const float y = static_cast<float>(plot.bottom) - static_cast<float>((plot.bottom - plot.top) * value / maximum);
        return std::clamp(y, static_cast<float>(plot.top), static_cast<float>(plot.bottom));
    };
    graphics.SetClip(Gdiplus::Rect(plot.left, plot.top, plot.right - plot.left, plot.bottom - plot.top));
    auto drawSeries = [&](const std::vector<double>& data, COLORREF color, double phaseOffset) {
        if (data.size() < 2) return;
        std::vector<Gdiplus::PointF> points;
        points.reserve(data.size());
        for (size_t i = 0; i + 1 < data.size(); ++i) {
            const float distanceFromPrevious = static_cast<float>((data.size() - 2) - i) + progress;
            const float x = static_cast<float>(plot.right) - distanceFromPrevious * step;
            points.emplace_back(x, valueY(data[i]));
        }
        const float previousY = valueY(data[data.size() - 2]);
        const float targetY = valueY(data.back());
        const Gdiplus::PointF head(static_cast<float>(plot.right), previousY + (targetY - previousY) * progress);
        if (std::abs(points.back().X - head.X) < 0.01f && std::abs(points.back().Y - head.Y) < 0.01f)
            points.back() = head;
        else
            points.push_back(head);
        Gdiplus::GraphicsPath curve;
        curve.AddCurve(points.data(), static_cast<INT>(points.size()), 0.36f);
        Gdiplus::Pen linePen(gpColor(color), static_cast<float>(S(2)));
        linePen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&linePen, &curve);

        const double seconds = GetTickCount64() / 1000.0;
        const float pulse = static_cast<float>(S(8) + (std::sin(seconds * 2.0 + phaseOffset) + 1.0) * S(3));
        const Gdiplus::PointF center = head;
        Gdiplus::SolidBrush halo(gpColor(color, 42));
        graphics.FillEllipse(&halo, center.X - pulse, center.Y - pulse, pulse * 2, pulse * 2);
        Gdiplus::SolidBrush ball(gpColor(color));
        const float radius = static_cast<float>(S(4));
        graphics.FillEllipse(&ball, center.X - radius, center.Y - radius, radius * 2, radius * 2);
    };
    drawSeries(g_downHistory, kBlue, 0.0);
    drawSeries(g_upHistory, kGreen, 1.7);
    graphics.ResetClip();
}

void drawActionCell(HDC dc, RECT cell, const DisplayConnection* connection, COLORREF rowColor) {
    HBRUSH background = CreateSolidBrush(rowColor);
    FillRect(dc, &cell, background);
    DeleteObject(background);
    if (!connection) return;
    const bool blocked = isBlocked(*connection);
    RECT pill{cell.left + S(9), cell.top + S(7), cell.right - S(9), cell.bottom - S(7)};
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::RectF bounds(static_cast<float>(pill.left), static_cast<float>(pill.top),
                          static_cast<float>(pill.right - pill.left), static_cast<float>(pill.bottom - pill.top));
    Gdiplus::GraphicsPath path;
    roundedPath(path, bounds, bounds.Height / 2.0f);
    Gdiplus::SolidBrush fill(gpColor(blocked ? RGB(32, 86, 65) : kErrorContainer));
    graphics.FillPath(&fill, &path);
    if (g_rowRippleKey == connection->key) {
        const DWORD elapsed = GetTickCount() - g_rowRippleStarted;
        if (elapsed < 520) {
            const float progress = std::min(1.0f, elapsed / 420.0f);
            const float radius = progress * static_cast<float>(std::max(pill.right - pill.left, pill.bottom - pill.top));
            const BYTE alpha = static_cast<BYTE>((1.0f - progress) * 95);
            Gdiplus::Region clip(&path);
            graphics.SetClip(&clip);
            Gdiplus::SolidBrush ripple(Gdiplus::Color(alpha, 255, 255, 255));
            graphics.FillEllipse(&ripple, static_cast<float>(g_rowRippleOrigin.x) - radius,
                                 static_cast<float>(g_rowRippleOrigin.y) - radius, radius * 2, radius * 2);
            graphics.ResetClip();
        }
    }
    drawText(dc, blocked ? L"ALLOW" : L"BLOCK", pill, blocked ? RGB(171, 242, 207) : kError,
             g_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void paintWindow(HDC target) {
    RECT client{};
    GetClientRect(g_window, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    const auto oldBitmap = SelectObject(dc, bitmap);
    HBRUSH background = CreateSolidBrush(kBg);
    FillRect(dc, &client, background);
    DeleteObject(background);

    const int margin = S(22);
    RECT title{margin, S(16), client.right - margin, S(52)};
    drawText(dc, L"Network Monitor", title, kText, g_titleFont);
    RECT subtitle{margin, S(45), client.right - margin, S(70)};
    drawText(dc, g_paused ? L"Monitoring paused" : L"System-wide connections and adapter traffic", subtitle, g_paused ? kOrange : kMuted, g_font);

    size_t active = 0, review = 0;
    for (const auto& [key, c] : g_connections) if (c.active) { ++active; if (c.review) ++review; }
    const int gap = S(12);
    const int cardY = S(82), cardH = S(90);
    const int cardW = (client.right - 2 * margin - 3 * gap) / 4;
    drawCard(dc, {margin, cardY, margin + cardW, cardY + cardH}, L"ACTIVE ENDPOINTS", std::to_wstring(active), kBlue);
    drawCard(dc, {margin + cardW + gap, cardY, margin + 2 * cardW + gap, cardY + cardH}, L"DOWNLOAD", formatBytes(g_downloadBps, true), kBlue);
    const bool highUpload = g_config.uploadAlertMbps > 0 && g_uploadBps * 8 / 1000000.0 >= g_config.uploadAlertMbps;
    drawCard(dc, {margin + 2 * (cardW + gap), cardY, margin + 3 * cardW + 2 * gap, cardY + cardH}, L"UPLOAD", formatBytes(g_uploadBps, true), highUpload ? kError : kOrange);
    drawCard(dc, {margin + 3 * (cardW + gap), cardY, client.right - margin, cardY + cardH}, L"REVIEW", std::to_wstring(review), review ? kError : kGreen);

    drawGraph(dc, {margin, S(188), client.right - margin, S(414)});
    RECT section{margin, S(424), client.right - margin, S(452)};
    drawText(dc, L"CONNECTIONS", section, kText, g_font);

    RECT bottom{margin, client.bottom - S(29), client.right - margin, client.bottom - S(6)};
    std::wstring status = g_status;
    if (std::chrono::steady_clock::now() > g_statusUntil) {
        status = L"TOTAL  ↓ " + formatBytes(static_cast<double>(g_totalReceived)) + L"   ↑ " + formatBytes(static_cast<double>(g_totalSent)) +
                 L"   ·   ACTIVE ADAPTERS  " + std::to_wstring(g_adapters.size()) + L"   ·   LOG RETENTION  " + std::to_wstring(g_config.retentionDays) + L" DAYS";
    }
    drawText(dc, status, bottom, kMuted, g_font);

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void paintGraphOnly(HDC target, RECT graphRect) {
    const int width = graphRect.right - graphRect.left;
    const int height = graphRect.bottom - graphRect.top;
    if (width <= 0 || height <= 0) return;
    if (!g_graphBufferDc || width != g_graphBufferWidth || height != g_graphBufferHeight) {
        if (g_graphBufferDc) {
            SelectObject(g_graphBufferDc, g_graphBufferOldBitmap);
            DeleteObject(g_graphBufferBitmap);
            DeleteDC(g_graphBufferDc);
        }
        g_graphBufferDc = CreateCompatibleDC(target);
        g_graphBufferBitmap = CreateCompatibleBitmap(target, width, height);
        g_graphBufferOldBitmap = SelectObject(g_graphBufferDc, g_graphBufferBitmap);
        g_graphBufferWidth = width;
        g_graphBufferHeight = height;
    }
    SetViewportOrgEx(g_graphBufferDc, -graphRect.left, -graphRect.top, nullptr);
    drawGraph(g_graphBufferDc, graphRect);
    SetViewportOrgEx(g_graphBufferDc, 0, 0, nullptr);
    BitBlt(target, graphRect.left, graphRect.top, width, height, g_graphBufferDc, 0, 0, SRCCOPY);
}

LRESULT CALLBACK materialButtonSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    auto& state = g_buttonStates[hwnd];
    switch (message) {
        case WM_MOUSEMOVE:
            if (!state.hover) {
                state.hover = true;
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tracking);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            state.hover = false;
            state.down = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
            state.down = true;
            state.rippling = true;
            state.rippleOrigin = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            state.rippleStarted = GetTickCount();
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_LBUTTONUP:
            state.down = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_KEYDOWN:
            if (wParam == VK_SPACE || wParam == VK_RETURN) {
                RECT rect{}; GetClientRect(hwnd, &rect);
                state.rippling = true;
                state.rippleOrigin = {(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};
                state.rippleStarted = GetTickCount();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_NCDESTROY:
            g_buttonStates.erase(hwnd);
            RemoveWindowSubclass(hwnd, materialButtonSubclass, 1);
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK materialHeaderSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{}; GetClientRect(hwnd, &client);
        HBRUSH background = CreateSolidBrush(kCard2); FillRect(dc, &client, background); DeleteObject(background);
        const int count = Header_GetItemCount(hwnd);
        for (int i = 0; i < count; ++i) {
            RECT rect{}; Header_GetItemRect(hwnd, i, &rect);
            wchar_t label[128]{};
            HDITEMW item{}; item.mask = HDI_TEXT; item.pszText = label; item.cchTextMax = static_cast<int>(std::size(label));
            Header_GetItem(hwnd, i, &item);
            rect.left += S(14); rect.right -= S(8);
            drawText(dc, label, rect, kMuted, g_font);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, materialHeaderSubclass, 2);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK searchSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_SETFOCUS && g_searchPlaceholder) {
        g_searchPlaceholder = false;
        SetWindowTextW(hwnd, L"");
    } else if (message == WM_KILLFOCUS && GetWindowTextLengthW(hwnd) == 0) {
        g_searchPlaceholder = true;
        SetWindowTextW(hwnd, L"Search connections");
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, searchSubclass, 3);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

HWND makeButton(const wchar_t* text, UINT id) {
    HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                                  g_window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    g_buttonStates.emplace(button, MaterialButtonState{});
    SetWindowSubclass(button, materialButtonSubclass, 1, 0);
    g_buttons.push_back(button);
    return button;
}

void layout() {
    RECT client{};
    GetClientRect(g_window, &client);
    const int margin = S(22);
    const int toolbarY = S(454);
    const int controlH = S(40);
    const int searchW = std::max<int>(S(240), client.right - 2 * margin - S(700));
    MoveWindow(g_search, margin, toolbarY, searchW, controlH, TRUE);
    int x = margin + searchW + S(8);
    const int widths[] = {76, 92, 76, 84, 84, 80, 62};
    for (size_t i = 0; i < g_buttons.size(); ++i) {
        const int width = S(widths[i]);
        MoveWindow(g_buttons[i], x, toolbarY, width, controlH, TRUE);
        x += width + S(7);
    }
    const int listTop = S(508);
    MoveWindow(g_list, margin, listTop, std::max<int>(100, client.right - 2 * margin),
               std::max<int>(80, client.bottom - listTop - S(38)), TRUE);
    RECT searchRect{}; GetClientRect(g_search, &searchRect);
    SetWindowRgn(g_search, CreateRoundRectRgn(0, 0, searchRect.right + 1, searchRect.bottom + 1, S(12), S(12)), TRUE);
    RECT listRect{}; GetClientRect(g_list, &listRect);
    SetWindowRgn(g_list, CreateRoundRectRgn(0, 0, listRect.right + 1, listRect.bottom + 1, S(16), S(16)), TRUE);
    const int total = client.right - 2 * margin - S(20);
    const int fixed = S(180 + 76 + 118 + 112 + 96);
    const int endpoints = std::max(S(320), total - fixed);
    ListView_SetColumnWidth(g_list, 0, S(180));
    ListView_SetColumnWidth(g_list, 1, S(76));
    ListView_SetColumnWidth(g_list, 2, endpoints / 2);
    ListView_SetColumnWidth(g_list, 3, endpoints - endpoints / 2);
    ListView_SetColumnWidth(g_list, 4, S(118));
    ListView_SetColumnWidth(g_list, 5, S(112));
    ListView_SetColumnWidth(g_list, 6, S(96));
}

void initializeControls() {
    g_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                               0, 0, 0, 0, g_window, reinterpret_cast<HMENU>(ID_SEARCH), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_search, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SetWindowTextW(g_search, L"Search connections");
    SendMessageW(g_search, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(14), S(14)));
    SetWindowSubclass(g_search, searchSubclass, 3, 0);
    makeButton(L"Pause", ID_PAUSE);
    makeButton(L"Export CSV", ID_EXPORT);
    makeButton(L"Logs", ID_LOGS);
    makeButton(L"Config", ID_CONFIG);
    makeButton(L"Reload", ID_RELOAD);
    makeButton(L"Copy", ID_COPY);
    makeButton(L"Exit", ID_EXIT);

    g_list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                             0, 0, 0, 0, g_window, reinterpret_cast<HMENU>(ID_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(g_list, kCard);
    ListView_SetTextBkColor(g_list, kCard);
    ListView_SetTextColor(g_list, kText);
    SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SetWindowTheme(g_list, L"DarkMode_Explorer", nullptr);
    SetWindowTheme(ListView_GetHeader(g_list), L"DarkMode_Explorer", nullptr);
    g_rowHeightImages = ImageList_Create(1, S(46), ILC_COLOR32, 1, 1);
    ListView_SetImageList(g_list, g_rowHeightImages, LVSIL_SMALL);
    const wchar_t* columns[] = {L"PROCESS", L"PROTOCOL", L"LOCAL ENDPOINT", L"REMOTE ENDPOINT", L"STATE", L"FIRST SEEN", L"ACTION"};
    for (int i = 0; i < 7; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(columns[i]);
        column.cx = S(100);
        column.iSubItem = i;
        ListView_InsertColumn(g_list, i, &column);
    }
    HWND listHeader = ListView_GetHeader(g_list);
    SetWindowTheme(listHeader, L"", L"");
    SetWindowSubclass(listHeader, materialHeaderSubclass, 2, 0);
    for (int i = 0; i < 7; ++i) {
        HDITEMW item{}; item.mask = HDI_FORMAT;
        Header_GetItem(listHeader, i, &item);
        item.fmt |= HDF_OWNERDRAW;
        Header_SetItem(listHeader, i, &item);
    }
    layout();
}

void reloadFonts() {
    if (g_font) DeleteObject(g_font);
    if (g_titleFont) DeleteObject(g_titleFont);
    if (g_valueFont) DeleteObject(g_valueFont);
    g_font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Google Sans Code");
    g_titleFont = CreateFontW(-S(22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Google Sans Code");
    g_valueFont = CreateFontW(-S(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Google Sans Code");
}

void exitApplication() {
    g_exiting = true;
    removeTrayIcon();
    DestroyWindow(g_window);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_showMessage && message == g_showMessage) {
        appendLog(L"{\"time\":\"" + isoTime() + L"\",\"event\":\"app_show_request\"}");
        showFromTray();
        return 0;
    }
    switch (message) {
        case WM_CREATE: {
            g_window = hwnd;
            HDC screen = GetDC(hwnd);
            g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
            ReleaseDC(hwnd, screen);
            reloadFonts();
            g_editBrush = CreateSolidBrush(kCard2);
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
            COLORREF caption = kBg;
            COLORREF captionText = kText;
            DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
            DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof(captionText));
            initializeControls();
            addTrayIcon();
            SetTimer(hwnd, TIMER_POLL, static_cast<UINT>(g_config.pollIntervalMs), nullptr);
            SetTimer(hwnd, TIMER_ANIMATION, 33, nullptr);
            startCapture();
            return 0;
        }
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = S(1040);
            limits->ptMinTrackSize.y = S(780);
            return 0;
        }
        case WM_DPICHANGED: {
            g_dpi = HIWORD(wParam);
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            reloadFonts();
            SendMessageW(g_search, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            for (HWND button : g_buttons) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            layout();
            return 0;
        }
        case WM_TIMER:
            if (wParam == TIMER_POLL) {
                startCapture();
                if (std::chrono::steady_clock::now() > g_statusUntil) InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wParam == TIMER_ANIMATION) {
                RECT graph{S(22), S(188), 0, S(414)};
                RECT client{}; GetClientRect(hwnd, &client); graph.right = client.right - S(22);
                InvalidateRect(hwnd, &graph, FALSE);
                for (auto& [button, state] : g_buttonStates) {
                    if (state.rippling && GetTickCount() - state.rippleStarted >= 540) state.rippling = false;
                    if (state.rippling || state.hover || state.down) InvalidateRect(button, nullptr, FALSE);
                }
                if (!g_rowRippleKey.empty()) {
                    if (GetTickCount() - g_rowRippleStarted >= 540) g_rowRippleKey.clear();
                    InvalidateRect(g_list, nullptr, FALSE);
                }
            }
            return 0;
        case WM_SNAPSHOT_READY:
            applySnapshot(std::unique_ptr<Snapshot>(reinterpret_cast<Snapshot*>(lParam)));
            return 0;
        case WM_FIREWALL_RESULT: {
            std::unique_ptr<FirewallResult> result(reinterpret_cast<FirewallResult*>(lParam));
            if (result->success) {
                if (result->blocked) g_blockedRules.insert(result->key);
                else g_blockedRules.erase(result->key);
                saveBlocklist();
                rebuildList();
                setStatus(result->blocked ? L"Connection blocked · the firewall rule remains active for future attempts"
                                          : L"Connection allowed · the firewall rule was removed", 8);
            } else {
                setStatus(L"Firewall change was cancelled or failed", 8);
            }
            return 0;
        }
        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            if (id == ID_SEARCH && HIWORD(wParam) == EN_CHANGE) rebuildList();
            else if (id == ID_PAUSE) {
                g_paused = !g_paused;
                SetWindowTextW(g_buttons[0], g_paused ? L"Resume" : L"Pause");
                setStatus(g_paused ? L"Sampling paused · existing data remains available" : L"Sampling resumed");
                if (!g_paused) startCapture();
            } else if (id == ID_EXPORT) exportCsv();
            else if (id == ID_LOGS) { fs::create_directories(g_logDir); openPath(g_logDir); }
            else if (id == ID_CONFIG) openPath(g_configPath);
            else if (id == ID_RELOAD) reloadConfig();
            else if (id == ID_COPY) copySelected();
            else if (id == ID_EXIT) exitApplication();
            return 0;
        }
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->hwndFrom == ListView_GetHeader(g_list) && header->code == NM_CUSTOMDRAW) {
                auto* custom = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
                if (custom->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (custom->dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int index = static_cast<int>(custom->dwItemSpec);
                    RECT rect{}; Header_GetItemRect(header->hwndFrom, index, &rect);
                    HBRUSH fill = CreateSolidBrush(kCard2); FillRect(custom->hdc, &rect, fill); DeleteObject(fill);
                    wchar_t label[128]{};
                    HDITEMW item{}; item.mask = HDI_TEXT; item.pszText = label; item.cchTextMax = static_cast<int>(std::size(label));
                    Header_GetItem(header->hwndFrom, index, &item);
                    rect.left += S(14); rect.right -= S(8);
                    drawText(custom->hdc, label, rect, kMuted, g_font);
                    return CDRF_SKIPDEFAULT;
                }
            }
            if (header->idFrom == ID_LIST && header->code == NM_CUSTOMDRAW) {
                auto* custom = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                if (custom->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (custom->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (custom->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const auto* connection = reinterpret_cast<const DisplayConnection*>(custom->nmcd.lItemlParam);
                    COLORREF row = (custom->nmcd.dwItemSpec % 2) ? RGB(32, 34, 39) : kCard;
                    if (ListView_GetItemState(g_list, static_cast<int>(custom->nmcd.dwItemSpec), LVIS_SELECTED) & LVIS_SELECTED)
                        row = RGB(39, 52, 72);
                    if (connection && g_rowRippleKey == connection->key && GetTickCount() - g_rowRippleStarted < 540) row = RGB(46, 52, 62);
                    if (custom->iSubItem == 6) {
                        RECT cell{};
                        ListView_GetSubItemRect(g_list, static_cast<int>(custom->nmcd.dwItemSpec), 6, LVIR_BOUNDS, &cell);
                        drawActionCell(custom->nmcd.hdc, cell, connection, row);
                        return CDRF_SKIPDEFAULT;
                    }
                    custom->clrText = connection && connection->review ? kOrange :
                                      (connection && !connection->active ? kMuted : kText);
                    custom->clrTextBk = row;
                    return CDRF_DODEFAULT;
                }
            }
            if (header->idFrom == ID_LIST && header->code == NM_CLICK) {
                auto* click = reinterpret_cast<NMLISTVIEW*>(lParam);
                if (click->iItem >= 0) {
                    LVITEMW item{}; item.mask = LVIF_PARAM; item.iItem = click->iItem;
                    if (ListView_GetItem(g_list, &item)) {
                        const auto* connection = reinterpret_cast<const DisplayConnection*>(item.lParam);
                        g_rowRippleKey = connection->key;
                        g_rowRippleStarted = GetTickCount();
                        g_rowRippleOrigin = click->ptAction;
                        if (click->iSubItem == 6) toggleFirewall(*connection);
                    }
                }
            }
            if (header->idFrom == ID_LIST && header->code == NM_DBLCLK) {
                auto* click = reinterpret_cast<NMLISTVIEW*>(lParam);
                if (click->iItem >= 0 && click->iSubItem == 3) {
                    LVITEMW item{}; item.mask = LVIF_PARAM; item.iItem = click->iItem;
                    if (ListView_GetItem(g_list, &item)) openRemoteAddress(*reinterpret_cast<const DisplayConnection*>(item.lParam));
                }
            } else if (header->idFrom == ID_LIST && header->code == NM_RETURN) copySelected();
            return 0;
        }
        case WM_DRAWITEM: {
            auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (item->CtlType == ODT_HEADER) {
                HBRUSH background = CreateSolidBrush(kCard2);
                FillRect(item->hDC, &item->rcItem, background);
                DeleteObject(background);
                wchar_t label[128]{};
                HDITEMW headerItem{}; headerItem.mask = HDI_TEXT; headerItem.pszText = label;
                headerItem.cchTextMax = static_cast<int>(std::size(label));
                Header_GetItem(item->hwndItem, static_cast<int>(item->itemID), &headerItem);
                RECT textRect = item->rcItem; textRect.left += S(14); textRect.right -= S(8);
                drawText(item->hDC, label, textRect, kMuted, g_font);
                return TRUE;
            }
            if (item->CtlType != ODT_BUTTON) break;
            const bool exitButton = item->CtlID == ID_EXIT;
            const bool primaryButton = item->CtlID == ID_PAUSE;
            auto& state = g_buttonStates[item->hwndItem];
            HBRUSH clear = CreateSolidBrush(kBg); FillRect(item->hDC, &item->rcItem, clear); DeleteObject(clear);
            Gdiplus::Graphics graphics(item->hDC);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::RectF bounds(static_cast<float>(item->rcItem.left), static_cast<float>(item->rcItem.top),
                                  static_cast<float>(item->rcItem.right - item->rcItem.left), static_cast<float>(item->rcItem.bottom - item->rcItem.top));
            Gdiplus::GraphicsPath path;
            roundedPath(path, bounds, bounds.Height / 2.0f);
            COLORREF base = exitButton ? kErrorContainer : (primaryButton ? kPrimaryContainer : kCard2);
            Gdiplus::SolidBrush fill(gpColor(base)); graphics.FillPath(&fill, &path);
            if (state.hover || state.down) {
                Gdiplus::SolidBrush layer(Gdiplus::Color(state.down ? 42 : 24, 255, 255, 255)); graphics.FillPath(&layer, &path);
            }
            if (state.rippling) {
                const DWORD elapsed = GetTickCount() - state.rippleStarted;
                const float progress = std::min(1.0f, elapsed / 440.0f);
                const float radius = progress * std::hypot(bounds.Width, bounds.Height);
                Gdiplus::Region clip(&path); graphics.SetClip(&clip);
                Gdiplus::SolidBrush ripple(Gdiplus::Color(static_cast<BYTE>((1.0f - progress) * 90), 255, 255, 255));
                graphics.FillEllipse(&ripple, state.rippleOrigin.x - radius, state.rippleOrigin.y - radius, radius * 2, radius * 2);
                graphics.ResetClip();
            }
            drawText(item->hDC, getWindowText(item->hwndItem), item->rcItem, exitButton ? kError : (primaryButton ? kBlue : kText), g_font,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, reinterpret_cast<HWND>(lParam) == g_search && g_searchPlaceholder ? kMuted : kText);
            SetBkColor(dc, kCard2);
            return reinterpret_cast<LRESULT>(g_editBrush);
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{}; GetClientRect(hwnd, &client);
            const RECT graph{S(22), S(188), client.right - S(22), S(414)};
            const bool graphOnly = paint.rcPaint.left >= graph.left && paint.rcPaint.top >= graph.top &&
                                   paint.rcPaint.right <= graph.right && paint.rcPaint.bottom <= graph.bottom;
            if (graphOnly) paintGraphOnly(dc, graph);
            else paintWindow(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_TRAY:
            if (lParam == WM_LBUTTONDBLCLK) showFromTray();
            else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                POINT point{}; GetCursorPos(&point);
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Open Network Monitor");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
                SetForegroundWindow(hwnd);
                const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (command == ID_TRAY_SHOW) showFromTray();
                else if (command == ID_TRAY_EXIT) exitApplication();
            }
            return 0;
        case WM_CLOSE:
            if (g_exiting) DestroyWindow(hwnd);
            else {
                appendLog(L"{\"time\":\"" + isoTime() + L"\",\"event\":\"app_hidden_to_tray\"}");
                hideToTray();
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_POLL);
            KillTimer(hwnd, TIMER_ANIMATION);
            removeTrayIcon();
            if (g_font) DeleteObject(g_font);
            if (g_titleFont) DeleteObject(g_titleFont);
            if (g_valueFont) DeleteObject(g_valueFont);
            if (g_editBrush) DeleteObject(g_editBrush);
            if (g_rowHeightImages) ImageList_Destroy(g_rowHeightImages);
            if (g_graphBufferDc) {
                SelectObject(g_graphBufferDc, g_graphBufferOldBitmap);
                DeleteObject(g_graphBufferBitmap);
                DeleteDC(g_graphBufferDc);
                g_graphBufferDc = nullptr;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    SetProcessDPIAware();
    WSADATA winsock{};
    WSAStartup(MAKEWORD(2, 2), &winsock);
    const int helperResult = runFirewallHelperIfRequested();
    if (helperResult >= 0) {
        WSACleanup();
        return helperResult;
    }
    g_showMessage = RegisterWindowMessageW(L"OMGNetworkMonitorShowMessage");
    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\OMGNetworkMonitorSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // A registered broadcast is reliable even when the first top-level window has
        // never been made visible and therefore cannot be found by shell heuristics.
        PostMessageW(HWND_BROADCAST, g_showMessage, 0, 0);
        if (singleInstance) CloseHandle(singleInstance);
        WSACleanup();
        return 0;
    }
    wchar_t modulePath[32768]{};
    GetModuleFileNameW(nullptr, modulePath, std::size(modulePath));
    g_exePath = modulePath;
    g_configPath = g_exePath.parent_path() / L"network-monitor.yaml";
    wchar_t localAppData[32768]{};
    ExpandEnvironmentStringsW(L"%LOCALAPPDATA%", localAppData, std::size(localAppData));
    g_dataDir = fs::path(localAppData) / L"OMG Network Monitor";
    g_logDir = g_dataDir / L"logs";
    fs::create_directories(g_logDir);
    loadBlocklist();
    g_config = loadConfig();
    updateAutostart();
    cleanupLogs();
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    g_appIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    windowClass.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_INFORMATION);
    windowClass.hIconSm = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_INFORMATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) return 1;

    HDC screenDc = GetDC(nullptr);
    const int startupDpi = GetDeviceCaps(screenDc, LOGPIXELSX);
    ReleaseDC(nullptr, screenDc);
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int workWidth = static_cast<int>(workArea.right - workArea.left);
    const int workHeight = static_cast<int>(workArea.bottom - workArea.top);
    const int initialWidth = std::min(MulDiv(1240, startupDpi, 96), workWidth);
    const int initialHeight = std::min(MulDiv(900, startupDpi, 96), workHeight);
    const int initialX = static_cast<int>(workArea.left) + std::max(0, (workWidth - initialWidth) / 2);
    const int initialY = static_cast<int>(workArea.top) + std::max(0, (workHeight - initialHeight) / 2);
    HWND window = CreateWindowExW(0, kWindowClass, L"OMG Network Monitor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  initialX, initialY, initialWidth, initialHeight, nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    const bool autostart = lower(commandLine ? commandLine : L"").find(L"--autostart") != std::wstring::npos;
    if (autostart && g_config.startMinimizedWithWindows) {
        // Establish the normal restored placement once before hiding. A window whose
        // first and only ShowWindow call is SW_HIDE may ignore later restore requests.
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        appendLog(L"{\"time\":\"" + isoTime() + L"\",\"event\":\"app_autostart_hidden\"}");
        hideToTray();
    } else {
        ShowWindow(window, showCommand == SW_HIDE ? SW_SHOW : showCommand);
        UpdateWindow(window);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    WSACleanup();
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    if (singleInstance) { ReleaseMutex(singleInstance); CloseHandle(singleInstance); }
    return static_cast<int>(message.wParam);
}
