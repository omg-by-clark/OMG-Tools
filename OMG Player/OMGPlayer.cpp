#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfplay.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace {

constexpr int id_open = 1001;
constexpr int id_play_pause = 1002;
constexpr int id_stop = 1003;
constexpr int id_mute = 1004;
constexpr int id_progress = 1005;
constexpr int id_volume = 1006;
constexpr int id_previous = 1007;
constexpr int id_rewind_10 = 1008;
constexpr int id_forward_10 = 1009;
constexpr int id_next = 1010;
constexpr int timer_progress = 2001;
constexpr int timer_backdrop = 2002;
constexpr int timer_control_hide = 2003;
constexpr int timer_ripple = 1;
constexpr int app_icon_id = 101;
constexpr int button_radius_px = 3;
constexpr UINT wm_slider_changed = WM_APP + 10;
constexpr UINT wm_player_event = WM_APP + 11;

constexpr WPARAM player_event_ready = 1;
constexpr WPARAM player_event_error = 2;
constexpr WPARAM player_event_ended = 3;
constexpr DWORD control_hide_delay_ms = 5000;

const wchar_t* const flat_button_class = L"OMGFlatRippleButton";
const wchar_t* const flat_slider_class = L"OMGFlatSlider";
const wchar_t* const flat_text_class = L"OMGFlatText";
const wchar_t* const video_surface_class = L"OMGPlayerVideoSurface";

const GUID iid_imfp_media_player_callback = {
    0x766c8ffb,
    0x5fdb,
    0x4fea,
    {0xa2, 0x8d, 0xb9, 0x12, 0x99, 0x6f, 0x51, 0xbd}
};

HWND g_main_window = nullptr;
HWND g_video_window_hwnd = nullptr;
HWND g_open_button = nullptr;
HWND g_play_pause_button = nullptr;
HWND g_stop_button = nullptr;
HWND g_mute_button = nullptr;
HWND g_previous_button = nullptr;
HWND g_rewind_10_button = nullptr;
HWND g_forward_10_button = nullptr;
HWND g_next_button = nullptr;
HWND g_progress_slider = nullptr;
HWND g_volume_slider = nullptr;
HWND g_status_text = nullptr;
HWND g_time_text = nullptr;

IMFPMediaPlayer* g_media_player = nullptr;
IMFPMediaItem* g_media_item = nullptr;
bool g_pending_autoplay = false;

bool g_loaded = false;
bool g_playing = false;
bool g_muted = false;
bool g_has_video = false;
bool g_tracking_progress = false;
bool g_updating_progress = false;
bool g_controls_visible = true;
int g_duration_ms = 0;
int g_last_volume = 800;
int g_backdrop_tick = 0;
int g_dpi = 96;
DWORD g_last_pointer_activity = 0;
bool g_has_pointer_position = false;
POINT g_last_pointer_position{0, 0};
std::wstring g_current_path;
std::wstring g_current_name;

struct flat_button_state {
    bool hover = false;
    bool pressed = false;
    bool ripple = false;
    double ripple_progress = 0.0;
    POINT ripple_origin{0, 0};
    HFONT font = nullptr;
};

struct flat_slider_state {
    int min_value = 0;
    int max_value = 100;
    int pos = 0;
    bool dragging = false;
    bool hover = false;
};

struct flat_text_state {
    HFONT font = nullptr;
};

struct backdrop_patch {
    double x;
    double y;
    double w;
    double h;
    COLORREF from;
    COLORREF to;
    double speed;
    double phase;
    double alpha;
    bool vertical;
};

class media_player_callback : public IMFPMediaPlayerCallback {
public:
    virtual ~media_player_callback() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == iid_imfp_media_player_callback) {
            *object = static_cast<IMFPMediaPlayerCallback*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&ref_count_);
    }

    STDMETHODIMP_(ULONG) Release() override {
        const ULONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event_header) override {
        if (event_header == nullptr || g_main_window == nullptr) {
            return;
        }
        if (FAILED(event_header->hrEvent)) {
            PostMessageW(g_main_window, wm_player_event, player_event_error, static_cast<LPARAM>(event_header->hrEvent));
            return;
        }
        if (event_header->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET) {
            PostMessageW(g_main_window, wm_player_event, player_event_ready, 0);
        } else if (event_header->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED) {
            PostMessageW(g_main_window, wm_player_event, player_event_ended, 0);
        } else if (event_header->eEventType == MFP_EVENT_TYPE_ERROR) {
            PostMessageW(g_main_window, wm_player_event, player_event_error, static_cast<LPARAM>(event_header->hrEvent));
        }
    }

private:
    volatile LONG ref_count_ = 1;
};

void layout_controls(HWND hwnd);
void seek_to(int position_ms);
void note_pointer_activity();
void note_keyboard_activity();
void forward_key_to_main(WPARAM key);

template <typename interface_type>
void release_interface(interface_type*& interface_pointer) {
    if (interface_pointer != nullptr) {
        interface_pointer->Release();
        interface_pointer = nullptr;
    }
}

int scale_px(int value) {
    return MulDiv(value, g_dpi, 96);
}

int system_dpi() {
    HDC dc = GetDC(nullptr);
    if (dc == nullptr) {
        return 96;
    }
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return dpi > 0 ? dpi : 96;
}

int dpi_for_window(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using get_dpi_for_window_proc = UINT(WINAPI*)(HWND);
        FARPROC proc = GetProcAddress(user32, "GetDpiForWindow");
        if (proc != nullptr) {
            get_dpi_for_window_proc get_dpi_for_window = nullptr;
            std::memcpy(&get_dpi_for_window, &proc, sizeof(get_dpi_for_window));
            return static_cast<int>(get_dpi_for_window(hwnd));
        }
    }
    return system_dpi();
}

std::wstring lower_copy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring extension_from_path(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    return dot == std::wstring::npos ? L"" : lower_copy(path.substr(dot));
}

std::wstring file_name_from_path(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool is_video_file(const std::wstring& path) {
    const std::wstring ext = extension_from_path(path);
    return ext == L".mp4" || ext == L".m4v" || ext == L".mov" ||
           ext == L".mkv" || ext == L".avi" || ext == L".wmv" ||
           ext == L".mpg" || ext == L".mpeg";
}

bool is_media_file(const std::wstring& path) {
    const std::wstring ext = extension_from_path(path);
    return is_video_file(path) || ext == L".mp3" || ext == L".wav" ||
           ext == L".wma" || ext == L".aac" || ext == L".m4a" ||
           ext == L".flac" || ext == L".ogg";
}

std::wstring directory_from_path(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring media_title_text() {
    if (g_current_name.empty()) {
        return L"Open a media file with OMG Player, or drop one here.";
    }
    return (g_has_video ? L"\U0001F3AC\uFE0F " : L"\U0001F3B5\uFE0F ") + g_current_name;
}

std::wstring format_time(int milliseconds) {
    milliseconds = std::max(0, milliseconds);
    const int total_seconds = milliseconds / 1000;
    const int hours = total_seconds / 3600;
    const int minutes = (total_seconds / 60) % 60;
    const int seconds = total_seconds % 60;

    wchar_t buffer[32]{};
    if (hours > 0) {
        swprintf_s(buffer, L"%d:%02d:%02d", hours, minutes, seconds);
    } else {
        swprintf_s(buffer, L"%02d:%02d", minutes, seconds);
    }
    return buffer;
}

int propvariant_100ns_to_ms(const PROPVARIANT& value) {
    LONGLONG ticks = 0;
    if (value.vt == VT_I8) {
        ticks = value.hVal.QuadPart;
    } else if (value.vt == VT_UI8) {
        ticks = static_cast<LONGLONG>(value.uhVal.QuadPart);
    } else if (value.vt == VT_I4) {
        ticks = static_cast<LONGLONG>(value.lVal);
    } else if (value.vt == VT_UI4) {
        ticks = static_cast<LONGLONG>(value.ulVal);
    } else if (value.vt == VT_FILETIME) {
        ULARGE_INTEGER file_time{};
        file_time.LowPart = value.filetime.dwLowDateTime;
        file_time.HighPart = value.filetime.dwHighDateTime;
        ticks = static_cast<LONGLONG>(file_time.QuadPart);
    } else {
        return 0;
    }
    return static_cast<int>(std::max<LONGLONG>(0, ticks / 10000));
}

std::wstring hresult_message(HRESULT result) {
    wchar_t* system_text = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(result),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&system_text),
        0,
        nullptr);

    std::wstringstream stream;
    if (system_text != nullptr) {
        stream << system_text;
        LocalFree(system_text);
    } else {
        stream << L"Media Foundation could not render this file.";
    }
    stream << L"\nHRESULT: 0x" << std::hex << static_cast<unsigned long>(result);
    return stream.str();
}

void set_status(const std::wstring& text) {
    SetWindowTextW(g_status_text, text.c_str());
}

void set_time_text(int pos_ms) {
    const std::wstring text = format_time(pos_ms) + L" / " + format_time(g_duration_ms);
    SetWindowTextW(g_time_text, text.c_str());
}

flat_slider_state* slider_state_of(HWND hwnd) {
    return reinterpret_cast<flat_slider_state*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void set_slider_range(HWND hwnd, int min_value, int max_value) {
    auto* state = slider_state_of(hwnd);
    if (state == nullptr) {
        return;
    }
    state->min_value = min_value;
    state->max_value = std::max(min_value + 1, max_value);
    state->pos = std::clamp(state->pos, state->min_value, state->max_value);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void set_slider_pos(HWND hwnd, int pos) {
    auto* state = slider_state_of(hwnd);
    if (state == nullptr) {
        return;
    }
    state->pos = std::clamp(pos, state->min_value, state->max_value);
    InvalidateRect(hwnd, nullptr, FALSE);
}

int get_slider_pos(HWND hwnd) {
    auto* state = slider_state_of(hwnd);
    return state == nullptr ? 0 : state->pos;
}

void update_control_state() {
    EnableWindow(g_play_pause_button, g_loaded);
    EnableWindow(g_stop_button, g_loaded);
    EnableWindow(g_progress_slider, g_loaded);
    EnableWindow(g_mute_button, g_loaded);
    EnableWindow(g_previous_button, g_loaded);
    EnableWindow(g_rewind_10_button, g_loaded);
    EnableWindow(g_forward_10_button, g_loaded);
    EnableWindow(g_next_button, g_loaded);
    SetWindowTextW(g_play_pause_button, g_playing ? L"\u23F8\uFE0F" : L"\u25B6\uFE0F");
    SetWindowTextW(g_mute_button, g_muted ? L"\U0001F507\uFE0F" : L"\U0001F50A\uFE0F");
}

void set_control_bar_visible(bool visible) {
    if (!g_has_video) {
        visible = true;
    }
    if (g_controls_visible == visible) {
        return;
    }
    g_controls_visible = visible;
    layout_controls(g_main_window);
}

bool read_pointer_position(POINT& position) {
    return GetCursorPos(&position) != 0;
}

void remember_pointer_position() {
    POINT position{};
    if (read_pointer_position(position)) {
        g_last_pointer_position = position;
        g_has_pointer_position = true;
    }
}

void note_pointer_activity() {
    POINT position{};
    if (read_pointer_position(position)) {
        const bool moved = !g_has_pointer_position ||
            position.x != g_last_pointer_position.x ||
            position.y != g_last_pointer_position.y;
        g_last_pointer_position = position;
        g_has_pointer_position = true;
        if (!moved) {
            return;
        }
    }

    g_last_pointer_activity = GetTickCount();
    if (g_has_video) {
        set_control_bar_visible(true);
    }
}

void note_keyboard_activity() {
    g_last_pointer_activity = GetTickCount();
    if (g_has_video) {
        set_control_bar_visible(true);
    }
}

void forward_key_to_main(WPARAM key) {
    if (g_main_window != nullptr) {
        SendMessageW(g_main_window, WM_KEYDOWN, key, 0);
    }
}

void resize_video_window() {
    if (g_media_player == nullptr || g_video_window_hwnd == nullptr) {
        return;
    }
    g_media_player->UpdateVideo();
}

void set_player_volume(int volume) {
    volume = std::clamp(volume, 0, 1000);
    if (volume > 0) {
        g_last_volume = volume;
    }
    g_muted = volume == 0;

    if (g_media_player != nullptr) {
        g_media_player->SetVolume(volume / 1000.0f);
        g_media_player->SetMute(g_muted ? TRUE : FALSE);
    }

    set_slider_pos(g_volume_slider, volume / 10);
    update_control_state();
}

int current_position_ms() {
    if (g_media_player == nullptr) {
        return 0;
    }
    PROPVARIANT position{};
    PropVariantInit(&position);
    if (FAILED(g_media_player->GetPosition(MFP_POSITIONTYPE_100NS, &position))) {
        PropVariantClear(&position);
        return 0;
    }
    const int position_ms = propvariant_100ns_to_ms(position);
    PropVariantClear(&position);
    return position_ms;
}

void refresh_progress() {
    if (!g_loaded || g_tracking_progress) {
        return;
    }

    const int pos = std::clamp(current_position_ms(), 0, std::max(0, g_duration_ms));
    g_updating_progress = true;
    set_slider_pos(g_progress_slider, pos);
    g_updating_progress = false;
    set_time_text(pos);
}

void close_media() {
    KillTimer(g_main_window, timer_progress);

    if (g_media_player != nullptr) {
        g_media_player->Stop();
        g_media_player->Shutdown();
    }

    release_interface(g_media_item);
    release_interface(g_media_player);

    g_loaded = false;
    g_playing = false;
    g_has_video = false;
    g_pending_autoplay = false;
    g_controls_visible = true;
    g_duration_ms = 0;
    g_current_path.clear();
    g_current_name.clear();

    set_slider_range(g_progress_slider, 0, 1);
    set_slider_pos(g_progress_slider, 0);
    set_time_text(0);
    set_status(L"Open a media file with OMG Player, or drop one here.");
    ShowWindow(g_video_window_hwnd, SW_HIDE);
    layout_controls(g_main_window);
    update_control_state();
}

void play_media() {
    if (!g_loaded || g_media_player == nullptr) {
        return;
    }
    if (SUCCEEDED(g_media_player->Play())) {
        g_playing = true;
        SetTimer(g_main_window, timer_progress, 250, nullptr);
        set_status(media_title_text());
    }
    update_control_state();
}

void pause_media() {
    if (!g_loaded || g_media_player == nullptr) {
        return;
    }
    if (SUCCEEDED(g_media_player->Pause())) {
        g_playing = false;
        set_status(media_title_text());
    }
    update_control_state();
}

void stop_media() {
    if (!g_loaded || g_media_player == nullptr) {
        return;
    }
    g_media_player->Stop();
    seek_to(0);
    g_playing = false;
    refresh_progress();
    set_status(media_title_text());
    update_control_state();
}

void toggle_play_pause() {
    if (!g_loaded) {
        return;
    }
    if (g_playing) {
        pause_media();
    } else {
        play_media();
    }
}

void seek_to(int position_ms) {
    if (!g_loaded || g_media_player == nullptr) {
        return;
    }

    position_ms = std::clamp(position_ms, 0, std::max(0, g_duration_ms));
    PROPVARIANT position{};
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = static_cast<LONGLONG>(position_ms) * 10000;
    if (SUCCEEDED(g_media_player->SetPosition(MFP_POSITIONTYPE_100NS, &position))) {
        set_time_text(position_ms);
    }
    PropVariantClear(&position);
}

void prepare_window_for_media(bool has_video) {
    RECT target_rect{0, 0, scale_px(has_video ? 1040 : 760), scale_px(has_video ? 680 : 154)};
    AdjustWindowRectEx(&target_rect, GetWindowLongW(g_main_window, GWL_STYLE), FALSE, GetWindowLongW(g_main_window, GWL_EXSTYLE));
    SetWindowPos(
        g_main_window,
        nullptr,
        0,
        0,
        target_rect.right - target_rect.left,
        target_rect.bottom - target_rect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(g_video_window_hwnd, has_video ? SW_SHOW : SW_HIDE);
    layout_controls(g_main_window);
    UpdateWindow(g_main_window);
    UpdateWindow(g_video_window_hwnd);
}

bool create_media_foundation_player(const std::wstring& path, std::wstring& error_text) {
    auto* callback = new media_player_callback();
    const HRESULT result = MFPCreateMediaPlayer(
        path.c_str(),
        FALSE,
        MFP_OPTION_FREE_THREADED_CALLBACK,
        callback,
        g_video_window_hwnd,
        &g_media_player);
    callback->Release();

    if (FAILED(result)) {
        error_text = hresult_message(result);
        return false;
    }

    g_media_player->SetVolume(g_last_volume / 1000.0f);
    g_media_player->SetMute(g_muted ? TRUE : FALSE);
    g_media_player->SetBorderColor(RGB(15, 17, 21));
    return true;
}

void handle_media_ready() {
    if (g_media_player == nullptr) {
        return;
    }

    release_interface(g_media_item);
    g_media_player->GetMediaItem(&g_media_item);

    if (g_media_item != nullptr) {
        WINBOOL has_video = FALSE;
        WINBOOL selected = FALSE;
        if (SUCCEEDED(g_media_item->HasVideo(&has_video, &selected))) {
            g_has_video = has_video && selected;
        }
    }

    PROPVARIANT duration{};
    PropVariantInit(&duration);
    if (SUCCEEDED(g_media_player->GetDuration(MFP_POSITIONTYPE_100NS, &duration))) {
        g_duration_ms = propvariant_100ns_to_ms(duration);
    } else if (g_media_item != nullptr && SUCCEEDED(g_media_item->GetDuration(MFP_POSITIONTYPE_100NS, &duration))) {
        g_duration_ms = propvariant_100ns_to_ms(duration);
    } else {
        g_duration_ms = 0;
    }
    PropVariantClear(&duration);

    g_loaded = true;
    g_playing = false;
    set_slider_range(g_progress_slider, 0, std::max(1, g_duration_ms));
    set_slider_pos(g_progress_slider, 0);
    set_player_volume(g_muted ? 0 : g_last_volume);
    prepare_window_for_media(g_has_video);
    resize_video_window();
    set_time_text(0);
    set_status(media_title_text());
    update_control_state();

    if (g_pending_autoplay) {
        g_pending_autoplay = false;
        play_media();
    }
}

void handle_media_error(HRESULT result) {
    std::wstring message = hresult_message(result);
    close_media();
    MessageBoxW(g_main_window, (L"Could not open this media file.\n\n" + message).c_str(), L"OMG Player", MB_ICONERROR);
    set_status(L"Open failed.");
}

void open_media_file(const std::wstring& path) {
    if (path.empty()) {
        return;
    }

    close_media();
    g_current_path = path;
    g_current_name = file_name_from_path(path);
    g_has_video = is_video_file(path);
    g_controls_visible = true;
    g_last_pointer_activity = GetTickCount();
    remember_pointer_position();
    prepare_window_for_media(g_has_video);

    std::wstring error_text;
    if (!create_media_foundation_player(path, error_text)) {
        close_media();
        MessageBoxW(g_main_window, (L"Could not open this media file.\n\n" + error_text).c_str(), L"OMG Player", MB_ICONERROR);
        set_status(L"Open failed.");
        return;
    }

    g_pending_autoplay = true;
    set_status(L"Loading: " + g_current_name);
    update_control_state();
}

void show_open_dialog() {
    wchar_t file_name[MAX_PATH * 4]{};
    OPENFILENAMEW open_file_name{};
    open_file_name.lStructSize = sizeof(open_file_name);
    open_file_name.hwndOwner = g_main_window;
    open_file_name.lpstrFile = file_name;
    open_file_name.nMaxFile = static_cast<DWORD>(std::size(file_name));
    open_file_name.lpstrFilter =
        L"Media files\0*.mp4;*.m4v;*.mov;*.mkv;*.avi;*.wmv;*.mpg;*.mpeg;*.mp3;*.wav;*.wma;*.aac;*.m4a;*.flac;*.ogg\0"
        L"Video files\0*.mp4;*.m4v;*.mov;*.mkv;*.avi;*.wmv;*.mpg;*.mpeg\0"
        L"Audio files\0*.mp3;*.wav;*.wma;*.aac;*.m4a;*.flac;*.ogg\0"
        L"All files\0*.*\0";
    open_file_name.lpstrTitle = L"Open media";
    open_file_name.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (GetOpenFileNameW(&open_file_name)) {
        open_media_file(file_name);
    }
}

void open_adjacent_media(int direction) {
    if (g_current_path.empty()) {
        return;
    }

    const std::wstring directory = directory_from_path(g_current_path);
    const std::wstring pattern = directory + L"\\*";
    std::vector<std::wstring> media_paths;

    WIN32_FIND_DATAW find_data{};
    HANDLE find_handle = FindFirstFileW(pattern.c_str(), &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        const std::wstring full_path = directory + L"\\" + find_data.cFileName;
        if (is_media_file(full_path)) {
            media_paths.push_back(full_path);
        }
    } while (FindNextFileW(find_handle, &find_data));
    FindClose(find_handle);

    if (media_paths.empty()) {
        return;
    }

    std::sort(media_paths.begin(), media_paths.end(), [](const std::wstring& a, const std::wstring& b) {
        return lower_copy(file_name_from_path(a)) < lower_copy(file_name_from_path(b));
    });

    const std::wstring current_lower = lower_copy(g_current_path);
    auto current = std::find_if(media_paths.begin(), media_paths.end(), [&](const std::wstring& item) {
        return lower_copy(item) == current_lower;
    });
    if (current == media_paths.end()) {
        return;
    }

    const int current_index = static_cast<int>(std::distance(media_paths.begin(), current));
    const int count = static_cast<int>(media_paths.size());
    const int next_index = (current_index + direction + count) % count;
    if (next_index != current_index || count > 1) {
        open_media_file(media_paths[static_cast<size_t>(next_index)]);
    }
}

void layout_controls(HWND hwnd) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    const bool show_video = g_has_video;
    const bool show_controls = !show_video || g_controls_visible;
    const int control_bar_h = scale_px(76);
    const int status_h = scale_px(28);
    const int minimum_video_h = scale_px(180);
    const int bar_y = show_controls ? std::max(0, height - control_bar_h) : height;
    const int status_y = show_video ? std::max(0, bar_y - (show_controls ? status_h : 0)) : 0;
    const int status_height = show_video ? status_h : std::max(scale_px(34), bar_y);

    if (show_video) {
        const int video_h = show_controls ? std::max(minimum_video_h, status_y) : height;
        MoveWindow(g_video_window_hwnd, 0, 0, width, video_h, TRUE);
        ShowWindow(g_video_window_hwnd, SW_SHOW);
    } else {
        ShowWindow(g_video_window_hwnd, SW_HIDE);
    }

    HWND controls[] = {
        g_status_text,
        g_previous_button,
        g_rewind_10_button,
        g_progress_slider,
        g_forward_10_button,
        g_next_button,
        g_open_button,
        g_play_pause_button,
        g_stop_button,
        g_mute_button,
        g_time_text,
        g_volume_slider,
    };
    for (HWND control : controls) {
        ShowWindow(control, show_controls ? SW_SHOW : SW_HIDE);
    }
    if (!show_controls) {
        resize_video_window();
        return;
    }

    MoveWindow(g_status_text, scale_px(12), status_y, std::max(1, width - scale_px(24)), status_height, TRUE);
    const int progress_button_w = scale_px(52);
    const int progress_button_h = scale_px(28);
    const int progress_gap = scale_px(6);
    const int progress_outer_gap = scale_px(10);
    const int progress_button_y = bar_y + scale_px(4);
    const int progress_slider_y = bar_y + scale_px(9);
    const int previous_x = scale_px(12);
    const int rewind_x = previous_x + progress_button_w + progress_gap;
    const int next_x = width - scale_px(12) - progress_button_w;
    const int forward_x = next_x - progress_gap - progress_button_w;
    const int progress_x = rewind_x + progress_button_w + progress_outer_gap;
    const int progress_w = std::max(1, forward_x - progress_outer_gap - progress_x);

    MoveWindow(g_previous_button, previous_x, progress_button_y, progress_button_w, progress_button_h, TRUE);
    MoveWindow(g_rewind_10_button, rewind_x, progress_button_y, progress_button_w, progress_button_h, TRUE);
    MoveWindow(g_progress_slider, progress_x, progress_slider_y, progress_w, scale_px(18), TRUE);
    MoveWindow(g_forward_10_button, forward_x, progress_button_y, progress_button_w, progress_button_h, TRUE);
    MoveWindow(g_next_button, next_x, progress_button_y, progress_button_w, progress_button_h, TRUE);
    MoveWindow(g_open_button, scale_px(12), bar_y + scale_px(33), scale_px(38), scale_px(36), TRUE);
    MoveWindow(g_play_pause_button, scale_px(58), bar_y + scale_px(33), scale_px(38), scale_px(36), TRUE);
    MoveWindow(g_stop_button, scale_px(104), bar_y + scale_px(33), scale_px(38), scale_px(36), TRUE);
    MoveWindow(g_mute_button, scale_px(150), bar_y + scale_px(33), scale_px(38), scale_px(36), TRUE);
    MoveWindow(g_time_text, scale_px(198), bar_y + scale_px(40), scale_px(132), scale_px(22), TRUE);
    MoveWindow(g_volume_slider, std::max(scale_px(344), width - scale_px(154)), bar_y + scale_px(44), scale_px(136), scale_px(16), TRUE);
    resize_video_window();
}

bool point_in_client(HWND hwnd, POINT pt) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    return PtInRect(&rect, pt) != 0;
}

COLORREF mix_color(COLORREF a, COLORREF b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return RGB(
        static_cast<int>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
        static_cast<int>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
        static_cast<int>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

POINT client_origin_in_main(HWND hwnd) {
    POINT origin{0, 0};
    if (g_main_window != nullptr && hwnd != g_main_window) {
        MapWindowPoints(hwnd, g_main_window, &origin, 1);
    }
    return origin;
}

void paint_backdrop(HWND hwnd, HDC dc, const RECT& rect) {
    const COLORREF base = RGB(246, 248, 251);
    HBRUSH base_brush = CreateSolidBrush(base);
    FillRect(dc, &rect, base_brush);
    DeleteObject(base_brush);

    RECT main_rect{0, 0, rect.right - rect.left, rect.bottom - rect.top};
    if (g_main_window != nullptr) {
        GetClientRect(g_main_window, &main_rect);
    }
    const int canvas_w = std::max(1L, main_rect.right - main_rect.left);
    const int canvas_h = std::max(1L, main_rect.bottom - main_rect.top);
    const POINT origin = client_origin_in_main(hwnd);
    const double t = g_backdrop_tick / 60.0;

    static constexpr std::array<backdrop_patch, 7> patches{{
        {0.08, 0.12, 0.30, 0.26, RGB(96, 210, 150), RGB(89, 183, 255), 0.22, 0.5, 0.18, false},
        {0.52, 0.03, 0.36, 0.22, RGB(112, 225, 230), RGB(255, 222, 126), 0.16, 2.1, 0.14, true},
        {0.70, 0.45, 0.28, 0.30, RGB(255, 166, 170), RGB(96, 202, 234), 0.19, 4.0, 0.13, false},
        {0.18, 0.62, 0.34, 0.24, RGB(255, 232, 118), RGB(89, 205, 162), 0.15, 5.2, 0.12, true},
        {0.42, 0.72, 0.22, 0.20, RGB(86, 147, 255), RGB(118, 231, 218), 0.24, 1.4, 0.11, false},
        {0.01, 0.78, 0.20, 0.18, RGB(255, 180, 178), RGB(255, 225, 134), 0.18, 3.3, 0.10, true},
        {0.82, 0.18, 0.16, 0.18, RGB(94, 214, 178), RGB(93, 172, 255), 0.20, 6.1, 0.11, false},
    }};

    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, GetStockObject(NULL_PEN)));
    for (const backdrop_patch& patch : patches) {
        const double patch_w = std::max<double>(scale_px(120), canvas_w * patch.w);
        const double patch_h = std::max<double>(scale_px(90), canvas_h * patch.h);
        const double center_x = canvas_w * patch.x + std::sin(t * patch.speed + patch.phase) * scale_px(42) - origin.x;
        const double center_y = canvas_h * patch.y + std::cos(t * patch.speed * 0.8 + patch.phase) * scale_px(34) - origin.y;
        constexpr int layers = 26;
        for (int layer = layers; layer >= 1; --layer) {
            const double ratio = static_cast<double>(layer) / layers;
            const double softness = std::pow(1.0 - ratio, 1.7);
            const double mix = 0.5 + 0.5 * std::sin(t * patch.speed + patch.phase + ratio * 2.2);
            const COLORREF accent = mix_color(patch.from, patch.to, mix);
            const COLORREF color = mix_color(base, accent, patch.alpha * softness);
            HBRUSH brush = CreateSolidBrush(color);
            HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, brush));
            const int radius_x = static_cast<int>(patch_w * ratio);
            const int radius_y = static_cast<int>(patch_h * ratio);
            Ellipse(
                dc,
                static_cast<int>(center_x) - radius_x,
                static_cast<int>(center_y) - radius_y,
                static_cast<int>(center_x) + radius_x,
                static_cast<int>(center_y) + radius_y);
            SelectObject(dc, old_brush);
            DeleteObject(brush);
        }
    }
    SelectObject(dc, old_pen);
}

HFONT make_font(int point_size, int weight = FW_NORMAL, const wchar_t* face = L"Segoe UI") {
    return CreateFontW(
        -MulDiv(point_size, g_dpi, 72),
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        face);
}

void paint_flat_button(HWND hwnd, flat_button_state* state) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    HDC mem_dc = CreateCompatibleDC(dc);
    HBITMAP mem_bitmap = CreateCompatibleBitmap(dc, std::max(1, width), std::max(1, height));
    HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(mem_dc, mem_bitmap));
    paint_backdrop(hwnd, mem_dc, rect);

    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    COLORREF text_color = enabled ? RGB(34, 40, 52) : RGB(151, 160, 174);
    if (enabled && state != nullptr && state->hover) {
        text_color = RGB(12, 20, 32);
    }

    HRGN clip = CreateRoundRectRgn(0, 0, width + 1, height + 1, scale_px(button_radius_px * 2), scale_px(button_radius_px * 2));
    SelectClipRgn(mem_dc, clip);

    if (enabled && state != nullptr && state->ripple) {
        const int max_dx = std::max(state->ripple_origin.x, width - state->ripple_origin.x);
        const int max_dy = std::max(state->ripple_origin.y, height - state->ripple_origin.y);
        const int radius = static_cast<int>(std::sqrt(static_cast<double>(max_dx * max_dx + max_dy * max_dy)) * state->ripple_progress) + scale_px(3);
        HBRUSH ripple_brush = CreateSolidBrush(RGB(222, 230, 242));
        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(mem_dc, ripple_brush));
        HPEN old_pen = static_cast<HPEN>(SelectObject(mem_dc, GetStockObject(NULL_PEN)));
        Ellipse(mem_dc, state->ripple_origin.x - radius, state->ripple_origin.y - radius, state->ripple_origin.x + radius, state->ripple_origin.y + radius);
        SelectObject(mem_dc, old_pen);
        SelectObject(mem_dc, old_brush);
        DeleteObject(ripple_brush);
    }

    wchar_t text[128]{};
    GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
    HFONT font = state != nullptr && state->font != nullptr ? state->font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT old_font = static_cast<HFONT>(SelectObject(mem_dc, font));
    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, text_color);
    DrawTextW(mem_dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(mem_dc, old_font);
    SelectClipRgn(mem_dc, nullptr);
    DeleteObject(clip);

    BitBlt(dc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(mem_bitmap);
    DeleteDC(mem_dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK flat_button_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<flat_button_state*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new flat_button_state()));
        return TRUE;
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_SETFONT:
        if (state != nullptr) {
            state->font = reinterpret_cast<HFONT>(wparam);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETFONT:
        return reinterpret_cast<LRESULT>(state != nullptr ? state->font : nullptr);
    case WM_SETTEXT: {
        LRESULT result = DefWindowProcW(hwnd, msg, wparam, lparam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    case WM_MOUSEMOVE:
        note_pointer_activity();
        if (state != nullptr && !state->hover && IsWindowEnabled(hwnd)) {
            state->hover = true;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state != nullptr) {
            state->hover = false;
            state->pressed = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (state != nullptr && IsWindowEnabled(hwnd)) {
            SetFocus(hwnd);
            SetCapture(hwnd);
            state->pressed = true;
            state->ripple = true;
            state->ripple_progress = 0.0;
            state->ripple_origin = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            SetTimer(hwnd, timer_ripple, 16, nullptr);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state != nullptr && IsWindowEnabled(hwnd)) {
            const POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const bool click_inside = state->pressed && point_in_client(hwnd, pt);
            state->pressed = false;
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            if (click_inside) {
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_SPACE || wparam == 'P' || wparam == VK_LEFT || wparam == VK_RIGHT) {
            forward_key_to_main(wparam);
            return 0;
        }
        if (wparam == VK_RETURN && state != nullptr && IsWindowEnabled(hwnd)) {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            state->ripple = true;
            state->ripple_progress = 0.0;
            state->ripple_origin = POINT{(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};
            SetTimer(hwnd, timer_ripple, 16, nullptr);
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == timer_ripple && state != nullptr) {
            state->ripple_progress += 0.085;
            if (state->ripple_progress >= 1.0) {
                state->ripple_progress = 1.0;
                state->ripple = false;
                KillTimer(hwnd, timer_ripple);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_PAINT:
        paint_flat_button(hwnd, state);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int slider_pos_from_x(HWND hwnd, int x) {
    auto* state = slider_state_of(hwnd);
    if (state == nullptr) {
        return 0;
    }
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int pad = scale_px(8);
    const int usable = std::max(1, static_cast<int>((rect.right - rect.left) - pad * 2));
    const double ratio = std::clamp(static_cast<double>(x - pad) / usable, 0.0, 1.0);
    return state->min_value + static_cast<int>((state->max_value - state->min_value) * ratio + 0.5);
}

void notify_slider_changed(HWND hwnd, bool live) {
    auto* state = slider_state_of(hwnd);
    if (state != nullptr) {
        SendMessageW(GetParent(hwnd), wm_slider_changed, MAKEWPARAM(GetDlgCtrlID(hwnd), live ? 1 : 0), static_cast<LPARAM>(state->pos));
    }
}

void paint_flat_slider(HWND hwnd, flat_slider_state* state) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    HDC mem_dc = CreateCompatibleDC(dc);
    HBITMAP mem_bitmap = CreateCompatibleBitmap(dc, std::max(1, width), std::max(1, height));
    HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(mem_dc, mem_bitmap));
    paint_backdrop(hwnd, mem_dc, rect);

    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    const int pad = scale_px(8);
    const int track_h = scale_px(4);
    const int track_y = std::max(0, (height - track_h) / 2);
    const int track_right = std::max(pad + 1, width - pad);
    const double ratio = state == nullptr || state->max_value <= state->min_value ? 0.0 : static_cast<double>(state->pos - state->min_value) / (state->max_value - state->min_value);
    const int fill_right = pad + static_cast<int>((track_right - pad) * std::clamp(ratio, 0.0, 1.0));

    HBRUSH track_brush = CreateSolidBrush(enabled ? RGB(218, 225, 235) : RGB(232, 235, 240));
    HBRUSH fill_brush = CreateSolidBrush(enabled ? RGB(68, 92, 128) : RGB(174, 181, 191));
    HPEN old_pen = static_cast<HPEN>(SelectObject(mem_dc, GetStockObject(NULL_PEN)));
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(mem_dc, track_brush));
    RoundRect(mem_dc, pad, track_y, track_right, track_y + track_h, track_h, track_h);
    SelectObject(mem_dc, fill_brush);
    RoundRect(mem_dc, pad, track_y, std::max(pad + track_h, fill_right), track_y + track_h, track_h, track_h);

    if (enabled) {
        HBRUSH thumb_brush = CreateSolidBrush(RGB(24, 32, 44));
        SelectObject(mem_dc, thumb_brush);
        const int radius = state != nullptr && (state->hover || state->dragging) ? scale_px(6) : scale_px(5);
        Ellipse(mem_dc, fill_right - radius, height / 2 - radius, fill_right + radius, height / 2 + radius);
        DeleteObject(thumb_brush);
    }

    SelectObject(mem_dc, old_brush);
    SelectObject(mem_dc, old_pen);
    DeleteObject(track_brush);
    DeleteObject(fill_brush);
    BitBlt(dc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(mem_bitmap);
    DeleteDC(mem_dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK flat_slider_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = slider_state_of(hwnd);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new flat_slider_state()));
        return TRUE;
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_MOUSEMOVE:
        note_pointer_activity();
        if (state != nullptr && IsWindowEnabled(hwnd)) {
            if (!state->hover) {
                state->hover = true;
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
            }
            if (state->dragging) {
                state->pos = slider_pos_from_x(hwnd, GET_X_LPARAM(lparam));
                notify_slider_changed(hwnd, true);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state != nullptr) {
            state->hover = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (state != nullptr && IsWindowEnabled(hwnd)) {
            SetFocus(hwnd);
            SetCapture(hwnd);
            state->dragging = true;
            state->pos = slider_pos_from_x(hwnd, GET_X_LPARAM(lparam));
            notify_slider_changed(hwnd, true);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state != nullptr && IsWindowEnabled(hwnd)) {
            state->pos = slider_pos_from_x(hwnd, GET_X_LPARAM(lparam));
            state->dragging = false;
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            notify_slider_changed(hwnd, false);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_SPACE || wparam == 'P' || wparam == VK_LEFT || wparam == VK_RIGHT) {
            forward_key_to_main(wparam);
            return 0;
        }
        if (state != nullptr && IsWindowEnabled(hwnd)) {
            const int step = std::max(1, (state->max_value - state->min_value) / 100);
            if (wparam == VK_LEFT || wparam == VK_DOWN) {
                state->pos = std::max(state->min_value, state->pos - step);
                notify_slider_changed(hwnd, false);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wparam == VK_RIGHT || wparam == VK_UP) {
                state->pos = std::min(state->max_value, state->pos + step);
                notify_slider_changed(hwnd, false);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        break;
    case WM_PAINT:
        paint_flat_slider(hwnd, state);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void paint_flat_text(HWND hwnd, flat_text_state* state) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    HDC mem_dc = CreateCompatibleDC(dc);
    HBITMAP mem_bitmap = CreateCompatibleBitmap(dc, std::max(1, width), std::max(1, height));
    HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(mem_dc, mem_bitmap));
    paint_backdrop(hwnd, mem_dc, rect);

    wchar_t text[MAX_PATH * 4]{};
    GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
    HFONT font = state != nullptr && state->font != nullptr ? state->font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT old_font = static_cast<HFONT>(SelectObject(mem_dc, font));

    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    UINT flags = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    if ((style & SS_CENTER) == SS_CENTER) {
        flags |= DT_CENTER;
    } else {
        flags |= DT_LEFT;
        rect.left += scale_px(2);
    }

    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, RGB(38, 45, 58));
    DrawTextW(mem_dc, text, -1, &rect, flags);
    SelectObject(mem_dc, old_font);

    BitBlt(dc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(mem_bitmap);
    DeleteDC(mem_dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK flat_text_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<flat_text_state*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new flat_text_state()));
        return TRUE;
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFONT:
        if (state != nullptr) {
            state->font = reinterpret_cast<HFONT>(wparam);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETFONT:
        return reinterpret_cast<LRESULT>(state != nullptr ? state->font : nullptr);
    case WM_SETTEXT: {
        LRESULT result = DefWindowProcW(hwnd, msg, wparam, lparam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    case WM_MOUSEMOVE:
        note_pointer_activity();
        return 0;
    case WM_PAINT:
        paint_flat_text(hwnd, state);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void paint_video_placeholder(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    HBRUSH brush = CreateSolidBrush(RGB(15, 17, 21));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(175, 184, 198));
    HFONT font = make_font(18, FW_SEMIBOLD);
    HFONT old_font = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, L"OMG Player", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old_font);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK video_surface_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_MOUSEMOVE:
        note_pointer_activity();
        return 0;
    case WM_PAINT:
        if (!g_loaded) {
            paint_video_placeholder(hwnd);
            return 0;
        }
        break;
    case WM_LBUTTONDBLCLK:
        SendMessageW(g_main_window, WM_SYSCOMMAND, IsZoomed(g_main_window) ? SC_RESTORE : SC_MAXIMIZE, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND create_flat_button(HWND parent, int id, const wchar_t* text) {
    return CreateWindowW(flat_button_class, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(id), nullptr, nullptr);
}

HWND create_flat_slider(HWND parent, int id) {
    return CreateWindowW(flat_slider_class, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(id), nullptr, nullptr);
}

HWND create_flat_text(HWND parent, const wchar_t* text, DWORD style) {
    return CreateWindowW(flat_text_class, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent, nullptr, nullptr, nullptr);
}

void register_window_classes(HINSTANCE instance) {
    WNDCLASSW button_class{};
    button_class.lpfnWndProc = flat_button_proc;
    button_class.hInstance = instance;
    button_class.hCursor = LoadCursorW(nullptr, IDC_HAND);
    button_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    button_class.lpszClassName = flat_button_class;
    RegisterClassW(&button_class);

    WNDCLASSW slider_class{};
    slider_class.lpfnWndProc = flat_slider_proc;
    slider_class.hInstance = instance;
    slider_class.hCursor = LoadCursorW(nullptr, IDC_HAND);
    slider_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    slider_class.lpszClassName = flat_slider_class;
    RegisterClassW(&slider_class);

    WNDCLASSW text_class{};
    text_class.lpfnWndProc = flat_text_proc;
    text_class.hInstance = instance;
    text_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    text_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    text_class.lpszClassName = flat_text_class;
    RegisterClassW(&text_class);

    WNDCLASSW video_class{};
    video_class.lpfnWndProc = video_surface_proc;
    video_class.hInstance = instance;
    video_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    video_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    video_class.lpszClassName = video_surface_class;
    RegisterClassW(&video_class);
}

void create_ui(HWND hwnd) {
    HFONT icon_font = make_font(19, FW_NORMAL, L"Segoe UI Emoji");
    HFONT small_font = make_font(10);
    HFONT status_font = make_font(16, FW_SEMIBOLD);

    g_open_button = create_flat_button(hwnd, id_open, L"\U0001F4C2\uFE0F");
    g_play_pause_button = create_flat_button(hwnd, id_play_pause, L"\u25B6\uFE0F");
    g_stop_button = create_flat_button(hwnd, id_stop, L"\u23F9\uFE0F");
    g_mute_button = create_flat_button(hwnd, id_mute, L"\U0001F50A\uFE0F");
    g_previous_button = create_flat_button(hwnd, id_previous, L"\U0001F448\uFE0F");
    g_rewind_10_button = create_flat_button(hwnd, id_rewind_10, L"\u21A9\uFE0F\U0001F51F");
    g_forward_10_button = create_flat_button(hwnd, id_forward_10, L"\U0001F51F\u21AA\uFE0F");
    g_next_button = create_flat_button(hwnd, id_next, L"\U0001F449\uFE0F");
    g_progress_slider = create_flat_slider(hwnd, id_progress);
    g_time_text = create_flat_text(hwnd, L"00:00 / 00:00", SS_CENTER);
    g_volume_slider = create_flat_slider(hwnd, id_volume);
    g_status_text = create_flat_text(hwnd, L"Open a media file with OMG Player, or drop one here.", SS_LEFT);
    g_video_window_hwnd = CreateWindowW(video_surface_class, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);

    HWND icon_controls[] = {g_open_button, g_play_pause_button, g_stop_button, g_mute_button, g_previous_button, g_rewind_10_button, g_forward_10_button, g_next_button};
    for (HWND control : icon_controls) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font), TRUE);
    }
    SendMessageW(g_time_text, WM_SETFONT, reinterpret_cast<WPARAM>(small_font), TRUE);
    SendMessageW(g_status_text, WM_SETFONT, reinterpret_cast<WPARAM>(status_font), TRUE);

    set_slider_range(g_progress_slider, 0, 1);
    set_slider_pos(g_progress_slider, 0);
    set_slider_range(g_volume_slider, 0, 100);
    set_slider_pos(g_volume_slider, g_last_volume / 10);
    DragAcceptFiles(hwnd, TRUE);
    update_control_state();
}

void handle_command(int id) {
    switch (id) {
    case id_open:
        show_open_dialog();
        break;
    case id_play_pause:
        toggle_play_pause();
        break;
    case id_stop:
        stop_media();
        break;
    case id_mute:
        set_player_volume(g_muted ? g_last_volume : 0);
        break;
    case id_previous:
        open_adjacent_media(-1);
        break;
    case id_rewind_10:
        seek_to(current_position_ms() - 10000);
        refresh_progress();
        break;
    case id_forward_10:
        seek_to(current_position_ms() + 10000);
        refresh_progress();
        break;
    case id_next:
        open_adjacent_media(1);
        break;
    default:
        break;
    }
}

void handle_key(WPARAM key) {
    note_keyboard_activity();
    const bool ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    switch (key) {
    case VK_SPACE:
    case 'P':
        toggle_play_pause();
        break;
    case 'O':
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            show_open_dialog();
        }
        break;
    case VK_LEFT:
        if (ctrl_down) {
            open_adjacent_media(-1);
        } else {
            seek_to(current_position_ms() - 15000);
            refresh_progress();
        }
        break;
    case VK_RIGHT:
        if (ctrl_down) {
            open_adjacent_media(1);
        } else {
            seek_to(current_position_ms() + 30000);
            refresh_progress();
        }
        break;
    case VK_UP:
        set_player_volume(std::min(1000, get_slider_pos(g_volume_slider) * 10 + 50));
        break;
    case VK_DOWN:
        set_player_volume(std::max(0, get_slider_pos(g_volume_slider) * 10 - 50));
        break;
    default:
        break;
    }
}

void invalidate_backdrop_surfaces() {
    InvalidateRect(g_main_window, nullptr, FALSE);
    HWND controls[] = {g_open_button, g_play_pause_button, g_stop_button, g_mute_button, g_previous_button, g_rewind_10_button, g_forward_10_button, g_next_button, g_progress_slider, g_volume_slider, g_status_text, g_time_text};
    for (HWND control : controls) {
        if (control != nullptr) {
            InvalidateRect(control, nullptr, FALSE);
        }
    }
}

void recreate_fonts() {
    HFONT icon_font = make_font(19, FW_NORMAL, L"Segoe UI Emoji");
    HFONT small_font = make_font(10);
    HFONT status_font = make_font(16, FW_SEMIBOLD);
    HWND icon_controls[] = {g_open_button, g_play_pause_button, g_stop_button, g_mute_button, g_previous_button, g_rewind_10_button, g_forward_10_button, g_next_button};
    for (HWND control : icon_controls) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font), TRUE);
    }
    SendMessageW(g_time_text, WM_SETFONT, reinterpret_cast<WPARAM>(small_font), TRUE);
    SendMessageW(g_status_text, WM_SETFONT, reinterpret_cast<WPARAM>(status_font), TRUE);
}

LRESULT CALLBACK main_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE:
        g_main_window = hwnd;
        g_dpi = dpi_for_window(hwnd);
        create_ui(hwnd);
        layout_controls(hwnd);
        SetTimer(hwnd, timer_backdrop, 50, nullptr);
        SetTimer(hwnd, timer_control_hide, 250, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        paint_backdrop(hwnd, dc, rect);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wparam);
        recreate_fonts();
        const RECT* suggested_rect = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(hwnd, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout_controls(hwnd);
        return 0;
    }
    case WM_SIZE:
        layout_controls(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        note_pointer_activity();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = scale_px(520);
        info->ptMinTrackSize.y = g_has_video ? scale_px(360) : scale_px(154);
        return 0;
    }
    case WM_COMMAND:
        handle_command(LOWORD(wparam));
        return 0;
    case wm_slider_changed: {
        const int id = LOWORD(wparam);
        const bool live = HIWORD(wparam) != 0;
        const int pos = static_cast<int>(lparam);
        if (id == id_volume) {
            set_player_volume(pos * 10);
        } else if (id == id_progress && g_loaded) {
            if (live) {
                g_tracking_progress = true;
                set_time_text(pos);
            } else {
                g_tracking_progress = false;
                if (!g_updating_progress) {
                    seek_to(pos);
                    refresh_progress();
                }
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wparam == timer_progress) {
            refresh_progress();
        } else if (wparam == timer_backdrop) {
            ++g_backdrop_tick;
            invalidate_backdrop_surfaces();
        } else if (wparam == timer_control_hide) {
            const DWORD now = GetTickCount();
            if (g_has_video && g_controls_visible && now - g_last_pointer_activity >= control_hide_delay_ms) {
                remember_pointer_position();
                set_control_bar_visible(false);
            }
        }
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        wchar_t path[MAX_PATH * 4]{};
        if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path)))) {
            open_media_file(path);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_KEYDOWN:
        handle_key(wparam);
        return 0;
    case wm_player_event:
        if (wparam == player_event_ready) {
            handle_media_ready();
        } else if (wparam == player_event_error) {
            handle_media_error(static_cast<HRESULT>(lparam));
        } else if (wparam == player_event_ended) {
            g_playing = false;
            refresh_progress();
            set_status(media_title_text());
            update_control_state();
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, timer_backdrop);
        KillTimer(hwnd, timer_control_hide);
        close_media();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void enable_high_dpi() {
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32 != nullptr) {
        using set_process_dpi_awareness_context_proc = BOOL(WINAPI*)(HANDLE);
        FARPROC proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        set_process_dpi_awareness_context_proc set_process_dpi_awareness_context = nullptr;
        std::memcpy(&set_process_dpi_awareness_context, &proc, sizeof(set_process_dpi_awareness_context));
        if (set_process_dpi_awareness_context != nullptr) {
            set_process_dpi_awareness_context(reinterpret_cast<HANDLE>(-4));
            FreeLibrary(user32);
            return;
        }
        FreeLibrary(user32);
    }
    SetProcessDPIAware();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int show_command) {
    enable_high_dpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&common_controls);

    register_window_classes(instance);

    WNDCLASSW main_class{};
    main_class.lpfnWndProc = main_window_proc;
    main_class.hInstance = instance;
    main_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    main_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(app_icon_id));
    if (main_class.hIcon == nullptr) {
        main_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    main_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    main_class.lpszClassName = L"OMGPlayerMainWindow";
    RegisterClassW(&main_class);

    g_dpi = system_dpi();
    RECT initial_rect{0, 0, scale_px(760), scale_px(154)};
    AdjustWindowRectEx(&initial_rect, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0,
        main_class.lpszClassName,
        L"OMG Player",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        initial_rect.right - initial_rect.left,
        initial_rect.bottom - initial_rect.top,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (hwnd == nullptr) {
        MFShutdown();
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (!arg.empty() && arg[0] != L'-' && GetFileAttributesW(arg.c_str()) != INVALID_FILE_ATTRIBUTES) {
                open_media_file(arg);
                break;
            }
        }
        LocalFree(argv);
    }
    (void)command_line;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    MFShutdown();
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
