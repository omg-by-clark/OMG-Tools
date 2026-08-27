#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <gdiplus.h>

#include "scintilla-include/Scintilla.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

constexpr int ID_NEW = 1001;
constexpr int ID_OPEN = 1002;
constexpr int ID_SAVE = 1003;
constexpr int ID_SAVE_AS = 1004;
constexpr int ID_RUN = 1005;
constexpr int ID_CLEAR = 1006;
constexpr int ID_EXIT = 1007;
constexpr int ID_SETTINGS = 1009;
constexpr int ID_ZOOM_IN = 1013;
constexpr int ID_ZOOM_OUT = 1014;
constexpr int ID_ZOOM_RESET = 1015;
constexpr int ID_FORMAT = 1017;
constexpr int ID_OUTPUT_TAB = 1018;
constexpr int ID_TERMINAL_TAB = 1019;
constexpr int ID_TERMINAL_FOCUS = 1020;
constexpr int ID_WINDOW_MINIMIZE = 1021;
constexpr int ID_WINDOW_FULLSCREEN = 1022;
constexpr int ID_TERMINAL_PROMPT = 1023;
constexpr int ID_TERMINAL_INPUT = 1024;
constexpr int ID_NEW_FILE_NAME = 1025;
constexpr int ID_NEW_FILE_CREATE = 1026;
constexpr int ID_NEW_FILE_CANCEL = 1027;
constexpr int ID_SETTINGS_THEME = 2001;
constexpr int ID_SETTINGS_SIZE = 2002;
constexpr int ID_SETTINGS_INDENT_TYPE = 2003;
constexpr int ID_SETTINGS_SPACES_LABEL = 2004;
constexpr int ID_SETTINGS_SPACES = 2005;
constexpr int ID_SETTINGS_FONT = 2006;
constexpr int ID_SETTINGS_APPLY = 2007;
constexpr int ID_SETTINGS_CLOSE = 2008;
constexpr int ID_SETTINGS_THEME_LABEL = 2009;
constexpr int ID_SETTINGS_SIZE_LABEL = 2010;
constexpr int ID_SETTINGS_INDENT_LABEL = 2011;
constexpr int ID_SETTINGS_FONT_LABEL = 2012;
constexpr UINT WM_RUN_FINISHED = WM_APP + 1;
constexpr UINT WM_TERMINAL_OUTPUT = WM_APP + 2;
constexpr UINT WM_TERMINAL_EXITED = WM_APP + 3;
constexpr UINT WM_INTERACTIVE_RUN_OUTPUT = WM_APP + 4;
constexpr UINT WM_INTERACTIVE_RUN_FINISHED = WM_APP + 5;

constexpr int SCLEX_CPP = 3;
constexpr int SCE_C_DEFAULT = 0;
constexpr int SCE_C_COMMENT = 1;
constexpr int SCE_C_COMMENTLINE = 2;
constexpr int SCE_C_COMMENTDOC = 3;
constexpr int SCE_C_NUMBER = 4;
constexpr int SCE_C_WORD = 5;
constexpr int SCE_C_STRING = 6;
constexpr int SCE_C_CHARACTER = 7;
constexpr int SCE_C_PREPROCESSOR = 9;
constexpr int SCE_C_OPERATOR = 10;
constexpr int SCE_C_IDENTIFIER = 11;
constexpr int SCE_C_STRINGEOL = 12;
constexpr int SCE_C_COMMENTLINEDOC = 15;
constexpr int SCE_C_WORD2 = 16;
constexpr int SCE_C_GLOBALCLASS = 19;
constexpr int SCE_C_STRINGRAW = 20;
constexpr int SCE_C_PREPROCESSORCOMMENT = 23;
constexpr int SCE_C_USERLITERAL = 25;
constexpr int SCE_C_ESCAPESEQUENCE = 27;
constexpr int SCE_OMG_BUILTIN = 28;
constexpr int INDIC_NAMESPACE = 19;
constexpr int INDIC_HEADER = 20;
constexpr int INDIC_FUNCTION = 21;
constexpr int INDIC_MACRO = 22;
constexpr int INDIC_ESCAPE = 23;
constexpr int INDIC_BRACKET_0 = 24;
constexpr int BRACKET_INDICATORS = 7;

HWND g_window = nullptr;
HWND g_editor = nullptr;
HWND g_output = nullptr;
HWND g_terminal_output = nullptr;
HWND g_terminal_prompt = nullptr;
HWND g_terminal_input = nullptr;
HWND g_status = nullptr;
HWND g_run_button = nullptr;
HWND g_file_label = nullptr;
HWND g_output_tab = nullptr;
HWND g_terminal_tab = nullptr;
HWND g_settings_window = nullptr;
HWND g_hovered_window_control = nullptr;
HFONT g_ui_font = nullptr;
HBRUSH g_window_brush = nullptr;
HBRUSH g_panel_brush = nullptr;
HBRUSH g_output_brush = nullptr;
HICON g_app_icon = nullptr;
HMODULE g_scintilla = nullptr;
HMODULE g_lexilla = nullptr;
void* g_cpp_lexer = nullptr;
DWORD g_startup_error = ERROR_SUCCESS;
ULONG_PTR g_gdiplus_token = 0;
std::filesystem::path g_file;
std::filesystem::path g_last_file;
std::filesystem::path g_terminal_directory;
bool g_dirty = false;
bool g_running = false;
bool g_fullscreen = true;
int g_code_size = 18;
int g_theme_index = 0;
bool g_indent_tabs = false;
int g_indent_spaces = 2;
std::wstring g_code_font_name = L"Google Sans Code";
std::map<std::wstring, int> g_completion_memory;
std::wstring g_output_text;
std::wstring g_terminal_text;
HANDLE g_terminal_process = nullptr;
HANDLE g_terminal_stdin_write = nullptr;
HANDLE g_terminal_stdout_read = nullptr;
HANDLE g_interactive_process = nullptr;
HANDLE g_interactive_stdin_write = nullptr;

enum class BottomPaneTab {
    Output,
    Terminal
};

BottomPaneTab g_bottom_tab = BottomPaneTab::Output;

enum class EditorLanguage {
    Omg,
    Cpp
};

void refresh_main_fonts();
void layout(HWND window);
void sync_terminal_directory();

std::filesystem::path module_directory() {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return std::filesystem::path(buffer).parent_path();
}

void write_startup_log(const std::string& message, bool append = true) {
    std::ofstream stream(module_directory() / L"OMG-IDE-startup.log",
                         std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (stream) stream << message << '\n';
}

std::filesystem::path default_save_directory() {
    wchar_t buffer[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) {
        return std::filesystem::path(buffer) / L"Desktop" / L"A-MyProject" / L"NOIP";
    }
    return module_directory();
}

std::filesystem::path default_save_path() {
    const std::filesystem::path directory = default_save_directory();
    const std::filesystem::path initial = directory / L"Untitled.omg";
    std::error_code error;
    if (!std::filesystem::exists(initial, error)) return initial;

    // A first save should create a file immediately, but must never overwrite
    // another unsaved document that already has the default name.
    for (unsigned int number = 1; number < 10000; ++number) {
        const std::filesystem::path candidate = directory /
            (L"Untitled (" + std::to_wstring(number) + L").omg");
        error.clear();
        if (!std::filesystem::exists(candidate, error)) return candidate;
    }
    return initial;
}

std::filesystem::path current_file_directory() {
    return g_file.empty() ? default_save_directory() : g_file.parent_path();
}

enum class CompletionKind {
    Keyword,
    Primitive,
    Type,
    Function,
    Variable,
    Namespace,
    Preprocessor,
    Header
};

struct CompletionItem {
    std::wstring text;
    CompletionKind kind;
};

HWND g_completion_popup = nullptr;
HFONT g_completion_font = nullptr;
HFONT g_completion_bold_font = nullptr;
HFONT g_completion_meta_font = nullptr;
std::vector<CompletionItem> g_completion_items;
std::wstring g_completion_query;
Sci_Position g_completion_start = 0;
int g_completion_selected = 0;
int g_completion_first_visible = 0;
int g_completion_row_height = 24;
int g_completion_visible_rows = 0;
wchar_t g_completion_swallow_char = 0;

struct Theme {
    const wchar_t* name;
    COLORREF background;
    COLORREF panel;
    COLORREF editor;
    COLORREF output;
    COLORREF line;
    COLORREF text;
    COLORREF muted;
    COLORREF green;
    COLORREF blue;
    COLORREF purple;
    COLORREF orange;
    COLORREF comment;
    COLORREF yellow;
    COLORREF red;
    COLORREF pink;
    COLORREF sky;
    COLORREF rosewater;
};

constexpr Theme THEMES[] = {
    {L"Catppuccin", RGB(35, 38, 52), RGB(41, 44, 60), RGB(48, 52, 70), RGB(41, 44, 60),
     RGB(65, 69, 89), RGB(198, 208, 245), RGB(131, 139, 167), RGB(166, 209, 137),
     RGB(140, 170, 238), RGB(202, 158, 230), RGB(239, 159, 118), RGB(148, 156, 187),
     RGB(229, 200, 144), RGB(231, 130, 132), RGB(244, 184, 228), RGB(153, 209, 219), RGB(242, 213, 207)},
    {L"Everforest", RGB(35, 42, 46), RGB(45, 53, 59), RGB(45, 53, 59), RGB(35, 42, 46),
     RGB(52, 63, 68), RGB(211, 198, 170), RGB(122, 132, 120), RGB(167, 192, 128),
     RGB(127, 187, 179), RGB(214, 153, 182), RGB(230, 152, 117), RGB(133, 146, 137),
     RGB(219, 188, 127), RGB(230, 126, 128), RGB(214, 153, 182), RGB(131, 192, 146), RGB(131, 192, 146)},
    {L"VS Code", RGB(37, 37, 38), RGB(45, 45, 48), RGB(30, 30, 30), RGB(24, 24, 24),
     RGB(60, 60, 60), RGB(212, 212, 212), RGB(133, 133, 133), RGB(106, 153, 85),
     RGB(86, 156, 214), RGB(197, 134, 192), RGB(206, 145, 120), RGB(106, 153, 85),
     RGB(220, 220, 170), RGB(244, 71, 71), RGB(197, 134, 192), RGB(78, 201, 176), RGB(206, 145, 120)}
};

const Theme& theme() { return THEMES[g_theme_index]; }

constexpr sptr_t opaque_element_colour(COLORREF colour) {
    return static_cast<sptr_t>(static_cast<unsigned long>(colour) | 0xFF000000UL);
}

int window_dpi(HWND window) {
    HDC dc = GetDC(window);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(window, dc);
    return dpi > 0 ? dpi : 96;
}

int scale_px(HWND window, int value) {
    return MulDiv(value, window_dpi(window), 96);
}

SIZE measure_ui_text(HWND window, const wchar_t* text) {
    SIZE size{};
    HDC dc = GetDC(window);
    if (!dc) return size;
    HFONT old_font = g_ui_font ? static_cast<HFONT>(SelectObject(dc, g_ui_font)) : nullptr;
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    if (old_font) SelectObject(dc, old_font);
    ReleaseDC(window, dc);
    return size;
}

int ui_text_height(HWND window) {
    HDC dc = GetDC(window);
    if (!dc) return scale_px(window, 20);
    HFONT old_font = g_ui_font ? static_cast<HFONT>(SelectObject(dc, g_ui_font)) : nullptr;
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    if (old_font) SelectObject(dc, old_font);
    ReleaseDC(window, dc);
    return std::max(1L, metrics.tmHeight);
}

std::wstring widen_utf8(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string narrow_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

sptr_t sci(UINT message, uptr_t wparam = 0, sptr_t lparam = 0) {
    return SendMessageW(g_editor, message, wparam, lparam);
}

std::wstring settings_path() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return (std::filesystem::path(path).parent_path() / L"OMG IDE.ini").wstring();
}

std::wstring memory_key(const std::wstring& value) {
    const std::string utf8 = narrow_utf8(value);
    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring key;
    for (unsigned char ch : utf8) {
        if (isalnum(ch) || ch == '_' || ch == '-') {
            key.push_back(static_cast<wchar_t>(ch));
        } else {
            key.push_back(L'%');
            key.push_back(hex[ch >> 4]);
            key.push_back(hex[ch & 0x0F]);
        }
    }
    return key;
}

const std::unordered_set<std::wstring>& cpp_keywords() {
    static const std::unordered_set<std::wstring> words = {
        L"alignas", L"alignof", L"asm", L"auto", L"break", L"case", L"catch", L"class", L"const",
        L"constexpr", L"continue", L"default", L"delete", L"do", L"else", L"enum", L"explicit", L"export",
        L"extern", L"for", L"friend", L"goto", L"if", L"inline", L"namespace", L"new", L"noexcept",
        L"operator", L"private", L"protected", L"public", L"return", L"sizeof", L"static", L"struct",
        L"switch", L"template", L"this", L"throw", L"try", L"typedef", L"typename", L"union", L"using",
        L"virtual", L"volatile", L"while", L"override", L"final", L"concept", L"requires", L"co_await",
        L"co_return", L"co_yield"
    };
    return words;
}

const std::unordered_set<std::wstring>& cpp_primitives() {
    static const std::unordered_set<std::wstring> words = {
        L"bool", L"char", L"char8_t", L"char16_t", L"char32_t", L"double", L"float", L"int", L"long",
        L"short", L"signed", L"unsigned", L"void", L"wchar_t"
    };
    return words;
}

const std::unordered_set<std::wstring>& cpp_types() {
    static const std::unordered_set<std::wstring> words = {
        L"size_t", L"string", L"wstring", L"vector", L"map", L"set", L"unordered_map", L"unordered_set",
        L"unique_ptr", L"shared_ptr", L"weak_ptr", L"optional", L"variant", L"tuple", L"array"
    };
    return words;
}

const std::unordered_set<std::wstring>& omg_keywords() {
    static const std::unordered_set<std::wstring> words = {
        L"var", L"val", L"func", L"for", L"in", L"to", L"while", L"repeat", L"until", L"if",
        L"else", L"return", L"break", L"continue", L"define", L"as", L"true", L"false", L"null",
        L"and", L"or", L"not"
    };
    return words;
}

const std::unordered_set<std::wstring>& omg_types() {
    static const std::unordered_set<std::wstring> words = {
        L"digit", L"float", L"string", L"char", L"array", L"queue", L"pqueue", L"pquue", L"greater_pqueue",
        L"gpqueue", L"stack"
    };
    return words;
}

const std::unordered_set<std::wstring>& omg_builtins() {
    static const std::unordered_set<std::wstring> words = {
        L"print", L"println", L"input", L"input_without_display", L"calc", L"to_digit", L"to_char",
        L"to_string", L"type_of", L"size_of", L"push", L"pop", L"top_of", L"front_of"
    };
    return words;
}

EditorLanguage current_language() {
    if (g_file.empty()) return EditorLanguage::Omg;
    std::wstring ext = g_file.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
    return ext == L".omg" ? EditorLanguage::Omg : EditorLanguage::Cpp;
}

bool is_omg_language() {
    return current_language() == EditorLanguage::Omg;
}

void load_completion_memory() {
    g_completion_memory.clear();
    const std::wstring path = settings_path();
    wchar_t names[8192]{};
    const DWORD length = GetPrivateProfileStringW(L"CompletionMemory", nullptr, L"", names,
                                                  static_cast<DWORD>(std::size(names)), path.c_str());
    auto check = [&](const std::wstring& word, const std::wstring& key) {
        if (memory_key(word) == key) {
            g_completion_memory[word] = static_cast<int>(GetPrivateProfileIntW(L"CompletionMemory", key.c_str(), 0, path.c_str()));
        }
    };
    for (DWORD i = 0; i < length;) {
        std::wstring key = names + i;
        i += static_cast<DWORD>(key.size()) + 1;
        for (const auto& word : cpp_keywords()) check(word, key);
        for (const auto& word : cpp_primitives()) check(word, key);
        for (const auto& word : cpp_types()) check(word, key);
        for (const auto& word : omg_keywords()) check(word, key);
        for (const auto& word : omg_types()) check(word, key);
        for (const auto& word : omg_builtins()) check(word, key);
        for (const auto& word : {L"std", L"cout", L"cin", L"endl", L"iostream", L"include"}) check(word, key);
    }
}

void remember_completion(const std::wstring& value) {
    int& score = g_completion_memory[value];
    score = std::min(score + 1, 1000000);
    WritePrivateProfileStringW(L"CompletionMemory", memory_key(value).c_str(),
                               std::to_wstring(score).c_str(), settings_path().c_str());
}

void load_settings() {
    const std::wstring path = settings_path();
    g_theme_index = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Appearance", L"Theme", 0, path.c_str())), 0, 2);
    g_code_size = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Appearance", L"FontSize", 18, path.c_str())), 12, 32);
    wchar_t font_name[LF_FACESIZE]{};
    GetPrivateProfileStringW(L"Appearance", L"CodeFont", L"Google Sans Code", font_name,
                             static_cast<DWORD>(std::size(font_name)), path.c_str());
    g_code_font_name = font_name[0] ? font_name : L"Google Sans Code";
    g_indent_tabs = GetPrivateProfileIntW(L"Editor", L"IndentTabs", 0, path.c_str()) != 0;
    g_indent_spaces = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Editor", L"IndentSpaces", 2, path.c_str())), 1, 12);
    wchar_t last_file[32768]{};
    GetPrivateProfileStringW(L"Session", L"LastFile", L"", last_file, static_cast<DWORD>(std::size(last_file)), path.c_str());
    g_last_file = last_file;
    load_completion_memory();
}

void save_settings() {
    const std::wstring path = settings_path();
    WritePrivateProfileStringW(L"Appearance", L"Theme", std::to_wstring(g_theme_index).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Appearance", L"FontSize", std::to_wstring(g_code_size).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Appearance", L"CodeFont", g_code_font_name.c_str(), path.c_str());
    WritePrivateProfileStringW(L"Editor", L"IndentTabs", g_indent_tabs ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Editor", L"IndentSpaces", std::to_wstring(g_indent_spaces).c_str(), path.c_str());
    std::error_code error;
    const bool can_restore = !g_file.empty() && std::filesystem::is_regular_file(g_file, error);
    WritePrivateProfileStringW(L"Session", L"LastFile", can_restore ? g_file.c_str() : L"", path.c_str());
}

void set_status(const std::wstring& text) {
    if (g_status) SetWindowTextW(g_status, text.c_str());
}

void update_title() {
    std::wstring name = g_file.empty() ? L"Untitled.omg" : g_file.filename().wstring();
    SetWindowTextW(g_window, (name + (g_dirty ? L" *" : L"") + L" - OMG IDE").c_str());
    if (g_file_label) SetWindowTextW(g_file_label, (name + (g_dirty ? L"  *" : L"")).c_str());
}

void set_dirty(bool dirty) {
    g_dirty = dirty;
    update_title();
}

std::string editor_text_utf8() {
    const auto length = static_cast<Sci_Position>(sci(SCI_GETTEXTLENGTH));
    std::string value(static_cast<size_t>(length) + 1, '\0');
    sci(SCI_GETTEXT, value.size(), reinterpret_cast<sptr_t>(value.data()));
    value.resize(static_cast<size_t>(length));
    return value;
}

std::wstring editor_text() {
    return widen_utf8(editor_text_utf8());
}

void set_editor_text(const std::wstring& text) {
    std::string utf8 = narrow_utf8(text);
    sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(utf8.c_str()));
}

COLORREF syntax_color(int style) {
    const Theme& t = theme();
    switch (style) {
    case SCE_C_WORD: return t.purple;
    case SCE_C_WORD2: return t.yellow;
    case SCE_C_GLOBALCLASS: return t.yellow;
    case SCE_C_PREPROCESSOR: return g_theme_index == 2 ? t.purple : (g_theme_index == 1 ? t.purple : t.yellow);
    case SCE_C_STRING:
    case SCE_C_CHARACTER:
    case SCE_C_STRINGRAW:
    case SCE_C_USERLITERAL: return t.green;
    case SCE_C_ESCAPESEQUENCE: return t.pink;
    case SCE_C_COMMENT:
    case SCE_C_COMMENTLINE:
    case SCE_C_COMMENTDOC:
    case SCE_C_COMMENTLINEDOC:
    case SCE_C_PREPROCESSORCOMMENT: return t.comment;
    case SCE_C_NUMBER: return g_theme_index == 1 ? t.purple : t.orange;
    case SCE_C_OPERATOR: return g_theme_index == 0 ? t.sky : (g_theme_index == 1 ? t.orange : t.text);
    case SCE_C_STRINGEOL: return t.red;
    default: return t.text;
    }
}

void style_set(int style, COLORREF fore, bool bold = false, bool italic = false) {
    sci(SCI_STYLESETFORE, style, fore);
    sci(SCI_STYLESETBACK, style, theme().editor);
    sci(SCI_STYLESETBOLD, style, bold);
    sci(SCI_STYLESETITALIC, style, italic);
}

void apply_syntax_overlays();
void recreate_completion_fonts();
void hide_completion();
COLORREF mix_colour(COLORREF base, COLORREF accent, int accent_weight);

void update_caret_line_visibility() {
    if (!g_editor) return;
    const Sci_Position start = static_cast<Sci_Position>(sci(SCI_GETSELECTIONSTART));
    const Sci_Position end = static_cast<Sci_Position>(sci(SCI_GETSELECTIONEND));
    // Match VS Code's behavior: an active text selection is its own visual
    // focus, so do not paint the current-line highlight underneath it.
    sci(SCI_SETCARETLINEVISIBLE, start == end ? TRUE : FALSE);
}

void configure_editor_theme() {
    if (!g_editor) return;
    const std::string font = narrow_utf8(g_code_font_name);
    sci(SCI_SETCODEPAGE, SC_CP_UTF8);
    sci(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(font.c_str()));
    sci(SCI_STYLESETSIZE, STYLE_DEFAULT, g_code_size);
    sci(SCI_STYLESETFORE, STYLE_DEFAULT, theme().text);
    sci(SCI_STYLESETBACK, STYLE_DEFAULT, theme().editor);
    sci(SCI_STYLECLEARALL);

    for (int style = 0; style <= SCE_C_ESCAPESEQUENCE; ++style) style_set(style, syntax_color(style));
    style_set(SCE_C_WORD, syntax_color(SCE_C_WORD));
    style_set(SCE_C_WORD2, syntax_color(SCE_C_WORD2), false, true);
    style_set(SCE_C_COMMENT, syntax_color(SCE_C_COMMENT), false, g_theme_index == 0);
    style_set(SCE_C_COMMENTLINE, syntax_color(SCE_C_COMMENTLINE), false, g_theme_index == 0);
    style_set(SCE_OMG_BUILTIN, theme().blue);

    sci(SCI_STYLESETFORE, STYLE_LINENUMBER, theme().muted);
    sci(SCI_STYLESETBACK, STYLE_LINENUMBER, theme().panel);
    sci(SCI_STYLESETFONT, STYLE_LINENUMBER, reinterpret_cast<sptr_t>(font.c_str()));
    sci(SCI_STYLESETSIZE, STYLE_LINENUMBER, g_code_size);
    sci(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    sci(SCI_SETMARGINWIDTHN, 0, std::max(54, g_code_size * 4));

    sci(SCI_SETCARETFORE, theme().rosewater);
    sci(SCI_SETCARETLINEVISIBLE, TRUE);
    sci(SCI_SETCARETLINELAYER, SC_LAYER_BASE);
    sci(SCI_SETCARETLINEBACKALPHA, SC_ALPHA_NOALPHA);
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, opaque_element_colour(theme().line));
    // Keep the classic message as a fallback for the bundled Scintilla
    // build, which may not expose the caret-line element API.
    sci(SCI_SETCARETLINEBACK, theme().line);

    sci(SCI_SETSELECTIONLAYER, SC_LAYER_BASE);
    sci(SCI_SETSELFORE, FALSE, 0);
    sci(SCI_SETSELALPHA, SC_ALPHA_NOALPHA);
    sci(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_SELECTION_TEXT);
    sci(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_SELECTION_ADDITIONAL_TEXT);
    sci(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_SELECTION_SECONDARY_TEXT);
    sci(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_TEXT);
    sci(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_TEXT);
    // Use the editor's neutral gray for selections.  The caret-line highlight
    // is hidden while a selection exists, so this gray becomes the only
    // background under selected text (never a yellow/accent color).
    const sptr_t selection_back = opaque_element_colour(theme().line);
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_BACK, selection_back);
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_ADDITIONAL_BACK, selection_back);
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_SECONDARY_BACK, selection_back);
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_BACK, selection_back);
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_BACK, selection_back);
    // Classic selection colors are required by older Scintilla builds and
    // use the Windows COLORREF format directly.
    sci(SCI_SETSELBACK, TRUE, selection_back);
    sci(SCI_SETWHITESPACEFORE, TRUE, theme().muted);
    sci(SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
    sci(SCI_SETMULTIPLESELECTION, TRUE);

    sci(SCI_SETTABWIDTH, 4);
    sci(SCI_SETUSETABS, g_indent_tabs);
    sci(SCI_SETINDENT, g_indent_tabs ? 4 : g_indent_spaces);
    sci(SCI_SETTABINDENTS, TRUE);
    sci(SCI_SETBACKSPACEUNINDENTS, TRUE);
    sci(SCI_SETEOLMODE, SC_EOL_CRLF);
    sci(SCI_SETIMEINTERACTION, SC_IME_INLINE);

    sci(SCI_AUTOCSETIGNORECASE, TRUE);
    sci(SCI_AUTOCSETCHOOSESINGLE, FALSE);
    sci(SCI_AUTOCSETAUTOHIDE, FALSE);
    sci(SCI_AUTOCSETDROPRESTOFWORD, TRUE);
    sci(SCI_AUTOCSETMAXHEIGHT, 10);
    sci(SCI_AUTOCSETMAXWIDTH, 60);
    sci(SCI_AUTOCSETSTYLE, STYLE_CALLTIP);

    // The installed Scintilla and Lexilla builds expose incompatible ILEXER
    // vtables. Attaching Lexilla's lexer makes the next SCI_SETKEYWORDS call
    // jump through a null virtual-function slot and crash during startup.
    // Keep the editor's built-in plain-text lexer until matching DLLs are
    // shipped together; the custom overlay highlighter remains active.
    const bool omg = is_omg_language();
    const std::string keywords = omg
        ? "var val func for in to while repeat until if else return break continue define as true false null and or not"
        : "alignas alignof asm auto bool break case catch char char8_t char16_t char32_t class const constexpr continue default delete do double else enum explicit export extern float for friend goto if inline int long namespace new noexcept operator private protected public return short signed sizeof static struct switch template this throw try typedef typename union unsigned using virtual void volatile wchar_t while override final concept requires co_await co_return co_yield";
    const std::string types = omg
        ? "digit float string char array queue pqueue pquue greater_pqueue gpqueue stack print println input input_without_display calc to_digit to_char to_string type_of size_of push pop top_of front_of"
        : "size_t string wstring vector map set unordered_map unordered_set unique_ptr shared_ptr weak_ptr optional variant tuple array";
    sci(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(keywords.c_str()));
    sci(SCI_SETKEYWORDS, 1, reinterpret_cast<sptr_t>(types.c_str()));
    sci(SCI_STYLESETFONT, STYLE_CALLTIP, reinterpret_cast<sptr_t>(font.c_str()));
    sci(SCI_STYLESETSIZE, STYLE_CALLTIP, std::max(10, g_code_size - 4));
    sci(SCI_STYLESETFORE, STYLE_CALLTIP, theme().text);
    sci(SCI_STYLESETBACK, STYLE_CALLTIP, theme().panel);
    recreate_completion_fonts();

    COLORREF bracket_colors[BRACKET_INDICATORS] = {
        theme().red, theme().orange, theme().yellow, theme().green, theme().blue, theme().sky, theme().purple
    };
    auto setup_indicator = [](int indicator, COLORREF color) {
        sci(SCI_INDICSETSTYLE, indicator, INDIC_TEXTFORE);
        sci(SCI_INDICSETFORE, indicator, color);
    };
    setup_indicator(INDIC_NAMESPACE, theme().yellow);
    setup_indicator(INDIC_HEADER, theme().green);
    setup_indicator(INDIC_FUNCTION, theme().blue);
    setup_indicator(INDIC_MACRO, theme().blue);
    setup_indicator(INDIC_ESCAPE, theme().pink);
    for (int i = 0; i < BRACKET_INDICATORS; ++i) setup_indicator(INDIC_BRACKET_0 + i, bracket_colors[i]);

    sci(SCI_COLOURISE, 0, -1);
    apply_syntax_overlays();
    update_caret_line_visibility();
}

bool ident_byte(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

bool style_is_string(int style) {
    return style == SCE_C_STRING || style == SCE_C_CHARACTER || style == SCE_C_STRINGRAW ||
           style == SCE_C_USERLITERAL || style == SCE_C_STRINGEOL;
}

bool style_is_code(int style) {
    return style == SCE_C_DEFAULT || style == SCE_C_OPERATOR || style == SCE_C_IDENTIFIER ||
           style == SCE_C_WORD || style == SCE_C_WORD2 || style == SCE_C_GLOBALCLASS;
}

void fill_indicator(int indicator, Sci_Position start, Sci_Position length) {
    if (length <= 0) return;
    sci(SCI_SETINDICATORCURRENT, indicator);
    sci(SCI_INDICATORFILLRANGE, start, length);
}

void clear_overlay_indicators(Sci_Position length) {
    sci(SCI_SETINDICATORCURRENT, INDIC_NAMESPACE);
    sci(SCI_INDICATORCLEARRANGE, 0, length);
    for (int indicator = INDIC_HEADER; indicator < INDIC_BRACKET_0 + BRACKET_INDICATORS; ++indicator) {
        sci(SCI_SETINDICATORCURRENT, indicator);
        sci(SCI_INDICATORCLEARRANGE, 0, length);
    }
}

void apply_omg_lexing(const std::string& text) {
    if (!is_omg_language()) return;

    const Sci_Position length = static_cast<Sci_Position>(text.size());
    sci(SCI_STARTSTYLING, 0, 0x1F);
    sci(SCI_SETSTYLING, length, SCE_C_DEFAULT);
    auto style_range = [](size_t start, size_t end, int style) {
        if (end <= start) return;
        sci(SCI_STARTSTYLING, static_cast<Sci_Position>(start), 0x1F);
        sci(SCI_SETSTYLING, static_cast<Sci_Position>(end - start), style);
    };

    for (size_t i = 0; i < text.size();) {
        const char ch = text[i];
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            const size_t start = i;
            i = text.find('\n', i + 2);
            if (i == std::string::npos) i = text.size();
            style_range(start, i, SCE_C_COMMENTLINE);
            continue;
        }
        if (ch == '"' || ch == '\'') {
            const size_t start = i;
            const char quote = ch;
            ++i;
            while (i < text.size() && text[i] != quote && text[i] != '\n' && text[i] != '\r') {
                if (text[i] == '\\' && i + 1 < text.size()) i += 2;
                else ++i;
            }
            if (i < text.size() && text[i] == quote) ++i;
            style_range(start, i, quote == '"' ? SCE_C_STRING : SCE_C_CHARACTER);
            for (size_t escape = start; escape + 1 < i; ++escape) {
                if (text[escape] == '\\') {
                    style_range(escape, escape + 2, SCE_C_ESCAPESEQUENCE);
                    ++escape;
                }
            }
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) ||
            (ch == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            const size_t start = i++;
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) ++i;
            style_range(start, i, SCE_C_NUMBER);
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            const size_t start = i++;
            while (i < text.size() && ident_byte(text[i])) ++i;
            const std::wstring word = widen_utf8(text.substr(start, i - start));
            if (omg_keywords().count(word)) style_range(start, i, SCE_C_WORD);
            else if (omg_types().count(word)) style_range(start, i, SCE_C_WORD2);
            else if (omg_builtins().count(word)) style_range(start, i, SCE_OMG_BUILTIN);
            else style_range(start, i, SCE_C_IDENTIFIER);
            continue;
        }
        if (std::string("+-*/%=!<>|&:,.").find(ch) != std::string::npos) {
            style_range(i, i + 1, SCE_C_OPERATOR);
        }
        ++i;
    }
}

void color_preprocessor_overlays(const std::string& text) {
    for (size_t line_start = 0; line_start < text.size();) {
        size_t line_end = text.find('\n', line_start);
        if (line_end == std::string::npos) line_end = text.size();
        size_t first = line_start;
        while (first < line_end && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
        if (first < line_end && text[first] == '#') {
            size_t word_start = first + 1;
            while (word_start < line_end && std::isspace(static_cast<unsigned char>(text[word_start]))) ++word_start;
            size_t word_end = word_start;
            while (word_end < line_end && ident_byte(text[word_end])) ++word_end;
            std::string directive = text.substr(word_start, word_end - word_start);
            if (directive == "include") {
                size_t open = text.find_first_of("<\"", word_end);
                if (open != std::string::npos && open < line_end) {
                    char close_char = text[open] == '<' ? '>' : '"';
                    size_t close = text.find(close_char, open + 1);
                    if (close != std::string::npos && close < line_end) {
                        fill_indicator(INDIC_HEADER, static_cast<Sci_Position>(open),
                                       static_cast<Sci_Position>(close - open + 1));
                    }
                }
            } else if (directive == "define") {
                size_t macro = word_end;
                while (macro < line_end && std::isspace(static_cast<unsigned char>(text[macro]))) ++macro;
                size_t macro_end = macro;
                while (macro_end < line_end && ident_byte(text[macro_end])) ++macro_end;
                fill_indicator(INDIC_MACRO, static_cast<Sci_Position>(macro), static_cast<Sci_Position>(macro_end - macro));
            }
        }
        line_start = line_end + (line_end < text.size() ? 1 : 0);
    }
}

void color_function_and_escape_overlays(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        int style = static_cast<int>(sci(SCI_GETSTYLEAT, static_cast<Sci_Position>(i)));
        if (style_is_string(style) && text[i] == '\\' && i + 1 < text.size()) {
            fill_indicator(INDIC_ESCAPE, static_cast<Sci_Position>(i), 2);
            i += 2;
            continue;
        }
        if (style_is_code(style) && (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_')) {
            size_t start = i++;
            while (i < text.size() && ident_byte(text[i])) ++i;
            std::wstring word = widen_utf8(text.substr(start, i - start));
            size_t next = i;
            while (next < text.size() && (text[next] == ' ' || text[next] == '\t')) ++next;
            if (!is_omg_language() && word == L"std") {
                fill_indicator(INDIC_NAMESPACE, static_cast<Sci_Position>(start), static_cast<Sci_Position>(i - start));
                continue;
            }
            bool keyword = is_omg_language()
                ? (omg_keywords().count(word) || omg_types().count(word) || omg_builtins().count(word))
                : (cpp_keywords().count(word) || cpp_primitives().count(word) || cpp_types().count(word));
            bool after_scope = start >= 2 && text[start - 1] == ':' && text[start - 2] == ':';
            if (!keyword && !after_scope && next < text.size() && text[next] == '(') {
                fill_indicator(INDIC_FUNCTION, static_cast<Sci_Position>(start), static_cast<Sci_Position>(i - start));
            }
            continue;
        }
        ++i;
    }
}

void color_bracket_overlays(const std::string& text) {
    std::vector<std::pair<char, size_t>> stack;
    auto match_open = [](char close) { return close == ')' ? '(' : (close == '}' ? '{' : '['); };
    for (size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (ch != '(' && ch != ')' && ch != '{' && ch != '}' && ch != '[' && ch != ']') continue;
        if (!style_is_code(static_cast<int>(sci(SCI_GETSTYLEAT, static_cast<Sci_Position>(i))))) continue;
        if (ch == '(' || ch == '{' || ch == '[') {
            int color = static_cast<int>(stack.size() % BRACKET_INDICATORS);
            fill_indicator(INDIC_BRACKET_0 + color, static_cast<Sci_Position>(i), 1);
            stack.push_back({ch, i});
        } else {
            char open = match_open(ch);
            int color = static_cast<int>(stack.empty() ? 0 : ((stack.size() - 1) % BRACKET_INDICATORS));
            for (size_t s = stack.size(); s > 0; --s) {
                if (stack[s - 1].first == open) {
                    color = static_cast<int>((s - 1) % BRACKET_INDICATORS);
                    stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(s - 1), stack.end());
                    break;
                }
            }
            fill_indicator(INDIC_BRACKET_0 + color, static_cast<Sci_Position>(i), 1);
        }
    }
}

void apply_syntax_overlays() {
    if (!g_editor) return;
    Sci_Position length = static_cast<Sci_Position>(sci(SCI_GETTEXTLENGTH));
    clear_overlay_indicators(length);
    if (length <= 0) return;
    sci(SCI_COLOURISE, 0, -1);
    std::string text = editor_text_utf8();
    apply_omg_lexing(text);
    color_preprocessor_overlays(text);
    color_function_and_escape_overlays(text);
    color_bracket_overlays(text);
}

void update_brace_match() {
    Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    Sci_Position brace = -1;
    if (pos > 0) {
        int ch = static_cast<int>(sci(SCI_GETCHARAT, pos - 1));
        if (std::string("(){}[]").find(static_cast<char>(ch)) != std::string::npos) brace = pos - 1;
    }
    if (brace < 0) {
        int ch = static_cast<int>(sci(SCI_GETCHARAT, pos));
        if (std::string("(){}[]").find(static_cast<char>(ch)) != std::string::npos) brace = pos;
    }
    if (brace >= 0) {
        Sci_Position match = static_cast<Sci_Position>(sci(SCI_BRACEMATCH, brace, 0));
        if (match >= 0) sci(SCI_BRACEHIGHLIGHT, brace, match);
        else sci(SCI_BRACEBADLIGHT, brace);
    } else {
        sci(SCI_BRACEHIGHLIGHT, static_cast<uptr_t>(-1), static_cast<sptr_t>(-1));
    }
}

bool identifier_char(wchar_t c) {
    return iswalnum(c) || c == L'_';
}

struct CompletionContext {
    std::wstring query;
    bool in_function_body = false;
    bool preprocessor_line = false;
    bool suppressed = false;
};

std::wstring previous_identifier_before(const std::wstring& text, size_t cursor) {
    while (cursor > 0 && iswspace(text[cursor - 1])) --cursor;
    size_t end = cursor;
    while (cursor > 0 && identifier_char(text[cursor - 1])) --cursor;
    return end > cursor ? text.substr(cursor, end - cursor) : L"";
}

size_t find_matching_open_paren(const std::wstring& text, size_t close_pos) {
    int depth = 0;
    for (size_t i = close_pos + 1; i > 0; --i) {
        const size_t pos = i - 1;
        if (text[pos] == L')') {
            ++depth;
        } else if (text[pos] == L'(') {
            if (depth == 0) return pos;
            --depth;
        }
    }
    return std::wstring::npos;
}

bool brace_opens_function_body(const std::wstring& text, size_t brace_pos) {
    size_t cursor = brace_pos;
    while (cursor > 0 && iswspace(text[cursor - 1])) --cursor;
    if (cursor == 0 || text[cursor - 1] != L')') return false;
    const size_t close_paren = cursor - 1;
    const size_t open_paren = find_matching_open_paren(text, close_paren);
    if (open_paren == std::wstring::npos) return false;
    const std::wstring before = previous_identifier_before(text, open_paren);
    static const std::unordered_set<std::wstring> control_words = {
        L"if", L"for", L"while", L"switch", L"catch"
    };
    return !before.empty() && !control_words.count(before);
}

bool completion_is_suppressed(const std::wstring& text, size_t caret) {
    enum class Region { Code, LineComment, BlockComment, String };
    Region region = Region::Code;
    wchar_t quote = L'\0';
    for (size_t i = 0; i < caret && i < text.size(); ++i) {
        const wchar_t current = text[i];
        if (region == Region::LineComment) {
            if (current == L'\n') region = Region::Code;
            continue;
        }
        if (region == Region::BlockComment) {
            if (current == L'*' && i + 1 < caret && text[i + 1] == L'/') {
                region = Region::Code;
                ++i;
            }
            continue;
        }
        if (region == Region::String) {
            if (current == L'\\' && i + 1 < caret) {
                ++i;
            } else if (current == quote) {
                region = Region::Code;
            }
            continue;
        }
        if (current == L'/' && i + 1 < caret) {
            if (text[i + 1] == L'/') {
                region = Region::LineComment;
                ++i;
                continue;
            }
            if (text[i + 1] == L'*') {
                region = Region::BlockComment;
                ++i;
                continue;
            }
        }
        if (current == L'\"' || current == L'\'' || current == L'`') {
            quote = current;
            region = Region::String;
        }
    }
    return region != Region::Code;
}

CompletionContext current_completion_context() {
    Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    std::wstring text = editor_text();
    size_t char_pos = static_cast<size_t>(std::clamp<Sci_Position>(pos, 0, static_cast<Sci_Position>(text.size())));

    size_t start = char_pos;
    while (start > 0 && identifier_char(text[start - 1])) --start;

    CompletionContext context;
    context.query = text.substr(start, char_pos - start);
    context.suppressed = completion_is_suppressed(text, char_pos);

    size_t line_start = text.rfind(L'\n', char_pos);
    line_start = line_start == std::wstring::npos ? 0 : line_start + 1;
    size_t first = line_start;
    while (first < char_pos && iswspace(text[first])) ++first;
    context.preprocessor_line = first < text.size() && first < char_pos && text[first] == L'#';

    std::vector<bool> function_stack;
    bool in_function = false;
    for (size_t i = 0; i < char_pos && i < text.size(); ++i) {
        if (text[i] == L'{') {
            const bool opens_function = brace_opens_function_body(text, i);
            function_stack.push_back(in_function || opens_function);
            if (opens_function) in_function = true;
        } else if (text[i] == L'}') {
            if (!function_stack.empty()) function_stack.pop_back();
            in_function = !function_stack.empty() && function_stack.back();
        }
    }
    context.in_function_body = in_function;
    return context;
}

bool fuzzy_match(const std::wstring& query, const std::wstring& candidate) {
    if (query.empty()) return false;
    size_t q = 0;
    for (wchar_t ch : candidate) {
        if (towlower(ch) == towlower(query[q]) && ++q == query.size()) return true;
    }
    return false;
}

bool fuzzy_match_positions(const std::wstring& query, const std::wstring& candidate,
                           std::vector<size_t>* positions = nullptr) {
    if (query.empty()) return false;
    size_t q = 0;
    std::vector<size_t> local_positions;
    for (size_t i = 0; i < candidate.size() && q < query.size(); ++i) {
        if (towlower(candidate[i]) == towlower(query[q])) {
            local_positions.push_back(i);
            ++q;
        }
    }
    if (q != query.size()) return false;
    if (positions) *positions = std::move(local_positions);
    return true;
}

struct CompletionMatchQuality {
    bool contiguous_prefix = false;
    int first_index = 1000000;
    int span = 1000000;
    int gap_total = 1000000;
};

CompletionMatchQuality completion_match_quality(const std::wstring& query, const std::wstring& candidate) {
    CompletionMatchQuality quality;
    quality.contiguous_prefix = true;
    if (query.size() > candidate.size()) {
        quality.contiguous_prefix = false;
    } else {
        for (size_t i = 0; i < query.size(); ++i) {
            if (towlower(candidate[i]) != towlower(query[i])) {
                quality.contiguous_prefix = false;
                break;
            }
        }
    }
    std::vector<size_t> positions;
    if (!fuzzy_match_positions(query, candidate, &positions) || positions.empty()) return quality;
    quality.first_index = static_cast<int>(positions.front());
    quality.span = static_cast<int>(positions.back() - positions.front());
    quality.gap_total = 0;
    for (size_t i = 1; i < positions.size(); ++i) {
        quality.gap_total += static_cast<int>(positions[i] - positions[i - 1] - 1);
    }
    return quality;
}

bool better_completion_match(const CompletionMatchQuality& a, const CompletionMatchQuality& b) {
    if (a.contiguous_prefix != b.contiguous_prefix) return a.contiguous_prefix;
    if (a.first_index != b.first_index) return a.first_index < b.first_index;
    if (a.span != b.span) return a.span < b.span;
    if (a.gap_total != b.gap_total) return a.gap_total < b.gap_total;
    return false;
}

std::wstring previous_identifier(const std::wstring& text, size_t start) {
    size_t cursor = start;
    while (cursor > 0 && iswspace(text[cursor - 1])) --cursor;
    size_t end = cursor;
    while (cursor > 0 && identifier_char(text[cursor - 1])) --cursor;
    return end > cursor ? text.substr(cursor, end - cursor) : L"";
}

bool line_has_directive_before(const std::wstring& text, size_t start, const wchar_t* directive) {
    size_t line_start = text.rfind(L'\n', start);
    line_start = line_start == std::wstring::npos ? 0 : line_start + 1;
    while (line_start < start && iswspace(text[line_start])) ++line_start;
    if (line_start >= start || text[line_start] != L'#') return false;
    ++line_start;
    while (line_start < start && iswspace(text[line_start])) ++line_start;
    const size_t length = wcslen(directive);
    return line_start + length <= start &&
           _wcsnicmp(text.c_str() + line_start, directive, length) == 0;
}

CompletionKind infer_completion_kind(const std::wstring& text, size_t start, size_t end) {
    size_t next = end;
    while (next < text.size() && iswspace(text[next])) ++next;
    if (next < text.size() && text[next] == L'(') return CompletionKind::Function;
    if (line_has_directive_before(text, start, L"define")) return CompletionKind::Preprocessor;
    const std::wstring prev = previous_identifier(text, start);
    if (is_omg_language()) {
        if (_wcsicmp(prev.c_str(), L"func") == 0) return CompletionKind::Function;
        if (_wcsicmp(prev.c_str(), L"var") == 0 || _wcsicmp(prev.c_str(), L"val") == 0 ||
            _wcsicmp(prev.c_str(), L"for") == 0) return CompletionKind::Variable;
        return CompletionKind::Variable;
    }
    if (_wcsicmp(prev.c_str(), L"namespace") == 0) return CompletionKind::Namespace;
    if (_wcsicmp(prev.c_str(), L"class") == 0 || _wcsicmp(prev.c_str(), L"struct") == 0 ||
        _wcsicmp(prev.c_str(), L"enum") == 0 || _wcsicmp(prev.c_str(), L"typedef") == 0 ||
        _wcsicmp(prev.c_str(), L"using") == 0 || _wcsicmp(prev.c_str(), L"typename") == 0) {
        return CompletionKind::Type;
    }
    return CompletionKind::Variable;
}

std::vector<CompletionItem> completion_items(const CompletionContext& context) {
    const std::wstring& query = context.query;
    std::vector<CompletionItem> items;
    std::set<std::wstring> seen;
    auto add = [&](const std::wstring& item, CompletionKind kind) {
        if (!seen.count(item) && fuzzy_match(query, item)) {
            seen.insert(item);
            items.push_back({item, kind});
        }
    };
    if (is_omg_language()) {
        for (const auto& word : omg_keywords()) add(word, CompletionKind::Keyword);
        for (const auto& word : omg_types()) add(word, CompletionKind::Type);
        for (const auto& word : omg_builtins()) add(word, CompletionKind::Function);
        add(L"pquue", CompletionKind::Type);
        add(L"greater_pqueue", CompletionKind::Type);
        add(L"gpqueue", CompletionKind::Type);
    } else {
        for (const auto& word : cpp_keywords()) add(word, CompletionKind::Keyword);
        for (const auto& word : cpp_primitives()) add(word, CompletionKind::Primitive);
        for (const auto& word : cpp_types()) add(word, CompletionKind::Type);
        add(L"std", CompletionKind::Namespace);
        add(L"cout", CompletionKind::Variable);
        add(L"cin", CompletionKind::Variable);
        add(L"endl", CompletionKind::Function);
        if (!context.in_function_body || context.preprocessor_line) {
            add(L"iostream", CompletionKind::Header);
            add(L"include", CompletionKind::Preprocessor);
        }
    }

    std::wstring text = editor_text();
    for (size_t i = 0; i < text.size();) {
        if (iswalpha(text[i]) || text[i] == L'_') {
            size_t start = i++;
            while (i < text.size() && identifier_char(text[i])) ++i;
            std::wstring candidate = text.substr(start, i - start);
            if (candidate != query) add(candidate, infer_completion_kind(text, start, i));
        } else {
            ++i;
        }
    }
    std::stable_sort(items.begin(), items.end(), [&](const CompletionItem& a, const CompletionItem& b) {
        int as = g_completion_memory.count(a.text) ? g_completion_memory[a.text] : 0;
        int bs = g_completion_memory.count(b.text) ? g_completion_memory[b.text] : 0;
        if (std::abs(as - bs) >= 10) return as > bs;
        CompletionMatchQuality aq = completion_match_quality(query, a.text);
        CompletionMatchQuality bq = completion_match_quality(query, b.text);
        if (better_completion_match(aq, bq)) return true;
        if (better_completion_match(bq, aq)) return false;
        if (as != bs) return as > bs;
        if (a.text.size() != b.text.size()) return a.text.size() < b.text.size();
        return a.text < b.text;
    });
    if (items.size() > 20) items.resize(20);
    return items;
}

COLORREF completion_colour(CompletionKind kind) {
    switch (kind) {
    case CompletionKind::Keyword:
    case CompletionKind::Primitive: return syntax_color(SCE_C_WORD);
    case CompletionKind::Type: return syntax_color(SCE_C_WORD2);
    case CompletionKind::Function: return theme().blue;
    case CompletionKind::Namespace: return theme().yellow;
    case CompletionKind::Preprocessor: return syntax_color(SCE_C_PREPROCESSOR);
    case CompletionKind::Header: return theme().green;
    case CompletionKind::Variable: return theme().text;
    }
    return theme().text;
}

const wchar_t* completion_kind_label(CompletionKind kind) {
    switch (kind) {
    case CompletionKind::Keyword:
    case CompletionKind::Primitive: return L"Keyword";
    case CompletionKind::Type: return L"Type";
    case CompletionKind::Function: return L"Function";
    case CompletionKind::Variable: return L"Variable";
    case CompletionKind::Namespace: return L"Namespace";
    case CompletionKind::Preprocessor: return L"Directive";
    case CompletionKind::Header: return L"Header";
    }
    return L"Item";
}

COLORREF mix_colour(COLORREF base, COLORREF accent, int accent_weight) {
    accent_weight = std::clamp(accent_weight, 0, 255);
    const int base_weight = 255 - accent_weight;
    const int r = (GetRValue(base) * base_weight + GetRValue(accent) * accent_weight) / 255;
    const int g = (GetGValue(base) * base_weight + GetGValue(accent) * accent_weight) / 255;
    const int b = (GetBValue(base) * base_weight + GetBValue(accent) * accent_weight) / 255;
    return RGB(r, g, b);
}

Gdiplus::Color gdip_colour(COLORREF colour, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(colour), GetGValue(colour), GetBValue(colour));
}

void add_rounded_rect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    radius = std::min(radius, std::min(rect.Width, rect.Height) / 2.0f);
    const float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void draw_completion_icon(HDC dc, const RECT& icon_rect, CompletionKind kind, COLORREF accent) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    const Gdiplus::RectF bounds(static_cast<Gdiplus::REAL>(icon_rect.left),
                                static_cast<Gdiplus::REAL>(icon_rect.top),
                                static_cast<Gdiplus::REAL>(icon_rect.right - icon_rect.left),
                                static_cast<Gdiplus::REAL>(icon_rect.bottom - icon_rect.top));
    Gdiplus::SolidBrush background(gdip_colour(mix_colour(theme().panel, accent, 52)));
    Gdiplus::GraphicsPath background_path;
    add_rounded_rect(background_path, bounds, bounds.Width * 0.3f);
    graphics.FillPath(&background, &background_path);

    const float scale_x = bounds.Width / 24.0f;
    const float scale_y = bounds.Height / 24.0f;
    auto px = [&](float x) { return bounds.X + x * scale_x; };
    auto py = [&](float y) { return bounds.Y + y * scale_y; };

    Gdiplus::Pen pen(gdip_colour(accent), std::max(1.4f, bounds.Width / 14.0f));
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush fill(gdip_colour(accent));

    const bool keyword_icon = kind == CompletionKind::Keyword || kind == CompletionKind::Primitive ||
                              kind == CompletionKind::Preprocessor;
    const bool function_icon = kind == CompletionKind::Function;
    const bool type_icon = kind == CompletionKind::Type || kind == CompletionKind::Header;

    if (keyword_icon) {
        graphics.FillEllipse(&fill, px(4.8f), py(8.8f), scale_x * 6.4f, scale_y * 6.4f);
        graphics.DrawLine(&pen, px(11.2f), py(12.0f), px(21.0f), py(12.0f));
        graphics.DrawLine(&pen, px(18.0f), py(12.0f), px(18.0f), py(15.0f));
        graphics.DrawLine(&pen, px(15.0f), py(12.0f), px(15.0f), py(14.0f));
        return;
    }

    if (function_icon) {
        graphics.DrawLine(&pen, px(9.0f), py(4.0f), px(7.8f), py(4.0f));
        graphics.DrawBezier(&pen, px(7.8f), py(4.0f), px(6.6f), py(4.0f), px(6.1f), py(4.8f), px(5.7f), py(6.0f));
        graphics.DrawLine(&pen, px(5.7f), py(6.0f), px(3.5f), py(18.0f));
        graphics.DrawBezier(&pen, px(3.5f), py(18.0f), px(3.2f), py(19.2f), px(2.6f), py(20.0f), px(1.0f), py(20.0f));
        graphics.DrawLine(&pen, px(3.8f), py(11.0f), px(10.0f), py(11.0f));
        graphics.DrawLine(&pen, px(13.5f), py(7.5f), px(20.5f), py(16.5f));
        graphics.DrawLine(&pen, px(20.5f), py(7.5f), px(13.5f), py(16.5f));
        return;
    }

    if (type_icon) {
        graphics.DrawLine(&pen, px(4.0f), py(6.0f), px(20.0f), py(6.0f));
        graphics.DrawLine(&pen, px(8.0f), py(6.0f), px(8.0f), py(20.0f));
        graphics.DrawLine(&pen, px(16.0f), py(6.0f), px(16.0f), py(20.0f));
        graphics.DrawLine(&pen, px(5.0f), py(20.0f), px(11.0f), py(20.0f));
        graphics.DrawLine(&pen, px(13.0f), py(20.0f), px(19.0f), py(20.0f));
        return;
    }

    Gdiplus::PointF points[] = {
        {px(5.0f), py(7.5f)}, {px(12.0f), py(4.0f)}, {px(19.0f), py(7.5f)},
        {px(19.0f), py(16.5f)}, {px(12.0f), py(20.0f)}, {px(5.0f), py(16.5f)}
    };
    graphics.DrawPolygon(&pen, points, 6);
    graphics.DrawLine(&pen, px(5.0f), py(7.5f), px(12.0f), py(11.0f));
    graphics.DrawLine(&pen, px(12.0f), py(11.0f), px(19.0f), py(7.5f));
    graphics.DrawLine(&pen, px(12.0f), py(11.0f), px(12.0f), py(20.0f));
}

bool completion_active() {
    return g_completion_popup && IsWindowVisible(g_completion_popup) && !g_completion_items.empty();
}

void hide_completion() {
    if (g_completion_popup) ShowWindow(g_completion_popup, SW_HIDE);
    g_completion_items.clear();
    g_completion_query.clear();
    g_completion_selected = 0;
    g_completion_first_visible = 0;
    g_completion_visible_rows = 0;
    sci(SCI_AUTOCCANCEL);
}

SIZE measure_completion_text(HWND window, const std::wstring& text, HFONT font) {
    SIZE size{};
    HDC dc = GetDC(window);
    if (!dc) return size;
    HFONT old_font = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    if (old_font) SelectObject(dc, old_font);
    ReleaseDC(window, dc);
    return size;
}

void recreate_completion_fonts() {
    hide_completion();
    if (g_completion_font) DeleteObject(g_completion_font);
    if (g_completion_bold_font) DeleteObject(g_completion_bold_font);
    if (g_completion_meta_font) DeleteObject(g_completion_meta_font);
    HWND reference = g_editor ? g_editor : g_window;
    const int dpi = window_dpi(reference);
    const int size = std::max(10, g_code_size - 4);
    const int meta_size = std::max(9, size - 3);
    const int height = -MulDiv(size, dpi, 72);
    const int meta_height = -MulDiv(meta_size, dpi, 72);
    g_completion_font = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    FIXED_PITCH | FF_MODERN, g_code_font_name.c_str());
    g_completion_bold_font = CreateFontW(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         FIXED_PITCH | FF_MODERN, g_code_font_name.c_str());
    g_completion_meta_font = CreateFontW(meta_height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         FIXED_PITCH | FF_MODERN, g_code_font_name.c_str());
    HDC dc = GetDC(reference);
    TEXTMETRICW metrics{};
    if (dc) {
        HFONT old_font = g_completion_font ? static_cast<HFONT>(SelectObject(dc, g_completion_font)) : nullptr;
        GetTextMetricsW(dc, &metrics);
        if (old_font) SelectObject(dc, old_font);
        ReleaseDC(reference, dc);
    }
    g_completion_row_height = std::max(18L, metrics.tmHeight + scale_px(reference, 4));
}

void ensure_completion_popup() {
    if (g_completion_popup) return;
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_window, GWLP_HINSTANCE));
    g_completion_popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                         L"OMGCompletionPopup", L"", WS_POPUP,
                                         0, 0, 0, 0, g_window, nullptr, instance, nullptr);
}

void position_completion_popup() {
    if (!completion_active()) return;
    const int dpi = window_dpi(g_editor);
    auto dp = [dpi](int value) { return MulDiv(value, dpi, 96); };
    int width = dp(260);
    for (const CompletionItem& item : g_completion_items) {
        const int icon_width = dp(28);
        const int label_width = static_cast<int>(measure_completion_text(
            g_editor, completion_kind_label(item.kind), g_completion_meta_font).cx);
        const int name_width = static_cast<int>(measure_completion_text(g_editor, item.text, g_completion_bold_font).cx);
        width = std::max(width, icon_width + name_width + label_width + dp(44));
    }
    RECT editor_area{};
    GetClientRect(g_editor, &editor_area);
    width = std::min(width, std::max(dp(260), static_cast<int>(editor_area.right) * 3 / 4));
    const int height = g_completion_visible_rows * g_completion_row_height + 2;

    const Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    const Sci_Position line = static_cast<Sci_Position>(sci(SCI_LINEFROMPOSITION, pos));
    POINT caret{
        static_cast<LONG>(sci(SCI_POINTXFROMPOSITION, 0, pos)),
        static_cast<LONG>(sci(SCI_POINTYFROMPOSITION, 0, pos))
    };
    POINT below = caret;
    below.y += static_cast<LONG>(sci(SCI_TEXTHEIGHT, line));
    ClientToScreen(g_editor, &caret);
    ClientToScreen(g_editor, &below);

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(below, MONITOR_DEFAULTTONEAREST), &monitor);
    const int work_left = static_cast<int>(monitor.rcWork.left);
    const int work_top = static_cast<int>(monitor.rcWork.top);
    const int work_right = static_cast<int>(monitor.rcWork.right);
    const int work_bottom = static_cast<int>(monitor.rcWork.bottom);
    int x = std::clamp(static_cast<int>(below.x), work_left, std::max(work_left, work_right - width));
    int y = below.y;
    if (y + height > work_bottom) y = caret.y - height;
    y = std::clamp(y, work_top, std::max(work_top, work_bottom - height));
    SetWindowPos(g_completion_popup, HWND_TOP, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void show_autocomplete() {
    CompletionContext context = current_completion_context();
    if (context.suppressed || context.query.empty()) {
        hide_completion();
        return;
    }
    std::wstring previous;
    if (completion_active() && g_completion_selected >= 0 &&
        g_completion_selected < static_cast<int>(g_completion_items.size())) {
        previous = g_completion_items[static_cast<size_t>(g_completion_selected)].text;
    }
    std::vector<CompletionItem> items = completion_items(context);
    if (items.empty()) {
        hide_completion();
        return;
    }
    g_completion_items = std::move(items);
    g_completion_query = context.query;
    const Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    g_completion_start = pos - static_cast<Sci_Position>(narrow_utf8(context.query).size());
    g_completion_selected = 0;
    if (!previous.empty()) {
        for (size_t i = 0; i < g_completion_items.size(); ++i) {
            if (g_completion_items[i].text == previous) {
                g_completion_selected = static_cast<int>(i);
                break;
            }
        }
    }
    g_completion_first_visible = std::clamp(g_completion_first_visible, 0,
                                            std::max(0, static_cast<int>(g_completion_items.size()) - 1));
    g_completion_visible_rows = std::min(8, static_cast<int>(g_completion_items.size()));
    if (g_completion_selected < g_completion_first_visible) g_completion_first_visible = g_completion_selected;
    if (g_completion_selected >= g_completion_first_visible + g_completion_visible_rows) {
        g_completion_first_visible = g_completion_selected - g_completion_visible_rows + 1;
    }
    sci(SCI_AUTOCCANCEL);
    ensure_completion_popup();
    if (!g_completion_popup) return;
    ShowWindow(g_completion_popup, SW_SHOWNOACTIVATE);
    position_completion_popup();
    InvalidateRect(g_completion_popup, nullptr, FALSE);
}

void move_completion_selection(int delta) {
    if (!completion_active()) return;
    const int count = static_cast<int>(g_completion_items.size());
    g_completion_selected = (g_completion_selected + delta % count + count) % count;
    if (g_completion_selected < g_completion_first_visible) g_completion_first_visible = g_completion_selected;
    if (g_completion_selected >= g_completion_first_visible + g_completion_visible_rows) {
        g_completion_first_visible = g_completion_selected - g_completion_visible_rows + 1;
    }
    InvalidateRect(g_completion_popup, nullptr, FALSE);
}

void accept_completion() {
    if (!completion_active() || g_completion_selected < 0 ||
        g_completion_selected >= static_cast<int>(g_completion_items.size())) return;
    CompletionItem chosen = g_completion_items[static_cast<size_t>(g_completion_selected)];
    const Sci_Position end = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    const Sci_Position start = std::clamp(g_completion_start, static_cast<Sci_Position>(0), end);
    std::string replacement = narrow_utf8(chosen.text);
    hide_completion();
    sci(SCI_BEGINUNDOACTION);
    sci(SCI_SETSEL, start, end);
    sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(replacement.c_str()));
    sci(SCI_GOTOPOS, start + static_cast<Sci_Position>(replacement.size()));
    sci(SCI_ENDUNDOACTION);
    remember_completion(chosen.text);
    apply_syntax_overlays();
}

void draw_completion_item(HDC dc, const CompletionItem& item, const RECT& row, bool selected) {
    HBRUSH background = CreateSolidBrush(selected ? mix_colour(theme().editor, theme().line, 160) : theme().panel);
    FillRect(dc, &row, background);
    DeleteObject(background);

    std::vector<bool> matched(item.text.size(), false);
    size_t query_index = 0;
    for (size_t i = 0; i < item.text.size() && query_index < g_completion_query.size(); ++i) {
        if (towlower(item.text[i]) == towlower(g_completion_query[query_index])) {
            matched[i] = true;
            ++query_index;
        }
    }

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, row.left, row.top, row.right, row.bottom);
    SetBkMode(dc, TRANSPARENT);
    const int icon_size = scale_px(g_completion_popup, 18);
    const int icon_left = row.left + scale_px(g_completion_popup, 10);
    const int icon_top = row.top + std::max(0, (g_completion_row_height - icon_size) / 2);
    RECT icon_rect{icon_left, icon_top, icon_left + icon_size, icon_top + icon_size};
    const COLORREF accent = completion_colour(item.kind);
    draw_completion_icon(dc, icon_rect, item.kind, accent);

    HFONT old_font = g_completion_bold_font ? static_cast<HFONT>(SelectObject(dc, g_completion_bold_font)) : nullptr;
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);

    HFONT name_font = g_completion_font ? g_completion_font : old_font;
    if (name_font) SelectObject(dc, name_font);
    GetTextMetricsW(dc, &metrics);
    int x = icon_rect.right + scale_px(g_completion_popup, 10);
    const int y = row.top + std::max(0, (g_completion_row_height - static_cast<int>(metrics.tmHeight)) / 2);
    const int kind_right = row.right - scale_px(g_completion_popup, 10);
    const wchar_t* kind_label = completion_kind_label(item.kind);
    HFONT meta_font = g_completion_meta_font ? g_completion_meta_font : name_font;
    if (meta_font) SelectObject(dc, meta_font);
    SIZE kind_extent{};
    GetTextExtentPoint32W(dc, kind_label, static_cast<int>(wcslen(kind_label)), &kind_extent);
    const int kind_x = kind_right - static_cast<int>(kind_extent.cx);
    SetTextColor(dc, theme().muted);
    TextOutW(dc, kind_x, row.top + std::max(0, (g_completion_row_height - static_cast<int>(kind_extent.cy)) / 2),
             kind_label, static_cast<int>(wcslen(kind_label)));

    SetTextColor(dc, accent);
    for (size_t start = 0; start < item.text.size();) {
        const bool bold = matched[start];
        size_t end = start + 1;
        while (end < item.text.size() && matched[end] == bold) ++end;
        const std::wstring segment = item.text.substr(start, end - start);
        HFONT segment_font = bold ? g_completion_bold_font : g_completion_font;
        if (segment_font) SelectObject(dc, segment_font);
        if (x >= kind_x - scale_px(g_completion_popup, 8)) break;
        TextOutW(dc, x, y, segment.c_str(), static_cast<int>(segment.size()));
        SIZE extent{};
        GetTextExtentPoint32W(dc, segment.c_str(), static_cast<int>(segment.size()), &extent);
        x += extent.cx;
        start = end;
    }
    if (old_font) SelectObject(dc, old_font);
    RestoreDC(dc, saved);
}

int completion_row_at(LPARAM lparam) {
    const int y = static_cast<short>(HIWORD(lparam));
    if (y <= 0 || g_completion_row_height <= 0) return -1;
    const int row = (y - 1) / g_completion_row_height;
    if (row < 0 || row >= g_completion_visible_rows) return -1;
    const int index = g_completion_first_visible + row;
    return index < static_cast<int>(g_completion_items.size()) ? index : -1;
}

LRESULT CALLBACK completion_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        const int index = completion_row_at(lparam);
        if (index >= 0 && index != g_completion_selected) {
            g_completion_selected = index;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int index = completion_row_at(lparam);
        if (index >= 0) {
            g_completion_selected = index;
            accept_completion();
            SetFocus(g_editor);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int maximum = std::max(0, static_cast<int>(g_completion_items.size()) - g_completion_visible_rows);
        g_completion_first_visible = std::clamp(g_completion_first_visible +
                                                (GET_WHEEL_DELTA_WPARAM(wparam) < 0 ? 1 : -1), 0, maximum);
        if (g_completion_selected < g_completion_first_visible) g_completion_selected = g_completion_first_visible;
        if (g_completion_selected >= g_completion_first_visible + g_completion_visible_rows) {
            g_completion_selected = g_completion_first_visible + g_completion_visible_rows - 1;
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT area{};
        GetClientRect(window, &area);
        HBRUSH panel = CreateSolidBrush(theme().panel);
        FillRect(dc, &area, panel);
        DeleteObject(panel);
        for (int row = 0; row < g_completion_visible_rows; ++row) {
            const int index = g_completion_first_visible + row;
            if (index >= static_cast<int>(g_completion_items.size())) break;
            RECT item_rect{1, 1 + row * g_completion_row_height, area.right - 1,
                           1 + (row + 1) * g_completion_row_height};
            draw_completion_item(dc, g_completion_items[static_cast<size_t>(index)], item_rect,
                                 index == g_completion_selected);
        }
        HBRUSH border = CreateSolidBrush(theme().muted);
        FrameRect(dc, &area, border);
        DeleteObject(border);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        if (g_completion_popup == window) g_completion_popup = nullptr;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

std::wstring indent_of_current_line() {
    Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    Sci_Position line = static_cast<Sci_Position>(sci(SCI_LINEFROMPOSITION, pos));
    Sci_Position start = static_cast<Sci_Position>(sci(SCI_POSITIONFROMLINE, line));
    std::string indent;
    for (Sci_Position i = start; i < pos; ++i) {
        char ch = static_cast<char>(sci(SCI_GETCHARAT, i));
        if (ch == ' ' || ch == '\t') indent.push_back(ch);
        else break;
    }
    return widen_utf8(indent);
}

std::wstring indent_unit() {
    return g_indent_tabs ? L"\t" : std::wstring(static_cast<size_t>(g_indent_spaces), L' ');
}

bool pair_boundary() {
    Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    int prev = pos > 0 ? static_cast<int>(sci(SCI_GETCHARAT, pos - 1)) : 0;
    int next = static_cast<int>(sci(SCI_GETCHARAT, pos));
    return (prev == '(' && next == ')') || (prev == '{' && next == '}') || (prev == '[' && next == ']') ||
           (prev == '"' && next == '"') || (prev == '\'' && next == '\'') || (prev == '`' && next == '`') ||
           (pos >= 2 && static_cast<int>(sci(SCI_GETCHARAT, pos - 2)) == '/' && prev == '*' && next == '*' &&
            static_cast<int>(sci(SCI_GETCHARAT, pos + 1)) == '/');
}

std::string current_line_before_caret() {
    Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    Sci_Position line = static_cast<Sci_Position>(sci(SCI_LINEFROMPOSITION, pos));
    Sci_Position start = static_cast<Sci_Position>(sci(SCI_POSITIONFROMLINE, line));
    std::string value;
    for (Sci_Position i = start; i < pos; ++i) value.push_back(static_cast<char>(sci(SCI_GETCHARAT, i)));
    return value;
}

bool line_wants_extra_indent(const std::string& line) {
    size_t end = line.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return false;
    char ch = line[end];
    return ch == '{' || ch == '(' || ch == '[' || ch == ':';
}

bool delete_pair_around_caret() {
    if (!pair_boundary()) return false;
    Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    bool block_comment = pos >= 2 && static_cast<int>(sci(SCI_GETCHARAT, pos - 2)) == '/' &&
                         static_cast<int>(sci(SCI_GETCHARAT, pos - 1)) == '*' &&
                         static_cast<int>(sci(SCI_GETCHARAT, pos)) == '*' &&
                         static_cast<int>(sci(SCI_GETCHARAT, pos + 1)) == '/';
    sci(SCI_BEGINUNDOACTION);
    if (block_comment) {
        sci(SCI_DELETERANGE, pos - 2, 4);
        sci(SCI_GOTOPOS, pos - 2);
    } else {
        sci(SCI_DELETERANGE, pos, 1);
        sci(SCI_DELETERANGE, pos - 1, 1);
        sci(SCI_GOTOPOS, pos - 1);
    }
    sci(SCI_ENDUNDOACTION);
    apply_syntax_overlays();
    return true;
}

bool delete_indent_unit_before_caret() {
    Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    Sci_Position line = static_cast<Sci_Position>(sci(SCI_LINEFROMPOSITION, pos));
    Sci_Position start = static_cast<Sci_Position>(sci(SCI_POSITIONFROMLINE, line));
    if (pos <= start) return false;
    for (Sci_Position i = start; i < pos; ++i) {
        char ch = static_cast<char>(sci(SCI_GETCHARAT, i));
        if (ch != ' ' && ch != '\t') return false;
    }
    if (g_indent_tabs) {
        if (static_cast<char>(sci(SCI_GETCHARAT, pos - 1)) != '\t') return false;
        sci(SCI_DELETERANGE, pos - 1, 1);
        return true;
    }
    Sci_Position column = pos - start;
    int step = std::max(1, g_indent_spaces);
    Sci_Position remove = column % step == 0 ? step : column % step;
    remove = std::min(remove, column);
    sci(SCI_DELETERANGE, pos - remove, remove);
    sci(SCI_GOTOPOS, pos - remove);
    return true;
}

LRESULT CALLBACK editor_proc(HWND editor, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) {
    if (message == WM_CHAR && g_completion_swallow_char != 0) {
        const wchar_t expected = g_completion_swallow_char;
        g_completion_swallow_char = 0;
        if (static_cast<wchar_t>(wparam) == expected) return 0;
    }
    if (message == WM_KILLFOCUS) hide_completion();
    if (message == WM_LBUTTONDOWN) hide_completion();

    if (message == WM_KEYDOWN && completion_active()) {
        switch (wparam) {
        case VK_UP:
            move_completion_selection(-1);
            return 0;
        case VK_DOWN:
            move_completion_selection(1);
            return 0;
        case VK_PRIOR:
            move_completion_selection(-std::max(1, g_completion_visible_rows));
            return 0;
        case VK_NEXT:
            move_completion_selection(std::max(1, g_completion_visible_rows));
            return 0;
        case VK_RETURN:
            accept_completion();
            g_completion_swallow_char = L'\r';
            return 0;
        case VK_TAB:
            accept_completion();
            g_completion_swallow_char = L'\t';
            return 0;
        case VK_SPACE:
            hide_completion();
            break;
        case VK_ESCAPE:
            hide_completion();
            return 0;
        case VK_BACK: {
            if (delete_pair_around_caret() || delete_indent_unit_before_caret()) {
                g_completion_swallow_char = L'\b';
                show_autocomplete();
                return 0;
            }
            LRESULT result = DefSubclassProc(editor, message, wparam, lparam);
            show_autocomplete();
            return result;
        }
        case VK_LEFT:
        case VK_RIGHT:
        case VK_HOME:
        case VK_END:
        case VK_DELETE:
            hide_completion();
            break;
        }
    }
    if (message == WM_KEYDOWN && wparam == VK_BACK) {
        if (delete_pair_around_caret() || delete_indent_unit_before_caret()) {
            g_completion_swallow_char = L'\b';
            return 0;
        }
    }
    if (message == WM_KEYDOWN && wparam == VK_DELETE) {
        if (delete_pair_around_caret()) return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_RETURN && !completion_active()) {
        std::wstring base = indent_of_current_line();
        Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
        std::wstring insert;
        Sci_Position caret_offset = 0;
        if (pair_boundary()) {
            std::wstring inner = base + indent_unit();
            insert = L"\r\n" + inner + L"\r\n" + base;
            caret_offset = static_cast<Sci_Position>(narrow_utf8(L"\r\n" + inner).size());
        } else {
            if (line_wants_extra_indent(current_line_before_caret())) base += indent_unit();
            insert = L"\r\n" + base;
            caret_offset = static_cast<Sci_Position>(narrow_utf8(insert).size());
        }
        sci(SCI_BEGINUNDOACTION);
        std::string utf8 = narrow_utf8(insert);
        sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(utf8.c_str()));
        sci(SCI_GOTOPOS, pos + caret_offset);
        sci(SCI_ENDUNDOACTION);
        apply_syntax_overlays();
        g_completion_swallow_char = L'\r';
        return 0;
    }
    return DefSubclassProc(editor, message, wparam, lparam);
}

void handle_char_added(int ch) {
    Sci_Position pos = static_cast<Sci_Position>(sci(SCI_GETCURRENTPOS));
    auto insert_close = [&](const char* close) {
        sci(SCI_INSERTTEXT, pos, reinterpret_cast<sptr_t>(close));
        sci(SCI_GOTOPOS, pos);
    };
    if (ch == '(') insert_close(")");
    else if (ch == '{') insert_close("}");
    else if (ch == '[') insert_close("]");
    else if (ch == '"') insert_close("\"");
    else if (ch == '\'') insert_close("'");
    else if (ch == '`') insert_close("`");
    else if (ch == '*' && pos >= 2 && static_cast<int>(sci(SCI_GETCHARAT, pos - 2)) == '/') insert_close("*/");
    else if (isalnum(ch) || ch == '_') show_autocomplete();
    else if (completion_active()) hide_completion();
}

bool write_current_file(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        MessageBoxW(g_window, L"The file could not be saved.", L"OMG IDE", MB_ICONERROR);
        return false;
    }
    std::string utf8 = editor_text_utf8();
    stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    g_file = path;
    configure_editor_theme();
    sci(SCI_SETSAVEPOINT);
    set_dirty(false);
    set_status(L"Saved: " + path.wstring());
    sync_terminal_directory();
    return true;
}

std::filesystem::path choose_file(bool save) {
    wchar_t buffer[32768]{};
    if (!g_file.empty()) wcsncpy_s(buffer, g_file.c_str(), _TRUNCATE);
    else wcsncpy_s(buffer, L"Untitled.omg", _TRUNCATE);
    const std::wstring initial_dir = current_file_directory().wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.lpstrInitialDir = initial_dir.empty() ? nullptr : initial_dir.c_str();
    dialog.lpstrFilter = L"OMG source (*.omg)\0*.omg\0C++ source (*.cpp)\0*.cpp\0C source (*.c)\0*.c\0Header files (*.h;*.hpp)\0*.h;*.hpp\0All files (*.*)\0*.*\0";
    dialog.lpstrDefExt = L"omg";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if ((save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) == FALSE) return {};
    return buffer;
}

bool save_file(bool save_as = false) {
    std::filesystem::path path = g_file;
    if (save_as) path = choose_file(true);
    else if (path.empty()) path = default_save_path();
    return !path.empty() && write_current_file(path);
}

bool confirm_discard() {
    if (!g_dirty) return true;
    int answer = MessageBoxW(g_window, L"Save changes before continuing?", L"OMG IDE", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) return false;
    return answer == IDNO || save_file();
}

struct NewFileNameDialog {
    HWND input = nullptr;
    std::wstring name = L"Untitled";
    bool accepted = false;
};

LRESULT CALLBACK new_file_name_input_proc(HWND control, UINT message, WPARAM wparam, LPARAM lparam,
                                          UINT_PTR, DWORD_PTR) {
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        SendMessageW(GetParent(control), WM_COMMAND, MAKEWPARAM(ID_NEW_FILE_CREATE, BN_CLICKED),
                     reinterpret_cast<LPARAM>(control));
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
        SendMessageW(GetParent(control), WM_COMMAND, MAKEWPARAM(ID_NEW_FILE_CANCEL, BN_CLICKED),
                     reinterpret_cast<LPARAM>(control));
        return 0;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK new_file_name_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<NewFileNameDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_CREATE: {
        state = reinterpret_cast<NewFileNameDialog*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        RECT client{};
        GetClientRect(window, &client);
        const int pad = scale_px(g_window, 18);
        const int gap = scale_px(g_window, 10);
        const int label_height = ui_text_height(g_window) + scale_px(g_window, 4);
        const int input_height = ui_text_height(g_window) + scale_px(g_window, 16);
        const int button_height = ui_text_height(g_window) + scale_px(g_window, 18);
        const int create_width = std::max(scale_px(g_window, 92), static_cast<int>(measure_ui_text(g_window, L"Create").cx) + scale_px(g_window, 28));
        const int cancel_width = std::max(scale_px(g_window, 92), static_cast<int>(measure_ui_text(g_window, L"Cancel").cx) + scale_px(g_window, 28));
        const int buttons_width = create_width + gap + cancel_width;
        const int button_y = client.bottom - pad - button_height;
        HWND label = CreateWindowW(L"STATIC", L"File name", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   pad, pad, std::max(0L, client.right - pad * 2), label_height, window, nullptr, nullptr, nullptr);
        state->input = CreateWindowW(L"EDIT", state->name.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     pad, pad + label_height + gap, std::max(0L, client.right - pad * 2), input_height,
                                     window, reinterpret_cast<HMENU>(ID_NEW_FILE_NAME), nullptr, nullptr);
        HWND create = CreateWindowW(L"BUTTON", L"Create", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                    static_cast<int>(std::max<LONG>(pad, client.right - pad - buttons_width)), button_y, create_width, button_height,
                                    window, reinterpret_cast<HMENU>(ID_NEW_FILE_CREATE), nullptr, nullptr);
        HWND cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    static_cast<int>(std::max<LONG>(pad, client.right - pad - cancel_width)), button_y, cancel_width, button_height,
                                    window, reinterpret_cast<HMENU>(ID_NEW_FILE_CANCEL), nullptr, nullptr);
        for (HWND control : {label, state->input, create, cancel}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_font), TRUE);
        }
        SetWindowSubclass(state->input, new_file_name_input_proc, 3, 0);
        SendMessageW(state->input, EM_SETSEL, 0, -1);
        SetFocus(state->input);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == ID_NEW_FILE_CREATE && state && state->input) {
            const int length = GetWindowTextLengthW(state->input);
            std::wstring name(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(state->input, name.data(), length + 1);
            name.resize(static_cast<size_t>(length));
            const bool invalid = name.empty() || name.find_first_of(L"\\\\/:*?\"<>|") != std::wstring::npos ||
                                 name.back() == L' ' || name.back() == L'.';
            if (invalid) {
                MessageBoxW(window, L"Enter a valid file name.", L"New file", MB_OK | MB_ICONWARNING);
                SetFocus(state->input);
                return 0;
            }
            if (std::filesystem::path(name).extension().empty()) name += L".omg";
            state->name = std::move(name);
            state->accepted = true;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wparam) == ID_NEW_FILE_CANCEL) {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, theme().text);
        SetBkColor(dc, theme().panel);
        return reinterpret_cast<LRESULT>(g_panel_brush);
    }
    case WM_ERASEBKGND: {
        RECT area{};
        GetClientRect(window, &area);
        FillRect(reinterpret_cast<HDC>(wparam), &area, g_panel_brush);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool ask_new_file_name(std::wstring& name) {
    static ATOM dialog_class = 0;
    if (!dialog_class) {
        WNDCLASSW descriptor{};
        descriptor.lpfnWndProc = new_file_name_proc;
        descriptor.hInstance = GetModuleHandleW(nullptr);
        descriptor.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        descriptor.hbrBackground = nullptr;
        descriptor.lpszClassName = L"OMGNewFileNameWindow";
        dialog_class = RegisterClassW(&descriptor);
        if (!dialog_class && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }

    NewFileNameDialog state{};
    const int pad = scale_px(g_window, 18);
    const int gap = scale_px(g_window, 10);
    const int create_width = static_cast<int>(measure_ui_text(g_window, L"Create").cx) + scale_px(g_window, 28);
    const int cancel_width = static_cast<int>(measure_ui_text(g_window, L"Cancel").cx) + scale_px(g_window, 28);
    const int width = std::max(scale_px(g_window, 440), create_width + cancel_width + gap + pad * 2);
    const int height = pad * 2 + (ui_text_height(g_window) + scale_px(g_window, 4)) + gap +
                       (ui_text_height(g_window) + scale_px(g_window, 16)) + gap +
                       (ui_text_height(g_window) + scale_px(g_window, 18));
    RECT owner{};
    GetWindowRect(g_window, &owner);
    const int x = owner.left + static_cast<int>(std::max<LONG>(0, (owner.right - owner.left - width) / 2));
    const int y = owner.top + static_cast<int>(std::max<LONG>(0, (owner.bottom - owner.top - height) / 2));
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"OMGNewFileNameWindow", L"New file",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
                                  g_window, nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog) return false;
    EnableWindow(g_window, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG event{};
    while (IsWindow(dialog) && GetMessageW(&event, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &event)) {
            TranslateMessage(&event);
            DispatchMessageW(&event);
        }
    }
    EnableWindow(g_window, TRUE);
    SetForegroundWindow(g_window);
    if (!state.accepted) return false;
    name = std::move(state.name);
    return true;
}

void new_file(bool ask_for_name = true) {
    if (!confirm_discard()) return;
    std::wstring name;
    if (ask_for_name && !ask_new_file_name(name)) return;
    g_file = ask_for_name ? default_save_directory() / name : std::filesystem::path{};
    configure_editor_theme();
    set_editor_text(L"");
    sci(SCI_EMPTYUNDOBUFFER);
    sci(SCI_SETSAVEPOINT);
    set_dirty(false);
    set_status(ask_for_name ? L"New file: " + g_file.wstring() : L"Ready");
    sync_terminal_directory();
    SetFocus(g_editor);
}

bool load_file(const std::filesystem::path& path, const wchar_t* status_prefix) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::ostringstream content;
    content << stream.rdbuf();
    sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(content.str().c_str()));
    g_file = path;
    configure_editor_theme();
    sci(SCI_EMPTYUNDOBUFFER);
    sci(SCI_SETSAVEPOINT);
    set_dirty(false);
    set_status(std::wstring(status_prefix) + path.wstring());
    sync_terminal_directory();
    SetFocus(g_editor);
    return true;
}

bool restore_last_file() {
    std::error_code error;
    if (g_last_file.empty() || !std::filesystem::is_regular_file(g_last_file, error)) return false;
    return load_file(g_last_file, L"Restored: ");
}

void open_file() {
    if (!confirm_discard()) return;
    std::filesystem::path path = choose_file(false);
    if (path.empty()) return;
    if (!load_file(path, L"Opened: ")) {
        MessageBoxW(g_window, L"The file could not be opened.", L"OMG IDE", MB_ICONERROR);
    }
}

std::wstring quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

std::wstring find_program(const std::wstring& name) {
    wchar_t path[32768]{};
    DWORD length = SearchPathW(nullptr, name.c_str(), nullptr, static_cast<DWORD>(std::size(path)), path, nullptr);
    return length && length < std::size(path) ? path : L"";
}

std::wstring find_omg_interpreter() {
    std::vector<std::filesystem::path> candidates = {
        module_directory() / L"omg.exe",
        module_directory().parent_path().parent_path() / L"OMG Lang" / L"omg.exe",
        current_file_directory() / L"omg.exe"
    };
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::exists(candidate, error)) return candidate.wstring();
    }
    return find_program(L"omg.exe");
}

struct ProcessResult {
    DWORD exit_code = 1;
    std::string output;
    bool started = false;
};

ProcessResult run_process(std::wstring command, const std::filesystem::path& directory) {
    ProcessResult result;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) return result;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');
    result.started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                    directory.empty() ? nullptr : directory.c_str(), &startup, &process) != FALSE;
    CloseHandle(write_pipe);
    if (!result.started) {
        CloseHandle(read_pipe);
        return result;
    }
    char chunk[4096];
    DWORD count = 0;
    while (ReadFile(read_pipe, chunk, sizeof(chunk), &count, nullptr) && count) result.output.append(chunk, count);
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &result.exit_code);
    CloseHandle(read_pipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}

void set_output(const std::wstring& text) {
    g_output_text = text;
    if (g_output) {
        SetWindowTextW(g_output, g_output_text.c_str());
        SendMessageW(g_output, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(g_output, EM_SCROLLCARET, 0, 0);
    }
}

std::wstring widen_oem(const std::string& text) {
    if (text.empty()) return L"";
    const int length = MultiByteToWideChar(CP_OEMCP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring value(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, text.data(), static_cast<int>(text.size()), value.data(), length);
    return value;
}

std::string narrow_oem(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_OEMCP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string value(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_OEMCP, 0, text.data(), static_cast<int>(text.size()), value.data(), length, nullptr, nullptr);
    return value;
}

void refresh_terminal_output() {
    if (!g_terminal_output) return;
    SetWindowTextW(g_terminal_output, g_terminal_text.c_str());
    SendMessageW(g_terminal_output, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(g_terminal_output, EM_SCROLLCARET, 0, 0);
}

void append_terminal_output(const std::wstring& text) {
    g_terminal_text += text;
    if (g_terminal_text.size() > 200000) {
        g_terminal_text.erase(0, g_terminal_text.size() - 160000);
    }
    refresh_terminal_output();
}

bool terminal_running() {
    return g_terminal_process != nullptr && g_terminal_stdin_write != nullptr;
}

bool interactive_run_running() {
    return g_interactive_process != nullptr && g_interactive_stdin_write != nullptr;
}

void send_interactive_input(const std::wstring& text) {
    if (!interactive_run_running()) return;
    std::string bytes = narrow_utf8(text + L"\r\n");
    DWORD written = 0;
    WriteFile(g_interactive_stdin_write, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}

void send_terminal_line(const std::wstring& text) {
    if (interactive_run_running()) {
        send_interactive_input(text);
        return;
    }
    if (!terminal_running()) return;
    std::string bytes = narrow_oem(text + L"\r\n");
    DWORD written = 0;
    WriteFile(g_terminal_stdin_write, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}

void sync_terminal_directory() {
    const std::filesystem::path desired = current_file_directory();
    if (desired.empty() || !terminal_running()) return;
    if (!g_terminal_directory.empty()) {
        const std::wstring current = g_terminal_directory.lexically_normal().wstring();
        const std::wstring next = desired.lexically_normal().wstring();
        if (_wcsicmp(current.c_str(), next.c_str()) == 0) return;
    }
    g_terminal_directory = desired;
    send_terminal_line(L"Set-Location -LiteralPath " + quote(desired));
}

void stop_terminal_session() {
    if (g_terminal_stdin_write) {
        CloseHandle(g_terminal_stdin_write);
        g_terminal_stdin_write = nullptr;
    }
    g_terminal_stdout_read = nullptr;
    if (g_terminal_process) {
        TerminateProcess(g_terminal_process, 0);
        CloseHandle(g_terminal_process);
        g_terminal_process = nullptr;
    }
}

void stop_interactive_run() {
    if (g_interactive_stdin_write) {
        CloseHandle(g_interactive_stdin_write);
        g_interactive_stdin_write = nullptr;
    }
    if (g_interactive_process) {
        TerminateProcess(g_interactive_process, 0);
        CloseHandle(g_interactive_process);
        g_interactive_process = nullptr;
    }
}

void start_terminal_session() {
    if (terminal_running()) {
        sync_terminal_directory();
        return;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, &security, 0)) return;
    if (!CreatePipe(&stdin_read, &stdin_write, &security, 0)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        return;
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stdout_write;

    PROCESS_INFORMATION process{};
    std::wstring command = L"powershell.exe -NoLogo -NoExit";
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');
    const std::filesystem::path directory = current_file_directory();
    const BOOL started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        directory.empty() ? nullptr : directory.c_str(), &startup, &process);
    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    if (!started) {
        CloseHandle(stdout_read);
        CloseHandle(stdin_write);
        append_terminal_output(L"Failed to start terminal.\r\n");
        return;
    }

    g_terminal_process = process.hProcess;
    CloseHandle(process.hThread);
    g_terminal_stdin_write = stdin_write;
    g_terminal_stdout_read = stdout_read;
    g_terminal_directory = directory;

    std::thread([read_pipe = stdout_read] {
        char chunk[4096];
        DWORD count = 0;
        while (ReadFile(read_pipe, chunk, sizeof(chunk), &count, nullptr) && count) {
            auto* payload = new std::wstring(widen_oem(std::string(chunk, chunk + count)));
            PostMessageW(g_window, WM_TERMINAL_OUTPUT, 0, reinterpret_cast<LPARAM>(payload));
        }
        CloseHandle(read_pipe);
        PostMessageW(g_window, WM_TERMINAL_EXITED, 0, 0);
    }).detach();
}

void show_bottom_tab(BottomPaneTab tab, bool focus_terminal_input = false) {
    g_bottom_tab = tab;
    if (tab == BottomPaneTab::Terminal && !interactive_run_running()) {
        start_terminal_session();
        sync_terminal_directory();
    }
    if (g_output) ShowWindow(g_output, tab == BottomPaneTab::Output ? SW_SHOW : SW_HIDE);
    if (g_terminal_output) ShowWindow(g_terminal_output, tab == BottomPaneTab::Terminal ? SW_SHOW : SW_HIDE);
    if (g_terminal_prompt) ShowWindow(g_terminal_prompt, tab == BottomPaneTab::Terminal ? SW_SHOW : SW_HIDE);
    if (g_terminal_input) ShowWindow(g_terminal_input, tab == BottomPaneTab::Terminal ? SW_SHOW : SW_HIDE);
    if (g_output_tab) InvalidateRect(g_output_tab, nullptr, FALSE);
    if (g_terminal_tab) InvalidateRect(g_terminal_tab, nullptr, FALSE);
    if (g_window) layout(g_window);
    if (focus_terminal_input && g_terminal_input) SetFocus(g_terminal_input);
}

void submit_terminal_command() {
    if (!g_terminal_input) return;
    wchar_t buffer[4096]{};
    GetWindowTextW(g_terminal_input, buffer, static_cast<int>(std::size(buffer)));
    std::wstring command = buffer;
    if (command.empty()) return;
    if (!interactive_run_running()) {
        start_terminal_session();
        sync_terminal_directory();
    }
    send_terminal_line(command);
    SetWindowTextW(g_terminal_input, L"");
}

LRESULT CALLBACK terminal_input_proc(HWND control, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR, DWORD_PTR) {
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        submit_terminal_command();
        return 0;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

void run_interactively(const std::wstring& command, const std::filesystem::path& directory,
                       const std::wstring& display_name) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, &security, 0) ||
        !CreatePipe(&stdin_read, &stdin_write, &security, 0)) {
        if (stdout_read) CloseHandle(stdout_read);
        if (stdout_write) CloseHandle(stdout_write);
        if (stdin_read) CloseHandle(stdin_read);
        if (stdin_write) CloseHandle(stdin_write);
        MessageBoxW(g_window, L"Could not create the interactive run pipes.", L"OMG IDE", MB_ICONERROR);
        return;
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stdout_write;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');
    const BOOL started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        directory.empty() ? nullptr : directory.c_str(), &startup, &process);
    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    if (!started) {
        CloseHandle(stdout_read);
        CloseHandle(stdin_write);
        MessageBoxW(g_window, L"The interactive program could not be started.", L"OMG IDE", MB_ICONERROR);
        return;
    }

    HANDLE wait_process = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), process.hProcess, GetCurrentProcess(), &wait_process, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        CloseHandle(stdout_read);
        CloseHandle(stdin_write);
        TerminateProcess(process.hProcess, 0);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        MessageBoxW(g_window, L"Could not monitor the interactive program.", L"OMG IDE", MB_ICONERROR);
        return;
    }
    g_interactive_process = process.hProcess;
    g_interactive_stdin_write = stdin_write;
    CloseHandle(process.hThread);
    g_running = true;
    EnableWindow(g_run_button, FALSE);
    show_bottom_tab(BottomPaneTab::Terminal, true);
    append_terminal_output(L"\r\n[Running " + display_name + L" — type input below and press Enter]\r\n");
    set_status(L"Running interactively: " + display_name);

    std::thread([read_pipe = stdout_read, wait_process] {
        char chunk[4096];
        DWORD count = 0;
        while (ReadFile(read_pipe, chunk, sizeof(chunk), &count, nullptr) && count) {
            auto* payload = new std::string(chunk, chunk + count);
            PostMessageW(g_window, WM_INTERACTIVE_RUN_OUTPUT, 0, reinterpret_cast<LPARAM>(payload));
        }
        CloseHandle(read_pipe);
        WaitForSingleObject(wait_process, INFINITE);
        DWORD exit_code = 1;
        GetExitCodeProcess(wait_process, &exit_code);
        CloseHandle(wait_process);
        PostMessageW(g_window, WM_INTERACTIVE_RUN_FINISHED, exit_code, 0);
    }).detach();
}

void run_code() {
    if (g_running) return;
    if (g_file.empty() || g_dirty) {
        if (!save_file()) return;
    }
    if (is_omg_language()) {
        std::wstring interpreter = find_omg_interpreter();
        if (interpreter.empty()) {
            MessageBoxW(g_window, L"omg.exe was not found. Build OMG Lang\\omg.cpp first.", L"Interpreter not found", MB_ICONERROR);
            return;
        }
        run_interactively(quote(std::filesystem::path(interpreter)) + L" " + quote(g_file), g_file.parent_path(),
                          g_file.filename().wstring());
        return;
    }
    std::wstring compiler = find_program(g_file.extension() == L".c" ? L"gcc.exe" : L"g++.exe");
    if (compiler.empty()) {
        MessageBoxW(g_window, L"g++/gcc was not found in PATH.", L"Compiler not found", MB_ICONERROR);
        return;
    }
    g_running = true;
    show_bottom_tab(BottomPaneTab::Output);
    EnableWindow(g_run_button, FALSE);
    set_output(L"> Building " + g_file.filename().wstring() + L"...\r\n\r\n");
    set_status(L"Building...");
    const auto source = g_file;
    const auto executable = source.parent_path() / (source.stem().wstring() + L".exe");
    std::thread([source, executable, compiler] {
        std::wstring command = quote(compiler) + L" -std=c++17 -Wall -Wextra " + quote(source) + L" -o " + quote(executable);
        ProcessResult build = run_process(command, source.parent_path());
        std::wstring report = widen_utf8(build.output);
        std::replace(report.begin(), report.end(), L'\n', L'\r');
        if (build.started && build.exit_code == 0) {
            report += L"\r\nBuild succeeded.\r\n> Running...\r\n\r\n";
            ProcessResult run = run_process(quote(executable), source.parent_path());
            report += widen_utf8(run.output) + L"\r\n\r\nProcess exited with code " + std::to_wstring(run.exit_code) + L".\r\n";
        } else {
            report += L"\r\nBuild failed with code " + std::to_wstring(build.exit_code) + L".\r\n";
        }
        auto* payload = new std::wstring(std::move(report));
        PostMessageW(g_window, WM_RUN_FINISHED, build.exit_code, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

std::wstring normalize_editor_newlines(std::wstring text) {
    text.erase(std::remove(text.begin(), text.end(), L'\r'), text.end());
    std::wstring result;
    for (wchar_t ch : text) {
        if (ch == L'\n') result += L"\r\n";
        else result.push_back(ch);
    }
    return result;
}

std::wstring fallback_google_format(const std::wstring& code) {
    std::wstring normalized = code;
    normalized.erase(std::remove(normalized.begin(), normalized.end(), L'\r'), normalized.end());
    std::wistringstream input(normalized);
    std::wstring line;
    std::wstring output;
    int indent = 0;
    while (std::getline(input, line)) {
        size_t first = line.find_first_not_of(L" \t");
        if (first == std::wstring::npos) {
            output += L"\r\n";
            continue;
        }
        std::wstring trimmed = line.substr(first);
        if (!trimmed.empty() && trimmed[0] == L'}') indent = std::max(0, indent - 1);
        output += std::wstring(static_cast<size_t>(indent * 2), L' ') + trimmed + L"\r\n";
        for (wchar_t ch : trimmed) {
            if (ch == L'{') ++indent;
            else if (ch == L'}') indent = std::max(0, indent - 1);
        }
    }
    return output;
}

std::wstring fallback_omg_format(const std::wstring& code) {
    std::wstring normalized = code;
    normalized.erase(std::remove(normalized.begin(), normalized.end(), L'\r'), normalized.end());
    std::wistringstream input(normalized);
    std::wstring line;
    std::wstring output;
    while (std::getline(input, line)) {
        size_t last = line.find_last_not_of(L" \t");
        if (last == std::wstring::npos) {
            output += L"\r\n";
            continue;
        }
        output += line.substr(0, last + 1) + L"\r\n";
    }
    return output;
}

void format_document() {
    std::wstring code = editor_text();
    if (is_omg_language()) {
        set_editor_text(fallback_omg_format(code));
        set_dirty(true);
        set_status(L"Formatted OMG source");
        return;
    }
    std::wstring formatted;
    std::wstring clang = find_program(L"clang-format.exe");
    bool used_clang = false;
    if (!clang.empty()) {
        wchar_t temp_path[MAX_PATH]{};
        wchar_t temp_file[MAX_PATH]{};
        if (GetTempPathW(static_cast<DWORD>(std::size(temp_path)), temp_path) &&
            GetTempFileNameW(temp_path, L"omg", 0, temp_file)) {
            std::filesystem::path path = temp_file;
            path.replace_extension(L".cpp");
            MoveFileW(temp_file, path.c_str());
            {
                std::ofstream stream(path, std::ios::binary | std::ios::trunc);
                std::string utf8 = editor_text_utf8();
                stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
            }
            ProcessResult result = run_process(quote(clang) + L" -style=Google " + quote(path), path.parent_path());
            DeleteFileW(path.c_str());
            if (result.started && result.exit_code == 0 && !result.output.empty()) {
                formatted = normalize_editor_newlines(widen_utf8(result.output));
                used_clang = true;
            }
        }
    }
    if (!used_clang) formatted = fallback_google_format(code);
    set_editor_text(formatted);
    set_dirty(true);
    set_status(used_clang ? L"Formatted with Google style (clang-format)" : L"Formatted with Google-ish fallback");
}

std::vector<std::wstring> monospace_fonts() {
    return {L"Google Sans Code", L"JetBrains Mono", L"Consolas", L"Cascadia Code", L"Cascadia Mono", L"Courier New", L"Lucida Console"};
}

int read_int_control(HWND window, int id, int fallback, int low, int high) {
    wchar_t buffer[32]{};
    GetWindowTextW(GetDlgItem(window, id), buffer, static_cast<int>(std::size(buffer)));
    wchar_t* end = nullptr;
    long value = wcstol(buffer, &end, 10);
    if (end == buffer) return fallback;
    return std::clamp(static_cast<int>(value), low, high);
}

void recreate_ui_font();
void refresh_main_fonts();
void layout(HWND window);
void layout_settings_window(HWND window);

struct SettingsLayoutMetrics {
    int margin;
    int row;
    int gap;
    int label_width;
    int control_width;
    int button_width;

    int client_width() const { return margin * 2 + label_width + gap + control_width; }
    int client_height() const { return margin * 2 + row * 6 + gap * 6; }
};

SettingsLayoutMetrics settings_layout_metrics(HWND window) {
    const int dpi = window_dpi(window);
    auto dp = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const int text_height = ui_text_height(window);
    int label_width = dp(170);
    for (const wchar_t* label : {L"Theme", L"Font size", L"Indent type", L"Space count", L"Code font"}) {
        label_width = std::max(label_width, static_cast<int>(measure_ui_text(window, label).cx) + dp(20));
    }
    int control_width = std::max(dp(380), static_cast<int>(measure_ui_text(window, L"Google Sans Code").cx) + dp(72));
    int button_width = std::max(dp(104), static_cast<int>(measure_ui_text(window, L"Apply").cx) + dp(36));
    button_width = std::max(button_width, static_cast<int>(measure_ui_text(window, L"Close").cx) + dp(36));
    return {std::max(dp(28), text_height / 2), text_height + dp(14),
            std::max(dp(12), text_height / 3), label_width, control_width, button_width};
}

void refresh_settings_indent_visibility(HWND window) {
    int type = static_cast<int>(SendMessageW(GetDlgItem(window, ID_SETTINGS_INDENT_TYPE), CB_GETCURSEL, 0, 0));
    ShowWindow(GetDlgItem(window, ID_SETTINGS_SPACES_LABEL), type == 1 ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(window, ID_SETTINGS_SPACES), type == 1 ? SW_HIDE : SW_SHOW);
}

void apply_settings_window(HWND window) {
    g_theme_index = std::clamp(static_cast<int>(SendMessageW(GetDlgItem(window, ID_SETTINGS_THEME), CB_GETCURSEL, 0, 0)), 0, 2);
    g_code_size = read_int_control(window, ID_SETTINGS_SIZE, g_code_size, 12, 32);
    g_indent_tabs = static_cast<int>(SendMessageW(GetDlgItem(window, ID_SETTINGS_INDENT_TYPE), CB_GETCURSEL, 0, 0)) == 1;
    g_indent_spaces = read_int_control(window, ID_SETTINGS_SPACES, g_indent_spaces, 1, 12);
    wchar_t font[LF_FACESIZE]{};
    GetWindowTextW(GetDlgItem(window, ID_SETTINGS_FONT), font, static_cast<int>(std::size(font)));
    if (font[0]) g_code_font_name = font;
    save_settings();
    recreate_ui_font();
    refresh_main_fonts();
    configure_editor_theme();
    layout(g_window);
    DestroyWindow(window);
}

HWND add_label(HWND window, int id, const wchar_t* text) {
    HWND label = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                               0, 0, 0, 0, window, reinterpret_cast<HMENU>(id), nullptr, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_font), TRUE);
    return label;
}

void populate_settings_window(HWND window) {
    add_label(window, ID_SETTINGS_THEME_LABEL, L"Theme");
    add_label(window, ID_SETTINGS_SIZE_LABEL, L"Font size");
    add_label(window, ID_SETTINGS_INDENT_LABEL, L"Indent type");
    add_label(window, ID_SETTINGS_SPACES_LABEL, L"Space count");
    add_label(window, ID_SETTINGS_FONT_LABEL, L"Code font");

    HWND theme_combo = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                     0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_SETTINGS_THEME), nullptr, nullptr);
    for (const Theme& item : THEMES) SendMessageW(theme_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.name));
    SendMessageW(theme_combo, CB_SETCURSEL, g_theme_index, 0);
    HWND size_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(g_code_size).c_str(),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0, 0, 0, 0,
                                     window, reinterpret_cast<HMENU>(ID_SETTINGS_SIZE), nullptr, nullptr);
    HWND indent_combo = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                      0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_SETTINGS_INDENT_TYPE), nullptr, nullptr);
    SendMessageW(indent_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Space"));
    SendMessageW(indent_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Tab (4 columns)"));
    SendMessageW(indent_combo, CB_SETCURSEL, g_indent_tabs ? 1 : 0, 0);
    HWND spaces_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(g_indent_spaces).c_str(),
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0, 0, 0, 0,
                                       window, reinterpret_cast<HMENU>(ID_SETTINGS_SPACES), nullptr, nullptr);
    HWND font_combo = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_SETTINGS_FONT), nullptr, nullptr);
    int font_index = 0;
    std::vector<std::wstring> fonts = monospace_fonts();
    for (int i = 0; i < static_cast<int>(fonts.size()); ++i) {
        SendMessageW(font_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(fonts[static_cast<size_t>(i)].c_str()));
        if (fonts[static_cast<size_t>(i)] == g_code_font_name) font_index = i;
    }
    SendMessageW(font_combo, CB_SETCURSEL, font_index, 0);
    HWND apply = CreateWindowW(L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                               0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_SETTINGS_APPLY), nullptr, nullptr);
    HWND close = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                               0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_SETTINGS_CLOSE), nullptr, nullptr);
    for (HWND control : {theme_combo, size_edit, indent_combo, spaces_edit, font_combo, apply, close}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_font), TRUE);
    }
    const int item_height = ui_text_height(window) + scale_px(window, 8);
    for (HWND combo : {theme_combo, indent_combo, font_combo}) {
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), item_height);
        SendMessageW(combo, CB_SETITEMHEIGHT, 0, item_height);
    }
    const int edit_margin = scale_px(window, 10);
    for (HWND edit : {size_edit, spaces_edit}) {
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(edit_margin, edit_margin));
    }
    refresh_settings_indent_visibility(window);
    layout_settings_window(window);
}

void layout_settings_window(HWND window) {
    if (!window) return;
    RECT area{};
    GetClientRect(window, &area);
    const int width = area.right - area.left;
    const SettingsLayoutMetrics metrics = settings_layout_metrics(window);
    const int control_x = metrics.margin + metrics.label_width + metrics.gap;
    const int control_width = std::max(scale_px(window, 180), width - control_x - metrics.margin);
    int y = metrics.margin;

    HDWP positions = BeginDeferWindowPos(12);
    auto place = [&](int id, int x, int top, int w, int h) {
        HWND control = GetDlgItem(window, id);
        if (!control) return;
        if (positions) {
            positions = DeferWindowPos(positions, control, nullptr, x, top, w, h,
                                       SWP_NOACTIVATE | SWP_NOZORDER);
        } else {
            SetWindowPos(control, nullptr, x, top, w, h, SWP_NOACTIVATE | SWP_NOZORDER);
        }
    };
    auto place_row = [&](int label_id, int control_id, int dropdown_rows = 0) {
        place(label_id, metrics.margin, y, metrics.label_width, metrics.row);
        place(control_id, control_x, y, control_width,
              dropdown_rows > 0 ? metrics.row * dropdown_rows : metrics.row);
        y += metrics.row + metrics.gap;
    };

    place_row(ID_SETTINGS_THEME_LABEL, ID_SETTINGS_THEME, 5);
    place_row(ID_SETTINGS_SIZE_LABEL, ID_SETTINGS_SIZE);
    place_row(ID_SETTINGS_INDENT_LABEL, ID_SETTINGS_INDENT_TYPE, 4);
    place_row(ID_SETTINGS_SPACES_LABEL, ID_SETTINGS_SPACES);
    place_row(ID_SETTINGS_FONT_LABEL, ID_SETTINGS_FONT, 8);
    y += metrics.gap;
    const int close_x = width - metrics.margin - metrics.button_width;
    const int apply_x = close_x - metrics.gap - metrics.button_width;
    place(ID_SETTINGS_APPLY, apply_x, y, metrics.button_width, metrics.row);
    place(ID_SETTINGS_CLOSE, close_x, y, metrics.button_width, metrics.row);
    if (positions) EndDeferWindowPos(positions);

    SendMessageW(GetDlgItem(window, ID_SETTINGS_FONT), CB_SETDROPPEDWIDTH,
                 std::max(control_width, metrics.control_width), 0);
}

LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        g_settings_window = window;
        populate_settings_window(window);
        return 0;
    case WM_SIZE:
        layout_settings_window(window);
        return 0;
    case WM_GETMINMAXINFO: {
        const SettingsLayoutMetrics metrics = settings_layout_metrics(window);
        RECT minimum{0, 0, metrics.client_width(), metrics.client_height()};
        AdjustWindowRectEx(&minimum, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                           static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = minimum.right - minimum.left;
        limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == ID_SETTINGS_INDENT_TYPE && HIWORD(wparam) == CBN_SELCHANGE) refresh_settings_indent_visibility(window);
        else if (LOWORD(wparam) == ID_SETTINGS_APPLY) apply_settings_window(window);
        else if (LOWORD(wparam) == ID_SETTINGS_CLOSE) DestroyWindow(window);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, theme().text);
        SetBkColor(dc, theme().panel);
        return reinterpret_cast<LRESULT>(g_panel_brush);
    }
    case WM_ERASEBKGND: {
        RECT area{};
        GetClientRect(window, &area);
        FillRect(reinterpret_cast<HDC>(wparam), &area, g_panel_brush);
        return 1;
    }
    case WM_DESTROY:
        if (g_settings_window == window) g_settings_window = nullptr;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void show_settings_window() {
    if (g_settings_window) {
        SetForegroundWindow(g_settings_window);
        return;
    }
    const DWORD style = WS_OVERLAPPEDWINDOW;
    const SettingsLayoutMetrics metrics = settings_layout_metrics(g_window);
    RECT desired{0, 0, metrics.client_width(), metrics.client_height()};
    AdjustWindowRectEx(&desired, style, FALSE, 0);
    int width = desired.right - desired.left;
    int height = desired.bottom - desired.top;

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &monitor);
    const int work_width = monitor.rcWork.right - monitor.rcWork.left;
    const int work_height = monitor.rcWork.bottom - monitor.rcWork.top;
    width = std::min(width, work_width * 9 / 10);
    height = std::min(height, work_height * 9 / 10);
    const int x = monitor.rcWork.left + (work_width - width) / 2;
    const int y = monitor.rcWork.top + (work_height - height) / 2;
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_window, GWLP_HINSTANCE));
    HWND window = CreateWindowExW(0, L"OMGSettingsWindow", L"Settings - OMG IDE", style,
                                  x, y, width, height, g_window, nullptr, instance, nullptr);
    if (window) ShowWindow(window, SW_SHOW);
}

void recreate_ui_font() {
    if (g_ui_font) DeleteObject(g_ui_font);
    const int dpi = window_dpi(g_window);
    int ui_size = std::clamp(g_code_size, 14, 22);
    g_ui_font = CreateFontW(-MulDiv(ui_size, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                            L"Google Sans Code");
}

void refresh_main_fonts() {
    if (!g_window || !g_ui_font) return;
    for (int id : {ID_NEW, ID_OPEN, ID_SAVE, ID_SAVE_AS, ID_ZOOM_OUT, ID_ZOOM_RESET, ID_ZOOM_IN, ID_RUN,
                   ID_OUTPUT_TAB, ID_TERMINAL_TAB, ID_WINDOW_MINIMIZE, ID_WINDOW_FULLSCREEN, ID_EXIT}) {
        HWND control = GetDlgItem(g_window, id);
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_font), TRUE);
    }
    for (HWND control : {g_file_label, g_output, g_terminal_output, g_terminal_prompt, g_terminal_input, g_status}) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_font), TRUE);
    }
    if (g_settings_window) {
        for (int id : {ID_SETTINGS_THEME_LABEL, ID_SETTINGS_SIZE_LABEL, ID_SETTINGS_INDENT_LABEL,
                       ID_SETTINGS_SPACES_LABEL, ID_SETTINGS_FONT_LABEL, ID_SETTINGS_THEME,
                       ID_SETTINGS_SIZE, ID_SETTINGS_INDENT_TYPE, ID_SETTINGS_SPACES,
                       ID_SETTINGS_FONT, ID_SETTINGS_APPLY, ID_SETTINGS_CLOSE}) {
            HWND control = GetDlgItem(g_settings_window, id);
            if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_font), TRUE);
        }
        const int item_height = ui_text_height(g_settings_window) + scale_px(g_settings_window, 8);
        for (int id : {ID_SETTINGS_THEME, ID_SETTINGS_INDENT_TYPE, ID_SETTINGS_FONT}) {
            HWND combo = GetDlgItem(g_settings_window, id);
            SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), item_height);
            SendMessageW(combo, CB_SETITEMHEIGHT, 0, item_height);
        }
        layout_settings_window(g_settings_window);
    }
}

void apply_theme() {
    if (g_window_brush) DeleteObject(g_window_brush);
    if (g_panel_brush) DeleteObject(g_panel_brush);
    if (g_output_brush) DeleteObject(g_output_brush);
    g_window_brush = CreateSolidBrush(theme().background);
    g_panel_brush = CreateSolidBrush(theme().panel);
    g_output_brush = CreateSolidBrush(theme().output);
    if (g_editor) configure_editor_theme();
    if (g_output) InvalidateRect(g_output, nullptr, TRUE);
    if (g_terminal_output) InvalidateRect(g_terminal_output, nullptr, TRUE);
    if (g_terminal_prompt) InvalidateRect(g_terminal_prompt, nullptr, TRUE);
    if (g_terminal_input) InvalidateRect(g_terminal_input, nullptr, TRUE);
    RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void zoom_code(int delta, bool reset = false) {
    int next = reset ? 18 : std::clamp(g_code_size + delta, 12, 32);
    if (next == g_code_size && !reset) return;
    g_code_size = next;
    recreate_ui_font();
    refresh_main_fonts();
    configure_editor_theme();
    save_settings();
    SetWindowTextW(GetDlgItem(g_window, ID_ZOOM_RESET), (std::to_wstring(g_code_size) + L" pt").c_str());
    layout(g_window);
}

void draw_button(const DRAWITEMSTRUCT* item) {
    bool run = item->CtlID == ID_RUN;
    bool tab = item->CtlID == ID_OUTPUT_TAB || item->CtlID == ID_TERMINAL_TAB;
    bool window_control = item->CtlID == ID_WINDOW_MINIMIZE ||
                          item->CtlID == ID_WINDOW_FULLSCREEN || item->CtlID == ID_EXIT;
    bool active_tab = (item->CtlID == ID_OUTPUT_TAB && g_bottom_tab == BottomPaneTab::Output) ||
                      (item->CtlID == ID_TERMINAL_TAB && g_bottom_tab == BottomPaneTab::Terminal);
    COLORREF fill = window_control ? theme().background :
                    (run ? theme().green : (active_tab ? theme().editor : theme().panel));
    if (window_control && (item->itemState & ODS_SELECTED)) {
        fill = item->CtlID == ID_EXIT ? RGB(120, 45, 52) : theme().line;
    } else if (window_control &&
               (item->hwndItem == g_hovered_window_control || (item->itemState & ODS_HOTLIGHT))) {
        // Keep each window control distinct, but make only the hovered button
        // lighter in the same understated way as VS Code's title-bar controls.
        fill = mix_colour(theme().background, theme().line, 150);
    }
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(item->hDC, &item->rcItem, brush);
    DeleteObject(brush);
    if (window_control) {
        const RECT& area = item->rcItem;
        const int width = area.right - area.left;
        const int height = area.bottom - area.top;
        const int center_x = area.left + width / 2;
        const int center_y = area.top + height / 2;
        const int icon = std::max(12, std::min(width, height) / 3);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, icon / 10), theme().text);
        HGDIOBJ old_pen = SelectObject(item->hDC, pen);
        HGDIOBJ old_brush = SelectObject(item->hDC, GetStockObject(HOLLOW_BRUSH));
        if (item->CtlID == ID_WINDOW_MINIMIZE) {
            MoveToEx(item->hDC, center_x - icon / 2, center_y + icon / 3, nullptr);
            LineTo(item->hDC, center_x + icon / 2, center_y + icon / 3);
        } else if (item->CtlID == ID_WINDOW_FULLSCREEN) {
            Rectangle(item->hDC, center_x - icon / 2 + icon / 4, center_y - icon / 2 - icon / 4,
                      center_x + icon / 2 + icon / 4, center_y + icon / 2 - icon / 4);
            Rectangle(item->hDC, center_x - icon / 2 - icon / 4, center_y - icon / 2 + icon / 4,
                      center_x + icon / 2 - icon / 4, center_y + icon / 2 + icon / 4);
        } else {
            MoveToEx(item->hDC, center_x - icon / 2, center_y - icon / 2, nullptr);
            LineTo(item->hDC, center_x + icon / 2, center_y + icon / 2);
            MoveToEx(item->hDC, center_x + icon / 2, center_y - icon / 2, nullptr);
            LineTo(item->hDC, center_x - icon / 2, center_y + icon / 2);
        }
        SelectObject(item->hDC, old_brush);
        SelectObject(item->hDC, old_pen);
        DeleteObject(pen);
        return;
    }
    wchar_t text[64]{};
    GetWindowTextW(item->hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, run ? theme().editor : (tab && !active_tab ? theme().muted : theme().text));
    HFONT old_font = static_cast<HFONT>(SelectObject(item->hDC, g_ui_font));
    DrawTextW(item->hDC, text, -1, const_cast<RECT*>(&item->rcItem), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(item->hDC, old_font);
}

void layout(HWND window) {
    hide_completion();
    RECT area{};
    GetClientRect(window, &area);
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    if (width <= 0 || height <= 0) return;

    const int dpi = window_dpi(window);
    auto dp = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const int text_height = ui_text_height(window);
    const int margin = dp(12);
    const int gap = dp(10);
    const int button_height = text_height + dp(16);

    auto button_width = [&](HWND control, int horizontal_padding) {
        wchar_t text[64]{};
        GetWindowTextW(control, text, static_cast<int>(std::size(text)));
        return std::max(button_height, static_cast<int>(measure_ui_text(window, text).cx) + dp(horizontal_padding));
    };

    struct ToolbarItem { HWND control; int width; };
    std::vector<ToolbarItem> items;
    for (int id : {ID_NEW, ID_OPEN, ID_SAVE, ID_SAVE_AS}) {
        HWND control = GetDlgItem(window, id);
        items.push_back({control, button_width(control, 36)});
    }
    HWND zoom_out = GetDlgItem(window, ID_ZOOM_OUT);
    HWND zoom_reset = GetDlgItem(window, ID_ZOOM_RESET);
    HWND zoom_in = GetDlgItem(window, ID_ZOOM_IN);
    items.push_back({zoom_out, button_width(zoom_out, 24)});
    items.push_back({zoom_reset, button_width(zoom_reset, 32)});
    items.push_back({zoom_in, button_width(zoom_in, 24)});

    HDWP positions = BeginDeferWindowPos(25);
    auto place = [&](HWND control, int x, int y, int w, int h) {
        if (!control) return;
        if (positions) {
            positions = DeferWindowPos(positions, control, nullptr, x, y, w, h,
                                       SWP_NOACTIVATE | SWP_NOZORDER);
        } else {
            SetWindowPos(control, nullptr, x, y, w, h, SWP_NOACTIVATE | SWP_NOZORDER);
        }
    };

    const int window_control_width = dp(46);
    const int window_controls_left = width - margin - window_control_width * 3;
    const int toolbar_right = std::max(margin, window_controls_left - gap);
    place(GetDlgItem(window, ID_WINDOW_MINIMIZE), window_controls_left, margin,
          window_control_width, button_height);
    place(GetDlgItem(window, ID_WINDOW_FULLSCREEN), window_controls_left + window_control_width, margin,
          window_control_width, button_height);
    place(GetDlgItem(window, ID_EXIT), window_controls_left + window_control_width * 2, margin,
          window_control_width, button_height);

    int x = margin;
    int y = margin;
    for (const ToolbarItem& item : items) {
        if (x > margin && x + item.width > toolbar_right) {
            x = margin;
            y += button_height + gap;
        }
        place(item.control, x, y, item.width, button_height);
        x += item.width + gap;
    }

    const int run_width = button_width(g_run_button, 44);
    if (x + run_width > toolbar_right) {
        x = margin;
        y += button_height + gap;
    }
    const int run_x = std::max(x, toolbar_right - run_width);
    place(g_run_button, run_x, y, run_width, button_height);

    const int toolbar_bottom = y + button_height + margin;
    const int tabbar_height = text_height + dp(22);
    place(g_file_label, margin, toolbar_bottom, std::max(0, width - margin * 2), tabbar_height);
    const int editor_top = toolbar_bottom + tabbar_height;

    const int output_header = text_height + dp(18);
    const int status_height = text_height + dp(16);
    const int available = std::max(0, height - editor_top - output_header - status_height);
    const int min_editor = dp(120);
    int output_height = std::clamp(available / 3, dp(100), dp(260));
    output_height = std::min(output_height, std::max(0, available - min_editor));
    const int editor_height = std::max(0, available - output_height);
    place(g_editor, 0, editor_top, width, editor_height);
    const int bottom_top = editor_top + editor_height;
    const int tab_height = output_header - dp(6);
    const int tab_y = bottom_top + dp(3);
    const int output_tab_width = std::max(dp(92), static_cast<int>(measure_ui_text(window, L"Output").cx) + dp(30));
    const int terminal_tab_width = std::max(dp(110), static_cast<int>(measure_ui_text(window, L"Terminal").cx) + dp(30));
    place(g_output_tab, margin, tab_y, output_tab_width, tab_height);
    place(g_terminal_tab, margin + output_tab_width + gap, tab_y, terminal_tab_width, tab_height);

    const int panel_top = bottom_top + output_header;
    const int terminal_input_height = text_height + dp(12);
    const int terminal_output_height = std::max(0, output_height - terminal_input_height);
    const int terminal_row_y = panel_top + terminal_output_height;
    const int terminal_prompt_width = static_cast<int>(measure_ui_text(window, L"> ").cx);
    place(g_output, 0, panel_top, width, output_height);
    place(g_terminal_output, 0, panel_top, width, terminal_output_height);
    place(g_terminal_prompt, margin, terminal_row_y, terminal_prompt_width, terminal_input_height);
    place(g_terminal_input, margin + terminal_prompt_width, terminal_row_y,
          std::max(0, width - margin * 2 - terminal_prompt_width), terminal_input_height);
    place(g_status, 0, height - status_height, width, status_height);
    if (positions) EndDeferWindowPos(positions);
    ShowWindow(g_output, g_bottom_tab == BottomPaneTab::Output ? SW_SHOW : SW_HIDE);
    ShowWindow(g_terminal_output, g_bottom_tab == BottomPaneTab::Terminal ? SW_SHOW : SW_HIDE);
    ShowWindow(g_terminal_prompt, g_bottom_tab == BottomPaneTab::Terminal ? SW_SHOW : SW_HIDE);
    ShowWindow(g_terminal_input, g_bottom_tab == BottomPaneTab::Terminal ? SW_SHOW : SW_HIDE);
}

void create_menu(HWND window) {
    SetMenu(window, nullptr);
}

LRESULT CALLBACK window_control_proc(HWND control, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR, DWORD_PTR) {
    if (message == WM_MOUSEMOVE) {
        if (g_hovered_window_control != control) {
            if (g_hovered_window_control) InvalidateRect(g_hovered_window_control, nullptr, FALSE);
            g_hovered_window_control = control;
            InvalidateRect(control, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, control, 0};
        TrackMouseEvent(&tracking);
    } else if (message == WM_MOUSELEAVE && g_hovered_window_control == control) {
        g_hovered_window_control = nullptr;
        InvalidateRect(control, nullptr, FALSE);
    } else if (message == WM_DESTROY && g_hovered_window_control == control) {
        g_hovered_window_control = nullptr;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

void set_fullscreen(bool enabled) {
    if (!g_window || enabled == g_fullscreen) return;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &monitor);

    if (enabled) {
        SetWindowLongPtrW(g_window, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN);
        // Maximize into the monitor work area so the Windows taskbar stays visible.
        const RECT& area = monitor.rcWork;
        SetWindowPos(g_window, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        const RECT& area = monitor.rcWork;
        const int work_width = static_cast<int>(area.right - area.left);
        const int work_height = static_cast<int>(area.bottom - area.top);
        const int width = std::min(1100, std::max(640, work_width - 80));
        const int height = std::min(760, std::max(480, work_height - 80));
        SetWindowLongPtrW(g_window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
        SetWindowPos(g_window, HWND_TOP, area.left + (work_width - width) / 2,
                     area.top + (work_height - height) / 2, width, height,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    g_fullscreen = enabled;
    layout(g_window);
}

template <typename Fn>
Fn dll_function(HMODULE module, const char* name) {
    FARPROC proc = GetProcAddress(module, name);
    Fn fn{};
    static_assert(sizeof(fn) == sizeof(proc), "function pointer size mismatch");
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
}

bool init_scintilla(HINSTANCE instance) {
    wchar_t exe_path[32768]{};
    GetModuleFileNameW(nullptr, exe_path, static_cast<DWORD>(std::size(exe_path)));
    std::filesystem::path dir = std::filesystem::path(exe_path).parent_path();
    // Keep the editor engine and lexer beside the executable, even when the IDE
    // is launched from a shortcut whose working directory is elsewhere.
    write_startup_log("Preparing the editor component directory.");
    SetDllDirectoryW(dir.c_str());
    write_startup_log("Loading Scintilla.dll.");
    g_scintilla = LoadLibraryW((dir / L"Scintilla.dll").c_str());
    if (!g_scintilla) {
        g_startup_error = GetLastError();
        write_startup_log("Scintilla.dll could not be loaded: " + std::to_string(g_startup_error));
        return false;
    }
    write_startup_log("Scintilla.dll loaded.");
    write_startup_log("Loading Lexilla.dll.");
    g_lexilla = LoadLibraryW((dir / L"Lexilla.dll").c_str());
    if (!g_lexilla) {
        g_startup_error = GetLastError();
        write_startup_log("Lexilla.dll could not be loaded: " + std::to_string(g_startup_error));
        return false;
    }
    write_startup_log("Lexilla.dll loaded.");
    using RegisterFn = int (*)(void*);
    auto reg = dll_function<RegisterFn>(g_scintilla, "Scintilla_RegisterClasses");
    if (reg) reg(instance);
    write_startup_log("Editor classes registered.");
    using CreateLexerFn = void* (*)(const char*);
    auto create_lexer = dll_function<CreateLexerFn>(g_lexilla, "CreateLexer");
    if (!create_lexer) {
        write_startup_log("Lexilla CreateLexer export is missing.");
        return false;
    }
    g_cpp_lexer = create_lexer("cpp");
    if (!g_cpp_lexer) write_startup_log("The C++ lexer could not be created.");
    else write_startup_log("C++ lexer created.");
    return g_cpp_lexer != nullptr;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SYSKEYDOWN:
        if (g_fullscreen && wparam == VK_RETURN) return 0;
        break;
    case WM_KEYDOWN:
        if (wparam == VK_F11) {
            set_fullscreen(!g_fullscreen);
            return 0;
        }
        break;
    case WM_SYSCOMMAND:
        if (g_fullscreen) {
            switch (wparam & 0xFFF0) {
            case SC_MAXIMIZE:
            case SC_SIZE:
                return 0;
            }
        }
        break;
    case WM_CREATE: {
        g_window = window;
        if (g_app_icon) {
            SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_app_icon));
            SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_app_icon));
        }
        load_settings();
        recreate_ui_font();
        create_menu(window);
        auto button = [window](int id, const wchar_t* text) {
            HWND control = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                         0, 0, 0, 0, window, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_font), TRUE);
            return control;
        };
        button(ID_NEW, L"New");
        button(ID_OPEN, L"Open");
        button(ID_SAVE, L"Save");
        button(ID_SAVE_AS, L"Save As");
        button(ID_ZOOM_OUT, L"-");
        button(ID_ZOOM_RESET, L"18 pt");
        button(ID_ZOOM_IN, L"+");
        g_run_button = button(ID_RUN, L"Run");
        g_output_tab = button(ID_OUTPUT_TAB, L"Output");
        g_terminal_tab = button(ID_TERMINAL_TAB, L"Terminal");
        button(ID_WINDOW_MINIMIZE, L"Minimize");
        button(ID_WINDOW_FULLSCREEN, L"Exit full screen");
        button(ID_EXIT, L"Close");
        g_file_label = CreateWindowW(L"STATIC", L"Untitled.omg", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        g_editor = CreateWindowW(L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        g_output = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        g_terminal_output = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        g_terminal_prompt = CreateWindowW(L"STATIC", L"> ", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                                          0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_TERMINAL_PROMPT), nullptr, nullptr);
        g_terminal_input = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                         0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_TERMINAL_INPUT), nullptr, nullptr);
        g_status = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        for (HWND control : {g_file_label, g_output, g_terminal_output, g_terminal_prompt, g_terminal_input, g_status}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_font), TRUE);
        }
        const int output_margin = scale_px(window, 10);
        SendMessageW(g_terminal_input, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        SendMessageW(g_terminal_output, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(output_margin, output_margin));
        SetWindowSubclass(g_editor, editor_proc, 1, 0);
        SetWindowSubclass(g_terminal_input, terminal_input_proc, 2, 0);
        for (int id : {ID_WINDOW_MINIMIZE, ID_WINDOW_FULLSCREEN, ID_EXIT}) {
            SetWindowSubclass(GetDlgItem(window, id), window_control_proc, static_cast<UINT_PTR>(id), 0);
        }
        apply_theme();
        SetWindowTextW(GetDlgItem(window, ID_ZOOM_RESET), (std::to_wstring(g_code_size) + L" pt").c_str());
        if (!restore_last_file()) new_file(false);
        show_bottom_tab(BottomPaneTab::Output);
        return 0;
    }
    case WM_SIZE:
        layout(window);
        return 0;
    case WM_COMMAND:
        // Toolbar buttons should only react to an actual click (0), a menu
        // command (0), or an accelerator (1). Focus notifications must not
        // reopen a modal dialog after it has just been dismissed.
        if (HIWORD(wparam) != 0 && HIWORD(wparam) != 1) return 0;
        switch (LOWORD(wparam)) {
        case ID_NEW: new_file(); break;
        case ID_OPEN: open_file(); break;
        case ID_SAVE: save_file(); break;
        case ID_SAVE_AS: save_file(true); break;
        case ID_FORMAT: format_document(); break;
        case ID_RUN: run_code(); break;
        case ID_CLEAR:
            if (g_bottom_tab == BottomPaneTab::Terminal) {
                g_terminal_text.clear();
                refresh_terminal_output();
            } else {
                set_output(L"");
            }
            break;
        case ID_OUTPUT_TAB: show_bottom_tab(BottomPaneTab::Output); break;
        case ID_TERMINAL_TAB:
        case ID_TERMINAL_FOCUS: show_bottom_tab(BottomPaneTab::Terminal, true); break;
        case ID_SETTINGS: show_settings_window(); break;
        case ID_ZOOM_IN: zoom_code(1); break;
        case ID_ZOOM_OUT: zoom_code(-1); break;
        case ID_ZOOM_RESET: zoom_code(0, true); break;
        case ID_WINDOW_MINIMIZE: ShowWindow(window, SW_MINIMIZE); break;
        case ID_WINDOW_FULLSCREEN: set_fullscreen(!g_fullscreen); break;
        case ID_EXIT: SendMessageW(window, WM_CLOSE, 0, 0); break;
        }
        return 0;
    case WM_NOTIFY: {
        auto* notice = reinterpret_cast<SCNotification*>(lparam);
        if (notice->nmhdr.hwndFrom == g_editor) {
            if (notice->nmhdr.code == SCN_CHARADDED) handle_char_added(notice->ch);
            else if (notice->nmhdr.code == SCN_UPDATEUI) {
                update_brace_match();
                update_caret_line_visibility();
                if (completion_active()) position_completion_popup();
            }
            else if (notice->nmhdr.code == SCN_MODIFIED &&
                     (notice->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
                set_dirty(true);
                apply_syntax_overlays();
            }
            else if (notice->nmhdr.code == SCN_SAVEPOINTREACHED) set_dirty(false);
        }
        return 0;
    }
    case WM_DRAWITEM:
        draw_button(reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
        return TRUE;
    case WM_RUN_FINISHED: {
        auto* report = reinterpret_cast<std::wstring*>(lparam);
        show_bottom_tab(BottomPaneTab::Output);
        set_output(*report);
        delete report;
        g_running = false;
        EnableWindow(g_run_button, TRUE);
        set_status(wparam == 0 ? L"Finished" : L"Run failed");
        return 0;
    }
    case WM_INTERACTIVE_RUN_OUTPUT: {
        auto* text = reinterpret_cast<std::string*>(lparam);
        append_terminal_output(widen_utf8(*text));
        delete text;
        return 0;
    }
    case WM_INTERACTIVE_RUN_FINISHED:
        if (g_interactive_stdin_write) {
            CloseHandle(g_interactive_stdin_write);
            g_interactive_stdin_write = nullptr;
        }
        if (g_interactive_process) {
            CloseHandle(g_interactive_process);
            g_interactive_process = nullptr;
        }
        g_running = false;
        EnableWindow(g_run_button, TRUE);
        append_terminal_output(L"\r\n[Process exited with code " + std::to_wstring(static_cast<DWORD>(wparam)) + L"]\r\n");
        set_status(L"Ready");
        return 0;
    case WM_TERMINAL_OUTPUT: {
        auto* text = reinterpret_cast<std::wstring*>(lparam);
        append_terminal_output(*text);
        delete text;
        return 0;
    }
    case WM_TERMINAL_EXITED:
        if (g_terminal_stdin_write) {
            CloseHandle(g_terminal_stdin_write);
            g_terminal_stdin_write = nullptr;
        }
        if (g_terminal_process) {
            CloseHandle(g_terminal_process);
            g_terminal_process = nullptr;
        }
        g_terminal_stdout_read = nullptr;
        append_terminal_output(L"\r\n[Terminal exited]\r\n");
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        HWND control = reinterpret_cast<HWND>(lparam);
        const bool output_like = control == g_output || control == g_terminal_output ||
                                 control == g_terminal_prompt || control == g_terminal_input;
        SetTextColor(dc, theme().text);
        SetBkColor(dc, output_like ? theme().output : theme().panel);
        return reinterpret_cast<LRESULT>(output_like ? g_output_brush : g_panel_brush);
    }
    case WM_ERASEBKGND: {
        RECT area{};
        GetClientRect(window, &area);
        FillRect(reinterpret_cast<HDC>(wparam), &area, g_window_brush);
        return 1;
    }
    case WM_CLOSE:
        if (confirm_discard()) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        save_settings();
        stop_interactive_run();
        stop_terminal_session();
        if (g_completion_popup) DestroyWindow(g_completion_popup);
        if (g_app_icon) {
            DestroyIcon(g_app_icon);
            g_app_icon = nullptr;
        }
        if (g_gdiplus_token) {
            Gdiplus::GdiplusShutdown(g_gdiplus_token);
            g_gdiplus_token = 0;
        }
        DeleteObject(g_completion_font);
        DeleteObject(g_completion_bold_font);
        DeleteObject(g_completion_meta_font);
        DeleteObject(g_ui_font);
        DeleteObject(g_window_brush);
        DeleteObject(g_panel_brush);
        DeleteObject(g_output_brush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    write_startup_log("OMG IDE launch started.", false);
    HWND existing = FindWindowW(L"OMGIDEWindow", nullptr);
    if (existing) {
        write_startup_log("Existing IDE window restored.");
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return 0;
    }

    write_startup_log("Preparing DPI settings.");
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    write_startup_log("Common controls ready.");
    write_startup_log("Starting drawing engine.");
    Gdiplus::GdiplusStartupInput gdiplus_startup;
    Gdiplus::GdiplusStartup(&g_gdiplus_token, &gdiplus_startup, nullptr);
    write_startup_log("Drawing engine ready.");
    if (!init_scintilla(instance)) {
        write_startup_log("IDE launch stopped while loading editor components.");
        MessageBoxW(nullptr, L"The IDE editor components could not be loaded. Keep Scintilla.dll and Lexilla.dll next to OMG-IDE.exe.",
                    L"OMG IDE", MB_ICONERROR);
        return 1;
    }

    WNDCLASSW settings_class{};
    settings_class.lpfnWndProc = settings_proc;
    settings_class.hInstance = instance;
    settings_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    settings_class.hbrBackground = nullptr;
    settings_class.lpszClassName = L"OMGSettingsWindow";
    RegisterClassW(&settings_class);

    WNDCLASSW completion_class{};
    completion_class.lpfnWndProc = completion_proc;
    completion_class.hInstance = instance;
    completion_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    completion_class.hbrBackground = nullptr;
    completion_class.lpszClassName = L"OMGCompletionPopup";
    RegisterClassW(&completion_class);

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = g_app_icon ? g_app_icon : LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = L"OMGIDEWindow";
    RegisterClassW(&window_class);

    HWND window = CreateWindowW(window_class.lpszClassName, L"OMG IDE", WS_POPUP | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760, nullptr, nullptr, instance, nullptr);
    if (!window) {
        g_startup_error = GetLastError();
        write_startup_log("Main window could not be created: " + std::to_string(g_startup_error));
        return 1;
    }
    write_startup_log("Main window created.");
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    // Treat full screen as borderless maximized mode: the taskbar must remain usable.
    const RECT& full_screen = monitor.rcWork;
    ShowWindow(window, show == SW_HIDE ? SW_SHOWNORMAL : show);
    SetWindowPos(window, HWND_TOP, full_screen.left, full_screen.top,
                 full_screen.right - full_screen.left, full_screen.bottom - full_screen.top,
                 SWP_SHOWWINDOW);
    UpdateWindow(window);
    SetForegroundWindow(window);
    write_startup_log("Main window was requested to show.");

    ACCEL shortcuts[] = {
        {FVIRTKEY | FCONTROL, 'N', ID_NEW}, {FVIRTKEY | FCONTROL, 'O', ID_OPEN},
        {FVIRTKEY | FCONTROL, 'S', ID_SAVE}, {FVIRTKEY, VK_F5, ID_RUN},
        {FVIRTKEY | FALT | FSHIFT, 'F', ID_FORMAT}, {FVIRTKEY | FCONTROL, VK_OEM_COMMA, ID_SETTINGS},
        {FVIRTKEY | FCONTROL, VK_OEM_3, ID_TERMINAL_FOCUS},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, ID_ZOOM_IN}, {FVIRTKEY | FCONTROL, VK_ADD, ID_ZOOM_IN},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, ID_ZOOM_OUT}, {FVIRTKEY | FCONTROL, VK_SUBTRACT, ID_ZOOM_OUT},
        {FVIRTKEY | FCONTROL, '0', ID_ZOOM_RESET}
    };
    HACCEL accelerators = CreateAcceleratorTableW(shortcuts, static_cast<int>(std::size(shortcuts)));
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window, accelerators, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    DestroyAcceleratorTable(accelerators);
    return static_cast<int>(message.wParam);
}
