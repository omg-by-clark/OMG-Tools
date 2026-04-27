#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// 用于存储上次会话的值
struct LastSession {
  string filename;
  string mode;
  string inputData;
  string expectedOutput;
  bool hasRecord = false;
} session;

void setColor(int color) {
  SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}

const int COLOR_GREEN = 10;
const int COLOR_RED = 12;
const int COLOR_YELLOW = 14;
const int COLOR_WHITE = 7;
const int COLOR_LIGHT_BLUE = 11; // 浅蓝色

string gppPath;
string gccPath;

template <typename t>
void println(const t & text) {
  cout << text << "\n";
}

// 自动在 Path 中寻找编译器
void refreshCompilers(bool showOutput = false) {
  char buffer[MAX_PATH];
  char* filePart;

  DWORD resGpp =
      SearchPathA(NULL, "g++.exe", NULL, MAX_PATH, buffer, &filePart);
  if (resGpp > 0 && resGpp < MAX_PATH) {
    gppPath = buffer;
    if (showOutput) cout << "找到 g++: " << gppPath << endl;
  } else {
    gppPath = "g++";  // Fallback 兜底方案
    if (showOutput) cout << "未在 Path 中找到 g++.exe，将使用默认命令" << endl;
  }

  DWORD resGcc =
      SearchPathA(NULL, "gcc.exe", NULL, MAX_PATH, buffer, &filePart);
  if (resGcc > 0 && resGcc < MAX_PATH) {
    gccPath = buffer;
    if (showOutput) cout << "找到 gcc: " << gccPath << endl;
  } else {
    gccPath = "gcc";  // Fallback 兜底方案
    if (showOutput) cout << "未在 Path 中找到 gcc.exe，将使用默认命令" << endl;
  }
}

string getExtension(const string& filename) {
  size_t pos = filename.rfind('.');
  if (pos != string::npos) {
    return filename.substr(pos + 1);
  }
  return "";
}

bool fileExists(const string& filename) {
  ifstream file(filename.c_str());
  return file.good();
}

// 读取文件全部内容
bool readFileContent(const string& filename, string& content) {
  ifstream file(filename.c_str());
  if (!file.is_open()) return false;
  stringstream ss;
  ss << file.rdbuf();
  content = ss.str();
  file.close();
  return true;
}

// 编译，错误信息存入 errorMsg，成功返回 true
bool compile(const string& filename, string& errorMsg) {
  string ext = getExtension(filename);
  string command;

  size_t pos = filename.rfind('.');
  string baseName = (pos != string::npos) ? filename.substr(0, pos) : filename;

  // 移除硬编码的 -std=c++20 以适应旧版编译器环境，仅保留 -O2 优化
  if (ext == "cpp") {
    command =
        "\"" + gppPath + "\" -O2 \"" + filename + "\" -o \"" + baseName + ".exe\"";
  } else if (ext == "c") {
    command =
        "\"" + gccPath + "\" -O2 \"" + filename + "\" -o \"" + baseName + ".exe\"";
  } else {
    command =
        "\"" + gppPath + "\" -O2 \"" + filename + "\" -o \"" + filename + ".exe\"";
  }

  // 重定向错误到临时文件，并且在最外层加双引号防止 cmd 扒掉内层引号
  string errorFile = "_compile_err_temp.txt";
  string fullCommand = "\"" + command + " 2> " + errorFile + "\"";
  int result = system(fullCommand.c_str());

  ifstream errIn(errorFile.c_str());
  if (errIn.is_open()) {
    stringstream ss;
    ss << errIn.rdbuf();
    errorMsg = ss.str();
    errIn.close();
    remove(errorFile.c_str());
  } else {
    errorMsg = "";
  }

  // 如果编译失败但 errorMsg 为空，尝试手动设置一个通用错误信息
  if (result != 0 && errorMsg.empty()) {
    errorMsg = "编译失败，未捕获到具体错误信息。请检查编译器路径或代码语法。";
  }

  return (result == 0);
}

// 运行程序并捕获输出（通过管道）
string runAndCapture(const string& exePath, const string& input) {
  string command;

  if (input == "-") {
    command = "\"\"" + exePath + "\" < nul\"";
  } else {
    // 【核心修复】将多行输入写入临时文件，使用 < 重定向，彻底解决 cmd 换行截断 Bug
    ofstream tempIn("_omg_temp_input.txt");
    tempIn << input;
    tempIn.close();
    command = "\"\"" + exePath + "\" < _omg_temp_input.txt\"";
  }

  FILE* pipe = _popen(command.c_str(), "r");
  if (!pipe) return "";
  char buffer[4096];
  string result;
  while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
    result += buffer;
  }
  _pclose(pipe);

  if (input != "-") {
    remove("_omg_temp_input.txt");
  }

  // 去除末尾换行
  while (result.length() > 0 && (result[result.length() - 1] == '\n' ||
                                 result[result.length() - 1] == '\r')) {
    result.erase(result.length() - 1);
  }
  return result;
}

// 探测并输出 C++ 版本
void printCppVersion() {
  ofstream out("_check_ver.cpp");
  out << "#include <iostream>\nint main(){std::cout<<__cplusplus;return 0;}";
  out.close();

  // 移除 -std=c++20 参数，以检查编译器的真实默认版本
  string cmd = "\"" + gppPath + "\" _check_ver.cpp -o _check_ver.exe";
  // 最外层加引号
  system(("\"" + cmd + " > nul 2>&1\"").c_str());

  string res = runAndCapture("_check_ver.exe", "");

  remove("_check_ver.cpp");
  remove("_check_ver.exe");

  string versionStr = "未知";
  if (res == "199711") versionStr = "C++ 98";
  else if (res == "201103") versionStr = "C++ 11";
  else if (res == "201402") versionStr = "C++ 14";
  else if (res == "201703") versionStr = "C++ 17";
  else if (res == "202002") versionStr = "C++ 20";
  else if (res == "202302") versionStr = "C++ 23";
  else if (res > "202302") versionStr = "C++ 26 (或更新)";
  else if (!res.empty()) versionStr = "未知版本 (" + res + ")";

  setColor(COLOR_LIGHT_BLUE);
  if (versionStr == "未知") {
    cout << "无法获取 C++ 版本，请检查编译器是否正常工作。" << endl;
  } else {
    cout << versionStr << endl;
  }
  setColor(COLOR_WHITE);
}

// 在新窗口中运行程序（不捕获输出）
void runInNewWindow(const string& exePath) {
  // start 命令的第一个双引号字符串是标题，后面跟着 cmd /k 运行具体命令
  string runCmd =
      "start \"\" cmd /k \"\"" + exePath +
      "\" & echo. & echo 程序已结束，按任意键关闭此窗口... & pause > nul\"";
  system(runCmd.c_str());
}

// 去除字符串首尾空白
string trim(const string& s) {
  size_t start = s.find_first_not_of(" \t\n\r");
  if (start == string::npos) return "";
  size_t end = s.find_last_not_of(" \t\n\r");
  return s.substr(start, end - start + 1);
}

// 解析命令行：支持双引号内的空格，引号本身不保留
vector<string> parseCommandLine(const string& line) {
  vector<string> args;
  string cur;
  bool inQuote = false;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c == '"') {
      inQuote = !inQuote;
    } else if (c == ' ' && !inQuote) {
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

  // 初始化时静默查找编译器
  refreshCompilers(false);
  setColor(15);
  println("");
  println("===== OMG 工具:  编译调试器 =====");
  println("作者：小鱼儿    |    版本：v0.5.1");
  println("\n支持的命令:");
  println("exit: 退出程序");
  println("refresh: 重新查找编译器");
  println("version: 查看 C++ 版本");
  println("\n调试指令: ");
  println("编译并运行: <filename> -r");
  println("仅编译: <filename> -c");
  println("编译并测试: <filename> -t <input> <expected_output>");
  println("编译并测试: <filename> -t [input_file] [expected_output_file]");
  while (true) {
    cout << "\n> ";
    string line;
    getline(cin, line);

    if (line == "exit" || line == "EXIT") {
      setColor(COLOR_GREEN);
      cout << "已退出程序。" << endl;
      setColor(COLOR_WHITE);
      break;
    }

    if (line == "refresh" || line == "REFRESH") {
      setColor(COLOR_GREEN);
      cout << "正在重新遍历 Path 查找编译器..." << endl;
      refreshCompilers(true);
      setColor(COLOR_WHITE);
      continue;
    }

    if (line == "version" || line == "VERSION") {
      printCppVersion();
      continue;
    }

    vector<string> args = parseCommandLine(line);
    if (args.empty()) continue;

    // 只有一个 ~ 代表完全沿用上次的每一个项
    if (args.size() == 1 && args[0] == "~") {
      if (!session.hasRecord) {
        setColor(COLOR_RED);
        cout << "未获取到上次输入" << endl;
        setColor(COLOR_WHITE);
        continue;
      }
      args.clear();
      args.push_back(session.filename);
      args.push_back(session.mode);
      if (session.mode == "-t") {
        args.push_back(session.inputData);
        args.push_back(session.expectedOutput);
      }
    }

    string filename = args[0];
    if (filename == "~") {
      if (!session.hasRecord) {
        setColor(COLOR_RED);
        cout << "未获取到上次输入" << endl;
        setColor(COLOR_WHITE);
        continue;
      }
      filename = session.filename;
    }

    if (filename == "exit" || filename == "EXIT") break;
    if (filename == "refresh" || filename == "REFRESH") continue;
    if (filename == "version" || filename == "VERSION") continue;

    if (!fileExists(filename)) {
      setColor(COLOR_RED);
      cout << "文件不存在: " << filename << endl;
      setColor(COLOR_WHITE);
      continue;
    }

    if (args.size() < 2) {
      setColor(COLOR_RED);
      cout << "请指定模式 -t, -r 或 -c" << endl;
      setColor(COLOR_WHITE);
      continue;
    }

    string mode = args[1];
    if (mode == "~") {
      if (!session.hasRecord) {
        setColor(COLOR_RED);
        cout << "未获取到上次输入" << endl;
        setColor(COLOR_WHITE);
        continue;
      }
      mode = session.mode;
    }

    string inputData, expectedOutput;

    if (mode == "-t") {
      if (args.size() < 4) {
        setColor(COLOR_RED);
        cout << "测试模式需要提供输入数据和预期输出" << endl;
        setColor(COLOR_WHITE);
        continue;
      }

      string rawInput = args[2];
      if (rawInput == "~") {
        if (!session.hasRecord) {
          setColor(COLOR_RED);
          cout << "未获取到上次输入" << endl;
          setColor(COLOR_WHITE);
          continue;
        }
        inputData = session.inputData;
      } else if (rawInput.size() >= 2 && rawInput.front() == '[' && rawInput.back() == ']') {
        string fname = rawInput.substr(1, rawInput.size() - 2);
        if (!readFileContent(fname, inputData)) {
          setColor(COLOR_RED);
          cout << "找不到文件" << endl;
          setColor(COLOR_WHITE);
          continue;
        }
      } else {
        inputData = rawInput;
      }

      string rawExpected = args[3];
      if (rawExpected == "~") {
        if (!session.hasRecord) {
          setColor(COLOR_RED);
          cout << "未获取到上次输入" << endl;
          setColor(COLOR_WHITE);
          continue;
        }
        expectedOutput = session.expectedOutput;
      } else if (rawExpected.size() >= 2 && rawExpected.front() == '[' && rawExpected.back() == ']') {
        string fname = rawExpected.substr(1, rawExpected.size() - 2);
        if (!readFileContent(fname, expectedOutput)) {
          setColor(COLOR_RED);
          cout << "找不到文件" << endl;
          setColor(COLOR_WHITE);
          continue;
        }
      } else {
        expectedOutput = rawExpected;
      }
    } else if (mode != "-r" && mode != "-c") {
      setColor(COLOR_RED);
      cout << "无效模式，请使用 -t, -r 或 -c" << endl;
      setColor(COLOR_WHITE);
      continue;
    }

    // 成功验证参数，将当前输入记入变量
    session.filename = filename;
    session.mode = mode;
    if (mode == "-t") {
      session.inputData = inputData;
      session.expectedOutput = expectedOutput;
    }
    session.hasRecord = true;

    // 开始编译
    setColor(COLOR_GREEN);
    cout << "文件识别成功，准备编译..." << endl;
    setColor(COLOR_WHITE);

    string errorMsg;
    if (!compile(filename, errorMsg)) {
      setColor(COLOR_YELLOW);
      cout << "Compile Error" << endl;
      cout << errorMsg << endl;
      setColor(COLOR_WHITE);
      continue;
    }

    setColor(COLOR_GREEN);
    cout << "✓ 编译成功！" << endl;
    setColor(COLOR_WHITE);

    size_t pos = filename.rfind('.');
    string baseName =
        (pos != string::npos) ? filename.substr(0, pos) : filename;
    string exePath = baseName + ".exe";

    if (mode == "-r") {
      cout << "正在新窗口中启动程序..." << endl;
      runInNewWindow(exePath);
    } else if (mode == "-c") {
      cout << "仅编译完成，生成可执行文件: " << exePath << endl;
    } else if (mode == "-t") {
      cout << "运行程序进行测试..." << endl;
      string actualOutput = runAndCapture(exePath, inputData);

      string expectedTrim = (expectedOutput == "-") ? "" : trim(expectedOutput);
      string actualTrim = trim(actualOutput);

      if (expectedTrim == actualTrim) {
        setColor(COLOR_GREEN);
        cout << "Accepted" << endl;
        setColor(COLOR_WHITE);
      } else {
        setColor(COLOR_RED);
        cout << "Wrong Answer" << endl;
        cout << "预期输出: " << expectedOutput << endl;
        cout << "实际输出: " << actualOutput << endl;
        setColor(COLOR_WHITE);
      }
    }
  }

  return 0;
}
