#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Gdiplus;

static const wchar_t* kIconDir = L"C:\\Catppuccin-Icons\\Theme\\CatppuccinIcon";
static const int kIconSizeBase = 24;
static const int kToolbarHeightBase = 58;
static const int kStatusHeightBase = 30;
static const int kSidebarWidthBase = 285;
static const COLORREF kBg = RGB(48, 52, 70);
static const COLORREF kSurface = RGB(41, 44, 60);
static const COLORREF kSurface2 = RGB(65, 69, 89);
static const COLORREF kText = RGB(198, 208, 245);
static const COLORREF kMuted = RGB(165, 173, 206);
static const COLORREF kButton = RGB(140, 170, 238);
static const COLORREF kHighlight = RGB(166, 209, 137);

enum ControlId {
    ID_BACK = 1001,
    ID_FORWARD,
    ID_UP,
    ID_REFRESH,
    ID_ADDRESS,
    ID_SEARCH,
    ID_TREE,
    ID_LIST,
    ID_CTX_COPY = 3001,
    ID_CTX_PASTE,
    ID_CTX_RENAME,
    ID_CTX_RECYCLE,
    ID_CTX_DELETE
};

struct Entry {
    std::wstring name;
    std::wstring path;
    bool isDir = false;
    ULONGLONG size = 0;
    FILETIME modified{};
};

struct SvgStyle {
    std::wstring fill = L"";
    std::wstring stroke = L"";
    double strokeWidth = 1.0;
};

static HWND g_hwnd = nullptr;
static HWND g_back = nullptr;
static HWND g_forward = nullptr;
static HWND g_up = nullptr;
static HWND g_refresh = nullptr;
static HWND g_address = nullptr;
static HWND g_search = nullptr;
static HWND g_tree = nullptr;
static HWND g_list = nullptr;
static HWND g_status = nullptr;
static HFONT g_font = nullptr;
static HBRUSH g_bgBrush = nullptr;
static HBRUSH g_surfaceBrush = nullptr;
static HIMAGELIST g_icons = nullptr;
static ULONG_PTR g_gdiplusToken = 0;
static std::vector<Entry> g_entries;
static std::wstring g_currentDir;
static std::wstring g_query;
static std::vector<std::wstring> g_history;
static int g_historyIndex = -1;
static bool g_navigatingFromHistory = false;
static std::unordered_map<std::wstring, int> g_iconIndex;
static std::unordered_map<std::wstring, std::wstring> g_extToIcon;
static std::unordered_map<HTREEITEM, std::wstring> g_treePaths;
static std::unordered_map<HTREEITEM, bool> g_treeLoaded;
static std::wstring g_iconCacheDir;
static UINT g_dpi = 96;

struct RippleState {
    DWORD started = 0;
    POINT pt{0, 0};
};

static std::unordered_map<HWND, RippleState> g_ripples;
static WNDPROC g_buttonProc = nullptr;

static int Dpi(int value) {
    return MulDiv(value, (int)g_dpi, 96);
}

static Color GdiColor(COLORREF c, BYTE alpha = 255) {
    return Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

static std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t ch) { return (wchar_t)towlower(ch); });
    return s;
}

static std::wstring Trim(std::wstring s) {
    size_t first = 0;
    while (first < s.size() && iswspace(s[first])) first++;
    size_t last = s.size();
    while (last > first && iswspace(s[last - 1])) last--;
    return s.substr(first, last - first);
}

static bool StartsWithInsensitive(const std::wstring& text, const std::wstring& prefix) {
    if (prefix.size() > text.size()) return false;
    return _wcsnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
}

static std::wstring WindowText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(len, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    return text;
}

static bool FuzzyMatch(const std::wstring& text, const std::wstring& query, std::vector<bool>* marks = nullptr) {
    if (marks) marks->assign(text.size(), false);
    if (query.empty()) return true;
    std::wstring lowerText = ToLower(text);
    std::wstring lowerQuery = ToLower(query);
    size_t qi = 0;
    for (size_t i = 0; i < lowerText.size() && qi < lowerQuery.size(); ++i) {
        if (lowerText[i] == lowerQuery[qi]) {
            if (marks) (*marks)[i] = true;
            qi++;
        }
    }
    return qi == lowerQuery.size();
}

static bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirectoryExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool IsDriveRoot(const std::wstring& path) {
    return path.size() == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
}

static std::wstring NormalizePath(const std::wstring& path) {
    wchar_t buffer[MAX_PATH * 4]{};
    DWORD len = GetFullPathNameW(path.c_str(), (DWORD)(MAX_PATH * 4), buffer, nullptr);
    std::wstring out = len ? std::wstring(buffer) : path;
    std::replace(out.begin(), out.end(), L'/', L'\\');
    while (out.size() > 3 && out.back() == L'\\') out.pop_back();
    return out;
}

static std::wstring ParentPath(const std::wstring& path) {
    std::wstring p = NormalizePath(path);
    if (IsDriveRoot(p)) return p;
    size_t pos = p.find_last_of(L'\\');
    if (pos == std::wstring::npos) return p;
    if (pos == 2) return p.substr(0, 3);
    return p.substr(0, pos);
}

static std::wstring JoinPath(const std::wstring& base, const std::wstring& name) {
    if (base.empty() || base.back() == L'\\') return base + name;
    return base + L"\\" + name;
}

static std::wstring ExistingPathOrFallback(const std::wstring& preferred, const std::wstring& fallback) {
    if (DirectoryExists(preferred) || FileExists(preferred)) return preferred;
    return fallback;
}

static std::wstring DesktopPath() { return L"C:\\Users\\35727\\Desktop"; }
static std::wstring DownloadsPath() { return L"D:\\Users\\35727\\Downloads"; }
static std::wstring ProjectsPath() {
    return ExistingPathOrFallback(L"C:\\Users\\35727\\Desktop\\A-MyProjects", L"C:\\Users\\35727\\Desktop\\A-MyProject");
}
static std::wstring PhotosPath() { return L"C:\\Users\\35727\\OneDrive\\\u56fe\u7247"; }
static std::wstring VideosPath() { return L"C:\\Users\\35727\\Videos"; }
static std::wstring UserPath() { return L"C:\\Users\\35727"; }

static bool IsDriveAbsolute(const std::wstring& text) {
    return text.size() >= 3 && iswalpha(text[0]) && text[1] == L':' && (text[2] == L'\\' || text[2] == L'/');
}

static std::wstring ExecutableDir() {
    wchar_t buffer[MAX_PATH * 4]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH * 4);
    std::wstring path = buffer;
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

static std::wstring ResolveJumpPath(const std::wstring& raw) {
    std::wstring input = Trim(raw);
    std::replace(input.begin(), input.end(), L'/', L'\\');
    while (!input.empty() && input.front() == L'\\') input.erase(input.begin());
    if (input.empty()) return L"";

    std::wstring first = input;
    std::wstring rest;
    size_t slash = input.find(L'\\');
    if (slash != std::wstring::npos) {
        first = input.substr(0, slash);
        rest = input.substr(slash + 1);
    }
    std::wstring firstLower = ToLower(first);

    std::wstring base;
    if (firstLower == L"proj") base = ProjectsPath();
    else if (firstLower == L"dwld") base = DownloadsPath();
    else if (firstLower == L"sstm") base = L"C:\\Windows\\System32";
    else if (firstLower == L"photos" || firstLower == L"phto") base = PhotosPath();
    else if (firstLower == L"video" || firstLower == L"vido") base = VideosPath();
    else if (firstLower == L"noip") base = JoinPath(ProjectsPath(), L"NOIP");
    else if (firstLower == L"c-root") base = L"C:\\";
    else if (firstLower == L"d-root") base = L"D:\\";
    else if (firstLower == L"u-root") base = L"E:\\";
    else if (firstLower == L"dskt") base = DesktopPath();
    else if (firstLower == L"user") base = UserPath();
    else if (firstLower == L"desktop") base = DesktopPath();
    else if (firstLower == L"downloads") base = DownloadsPath();
    else if (firstLower == L"projects") base = ProjectsPath();

    if (!base.empty()) return rest.empty() ? base : JoinPath(base, rest);
    if (IsDriveAbsolute(input) || StartsWithInsensitive(input, L"\\\\")) return NormalizePath(input);

    std::vector<std::wstring> candidates = {
        JoinPath(g_currentDir.empty() ? DesktopPath() : g_currentDir, input),
        JoinPath(DesktopPath(), input),
        JoinPath(ProjectsPath(), input),
        JoinPath(UserPath(), input)
    };
    for (const auto& candidate : candidates) {
        if (DirectoryExists(candidate) || FileExists(candidate)) return candidate;
    }
    return candidates.front();
}

static bool SearchTextIsJump(const std::wstring& text) {
    std::wstring trimmed = Trim(text);
    return !trimmed.empty() && trimmed[0] == L'>';
}

static std::wstring GetExtension(const std::wstring& name) {
    size_t slash = name.find_last_of(L"\\/");
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash) || dot == name.size() - 1) return L"";
    return ToLower(name.substr(dot + 1));
}

static std::wstring TypeKey(const Entry& entry) {
    if (entry.isDir) return L"";
    std::wstring ext = GetExtension(entry.name);
    return ext.empty() ? L"~" : ext;
}

static std::wstring ExtensionTypeLabel(const std::wstring& ext) {
    static const std::unordered_map<std::wstring, std::wstring> labels = {
        {L"txt", L"Plaintext File"}, {L"log", L"Log File"}, {L"md", L"Markdown Document"}, {L"markdown", L"Markdown Document"},
        {L"pdf", L"PDF Document"}, {L"doc", L"Word Document"}, {L"docx", L"Word Document"},
        {L"xls", L"Excel Spreadsheet"}, {L"xlsx", L"Excel Spreadsheet"}, {L"csv", L"CSV Data"},
        {L"ppt", L"PowerPoint Presentation"}, {L"pptx", L"PowerPoint Presentation"},
        {L"png", L"PNG Image"}, {L"jpg", L"JPEG Image"}, {L"jpeg", L"JPEG Image"}, {L"gif", L"GIF Image"},
        {L"bmp", L"BMP Image"}, {L"webp", L"WebP Image"}, {L"svg", L"SVG Image"}, {L"ico", L"Icon File"},
        {L"mp3", L"MP3 Audio"}, {L"wav", L"WAV Audio"}, {L"flac", L"FLAC Audio"}, {L"ogg", L"OGG Audio"},
        {L"mp4", L"MP4 Video"}, {L"mkv", L"Matroska Video"}, {L"mov", L"QuickTime Video"}, {L"avi", L"AVI Video"},
        {L"zip", L"ZIP Archive"}, {L"rar", L"RAR Archive"}, {L"7z", L"7-Zip Archive"}, {L"tar", L"TAR Archive"}, {L"gz", L"Gzip Archive"},
        {L"py", L"Python Code"}, {L"js", L"JavaScript Code"}, {L"mjs", L"JavaScript Module"}, {L"cjs", L"JavaScript Code"},
        {L"ts", L"TypeScript Code"}, {L"tsx", L"React TypeScript Code"}, {L"jsx", L"React JavaScript Code"},
        {L"html", L"HTML Document"}, {L"htm", L"HTML Document"}, {L"css", L"CSS Stylesheet"},
        {L"json", L"JSON Data"}, {L"xml", L"XML Document"}, {L"yaml", L"YAML Data"}, {L"yml", L"YAML Data"},
        {L"cpp", L"C++ Code"}, {L"cc", L"C++ Code"}, {L"cxx", L"C++ Code"}, {L"c", L"C Code"},
        {L"h", L"C Header"}, {L"hpp", L"C++ Header"}, {L"cs", L"C# Code"}, {L"java", L"Java Code"},
        {L"go", L"Go Code"}, {L"rs", L"Rust Code"}, {L"vue", L"Vue Component"},
        {L"ps1", L"PowerShell Script"}, {L"bat", L"Batch Script"}, {L"cmd", L"Command Script"}, {L"sh", L"Shell Script"},
        {L"exe", L"Application"}, {L"dll", L"Dynamic Link Library"}, {L"bin", L"Binary File"}, {L"ini", L"Configuration File"}
    };
    auto it = labels.find(ext);
    if (it != labels.end()) return it->second;
    return L"";
}

static std::wstring TypeLabel(const Entry& entry) {
    if (entry.isDir) return L"Folder";
    std::wstring ext = GetExtension(entry.name);
    if (ext.empty()) return L"File";
    std::wstring label = ExtensionTypeLabel(ext);
    if (!label.empty()) return label;
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t ch) { return (wchar_t)towupper(ch); });
    return ext + L" File";
}

static std::wstring ReadTextUtf8(const std::wstring& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return L"";
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (needed <= 0) return L"";
    std::wstring out(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), out.data(), needed);
    return out;
}

static std::map<std::wstring, std::wstring> ParseAttrs(const std::wstring& raw) {
    std::map<std::wstring, std::wstring> attrs;
    static const std::wregex re(LR"attr(([A-Za-z_:][-A-Za-z0-9_:.]*)\s*=\s*"([^"]*)")attr");
    for (auto it = std::wsregex_iterator(raw.begin(), raw.end(), re); it != std::wsregex_iterator(); ++it) {
        attrs[(*it)[1].str()] = (*it)[2].str();
    }
    return attrs;
}

static double AttrDouble(const std::map<std::wstring, std::wstring>& attrs, const std::wstring& key, double fallback) {
    auto it = attrs.find(key);
    if (it == attrs.end() || it->second.empty()) return fallback;
    try { return std::stod(it->second); } catch (...) { return fallback; }
}

static Color ParseColor(const std::wstring& value, BYTE alpha = 255) {
    if (value.size() == 7 && value[0] == L'#') {
        int r = std::stoi(value.substr(1, 2), nullptr, 16);
        int g = std::stoi(value.substr(3, 2), nullptr, 16);
        int b = std::stoi(value.substr(5, 2), nullptr, 16);
        return Color(alpha, (BYTE)r, (BYTE)g, (BYTE)b);
    }
    if (value.size() == 4 && value[0] == L'#') {
        int r = std::stoi(value.substr(1, 1), nullptr, 16) * 17;
        int g = std::stoi(value.substr(2, 1), nullptr, 16) * 17;
        int b = std::stoi(value.substr(3, 1), nullptr, 16) * 17;
        return Color(alpha, (BYTE)r, (BYTE)g, (BYTE)b);
    }
    return Color(alpha, 202, 211, 245);
}

static SvgStyle ApplyStyle(SvgStyle base, const std::map<std::wstring, std::wstring>& attrs) {
    auto fill = attrs.find(L"fill");
    if (fill != attrs.end()) base.fill = fill->second;
    auto stroke = attrs.find(L"stroke");
    if (stroke != attrs.end()) base.stroke = stroke->second;
    auto width = attrs.find(L"stroke-width");
    if (width != attrs.end()) {
        try { base.strokeWidth = std::stod(width->second); } catch (...) {}
    }
    return base;
}

class PathParser {
public:
    explicit PathParser(std::wstring data) : d(std::move(data)) {}

    std::unique_ptr<GraphicsPath> Parse() {
        auto path = std::make_unique<GraphicsPath>();
        wchar_t cmd = 0;
        PointF cur(0, 0), start(0, 0), lastCtrl(0, 0);
        bool hasLastCtrl = false;

        while (SkipSeparators(), pos < d.size()) {
            if (IsCommand(d[pos])) cmd = d[pos++];
            if (!cmd) break;

            bool relative = iswlower(cmd) != 0;
            wchar_t op = (wchar_t)towupper(cmd);

            if (op == L'M') {
                bool first = true;
                while (HasNumberAhead()) {
                    double x = ReadNumber(), y = ReadNumber();
                    PointF p = MakePoint(x, y, relative, cur);
                    if (first) {
                        path->StartFigure();
                        cur = start = p;
                        first = false;
                    } else {
                        path->AddLine(cur, p);
                        cur = p;
                    }
                }
                hasLastCtrl = false;
            } else if (op == L'L') {
                while (HasNumberAhead()) {
                    PointF p = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    path->AddLine(cur, p);
                    cur = p;
                }
                hasLastCtrl = false;
            } else if (op == L'H') {
                while (HasNumberAhead()) {
                    double x = ReadNumber();
                    PointF p((REAL)(relative ? cur.X + x : x), cur.Y);
                    path->AddLine(cur, p);
                    cur = p;
                }
                hasLastCtrl = false;
            } else if (op == L'V') {
                while (HasNumberAhead()) {
                    double y = ReadNumber();
                    PointF p(cur.X, (REAL)(relative ? cur.Y + y : y));
                    path->AddLine(cur, p);
                    cur = p;
                }
                hasLastCtrl = false;
            } else if (op == L'C') {
                while (HasNumberAhead()) {
                    PointF c1 = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    PointF c2 = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    PointF p = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    path->AddBezier(cur, c1, c2, p);
                    cur = p;
                    lastCtrl = c2;
                    hasLastCtrl = true;
                }
            } else if (op == L'S') {
                while (HasNumberAhead()) {
                    PointF c1 = hasLastCtrl ? PointF(2 * cur.X - lastCtrl.X, 2 * cur.Y - lastCtrl.Y) : cur;
                    PointF c2 = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    PointF p = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    path->AddBezier(cur, c1, c2, p);
                    cur = p;
                    lastCtrl = c2;
                    hasLastCtrl = true;
                }
            } else if (op == L'Q') {
                while (HasNumberAhead()) {
                    PointF q = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    PointF p = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    PointF c1(cur.X + (2.0f / 3.0f) * (q.X - cur.X), cur.Y + (2.0f / 3.0f) * (q.Y - cur.Y));
                    PointF c2(p.X + (2.0f / 3.0f) * (q.X - p.X), p.Y + (2.0f / 3.0f) * (q.Y - p.Y));
                    path->AddBezier(cur, c1, c2, p);
                    cur = p;
                    lastCtrl = q;
                    hasLastCtrl = true;
                }
            } else if (op == L'T') {
                while (HasNumberAhead()) {
                    PointF q = hasLastCtrl ? PointF(2 * cur.X - lastCtrl.X, 2 * cur.Y - lastCtrl.Y) : cur;
                    PointF p = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    PointF c1(cur.X + (2.0f / 3.0f) * (q.X - cur.X), cur.Y + (2.0f / 3.0f) * (q.Y - cur.Y));
                    PointF c2(p.X + (2.0f / 3.0f) * (q.X - p.X), p.Y + (2.0f / 3.0f) * (q.Y - p.Y));
                    path->AddBezier(cur, c1, c2, p);
                    cur = p;
                    lastCtrl = q;
                    hasLastCtrl = true;
                }
            } else if (op == L'A') {
                while (HasNumberAhead()) {
                    ReadNumber();
                    ReadNumber();
                    ReadNumber();
                    ReadNumber();
                    ReadNumber();
                    PointF p = MakePoint(ReadNumber(), ReadNumber(), relative, cur);
                    path->AddLine(cur, p);
                    cur = p;
                }
                hasLastCtrl = false;
            } else if (op == L'Z') {
                path->CloseFigure();
                cur = start;
                hasLastCtrl = false;
            } else {
                break;
            }
        }
        return path;
    }

private:
    std::wstring d;
    size_t pos = 0;

    static bool IsCommand(wchar_t ch) {
        return wcschr(L"MmLlHhVvCcSsQqTtAaZz", ch) != nullptr;
    }

    void SkipSeparators() {
        while (pos < d.size() && (iswspace(d[pos]) || d[pos] == L',')) pos++;
    }

    bool HasNumberAhead() {
        SkipSeparators();
        return pos < d.size() && !IsCommand(d[pos]);
    }

    double ReadNumber() {
        SkipSeparators();
        size_t start = pos;
        if (pos < d.size() && (d[pos] == L'+' || d[pos] == L'-')) pos++;
        while (pos < d.size() && iswdigit(d[pos])) pos++;
        if (pos < d.size() && d[pos] == L'.') {
            pos++;
            while (pos < d.size() && iswdigit(d[pos])) pos++;
        }
        if (pos < d.size() && (d[pos] == L'e' || d[pos] == L'E')) {
            pos++;
            if (pos < d.size() && (d[pos] == L'+' || d[pos] == L'-')) pos++;
            while (pos < d.size() && iswdigit(d[pos])) pos++;
        }
        if (start == pos) return 0;
        return std::stod(d.substr(start, pos - start));
    }

    static PointF MakePoint(double x, double y, bool relative, const PointF& cur) {
        return relative ? PointF((REAL)(cur.X + x), (REAL)(cur.Y + y)) : PointF((REAL)x, (REAL)y);
    }
};

static bool ExtractViewBox(const std::wstring& svg, RectF& viewBox) {
    std::wregex re(LR"viewbox(viewBox\s*=\s*"([^"]+)")viewbox");
    std::wsmatch m;
    if (!std::regex_search(svg, m, re)) {
        viewBox = RectF(0, 0, 16, 16);
        return false;
    }
    std::wistringstream ss(m[1].str());
    double x = 0, y = 0, w = 16, h = 16;
    ss >> x >> y >> w >> h;
    viewBox = RectF((REAL)x, (REAL)y, (REAL)w, (REAL)h);
    return true;
}

static void DrawPath(Graphics& g, const std::map<std::wstring, std::wstring>& attrs, const SvgStyle& style) {
    auto dIt = attrs.find(L"d");
    if (dIt == attrs.end()) return;
    PathParser parser(dIt->second);
    auto path = parser.Parse();
    if (!style.fill.empty() && style.fill != L"none") {
        SolidBrush brush(ParseColor(style.fill));
        g.FillPath(&brush, path.get());
    }
    if (!style.stroke.empty() && style.stroke != L"none") {
        Pen pen(ParseColor(style.stroke), (REAL)style.strokeWidth);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        g.DrawPath(&pen, path.get());
    }
}

static std::vector<PointF> ParsePointList(const std::wstring& raw) {
    std::vector<double> nums;
    size_t pos = 0;
    while (pos < raw.size()) {
        while (pos < raw.size() && (iswspace(raw[pos]) || raw[pos] == L',')) pos++;
        size_t start = pos;
        if (pos < raw.size() && (raw[pos] == L'+' || raw[pos] == L'-')) pos++;
        while (pos < raw.size() && (iswdigit(raw[pos]) || raw[pos] == L'.')) pos++;
        if (start != pos) {
            try { nums.push_back(std::stod(raw.substr(start, pos - start))); } catch (...) {}
        } else {
            pos++;
        }
    }
    std::vector<PointF> points;
    for (size_t i = 1; i < nums.size(); i += 2) points.emplace_back((REAL)nums[i - 1], (REAL)nums[i]);
    return points;
}

static void DrawShape(Graphics& g, const std::wstring& tag, const std::map<std::wstring, std::wstring>& attrs, const SvgStyle& style) {
    if (tag == L"path") {
        DrawPath(g, attrs, style);
        return;
    }

    std::unique_ptr<GraphicsPath> path(new GraphicsPath());
    if (tag == L"rect") {
        REAL x = (REAL)AttrDouble(attrs, L"x", 0), y = (REAL)AttrDouble(attrs, L"y", 0);
        REAL w = (REAL)AttrDouble(attrs, L"width", 0), h = (REAL)AttrDouble(attrs, L"height", 0);
        path->AddRectangle(RectF(x, y, w, h));
    } else if (tag == L"circle") {
        REAL cx = (REAL)AttrDouble(attrs, L"cx", 0), cy = (REAL)AttrDouble(attrs, L"cy", 0), r = (REAL)AttrDouble(attrs, L"r", 0);
        path->AddEllipse(cx - r, cy - r, r * 2, r * 2);
    } else if (tag == L"ellipse") {
        REAL cx = (REAL)AttrDouble(attrs, L"cx", 0), cy = (REAL)AttrDouble(attrs, L"cy", 0);
        REAL rx = (REAL)AttrDouble(attrs, L"rx", 0), ry = (REAL)AttrDouble(attrs, L"ry", 0);
        path->AddEllipse(cx - rx, cy - ry, rx * 2, ry * 2);
    } else if (tag == L"line") {
        PointF a((REAL)AttrDouble(attrs, L"x1", 0), (REAL)AttrDouble(attrs, L"y1", 0));
        PointF b((REAL)AttrDouble(attrs, L"x2", 0), (REAL)AttrDouble(attrs, L"y2", 0));
        path->AddLine(a, b);
    } else if (tag == L"polygon" || tag == L"polyline") {
        auto it = attrs.find(L"points");
        if (it == attrs.end()) return;
        auto points = ParsePointList(it->second);
        if (points.size() >= 2) {
            path->AddLines(points.data(), (INT)points.size());
            if (tag == L"polygon") path->CloseFigure();
        }
    } else {
        return;
    }

    if (!style.fill.empty() && style.fill != L"none") {
        SolidBrush brush(ParseColor(style.fill));
        g.FillPath(&brush, path.get());
    }
    if (!style.stroke.empty() && style.stroke != L"none") {
        Pen pen(ParseColor(style.stroke), (REAL)style.strokeWidth);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        g.DrawPath(&pen, path.get());
    }
}

static HBITMAP RenderSvgBitmap(const std::wstring& svgPath, int size) {
    std::wstring svg = ReadTextUtf8(svgPath);
    if (svg.empty()) return nullptr;

    Bitmap bitmap(size, size, PixelFormat32bppARGB);
    Graphics g(&bitmap);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(GdiColor(kBg));

    RectF viewBox;
    ExtractViewBox(svg, viewBox);
    REAL pad = 2.0f;
    REAL scale = (REAL)std::min((size - pad * 2) / viewBox.Width, (size - pad * 2) / viewBox.Height);
    g.TranslateTransform(pad, pad);
    g.ScaleTransform(scale, scale);
    g.TranslateTransform(-viewBox.X, -viewBox.Y);

    std::vector<SvgStyle> stack;
    stack.push_back(SvgStyle());

    static const std::wregex tagRe(LR"(<\s*(/)?\s*(g|path|rect|circle|ellipse|line|polygon|polyline)\b([^>]*?)(/)?>)");
    for (auto it = std::wsregex_iterator(svg.begin(), svg.end(), tagRe); it != std::wsregex_iterator(); ++it) {
        bool closing = (*it)[1].matched;
        std::wstring tag = (*it)[2].str();
        std::wstring rawAttrs = (*it)[3].str();
        bool selfClosing = (*it)[4].matched || (!rawAttrs.empty() && rawAttrs.back() == L'/');

        if (closing) {
            if (tag == L"g" && stack.size() > 1) stack.pop_back();
            continue;
        }

        auto attrs = ParseAttrs(rawAttrs);
        SvgStyle style = ApplyStyle(stack.back(), attrs);
        if (tag == L"g") {
            stack.push_back(style);
            if (selfClosing && stack.size() > 1) stack.pop_back();
        } else {
            DrawShape(g, tag, attrs, style);
        }
    }

    HBITMAP hbmp = nullptr;
    bitmap.GetHBITMAP(GdiColor(kBg), &hbmp);
    return hbmp;
}

static HBITMAP LoadPngBitmap(const std::wstring& pngPath, int size) {
    if (!FileExists(pngPath)) return nullptr;
    Bitmap source(pngPath.c_str());
    if (source.GetLastStatus() != Ok) return nullptr;

    Bitmap bitmap(size, size, PixelFormat32bppARGB);
    Graphics g(&bitmap);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.Clear(GdiColor(kBg));
    g.DrawImage(&source, Rect(0, 0, size, size), 0, 0, source.GetWidth(), source.GetHeight(), UnitPixel);

    HBITMAP hbmp = nullptr;
    bitmap.GetHBITMAP(GdiColor(kBg), &hbmp);
    return hbmp;
}

static int AddIcon(const std::wstring& key, const std::wstring& svgName) {
    auto existing = g_iconIndex.find(key);
    if (existing != g_iconIndex.end()) return existing->second;
    std::wstring path = JoinPath(kIconDir, svgName + L".svg");
    std::wstring pngPath = JoinPath(g_iconCacheDir, svgName + L".png");
    HBITMAP hbmp = LoadPngBitmap(pngPath, Dpi(kIconSizeBase));
    if (!hbmp && FileExists(path)) hbmp = RenderSvgBitmap(path, Dpi(kIconSizeBase));
    if (!hbmp) return g_iconIndex.count(L"generic") ? g_iconIndex[L"generic"] : 0;
    int index = ImageList_Add(g_icons, hbmp, nullptr);
    DeleteObject(hbmp);
    if (index < 0) index = 0;
    g_iconIndex[key] = index;
    return index;
}

static void InitExtensionMap() {
    g_extToIcon = {
        {L"txt", L"text"}, {L"log", L"text"}, {L"md", L"markdown"}, {L"markdown", L"markdown"},
        {L"cpp", L"cpp"}, {L"cc", L"cpp"}, {L"cxx", L"cpp"}, {L"c", L"c"}, {L"h", L"c-header"}, {L"hpp", L"cpp-header"},
        {L"py", L"python"}, {L"js", L"javascript"}, {L"mjs", L"javascript"}, {L"cjs", L"javascript"},
        {L"ts", L"typescript"}, {L"tsx", L"react"}, {L"jsx", L"react"}, {L"html", L"html"}, {L"htm", L"html"},
        {L"css", L"css"}, {L"json", L"json"}, {L"xml", L"xml"}, {L"yaml", L"yaml"}, {L"yml", L"yaml"},
        {L"ps1", L"powershell"}, {L"bat", L"batch"}, {L"cmd", L"batch"}, {L"sh", L"bash"},
        {L"png", L"image"}, {L"jpg", L"image"}, {L"jpeg", L"image"}, {L"gif", L"image"}, {L"bmp", L"image"}, {L"webp", L"image"}, {L"svg", L"svg"},
        {L"mp3", L"audio"}, {L"wav", L"audio"}, {L"flac", L"audio"}, {L"ogg", L"audio"},
        {L"mp4", L"video"}, {L"mkv", L"video"}, {L"mov", L"video"}, {L"avi", L"video"},
        {L"zip", L"zip"}, {L"rar", L"zip"}, {L"7z", L"zip"}, {L"tar", L"zip"}, {L"gz", L"zip"},
        {L"pdf", L"pdf"}, {L"exe", L"exe"}, {L"dll", L"binary"}, {L"bin", L"binary"},
        {L"rs", L"rust"}, {L"go", L"go"}, {L"java", L"java"}, {L"cs", L"csharp"}, {L"vue", L"vue"}
    };
}

static int IconForEntry(const Entry& entry) {
    if (entry.isDir) return g_iconIndex[L"folder"];
    std::wstring ext = GetExtension(entry.name);
    auto it = g_extToIcon.find(ext);
    if (it == g_extToIcon.end()) return g_iconIndex[L"generic"];
    return AddIcon(it->second, it->second);
}

static std::wstring HumanSize(ULONGLONG bytes) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    wchar_t buf[64]{};
    if (unit == 0) swprintf(buf, 64, L"%llu %s", bytes, units[unit]);
    else swprintf(buf, 64, L"%.1f %s", value, units[unit]);
    return buf;
}

static std::wstring FormatFileTime(const FILETIME& ft) {
    FILETIME local{};
    SYSTEMTIME st{};
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &st);
    wchar_t buf[64]{};
    swprintf(buf, 64, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

static void SetStatus(const std::wstring& text) {
    SetWindowTextW(g_status, text.c_str());
}

static void DrawRoundedButton(const DRAWITEMSTRUCT* dis) {
    FillRect(dis->hDC, &dis->rcItem, g_bgBrush);

    Graphics g(dis->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    RectF r((REAL)dis->rcItem.left, (REAL)dis->rcItem.top,
            (REAL)(dis->rcItem.right - dis->rcItem.left),
            (REAL)(dis->rcItem.bottom - dis->rcItem.top));
    REAL radius = (REAL)Dpi(14);
    GraphicsPath path;
    path.AddArc(r.X, r.Y, radius, radius, 180, 90);
    path.AddArc(r.X + r.Width - radius, r.Y, radius, radius, 270, 90);
    path.AddArc(r.X + r.Width - radius, r.Y + r.Height - radius, radius, radius, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - radius, radius, radius, 90, 90);
    path.CloseFigure();

    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    SolidBrush bg(disabled ? GdiColor(kSurface2, 120) : GdiColor(kButton));
    g.FillPath(&bg, &path);

    auto ripple = g_ripples.find(dis->hwndItem);
    if (ripple != g_ripples.end()) {
        DWORD elapsed = GetTickCount() - ripple->second.started;
        if (elapsed < 420) {
            Region oldClip;
            g.GetClip(&oldClip);
            g.SetClip(&path);
            REAL maxRadius = std::max(r.Width, r.Height) * 1.35f;
            REAL rr = maxRadius * ((REAL)elapsed / 420.0f);
            BYTE alpha = (BYTE)(80 * (1.0f - (REAL)elapsed / 420.0f));
            SolidBrush wave(Color(alpha, 255, 255, 255));
            g.FillEllipse(&wave, (REAL)ripple->second.pt.x - rr, (REAL)ripple->second.pt.y - rr, rr * 2, rr * 2);
            g.SetClip(&oldClip, CombineModeReplace);
        }
    }

    wchar_t text[128]{};
    GetWindowTextW(dis->hwndItem, text, 128);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, disabled ? kMuted : kBg);
    HFONT oldFont = (HFONT)SelectObject(dis->hDC, g_font);
    DrawTextW(dis->hDC, text, -1, const_cast<RECT*>(&dis->rcItem), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dis->hDC, oldFont);
}

static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_LBUTTONDOWN) {
        g_ripples[hwnd] = { GetTickCount(), { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) } };
        SetTimer(g_hwnd, 42, 16, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return CallWindowProcW(g_buttonProc, hwnd, msg, wParam, lParam);
}

static void StyleButton(HWND button) {
    LONG_PTR style = GetWindowLongPtrW(button, GWL_STYLE);
    SetWindowLongPtrW(button, GWL_STYLE, style | BS_OWNERDRAW);
    if (!g_buttonProc) g_buttonProc = (WNDPROC)GetWindowLongPtrW(button, GWLP_WNDPROC);
    SetWindowLongPtrW(button, GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
}

static void SetDarkListColors() {
    ListView_SetBkColor(g_list, kBg);
    ListView_SetTextBkColor(g_list, kBg);
    ListView_SetTextColor(g_list, kText);
    TreeView_SetBkColor(g_tree, kSurface);
    TreeView_SetTextColor(g_tree, kText);
}

static std::vector<Entry> EnumerateDirectory(const std::wstring& dir, bool foldersOnly) {
    std::vector<Entry> items;
    WIN32_FIND_DATAW fd{};
    std::wstring mask = JoinPath(dir, L"*");
    HANDLE h = FindFirstFileW(mask.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return items;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (foldersOnly && !isDir) continue;
        Entry e;
        e.name = name;
        e.path = JoinPath(dir, name);
        e.isDir = isDir;
        e.size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        e.modified = fd.ftLastWriteTime;
        items.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(items.begin(), items.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        std::wstring at = TypeKey(a);
        std::wstring bt = TypeKey(b);
        int typeCmp = _wcsicmp(at.c_str(), bt.c_str());
        if (typeCmp != 0) return typeCmp < 0;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return items;
}

static bool HasSubFolders(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    std::wstring mask = JoinPath(dir, L"*");
    HANDLE h = FindFirstFileW(mask.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            found = true;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

static HTREEITEM InsertTreeItem(HTREEITEM parent, const std::wstring& text, const std::wstring& path, int icon, bool mayHaveChildren) {
    TVINSERTSTRUCTW ins{};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_SORT;
    ins.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    ins.item.pszText = const_cast<LPWSTR>(text.c_str());
    ins.item.iImage = icon;
    ins.item.iSelectedImage = icon;
    HTREEITEM item = TreeView_InsertItem(g_tree, &ins);
    if (!path.empty()) g_treePaths[item] = path;
    if (mayHaveChildren) {
        TVINSERTSTRUCTW dummy{};
        dummy.hParent = item;
        dummy.hInsertAfter = TVI_LAST;
        dummy.item.mask = TVIF_TEXT;
        dummy.item.pszText = const_cast<LPWSTR>(L"");
        TreeView_InsertItem(g_tree, &dummy);
        g_treeLoaded[item] = false;
    } else {
        g_treeLoaded[item] = true;
    }
    return item;
}

static std::vector<std::wstring> GetDrives() {
    std::vector<std::wstring> drives;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (mask & (1u << i)) {
            wchar_t d[] = { (wchar_t)(L'A' + i), L':', L'\\', L'\0' };
            drives.emplace_back(d);
        }
    }
    return drives;
}

static void PopulateTreeRoot() {
    TreeView_DeleteAllItems(g_tree);
    g_treePaths.clear();
    g_treeLoaded.clear();

    HTREEITEM pinned = InsertTreeItem(TVI_ROOT, L"Pinned", L"", g_iconIndex[L"folder"], false);
    g_treeLoaded[pinned] = true;
    InsertTreeItem(pinned, L"Desktop", DesktopPath(), g_iconIndex[L"folder"], DirectoryExists(DesktopPath()));
    InsertTreeItem(pinned, L"Downloads", DownloadsPath(), g_iconIndex[L"folder"], DirectoryExists(DownloadsPath()));
    InsertTreeItem(pinned, L"Projects", ProjectsPath(), g_iconIndex[L"folder"], DirectoryExists(ProjectsPath()));
    TreeView_Expand(g_tree, pinned, TVE_EXPAND);

    HTREEITEM root = InsertTreeItem(TVI_ROOT, L"This PC", L"", g_iconIndex[L"folder"], false);
    g_treeLoaded[root] = true;
    for (const auto& drive : GetDrives()) {
        std::wstring text = L"Local Disk (" + drive.substr(0, 2) + L")";
        InsertTreeItem(root, text, drive, g_iconIndex[L"folder"], true);
    }
    TreeView_Expand(g_tree, root, TVE_EXPAND);
}

static void PopulateTreeChildren(HTREEITEM item) {
    if (g_treeLoaded[item]) return;
    auto pathIt = g_treePaths.find(item);
    if (pathIt == g_treePaths.end()) return;
    TreeView_DeleteItem(g_tree, TreeView_GetChild(g_tree, item));
    auto folders = EnumerateDirectory(pathIt->second, true);
    for (const auto& folder : folders) {
        InsertTreeItem(item, folder.name, folder.path, g_iconIndex[L"folder"], HasSubFolders(folder.path));
    }
    g_treeLoaded[item] = true;
}

static void FillList(const std::wstring& dir);

static bool PathListToClipboard(const std::vector<std::wstring>& paths) {
    if (paths.empty() || !OpenClipboard(g_hwnd)) return false;
    EmptyClipboard();
    size_t chars = 0;
    for (const auto& p : paths) chars += p.size() + 1;
    chars += 1;
    SIZE_T bytes = sizeof(DROPFILES) + chars * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    auto* df = (DROPFILES*)GlobalLock(mem);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    wchar_t* out = (wchar_t*)((BYTE*)df + sizeof(DROPFILES));
    for (const auto& p : paths) {
        wcscpy(out, p.c_str());
        out += p.size() + 1;
    }
    *out = L'\0';
    GlobalUnlock(mem);
    SetClipboardData(CF_HDROP, mem);
    CloseClipboard();
    return true;
}

static bool CopyDirectoryRecursive(const std::wstring& src, const std::wstring& dst) {
    CreateDirectoryW(dst.c_str(), nullptr);
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(JoinPath(src, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring from = JoinPath(src, name);
        std::wstring to = JoinPath(dst, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = CopyDirectoryRecursive(from, to) && ok;
        else ok = CopyFileW(from.c_str(), to.c_str(), FALSE) && ok;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

static std::wstring UniqueDestination(const std::wstring& dir, const std::wstring& name) {
    std::wstring candidate = JoinPath(dir, name);
    if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) return candidate;
    std::wstring stem = name;
    std::wstring ext;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        stem = name.substr(0, dot);
        ext = name.substr(dot);
    }
    for (int i = 2; i < 1000; ++i) {
        candidate = JoinPath(dir, stem + L" (" + std::to_wstring(i) + L")" + ext);
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) return candidate;
    }
    return JoinPath(dir, name);
}

static void PasteFromClipboard() {
    if (!OpenClipboard(g_hwnd)) return;
    HDROP drop = (HDROP)GetClipboardData(CF_HDROP);
    if (!drop) {
        CloseClipboard();
        return;
    }
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    bool ok = true;
    for (UINT i = 0; i < count; ++i) {
        wchar_t path[MAX_PATH * 4]{};
        DragQueryFileW(drop, i, path, MAX_PATH * 4);
        std::wstring src = path;
        size_t pos = src.find_last_of(L"\\/");
        std::wstring name = pos == std::wstring::npos ? src : src.substr(pos + 1);
        std::wstring dst = UniqueDestination(g_currentDir, name);
        DWORD attrs = GetFileAttributesW(src.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            ok = false;
        } else if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            ok = CopyDirectoryRecursive(src, dst) && ok;
        } else {
            ok = CopyFileW(src.c_str(), dst.c_str(), FALSE) && ok;
        }
    }
    CloseClipboard();
    FillList(g_currentDir);
    if (!ok) MessageBoxW(g_hwnd, L"Some items could not be pasted.", L"File Viewer", MB_ICONWARNING);
}

static bool DeletePathWithShell(const std::wstring& path, bool recycle) {
    std::wstring doubleNull = path + L'\0' + L'\0';
    SHFILEOPSTRUCTW op{};
    op.hwnd = g_hwnd;
    op.wFunc = FO_DELETE;
    op.pFrom = doubleNull.c_str();
    op.fFlags = FOF_NOCONFIRMMKDIR | FOF_SILENT;
    if (recycle) op.fFlags |= FOF_ALLOWUNDO;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

static void FillList(const std::wstring& dir) {
    ListView_DeleteAllItems(g_list);
    g_currentDir = NormalizePath(dir);
    g_entries.clear();
    auto allEntries = EnumerateDirectory(dir, false);
    for (const auto& e : allEntries) {
        if (FuzzyMatch(e.name, g_query)) g_entries.push_back(e);
    }

    for (size_t i = 0; i < g_entries.size(); ++i) {
        const Entry& e = g_entries[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = (int)i;
        item.pszText = const_cast<LPWSTR>(e.name.c_str());
        item.iImage = IconForEntry(e);
        item.lParam = (LPARAM)i;
        ListView_InsertItem(g_list, &item);

        std::wstring type = TypeLabel(e);
        std::wstring size = e.isDir ? L"" : HumanSize(e.size);
        std::wstring modified = FormatFileTime(e.modified);
        ListView_SetItemText(g_list, (int)i, 1, const_cast<LPWSTR>(type.c_str()));
        ListView_SetItemText(g_list, (int)i, 2, const_cast<LPWSTR>(size.c_str()));
        ListView_SetItemText(g_list, (int)i, 3, const_cast<LPWSTR>(modified.c_str()));
    }

    int folders = 0;
    for (const auto& e : g_entries) if (e.isDir) folders++;
    wchar_t status[128]{};
    if (g_query.empty()) {
        swprintf(status, 128, L"%d items    %d folders    %d files", (int)g_entries.size(), folders, (int)g_entries.size() - folders);
    } else {
        swprintf(status, 128, L"%d / %d matches", (int)g_entries.size(), (int)allEntries.size());
    }
    SetStatus(status);
}

static void SelectEntryByPath(const std::wstring& path) {
    std::wstring target = NormalizePath(path);
    for (int i = 0; i < (int)g_entries.size(); ++i) {
        if (_wcsicmp(NormalizePath(g_entries[i].path).c_str(), target.c_str()) == 0) {
            ListView_SetItemState(g_list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g_list, i, FALSE);
            SetFocus(g_list);
            return;
        }
    }
}

static void DrawHighlightedName(HDC hdc, RECT rc, const Entry& entry, bool selected) {
    std::vector<bool> marks;
    FuzzyMatch(entry.name, g_query, &marks);
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_font);
    int x = rc.left;
    int y = rc.top + (rc.bottom - rc.top - Dpi(18)) / 2;
    for (size_t i = 0; i < entry.name.size(); ++i) {
        wchar_t ch[2] = { entry.name[i], 0 };
        SetTextColor(hdc, (!g_query.empty() && i < marks.size() && marks[i]) ? kHighlight : (selected ? RGB(255, 255, 255) : kText));
        TextOutW(hdc, x, y, ch, 1);
        SIZE sz{};
        GetTextExtentPoint32W(hdc, ch, 1, &sz);
        x += sz.cx;
        if (x > rc.right - Dpi(8)) break;
    }
    SelectObject(hdc, oldFont);
}

static void DrawListSubItem(NMLVCUSTOMDRAW* cd) {
    int row = (int)cd->nmcd.dwItemSpec;
    int sub = cd->iSubItem;
    if (row < 0 || row >= (int)g_entries.size()) return;

    RECT rc{};
    ListView_GetSubItemRect(g_list, row, sub, LVIR_BOUNDS, &rc);
    bool selected = (ListView_GetItemState(g_list, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    HBRUSH bg = CreateSolidBrush(selected ? kSurface2 : kBg);
    FillRect(cd->nmcd.hdc, &rc, bg);
    DeleteObject(bg);

    RECT textRc = rc;
    textRc.left += Dpi(10);
    textRc.right -= Dpi(8);
    SetBkMode(cd->nmcd.hdc, TRANSPARENT);
    SetTextColor(cd->nmcd.hdc, selected ? RGB(255, 255, 255) : (sub == 0 ? kText : kMuted));
    HFONT oldFont = (HFONT)SelectObject(cd->nmcd.hdc, g_font);

    const Entry& e = g_entries[row];
    if (sub == 0) {
        int icon = IconForEntry(e);
        ImageList_Draw(g_icons, icon, cd->nmcd.hdc, textRc.left, rc.top + (rc.bottom - rc.top - Dpi(kIconSizeBase)) / 2, ILD_TRANSPARENT);
        textRc.left += Dpi(kIconSizeBase + 10);
        DrawHighlightedName(cd->nmcd.hdc, textRc, e, selected);
    } else {
        std::wstring text;
        if (sub == 1) text = TypeLabel(e);
        else if (sub == 2) text = e.isDir ? L"" : HumanSize(e.size);
        else if (sub == 3) text = FormatFileTime(e.modified);
        DrawTextW(cd->nmcd.hdc, text.c_str(), -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(cd->nmcd.hdc, oldFont);
}

static LRESULT HandleListCustomDraw(LPARAM lParam) {
    auto* cd = (NMLVCUSTOMDRAW*)lParam;
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        DrawListSubItem(cd);
        return CDRF_SKIPDEFAULT;
    default:
        return CDRF_DODEFAULT;
    }
}

static int SelectedIndex() {
    return ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
}

static void ShowContextMenu(POINT screenPt) {
    int selected = SelectedIndex();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (selected < 0 ? MF_GRAYED : 0), ID_CTX_COPY, L"Copy");
    AppendMenuW(menu, MF_STRING, ID_CTX_PASTE, L"Paste");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (selected < 0 ? MF_GRAYED : 0), ID_CTX_RENAME, L"Rename");
    AppendMenuW(menu, MF_STRING | (selected < 0 ? MF_GRAYED : 0), ID_CTX_RECYCLE, L"Move to Recycle Bin");
    AppendMenuW(menu, MF_STRING | (selected < 0 ? MF_GRAYED : 0), ID_CTX_DELETE, L"Delete permanently");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

static void UpdateNavButtons() {
    EnableWindow(g_back, g_historyIndex > 0);
    EnableWindow(g_forward, g_historyIndex >= 0 && g_historyIndex < (int)g_history.size() - 1);
}

static bool NavigateTo(const std::wstring& rawPath, bool addHistory = true) {
    std::wstring path = NormalizePath(rawPath);
    if (!DirectoryExists(path)) {
        MessageBoxW(g_hwnd, L"This location cannot be opened.", L"File Viewer", MB_ICONWARNING);
        return false;
    }
    SetWindowTextW(g_address, path.c_str());
    FillList(path);

    if (addHistory && !g_navigatingFromHistory) {
        if (g_historyIndex >= 0 && g_historyIndex < (int)g_history.size() - 1) {
            g_history.erase(g_history.begin() + g_historyIndex + 1, g_history.end());
        }
        if (g_history.empty() || _wcsicmp(g_history.back().c_str(), path.c_str()) != 0) {
            g_history.push_back(path);
            g_historyIndex = (int)g_history.size() - 1;
        }
    }
    UpdateNavButtons();
    return true;
}

static bool ExecuteSearchJump() {
    std::wstring text = Trim(WindowText(g_search));
    if (text.empty() || text[0] != L'>') return false;
    std::wstring resolved = ResolveJumpPath(text.substr(1));
    if (resolved.empty()) return true;

    if (DirectoryExists(resolved)) {
        NavigateTo(resolved);
        SetStatus(L"Opened " + NormalizePath(resolved));
    } else if (FileExists(resolved)) {
        std::wstring parent = ParentPath(resolved);
        if (NavigateTo(parent)) SelectEntryByPath(resolved);
    } else {
        MessageBoxW(g_hwnd, L"No matching file or folder was found.", L"File Viewer", MB_ICONWARNING);
    }
    return true;
}

static void OpenSelected() {
    int selected = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= (int)g_entries.size()) return;
    const Entry& e = g_entries[selected];
    if (e.isDir) {
        NavigateTo(e.path);
    } else {
        ShellExecuteW(g_hwnd, L"open", e.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

static void InitIcons() {
    int iconSize = Dpi(kIconSizeBase);
    g_icons = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK, 32, 64);
    g_iconCacheDir = JoinPath(ExecutableDir(), L"icon-cache\\96");
    AddIcon(L"generic", L"text");
    AddIcon(L"folder", L"_folder");
    AddIcon(L"folder_open", L"_folder_open");
    InitExtensionMap();
}

static void SetupListColumns() {
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    col.cx = Dpi(360);
    col.pszText = const_cast<LPWSTR>(L"Name");
    ListView_InsertColumn(g_list, 0, &col);
    col.cx = Dpi(130);
    col.pszText = const_cast<LPWSTR>(L"Type");
    ListView_InsertColumn(g_list, 1, &col);
    col.cx = Dpi(110);
    col.pszText = const_cast<LPWSTR>(L"Size");
    ListView_InsertColumn(g_list, 2, &col);
    col.cx = Dpi(160);
    col.pszText = const_cast<LPWSTR>(L"Date modified");
    ListView_InsertColumn(g_list, 3, &col);
}

static void ResizeChildren(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int toolbarHeight = Dpi(kToolbarHeightBase);
    int statusHeight = Dpi(kStatusHeightBase);
    int buttonTop = Dpi(11);
    int buttonSize = Dpi(36);
    int x = Dpi(12);
    MoveWindow(g_back, x, buttonTop, buttonSize, buttonSize, TRUE); x += buttonSize + 6;
    MoveWindow(g_forward, x, buttonTop, buttonSize, buttonSize, TRUE); x += buttonSize + 6;
    MoveWindow(g_up, x, buttonTop, buttonSize, buttonSize, TRUE); x += buttonSize + 6;
    MoveWindow(g_refresh, x, buttonTop, Dpi(88), buttonSize, TRUE); x += Dpi(96);
    int searchWidth = std::min(Dpi(260), std::max(Dpi(160), width / 4));
    MoveWindow(g_search, width - searchWidth - Dpi(12), buttonTop, searchWidth, buttonSize, TRUE);
    MoveWindow(g_address, x, buttonTop, std::max(Dpi(120), width - x - searchWidth - Dpi(24)), buttonSize, TRUE);

    int contentTop = toolbarHeight;
    int contentHeight = std::max(0, height - toolbarHeight - statusHeight);
    int side = std::min(Dpi(kSidebarWidthBase), width / 2);
    MoveWindow(g_tree, Dpi(10), contentTop + Dpi(6), std::max(0, side - Dpi(16)), std::max(0, contentHeight - Dpi(12)), TRUE);
    MoveWindow(g_list, side + Dpi(2), contentTop + Dpi(6), std::max(0, width - side - Dpi(12)), std::max(0, contentHeight - Dpi(12)), TRUE);
    MoveWindow(g_status, Dpi(12), height - statusHeight, width - Dpi(24), statusHeight, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_bgBrush = CreateSolidBrush(kBg);
        g_surfaceBrush = CreateSolidBrush(kSurface);
        HDC screen = GetDC(hwnd);
        g_dpi = (UINT)GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(hwnd, screen);
        g_font = CreateFontW(-Dpi(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Noto Sans SC");

        g_back = CreateWindowW(L"BUTTON", L"\u2190", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)ID_BACK, nullptr, nullptr);
        g_forward = CreateWindowW(L"BUTTON", L"\u2192", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)ID_FORWARD, nullptr, nullptr);
        g_up = CreateWindowW(L"BUTTON", L"\u2191", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)ID_UP, nullptr, nullptr);
        g_refresh = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)ID_REFRESH, nullptr, nullptr);
        g_address = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_ADDRESS, nullptr, nullptr);
        g_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_SEARCH, nullptr, nullptr);
        g_tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd, (HMENU)ID_TREE, nullptr, nullptr);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | LVS_EDITLABELS, 0, 0, 0, 0, hwnd, (HMENU)ID_LIST, nullptr, nullptr);
        g_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);

        HWND controls[] = { g_back, g_forward, g_up, g_refresh, g_address, g_search, g_tree, g_list, g_status };
        for (HWND c : controls) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        StyleButton(g_back);
        StyleButton(g_forward);
        StyleButton(g_up);
        StyleButton(g_refresh);
        SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search or >path");
        SetWindowTheme(g_tree, L"Explorer", nullptr);
        SetWindowTheme(g_list, L"Explorer", nullptr);

        InitIcons();
        TreeView_SetImageList(g_tree, g_icons, TVSIL_NORMAL);
        ListView_SetImageList(g_list, g_icons, LVSIL_SMALL);
        SetDarkListColors();
        SetupListColumns();
        PopulateTreeRoot();

        wchar_t winDir[MAX_PATH]{};
        GetWindowsDirectoryW(winDir, MAX_PATH);
        std::wstring start = std::wstring(winDir).substr(0, 3);
        NavigateTo(start);
        ResizeChildren(hwnd);
        return 0;
    }
    case WM_SIZE:
        ResizeChildren(hwnd);
        return 0;
    case WM_ERASEBKGND: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, g_bgBrush);
        return 1;
    }
    case WM_DRAWITEM:
        DrawRoundedButton((DRAWITEMSTRUCT*)lParam);
        return TRUE;
    case WM_TIMER:
        if (wParam == 42) {
            bool active = false;
            DWORD now = GetTickCount();
            for (auto it = g_ripples.begin(); it != g_ripples.end();) {
                if (now - it->second.started >= 420) {
                    InvalidateRect(it->first, nullptr, FALSE);
                    it = g_ripples.erase(it);
                } else {
                    active = true;
                    InvalidateRect(it->first, nullptr, FALSE);
                    ++it;
                }
            }
            if (!active) KillTimer(hwnd, 42);
        }
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wParam, kText);
        SetBkColor((HDC)wParam, kSurface);
        return (LRESULT)g_surfaceBrush;
    case WM_CONTEXTMENU:
        if ((HWND)wParam == g_list) {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pt.x == -1 && pt.y == -1) {
                RECT rc{};
                int selected = SelectedIndex();
                if (selected >= 0) ListView_GetItemRect(g_list, selected, &rc, LVIR_BOUNDS);
                else GetClientRect(g_list, &rc);
                pt = { rc.left + Dpi(32), rc.top + Dpi(18) };
                ClientToScreen(g_list, &pt);
            }
            ShowContextMenu(pt);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BACK && HIWORD(wParam) == BN_CLICKED && g_historyIndex > 0) {
            g_navigatingFromHistory = true;
            g_historyIndex--;
            NavigateTo(g_history[g_historyIndex], false);
            g_navigatingFromHistory = false;
        } else if (LOWORD(wParam) == ID_FORWARD && HIWORD(wParam) == BN_CLICKED && g_historyIndex < (int)g_history.size() - 1) {
            g_navigatingFromHistory = true;
            g_historyIndex++;
            NavigateTo(g_history[g_historyIndex], false);
            g_navigatingFromHistory = false;
        } else if (LOWORD(wParam) == ID_UP && HIWORD(wParam) == BN_CLICKED) {
            NavigateTo(ParentPath(WindowText(g_address)));
        } else if (LOWORD(wParam) == ID_REFRESH && HIWORD(wParam) == BN_CLICKED) {
            NavigateTo(WindowText(g_address), false);
        } else if (LOWORD(wParam) == ID_SEARCH && HIWORD(wParam) == EN_CHANGE) {
            std::wstring searchText = WindowText(g_search);
            g_query = SearchTextIsJump(searchText) ? L"" : searchText;
            if (!g_currentDir.empty()) FillList(g_currentDir);
            if (SearchTextIsJump(searchText)) SetStatus(L"Press Enter to open this path.");
        } else if (LOWORD(wParam) == ID_ADDRESS && HIWORD(wParam) == EN_UPDATE) {
            // handled on Enter through WM_KEYDOWN subclass-free accelerator below
        } else if (LOWORD(wParam) == ID_CTX_COPY) {
            int selected = SelectedIndex();
            if (selected >= 0 && selected < (int)g_entries.size()) PathListToClipboard({ g_entries[selected].path });
        } else if (LOWORD(wParam) == ID_CTX_PASTE) {
            PasteFromClipboard();
        } else if (LOWORD(wParam) == ID_CTX_RENAME) {
            int selected = SelectedIndex();
            if (selected >= 0) ListView_EditLabel(g_list, selected);
        } else if (LOWORD(wParam) == ID_CTX_RECYCLE || LOWORD(wParam) == ID_CTX_DELETE) {
            int selected = SelectedIndex();
            if (selected >= 0 && selected < (int)g_entries.size()) {
                bool recycle = LOWORD(wParam) == ID_CTX_RECYCLE;
                if (recycle || MessageBoxW(hwnd, L"Permanently delete this item?", L"File Viewer", MB_ICONWARNING | MB_YESNO) == IDYES) {
                    DeletePathWithShell(g_entries[selected].path, recycle);
                    FillList(g_currentDir);
                }
            }
        }
        return 0;
    case WM_NOTIFY: {
        LPNMHDR hdr = (LPNMHDR)lParam;
        if (hdr->hwndFrom == g_list && hdr->code == NM_CUSTOMDRAW) {
            return HandleListCustomDraw(lParam);
        }
        if (hdr->hwndFrom == g_list && hdr->code == NM_DBLCLK) {
            OpenSelected();
            return 0;
        }
        if (hdr->hwndFrom == g_list && hdr->code == NM_RCLICK) {
            DWORD pos = GetMessagePos();
            POINT pt{ GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
            POINT client = pt;
            ScreenToClient(g_list, &client);
            LVHITTESTINFO hit{};
            hit.pt = client;
            int item = ListView_HitTest(g_list, &hit);
            if (item >= 0) {
                ListView_SetItemState(g_list, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            ShowContextMenu(pt);
            return 0;
        }
        if (hdr->hwndFrom == g_list && hdr->code == LVN_ENDLABELEDITW) {
            auto* edit = (NMLVDISPINFOW*)lParam;
            if (edit->item.pszText && edit->item.iItem >= 0 && edit->item.iItem < (int)g_entries.size()) {
                std::wstring newName = edit->item.pszText;
                if (!newName.empty() && newName.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos) {
                    std::wstring dst = JoinPath(g_currentDir, newName);
                    if (MoveFileW(g_entries[edit->item.iItem].path.c_str(), dst.c_str())) {
                        FillList(g_currentDir);
                        return TRUE;
                    }
                    MessageBoxW(hwnd, L"Rename failed.", L"File Viewer", MB_ICONWARNING);
                }
            }
            return FALSE;
        }
        if (hdr->hwndFrom == g_tree && hdr->code == TVN_ITEMEXPANDINGW) {
            auto info = (LPNMTREEVIEWW)lParam;
            if (info->action == TVE_EXPAND) PopulateTreeChildren(info->itemNew.hItem);
            return 0;
        }
        if (hdr->hwndFrom == g_tree && hdr->code == TVN_SELCHANGEDW) {
            auto info = (LPNMTREEVIEWW)lParam;
            auto it = g_treePaths.find(info->itemNew.hItem);
            if (it != g_treePaths.end() && !it->second.empty()) NavigateTo(it->second);
            return 0;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (GetFocus() == g_address && wParam == VK_RETURN) {
            NavigateTo(WindowText(g_address));
            return 0;
        }
        if (GetFocus() == g_search && wParam == VK_RETURN) {
            ExecuteSearchJump();
            return 0;
        }
        if (GetFocus() == g_list && wParam == VK_RETURN) {
            OpenSelected();
            return 0;
        }
        if (GetFocus() == g_list && wParam == VK_F2) {
            int selected = SelectedIndex();
            if (selected >= 0) ListView_EditLabel(g_list, selected);
            return 0;
        }
        if (GetFocus() == g_list && wParam == VK_DELETE) {
            int selected = SelectedIndex();
            if (selected >= 0 && selected < (int)g_entries.size()) {
                DeletePathWithShell(g_entries[selected].path, true);
                FillList(g_currentDir);
            }
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'C') {
            int selected = SelectedIndex();
            if (selected >= 0 && selected < (int)g_entries.size()) PathListToClipboard({ g_entries[selected].path });
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'V') {
            PasteFromClipboard();
            return 0;
        }
        return 0;
    case WM_DESTROY:
        if (g_icons) ImageList_Destroy(g_icons);
        if (g_font) DeleteObject(g_font);
        if (g_bgBrush) DeleteObject(g_bgBrush);
        if (g_surfaceBrush) DeleteObject(g_surfaceBrush);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static int SmokeTest() {
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) return 2;
    g_iconCacheDir = JoinPath(ExecutableDir(), L"icon-cache\\96");
    const wchar_t* names[] = { L"_folder", L"text", L"cpp", L"python", L"image" };
    for (const wchar_t* name : names) {
        std::wstring pngPath = JoinPath(g_iconCacheDir, std::wstring(name) + L".png");
        HBITMAP hbmp = LoadPngBitmap(pngPath, Dpi(kIconSizeBase));
        if (!hbmp) {
            GdiplusShutdown(g_gdiplusToken);
            return 3;
        }
        DeleteObject(hbmp);
    }
    auto drives = GetDrives();
    if (ResolveJumpPath(L"Proj") != ProjectsPath()) return 5;
    if (ResolveJumpPath(L"Dwld") != DownloadsPath()) return 6;
    if (ResolveJumpPath(L"Sstm") != L"C:\\Windows\\System32") return 7;
    if (ResolveJumpPath(L"Phto") != PhotosPath()) return 8;
    if (ResolveJumpPath(L"Vido") != VideosPath()) return 9;
    if (ResolveJumpPath(L"Noip") != JoinPath(ProjectsPath(), L"NOIP")) return 10;
    if (ResolveJumpPath(L"C-Root") != L"C:\\") return 11;
    if (ResolveJumpPath(L"D-Root") != L"D:\\") return 12;
    if (ResolveJumpPath(L"U-Root") != L"E:\\") return 13;
    if (ResolveJumpPath(L"Dskt") != DesktopPath()) return 14;
    if (ResolveJumpPath(L"User") != UserPath()) return 15;
    if (ResolveJumpPath(L"Desktop\\A-MyProject") != JoinPath(DesktopPath(), L"A-MyProject")) return 16;
    GdiplusShutdown(g_gdiplusToken);
    return drives.empty() ? 4 : 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR cmdLine, int nCmdShow) {
    if (wcsstr(cmdLine, L"--smoke-test")) return SmokeTest();

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    FARPROC dpiProc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (dpiProc) {
        union {
            FARPROC raw;
            BOOL (WINAPI *typed)(DPI_AWARENESS_CONTEXT);
        } setDpiContext{ dpiProc };
        setDpiContext.typed(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    else SetProcessDPIAware();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) {
        MessageBoxW(nullptr, L"GDI+ initialization failed.", L"File Viewer", MB_ICONERROR);
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"OMGFileViewerWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"File Viewer", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1120, 720,
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (GetFocus() == g_address && msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
            continue;
        }
        if (GetFocus() == g_search && msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
            continue;
        }
        if (GetFocus() == g_list && msg.message == WM_KEYDOWN &&
            (msg.wParam == VK_RETURN || msg.wParam == VK_F2 || msg.wParam == VK_DELETE ||
             ((GetKeyState(VK_CONTROL) & 0x8000) && (msg.wParam == 'C' || msg.wParam == 'V')))) {
            SendMessageW(hwnd, WM_KEYDOWN, msg.wParam, msg.lParam);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}
