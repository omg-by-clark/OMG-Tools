#define PSAPI_VERSION 1
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
using namespace std;

// 用于存储上次会话的值
struct last_session_t {
  string file_name;
  string mode;
  string input_data;
  string expected_output;
  string input_file_name;
  string expected_file_name;
  bool input_from_file = false;
  bool expected_from_file = false;
  bool strict_compare = false;
  bool has_record = false;
} current_session;

string work_file;
string input_file;
string output_file;

void set_color(int color) {
  SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
  /*
  0 黑色     9 亮蓝色
  1 深蓝色   10 亮绿色
  2 深绿色   11 亮青色
  3 深青色   12 亮红色
  4 深红色   13 亮紫色
  5 深紫色   14 亮黄色
  6 深黄色   15 亮白色
  7 浅灰色
  8 暗灰色
*/
}

const int color_black = 0;  // 新增：黑色，用于隐形空格点
const int color_green = 10;
const int color_light_blue = 11;
const int color_red = 12;
const int color_light_purple = 13;  // 浅紫色，用于 TLE 和 MLE
const int color_yellow = 14;
const int color_white = 15;
const int color_dark_gray = 8;

string gpp_path;
string gcc_path;
double max_time_limit = 1.0;  // 全局时间限制（秒），默认 1s
size_t max_memory_limit =
    256 * 1024 * 1024;  // 全局内存限制（字节），默认 256MB

template <typename t>
void println(const t& text) {
  cout << text << "\n";
}

// 识别 Windows 退出代码的含义，用于 WA 时判断程序是否非正常退出
string get_exit_reason(DWORD code) {
  switch (code) {
    case 0:
      return "正常退出";
    case 1:
      return "被系统或工具强制停止 (超时或内存超限)";
    case 0xC0000005:
      return "内存访问越界 (Segmentation Fault)";
    case 0xC00000FD:
      return "栈溢出 (Stack Overflow)";
    case 0xC0000094:
      return "除以零异常 (Integer Division by Zero)";
    case 0x40010004:
      return "被用户停止 (Ctrl+C)";
    case 0xC0000025:
      return "无法继续执行的非连续异常";
    case 0xC0000026:
      return "无效的异常处理";
    case 0xC000001D:
      return "非法指令";
    default:
      return "未知或未定义的错误代码";
  }
}

// 计算两个字符串的 Levenshtein 距离，用于拼写纠错
int get_edit_distance(const string& s1, const string& s2) {
  int len1 = s1.size(), len2 = s2.size();
  vector<vector<int>> dp(len1 + 1, vector<int>(len2 + 1));
  for (int i = 0; i <= len1; ++i) dp[i][0] = i;
  for (int j = 0; j <= len2; ++j) dp[0][j] = j;
  for (int i = 1; i <= len1; ++i) {
    for (int j = 1; j <= len2; ++j) {
      if (s1[i - 1] == s2[j - 1])
        dp[i][j] = dp[i - 1][j - 1];
      else
        dp[i][j] = min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]}) + 1;
    }
  }
  return dp[len1][len2];
}

string to_lower_string(string s) {
  for (char& c : s) {
    c = (char)tolower((unsigned char)c);
  }
  return s;
}

bool is_subsequence(const string& small, const string& big) {
  size_t j = 0;
  for (char c : big) {
    if (j < small.size() && small[j] == c) ++j;
  }
  return j == small.size();
}

// 查找最接近的有效指令，最多返回 5 个候选，但不替用户执行
vector<string> get_command_suggestions(const string& cmd) {
  vector<string> valid_cmds = {"exit", "refresh", "version", "tlimit",
                               "mlimit", "set_work_file", "sw",
                               "set_input_file", "si", "set_output_file",
                               "so", "test", "run", "compile", "cls"};
  string needle = to_lower_string(cmd);
  vector<pair<int, string>> ranked;

  for (const string& v : valid_cmds) {
    string target = to_lower_string(v);
    if (needle == target) continue;

    int score = 100000;
    if (target.find(needle) == 0) {
      score = (int)target.size() - (int)needle.size();
    } else if (needle.size() == 1 && target.size() <= 4 &&
               target.find(needle) != string::npos) {
      score = 20 + (int)target.find(needle) * 3 + (int)target.size();
    } else if (needle.size() >= 2 && target.find(needle) != string::npos) {
      score = 40 + (int)target.find(needle) * 3 + (int)target.size();
    } else if (needle.size() >= 2 && is_subsequence(needle, target)) {
      score = 55 + (int)target.size();
    } else if (needle.size() >= 2 &&
               abs((int)needle.size() - (int)target.size()) <= 2) {
      int dist = get_edit_distance(needle, target);
      int limit = (needle.size() <= 3) ? 1 : 2;
      if (dist <= limit) score = 70 + dist * 10 + (int)target.size();
    }

    if (score < 100000) ranked.push_back(make_pair(score, v));
  }

  sort(ranked.begin(), ranked.end());
  vector<string> result;
  for (size_t i = 0; i < ranked.size() && result.size() < 5; ++i) {
    result.push_back(ranked[i].second);
  }
  return result;
}

void print_command_with_match(const string& cmd, const string& suggestion) {
  string needle = to_lower_string(cmd);
  string target = to_lower_string(suggestion);
  size_t j = 0;
  for (size_t i = 0; i < suggestion.size(); ++i) {
    if (j < needle.size() && target[i] == needle[j]) {
      set_color(color_light_purple);
      cout << suggestion[i];
      ++j;
    } else {
      set_color(color_light_blue);
      cout << suggestion[i];
    }
  }
  set_color(color_white);
}

void print_command_suggestions(const string& cmd) {
  vector<string> suggestions = get_command_suggestions(cmd);
  if (suggestions.empty()) return;

  set_color(color_dark_gray);
  cout << "候选指令:" << endl;
  for (const string& suggestion : suggestions) {
    cout << "  ";
    print_command_with_match(cmd, suggestion);
    cout << endl;
  }
  set_color(color_white);
}

// 自动在 Path 中寻找编译器
void refresh_compilers(bool show_output = false) {
  char buffer[MAX_PATH];
  char* file_part;

  DWORD res_gpp =
      SearchPathA(NULL, "g++.exe", NULL, MAX_PATH, buffer, &file_part);
  if (res_gpp > 0 && res_gpp < MAX_PATH) {
    gpp_path = buffer;
    if (show_output) cout << "找到 g++: " << gpp_path << endl;
  } else {
    gpp_path = "g++";
    if (show_output) cout << "未在 Path 中找到 g++.exe，将使用默认命令" << endl;
  }

  DWORD res_gcc =
      SearchPathA(NULL, "gcc.exe", NULL, MAX_PATH, buffer, &file_part);
  if (res_gcc > 0 && res_gcc < MAX_PATH) {
    gcc_path = buffer;
    if (show_output) cout << "找到 gcc: " << gcc_path << endl;
  } else {
    gcc_path = "gcc";
    if (show_output) cout << "未在 Path 中找到 gcc.exe，将使用默认命令" << endl;
  }
}

string get_extension(const string& file_name) {
  size_t pos = file_name.rfind('.');
  if (pos != string::npos) {
    return file_name.substr(pos + 1);
  }
  return "";
}

bool file_exists(const string& file_name) {
  ifstream file(file_name.c_str());
  return file.good();
}

// 读取文件全部内容
bool read_file_content(const string& file_name, string& content) {
  ifstream file(file_name.c_str());
  if (!file.is_open()) return false;
  stringstream ss;
  ss << file.rdbuf();
  content = ss.str();
  if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
      (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) {
    content.erase(0, 3);
  }
  file.close();
  return true;
}

// 编译，错误信息存入 error_msg，成功返回 true
bool compile_code(const string& file_name, string& error_msg) {
  string ext = get_extension(file_name);
  string command;

  size_t pos = file_name.rfind('.');
  string base_name =
      (pos != string::npos) ? file_name.substr(0, pos) : file_name;

  if (ext == "cpp") {
    command = "\"" + gpp_path + "\" -O2 \"" + file_name + "\" -o \"" +
              base_name + ".exe\"";
  } else if (ext == "c") {
    command = "\"" + gcc_path + "\" -O2 \"" + file_name + "\" -o \"" +
              base_name + ".exe\"";
  } else {
    command = "\"" + gpp_path + "\" -O2 \"" + file_name + "\" -o \"" +
              file_name + ".exe\"";
  }

  string error_file = "_compile_err_temp.txt";
  string full_command = "\"" + command + " 2> " + error_file + "\"";
  int result = system(full_command.c_str());

  ifstream err_in(error_file.c_str());
  if (err_in.is_open()) {
    stringstream ss;
    ss << err_in.rdbuf();
    error_msg = ss.str();
    err_in.close();
    remove(error_file.c_str());
  } else {
    error_msg = "";
  }

  if (result != 0 && error_msg.empty()) {
    error_msg = "编译失败，未捕获到具体错误信息。请检查编译器路径或代码语法。";
  }

  return (result == 0);
}

// 采用 Windows API 重新实现的运行与捕获，支持强制 TLE 和 MLE
// 中断，同时返回峰值内存
string run_and_capture(const string& exe_path, const string& input,
                       bool& is_tle, bool& is_mle, DWORD& exit_code,
                       double time_limit_sec, size_t& peak_memory) {
  is_tle = false;
  is_mle = false;
  exit_code = 0;
  peak_memory = 0;  // 初始化峰值内存记录
  string result;

  HANDLE h_stdout_read, h_stdout_write;
  HANDLE h_stdin_read, h_stdin_write;
  SECURITY_ATTRIBUTES sa_attr;
  sa_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa_attr.bInheritHandle = TRUE;
  sa_attr.lpSecurityDescriptor = NULL;

  if (!CreatePipe(&h_stdout_read, &h_stdout_write, &sa_attr, 0)) return "";
  SetHandleInformation(h_stdout_read, HANDLE_FLAG_INHERIT, 0);

  if (!CreatePipe(&h_stdin_read, &h_stdin_write, &sa_attr, 0)) return "";
  SetHandleInformation(h_stdin_write, HANDLE_FLAG_INHERIT, 0);

  // 写入输入数据
  if (input != "-" && !input.empty()) {
    DWORD written;
    WriteFile(h_stdin_write, input.c_str(), input.length(), &written, NULL);
  }
  // 必须关闭写端，否则如果子进程等待输入会卡死
  CloseHandle(h_stdin_write);

  PROCESS_INFORMATION pi_proc_info;
  STARTUPINFO si_start_info;
  ZeroMemory(&pi_proc_info, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&si_start_info, sizeof(STARTUPINFO));
  si_start_info.cb = sizeof(STARTUPINFO);
  si_start_info.hStdError = h_stdout_write;
  si_start_info.hStdOutput = h_stdout_write;
  si_start_info.hStdInput = h_stdin_read;
  si_start_info.dwFlags |= STARTF_USESTDHANDLES;

  string cmd_line = "\"" + exe_path + "\"";
  vector<char> cmd_buffer(cmd_line.begin(), cmd_line.end());
  cmd_buffer.push_back('\0');

  // 创建子进程
  bool success = CreateProcessA(NULL, cmd_buffer.data(), NULL, NULL, TRUE, 0,
                                NULL, NULL, &si_start_info, &pi_proc_info);

  // 父进程不需要留着 stdout 的写端和 stdin 的读端
  CloseHandle(h_stdout_write);
  CloseHandle(h_stdin_read);

  if (!success) {
    CloseHandle(h_stdout_read);
    return "";
  }

  // 实施制裁：等待进程结束或超时，通过轮询检查内存
  DWORD timeout_ms =
      (time_limit_sec > 0.0) ? (DWORD)(time_limit_sec * 1000.0) : INFINITE;
  DWORD start_tick = GetTickCount();

  while (true) {
    DWORD wait_res =
        WaitForSingleObject(pi_proc_info.hProcess, 50);  // 每 50ms 轮询检查一次
    if (wait_res == WAIT_OBJECT_0) {
      break;  // 程序正常或异常退出
    }

    // 检查当前物理内存使用情况并更新峰值
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(pi_proc_info.hProcess, &pmc, sizeof(pmc))) {
      // 记录系统维护的峰值内存（PeakWorkingSetSize
      // 能够记录程序的历史最高使用量）
      if (pmc.PeakWorkingSetSize > peak_memory) {
        peak_memory = pmc.PeakWorkingSetSize;
      }
      // 实时制裁内存超限
      if (pmc.WorkingSetSize > max_memory_limit) {
        TerminateProcess(pi_proc_info.hProcess, 1);
        is_mle = true;
        break;
      }
    }

    // 检查时间是否超时
    if (timeout_ms != INFINITE && (GetTickCount() - start_tick) >= timeout_ms) {
      TerminateProcess(pi_proc_info.hProcess, 1);
      is_tle = true;
      break;
    }
  }

  // 程序退出后，最后再检查一次峰值内存，确保没漏掉瞬间的内存爆炸
  PROCESS_MEMORY_COUNTERS pmc_final;
  if (GetProcessMemoryInfo(pi_proc_info.hProcess, &pmc_final,
                           sizeof(pmc_final))) {
    if (pmc_final.PeakWorkingSetSize > peak_memory) {
      peak_memory = pmc_final.PeakWorkingSetSize;
    }
  }

  // 获取子进程的退出代码
  GetExitCodeProcess(pi_proc_info.hProcess, &exit_code);

  // 读取已经输出的残骸数据
  DWORD bytes_read;
  char buffer[4096];
  while (
      ReadFile(h_stdout_read, buffer, sizeof(buffer) - 1, &bytes_read, NULL) &&
      bytes_read > 0) {
    buffer[bytes_read] = '\0';
    result += buffer;
  }

  CloseHandle(h_stdout_read);
  CloseHandle(pi_proc_info.hProcess);
  CloseHandle(pi_proc_info.hThread);

  while (result.length() > 0 && (result[result.length() - 1] == '\n' ||
                                 result[result.length() - 1] == '\r')) {
    result.erase(result.length() - 1);
  }
  return result;
}

// 探测并输出 C++ 版本
void print_cpp_version() {
  ofstream out("_check_ver.cpp");
  out << "#include <iostream>\nint main(){std::cout<<__cplusplus;return 0;}";
  out.close();

  string cmd = "\"" + gpp_path + "\" _check_ver.cpp -o _check_ver.exe";
  system(("\"" + cmd + " > nul 2>&1\"").c_str());

  bool dummy_tle = false;
  bool dummy_mle = false;
  DWORD dummy_exit = 0;
  size_t dummy_peak = 0;  // 捕获检测版本时的无用内存占用数据
  string res = run_and_capture("_check_ver.exe", "", dummy_tle, dummy_mle,
                               dummy_exit, 0.0, dummy_peak);

  remove("_check_ver.cpp");
  remove("_check_ver.exe");

  string version_str = "未知";
  if (res == "199711")
    version_str = "C++ 98";
  else if (res == "201103")
    version_str = "C++ 11";
  else if (res == "201402")
    version_str = "C++ 14";
  else if (res == "201703")
    version_str = "C++ 17";
  else if (res == "202002")
    version_str = "C++ 20";
  else if (res == "202302")
    version_str = "C++ 23";
  else if (res > "202302")
    version_str = "C++ 26 (或更新)";
  else if (!res.empty())
    version_str = "未知版本 (" + res + ")";

  set_color(color_light_blue);
  if (version_str == "未知") {
    cout << "无法获取 C++ 版本，请检查编译器是否正常工作。" << endl;
  } else {
    cout << version_str << endl;
  }
  set_color(color_white);
}

// 在新窗口中运行程序
void run_in_new_window(const string& exe_path) {
  string run_cmd =
      "start \"\" cmd /k \"\"" + exe_path +
      "\" & echo. & echo 程序已结束，按任意键关闭此窗口... & pause > nul\"";
  system(run_cmd.c_str());
}

// 去除字符串首尾空白
string trim_string(const string& s) {
  size_t start = s.find_first_not_of(" \t\n\r");
  if (start == string::npos) return "";
  size_t end = s.find_last_not_of(" \t\n\r");
  return s.substr(start, end - start + 1);
}

string normalize_output_loose(const string& s) {
  string result;
  size_t i = 0;
  while (i < s.size()) {
    string line;
    while (i < s.size() && s[i] != '\n' && s[i] != '\r') {
      line += s[i++];
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    result += line;

    if (i < s.size()) {
      if (s[i] == '\r') {
        ++i;
        if (i < s.size() && s[i] == '\n') ++i;
      } else if (s[i] == '\n') {
        ++i;
      }
      result += '\n';
    }
  }

  while (!result.empty() &&
         (result.back() == '\n' || result.back() == ' ' ||
          result.back() == '\t')) {
    result.pop_back();
  }
  return result;
}

bool output_matches(const string& expected_output, const string& actual_output,
                    bool strict_compare) {
  string expected = (expected_output == "-") ? "" : expected_output;
  if (strict_compare) return expected == actual_output;
  return normalize_output_loose(expected) == normalize_output_loose(actual_output);
}

bool is_strict_flag(const string& arg) {
  return arg == "--strict" || arg == "-s";
}

string get_exe_path(const string& file_name) {
  size_t pos = file_name.rfind('.');
  string base_name =
      (pos != string::npos) ? file_name.substr(0, pos) : file_name;
  return base_name + ".exe";
}

bool require_work_file(string& file_name) {
  if (work_file.empty()) {
    set_color(color_red);
    cout << "请先使用 set_work_file 或 sw 设置工作文件，例如: sw 1.cpp" << endl;
    set_color(color_white);
    return false;
  }
  if (!file_exists(work_file)) {
    set_color(color_red);
    cout << "工作文件不存在: " << work_file << endl;
    set_color(color_white);
    return false;
  }
  file_name = work_file;
  return true;
}

bool read_named_file(const string& file_name, const string& label,
                     string& content) {
  if (file_name.empty()) {
    set_color(color_red);
    cout << "请先设置" << label << "文件" << endl;
    set_color(color_white);
    return false;
  }
  if (!read_file_content(file_name, content)) {
    set_color(color_red);
    cout << "无法读取" << label << "文件: " << file_name << endl;
    set_color(color_white);
    return false;
  }
  return true;
}

void execute_debug_command(const string& file_name, const string& mode,
                           const string& input_data = "",
                           const string& expected_output = "",
                           bool input_from_file = false,
                           const string& input_file_name = "",
                           bool expected_from_file = false,
                           const string& expected_file_name = "",
                           bool strict_compare = false) {
  current_session.file_name = file_name;
  current_session.mode = mode;
  if (mode == "-t") {
    current_session.input_data = input_data;
    current_session.expected_output = expected_output;
    current_session.input_from_file = input_from_file;
    current_session.input_file_name = input_file_name;
    current_session.expected_from_file = expected_from_file;
    current_session.expected_file_name = expected_file_name;
    current_session.strict_compare = strict_compare;
  }
  current_session.has_record = true;

  set_color(color_green);
  cout << "文件识别成功，准备编译..." << endl;
  set_color(color_white);

  string error_msg;
  if (!compile_code(file_name, error_msg)) {
    set_color(color_yellow);
    cout << "Compile Error" << endl;
    cout << error_msg << endl;
    set_color(color_white);
    return;
  }

  set_color(color_green);
  cout << "✓ 编译成功！" << endl;
  set_color(color_white);

  string exe_path = get_exe_path(file_name);

  if (mode == "-r") {
    cout << "正在新窗口中启动程序..." << endl;
    run_in_new_window(exe_path);
    return;
  }

  if (mode == "-c") {
    cout << "仅编译完成，生成可执行文件: " << exe_path << endl;
    return;
  }

  if (mode != "-t") return;

  cout << "运行程序进行测试..." << endl;

  // 记录开始时间
  auto start_time = std::chrono::high_resolution_clock::now();

  bool is_tle = false;
  bool is_mle = false;
  DWORD exit_code = 0;
  size_t peak_memory = 0;  // 定义变量捕获峰值内存
  string actual_output =
      run_and_capture(exe_path, input_data, is_tle, is_mle, exit_code,
                      max_time_limit, peak_memory);

  // 记录结束时间并计算耗时
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         end_time - start_time)
                         .count();

  bool is_ac = output_matches(expected_output, actual_output, strict_compare);

  // 将捕获到的字节数转换为 KB 以供显示
  size_t mem_kb = peak_memory / 1024;

  // MLE 判定
  if (is_mle) {
    set_color(color_light_purple);
    cout << "Memory Limit Exceeded" << endl;
    cout << "超出内存限制! 内存用量: " << mem_kb
         << " KB / 规定内存: " << (max_memory_limit / 1024) << " KB (用时："
         << duration_ms << " ms)" << endl;
    set_color(color_white);
  }
  // TLE 判定
  else if (is_tle) {
    set_color(color_light_purple);
    cout << "Time Limit Exceeded" << endl;
    cout << "超出限时! 实际耗时: " << duration_ms
         << " ms / 规定限时: " << (max_time_limit * 1000)
         << " ms (内存用量：" << mem_kb << " KB)" << endl;
    set_color(color_white);
  }
  // AC 判定
  else if (is_ac) {
    set_color(color_green);
    cout << "Accepted (耗时: " << duration_ms << " ms) (内存用量："
         << mem_kb << " KB)" << endl;
    set_color(color_white);
  }
  // WA 判定
  else {
    set_color(color_red);
    cout << "Wrong Answer (耗时: " << duration_ms << " ms) (内存用量："
         << mem_kb << " KB)" << endl;

    // 识别非正常退出
    if (exit_code != 0) {
      set_color(color_yellow);
      cout << "程序异常退出! 退出代码: 0x" << hex << exit_code << dec << " ("
           << get_exit_reason(exit_code) << ")" << endl;
    }

    set_color(color_red);
    cout << "预期输出:" << endl;
    for (char c : expected_output) {
      if (c == ' ') {
        set_color(color_black);
        cout << "·";
        set_color(color_red);
      } else {
        cout << c;
      }
    }
    cout << endl;

    cout << "实际输出:" << endl;
    for (char c : actual_output) {
      if (c == ' ') {
        set_color(color_black);
        cout << "·";
        set_color(color_red);
      } else {
        cout << c;
      }
    }
    cout << endl;

    set_color(color_white);
  }
}

bool rerun_current_session() {
  if (!current_session.has_record) {
    set_color(color_red);
    cout << "未获取到上次输入" << endl;
    set_color(color_white);
    return false;
  }

  string input_data = current_session.input_data;
  string expected_output = current_session.expected_output;
  if (current_session.mode == "-t") {
    if (current_session.input_from_file &&
        !read_named_file(current_session.input_file_name, "输入", input_data)) {
      return false;
    }
    if (current_session.expected_from_file &&
        !read_named_file(current_session.expected_file_name, "输出",
                         expected_output)) {
      return false;
    }
  }

  execute_debug_command(
      current_session.file_name, current_session.mode, input_data,
      expected_output, current_session.input_from_file,
      current_session.input_file_name, current_session.expected_from_file,
      current_session.expected_file_name, current_session.strict_compare);
  return true;
}

// 解析命令行
vector<string> parse_command_line(const string& line) {
  vector<string> args;
  string cur;
  bool in_quote = false;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c == '"') {
      in_quote = !in_quote;
    } else if (c == ' ' && !in_quote) {
      if (!cur.empty()) {
        args.push_back(cur);
        cur.clear();
      }
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) args.push_back(cur);
  return args;
}

int main() {
  SetConsoleOutputCP(CP_UTF8);

  refresh_compilers(false);
  set_color(color_white);
  println("");
  println("===== OMG 工具:  编译调试器 =====");
  println(
      "作者：小鱼儿    |    版本：v0.6.1");  // 为加入了物理级 TLE 制裁升个版本
  println("\n支持的命令:");
  set_color(color_green);
  cout << "exit";
  set_color(color_white);
  println(": 退出程序");
  set_color(color_green);
  cout << "refresh";
  set_color(color_white);
  println(": 重新查找编译器");
  set_color(color_green);
  cout << "version";
  set_color(color_white);
  println(": 查看 C++ 版本");
  set_color(color_green);
  cout << "cls";
  set_color(color_white);
  println(": 清屏");
  set_color(color_green);
  cout << "tlimit <秒数>";
  set_color(color_white);
  println(": 设置测试运行限时");
  set_color(color_green);
  cout << "mlimit <MB>";
  set_color(color_white);
  println(": 设置测试运行内存限制");

  println("\n调试指令: ");
  set_color(color_green);
  cout << "sw <filename>";
  set_color(color_white);
  println(": 设置工作文件");
  set_color(color_green);
  cout << "si <input_file>";
  set_color(color_white);
  println(": 设置输入文件");
  set_color(color_green);
  cout << "so <output_file>";
  set_color(color_white);
  println(": 设置输出文件");
  set_color(color_green);
  cout << "run / compile";
  set_color(color_white);
  println(": 使用工作文件编译运行 / 仅编译");
  set_color(color_green);
  cout << "test <input> <expected_output>";
  set_color(color_white);
  println(": 使用直接输入输出测试，例如 test \"1 + 1\" \"2\"");
  set_color(color_green);
  cout << "test uf / test uif <expected> / test uof <input>";
  set_color(color_white);
  println(": 使用已设置的输入/输出文件测试");
  set_color(color_green);
  cout << "--strict / -s";
  set_color(color_white);
  println(": 严格比较输出，默认忽略行尾空白和末尾换行");
  set_color(color_dark_gray);
  println("旧格式 <filename> -r/-c/-t 仍可使用。");
  set_color(color_white);

  while (true) {
    cout << "\n> ";
    string line;
    getline(cin, line);

    if (line == "exit") {
      set_color(color_green);
      cout << "已退出程序。" << endl;
      set_color(color_white);
      break;
    }

    if (line == "refresh") {
      set_color(color_green);
      cout << "正在重新遍历 Path 查找编译器..." << endl;
      refresh_compilers(true);
      set_color(color_white);
      continue;
    }

    if (line == "version") {
      print_cpp_version();
      continue;
    }

    if (line == "cls") {
      system("cls");
      continue;
    }

    vector<string> args = parse_command_line(line);
    if (args.empty()) continue;

    // 拦截并禁止使用 time_limit
    if (args[0] == "time_limit") {
      set_color(color_red);
      cout << "错误: 仅允许使用 ";
      set_color(color_light_blue);
      cout << "tlimit";
      set_color(color_red);
      cout << " 而不是 time_limit" << endl;
      set_color(color_white);
      continue;
    }

    // 处理 tlimit 指令
    if (args[0] == "tlimit") {
      if (args.size() >= 2) {
        try {
          max_time_limit = stod(args[1]);
          set_color(color_green);
          cout << "时间限制已设置为: " << max_time_limit << " 秒" << endl;
        } catch (...) {
          set_color(color_red);
          cout << "错误: 请输入有效的数字！" << endl;
        }
      } else {
        set_color(color_yellow);
        cout << "当前时间限制: " << max_time_limit
             << " 秒 (输入 'tlimit <秒数>' 来修改)" << endl;
      }
      set_color(color_white);
      continue;
    }

    // 处理内存限制 mlimit 指令
    if (args[0] == "mlimit") {
      if (args.size() >= 2) {
        try {
          double mb = stod(args[1]);
          if (mb > 1536.0) {
            set_color(color_red);
            cout << "错误: mlimit 不能超过 1.5GB (1536MB)！" << endl;
          } else {
            max_memory_limit = (size_t)(mb * 1024.0 * 1024.0);
            set_color(color_green);
            cout << "内存限制已设置为: " << mb << " MB" << endl;
          }
        } catch (...) {
          set_color(color_red);
          cout << "错误: 请输入有效的数字！" << endl;
        }
      } else {
        set_color(color_yellow);
        cout << "当前内存限制: " << (max_memory_limit / 1024 / 1024)
             << " MB (输入 'mlimit <MB>' 来修改)" << endl;
      }
      set_color(color_white);
      continue;
    }

    if (args[0] == "set_work_file" || args[0] == "sw") {
      if (args.size() < 2) {
        set_color(color_yellow);
        cout << "当前工作文件: "
             << (work_file.empty() ? "未设置" : work_file) << endl;
        set_color(color_white);
        continue;
      }
      if (!file_exists(args[1])) {
        set_color(color_red);
        cout << "工作文件不存在: " << args[1] << endl;
        set_color(color_white);
        continue;
      }
      work_file = args[1];
      set_color(color_green);
      cout << "工作文件已设置为: " << work_file << endl;
      set_color(color_white);
      continue;
    }

    if (args[0] == "set_input_file" || args[0] == "si") {
      if (args.size() < 2) {
        set_color(color_yellow);
        cout << "当前输入文件: "
             << (input_file.empty() ? "未设置" : input_file) << endl;
        set_color(color_white);
        continue;
      }
      if (!file_exists(args[1])) {
        set_color(color_red);
        cout << "输入文件不存在: " << args[1] << endl;
        set_color(color_white);
        continue;
      }
      input_file = args[1];
      set_color(color_green);
      cout << "输入文件已设置为: " << input_file << endl;
      set_color(color_white);
      continue;
    }

    if (args[0] == "set_output_file" || args[0] == "so") {
      if (args.size() < 2) {
        set_color(color_yellow);
        cout << "当前输出文件: "
             << (output_file.empty() ? "未设置" : output_file) << endl;
        set_color(color_white);
        continue;
      }
      if (!file_exists(args[1])) {
        set_color(color_red);
        cout << "输出文件不存在: " << args[1] << endl;
        set_color(color_white);
        continue;
      }
      output_file = args[1];
      set_color(color_green);
      cout << "输出文件已设置为: " << output_file << endl;
      set_color(color_white);
      continue;
    }

    if (args[0] == "run" || args[0] == "compile") {
      string file_name;
      if (!require_work_file(file_name)) continue;
      execute_debug_command(file_name, args[0] == "run" ? "-r" : "-c");
      continue;
    }

    if (args[0] == "test") {
      string file_name;
      if (!require_work_file(file_name)) continue;

      string input_data, expected_output;
      string input_source, expected_source;
      bool input_from_file = false;
      bool expected_from_file = false;
      bool strict_compare = false;

      if (args.size() >= 2 && (args[1] == "use_file" || args[1] == "uf")) {
        if (!read_named_file(input_file, "输入", input_data)) continue;
        if (!read_named_file(output_file, "输出", expected_output)) continue;
        input_from_file = true;
        expected_from_file = true;
        input_source = input_file;
        expected_source = output_file;
        if (args.size() >= 3 && is_strict_flag(args[2])) strict_compare = true;
      } else if (args.size() >= 3 &&
                 (args[1] == "use_input_file" || args[1] == "uif")) {
        if (!read_named_file(input_file, "输入", input_data)) continue;
        expected_output = args[2];
        input_from_file = true;
        input_source = input_file;
        if (args.size() >= 4 && is_strict_flag(args[3])) strict_compare = true;
      } else if (args.size() >= 3 &&
                 (args[1] == "use_output_file" || args[1] == "uof")) {
        input_data = args[2];
        if (!read_named_file(output_file, "输出", expected_output)) continue;
        expected_from_file = true;
        expected_source = output_file;
        if (args.size() >= 4 && is_strict_flag(args[3])) strict_compare = true;
      } else if (args.size() >= 3) {
        input_data = args[1];
        expected_output = args[2];
        if (args.size() >= 4 && is_strict_flag(args[3])) strict_compare = true;
      } else {
        set_color(color_red);
        cout << "测试模式需要提供输入数据和预期输出，或使用 test uf/uif/uof"
             << endl;
        set_color(color_white);
        continue;
      }

      execute_debug_command(file_name, "-t", input_data, expected_output,
                            input_from_file, input_source, expected_from_file,
                            expected_source, strict_compare);
      continue;
    }

    if (args.size() == 1 && args[0] == "~") {
      rerun_current_session();
      continue;
    }

    string file_name = args[0];
    if (file_name == "~") {
      if (!current_session.has_record) {
        set_color(color_red);
        cout << "未获取到上次输入" << endl;
        set_color(color_white);
        continue;
      }
      file_name = current_session.file_name;
    }

    if (file_name == "exit") break;
    if (file_name == "refresh") continue;
    if (file_name == "version") continue;
    if (file_name == "cls") continue;

    if (!file_exists(file_name)) {
      set_color(color_red);
      cout << "文件或指令不存在: " << file_name << endl;
      set_color(color_white);
      print_command_suggestions(file_name);
      continue;
    }

    if (args.size() < 2) {
      set_color(color_red);
      cout << "请指定模式 -t, -r 或 -c" << endl;
      set_color(color_white);
      continue;
    }

    string mode = args[1];
    if (mode == "~") {
      if (!current_session.has_record) {
        set_color(color_red);
        cout << "未获取到上次输入" << endl;
        set_color(color_white);
        continue;
      }
      mode = current_session.mode;
    }

    string input_data, expected_output;
    string input_source, expected_source;
    bool input_from_file = false;
    bool expected_from_file = false;
    bool strict_compare = false;
    bool reused_test_part = false;

    if (mode == "-t") {
      if (args.size() < 4) {
        set_color(color_red);
        cout << "测试模式需要提供输入数据和预期输出" << endl;
        set_color(color_white);
        continue;
      }

      string raw_input = args[2];
      if (raw_input == "~") {
        if (!current_session.has_record) {
          set_color(color_red);
          cout << "未获取到上次输入" << endl;
          set_color(color_white);
          continue;
        }
        input_data = current_session.input_data;
        input_from_file = current_session.input_from_file;
        input_source = current_session.input_file_name;
        reused_test_part = true;
      } else if (raw_input == "?") {
        input_data = "";  // "?" 代表没有输入数据
      } else if (raw_input.size() >= 2 && raw_input.front() == '[' &&
                 raw_input.back() == ']') {
        string fname = raw_input.substr(1, raw_input.size() - 2);
        if (!read_file_content(fname, input_data)) {
          set_color(color_red);
          cout << "找不到文件: " << fname << endl;
          set_color(color_white);
          continue;
        }
        input_from_file = true;
        input_source = fname;
      } else if (file_exists(raw_input)) {
        // 省略中括号时，若文件存在则直接读取文件内容
        if (!read_file_content(raw_input, input_data)) {
          set_color(color_red);
          cout << "无法读取文件内容: " << raw_input << endl;
          set_color(color_white);
          continue;
        }
        input_from_file = true;
        input_source = raw_input;
      } else {
        input_data = raw_input;
      }

      string raw_expected = args[3];
      if (raw_expected == "~") {
        if (!current_session.has_record) {
          set_color(color_red);
          cout << "未获取到上次输入" << endl;
          set_color(color_white);
          continue;
        }
        expected_output = current_session.expected_output;
        expected_from_file = current_session.expected_from_file;
        expected_source = current_session.expected_file_name;
        reused_test_part = true;
      } else if (raw_expected == "?") {
        expected_output = "";  // 预期输出也可以用 "?" 表示为空
      } else if (raw_expected.size() >= 2 && raw_expected.front() == '[' &&
                 raw_expected.back() == ']') {
        string fname = raw_expected.substr(1, raw_expected.size() - 2);
        if (!read_file_content(fname, expected_output)) {
          set_color(color_red);
          cout << "找不到文件: " << fname << endl;
          set_color(color_white);
          continue;
        }
        expected_from_file = true;
        expected_source = fname;
      } else if (file_exists(raw_expected)) {
        // 省略中括号时，若文件存在则直接读取文件内容
        if (!read_file_content(raw_expected, expected_output)) {
          set_color(color_red);
          cout << "无法读取文件内容: " << raw_expected << endl;
          set_color(color_white);
          continue;
        }
        expected_from_file = true;
        expected_source = raw_expected;
      } else {
        expected_output = raw_expected;
      }

      if (reused_test_part) strict_compare = current_session.strict_compare;
      if (args.size() >= 5) strict_compare = is_strict_flag(args[4]);
    } else if (mode != "-r" && mode != "-c") {
      set_color(color_red);
      cout << "无效模式，请使用 -t, -r 或 -c" << endl;
      set_color(color_white);
      continue;
    }

    execute_debug_command(file_name, mode, input_data, expected_output,
                          input_from_file, input_source, expected_from_file,
                          expected_source, strict_compare);
  }

  return 0;
}
