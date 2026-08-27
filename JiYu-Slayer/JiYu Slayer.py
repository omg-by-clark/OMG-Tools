# 第一部分：导入所有需要的库
import tkinter  # 用于创建图形用户界面（就是那些窗口、按钮、输入框）
from tkinter import ttk  # 用于创建下拉框等高级组件
import tkinter.messagebox  # 用于弹出各种提示框（比如警告、错误提示）
import psutil  # 用于查看系统运行的所有程序（进程）
import keyboard  # 用于监听和设置全局快捷键（即使程序隐藏也能响应）
import threading  # 用于创建线程，防止程序界面卡顿
import ctypes  # 用于调用 Windows 系统底层功能
import sys  # 用于控制程序的启动和退出
import os  # 用于文件和系统路径操作
import subprocess  # 用于启动外部程序
import winreg  # 用于操作 Windows 注册表（实现开机自启动）
from PIL import Image, ImageDraw  # 用于绘制系统托盘图标
import pystray  # 用于在 Windows 右下角创建系统托盘图标

# 第二部分：确保程序只有一个副本在运行（单例模式）
mutex_name_string = "JiYu_Slayer_Single_Instance_Mutex"
mutex_handle_object = ctypes.windll.kernel32.CreateMutexW(
    None, False, mutex_name_string)
if ctypes.windll.kernel32.GetLastError() == 183:
    temporary_hidden_window = tkinter.Tk()
    temporary_hidden_window.withdraw()
    tkinter.messagebox.showerror("运行冲突", "JiYu Slayer 已经在运行了，请勿重复打开！")
    sys.exit()

# 第三部分：主程序类


class Ji_Yu_Slayer_Application:
    """
    程序的核心类。
    包含了所有窗口绘制、底层劫持、进程扫描的逻辑。
    """

    def __init__(self):
        print("[✓] JiYu Slayer 程序正在启动...")

        # ========== 核心修复：必须先创建主窗口，再创建 Tkinter 变量 ==========
        self.main_application_window = tkinter.Tk()
        self.main_application_window.title("OMG 工具：JiYu Inspector v2.1")
        self.main_application_window.configure(bg="#f3f3f3")
        self.main_application_window.option_add("*Font", "微软雅黑 9")

        # ========== 初始化程序状态变量 ==========
        self.target_process_name_string = "StudentMain.exe"
        self.detected_process_path_string = "尚未检测到极域进程路径"
        self.last_detected_processes_list = []
        self.current_hotkey_string = "ctrl+shift+x"
        self.detection_interval_seconds_integer = 5

        # 新增的状态变量 (现在有了主窗口，不会再报错了)
        self.is_startup_enabled_boolean = tkinter.BooleanVar(
            self.main_application_window, value=False)
        self.is_topmost_enabled_boolean = tkinter.BooleanVar(
            self.main_application_window, value=False)

        # 设置窗口大小和位置（右下角）
        screen_width = self.main_application_window.winfo_screenwidth()
        screen_height = self.main_application_window.winfo_screenheight()
        window_width = 360
        window_height = 480
        x = screen_width - window_width - 20
        y = screen_height - window_height - 60
        self.main_application_window.geometry(
            f"{window_width}x{window_height}+{x}+{y}")

        # 关闭窗口时隐藏而不是退出
        self.main_application_window.protocol(
            "WM_DELETE_WINDOW", self.hide_window_to_background)

        # ========== 初始化功能模块 ==========
        self.detect_jiyu_process_path_action()
        self.build_main_user_interface()
        self.check_startup_registry_status()
        self.register_global_hotkey()
        self.create_system_tray_icon()

        print("[✓] JiYu Slayer 启动完成！")

    def detect_jiyu_process_path_action(self):
        """遍历系统中所有运行的程序，寻找极域并获取其完整路径"""
        try:
            for proc in psutil.process_iter(['name', 'exe']):
                try:
                    if proc.info['name'] == self.target_process_name_string:
                        self.detected_process_path_string = proc.info['exe']
                        self.write_log_message(
                            f"[✓] 寻寻找极域：{self.detected_process_path_string}")
                        break
                except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.Error):
                    pass
        except Exception as ex:
            self.write_log_message(f"[错误] 寻找极域时出错：{ex}")

    def build_main_user_interface(self):
        """创建主窗口内的所有组件"""

        # 1. 只读扫描按钮
        self.scan_process_button = tkinter.Button(
            self.main_application_window,
            text="🔍 扫描极域状态",
            command=self.scan_and_report_jiyu_status_action,
            font=("微软雅黑", 12, "bold"),
            bg="#2563EB",
            fg="white",
            pady=8,
            relief="raised",
            bd=1
        )
        self.scan_process_button.pack(pady=10, fill="x", padx=20)

        # 2. 安全辅助操作
        safe_action_frame = tkinter.Frame(self.main_application_window)
        safe_action_frame.pack(pady=5, fill="x", padx=20)

        tkinter.Label(safe_action_frame, text="辅助操作:", font=(
            "微软雅黑", 9)).pack(side="left", padx=(0, 5))

        self.open_uninstall_button = tkinter.Button(
            safe_action_frame,
            text="打开卸载入口",
            command=self.open_windows_uninstall_panel_action,
            font=("微软雅黑", 9, "bold"),
            bg="#0F766E",
            fg="white",
            relief="flat",
            bd=0,
            activebackground="#115E59"
        )
        self.open_uninstall_button.pack(side="right")

        self.copy_path_button = tkinter.Button(
            self.main_application_window,
            text="📋 复制检测到的路径",
            command=self.copy_detected_path_action,
            font=("微软雅黑", 9),
            bg="#E5E7EB",
            fg="#111827",
            pady=5,
            relief="flat",
            bd=0
        )
        self.copy_path_button.pack(pady=8, fill="x", padx=20)

        # 4. 设置按钮 (黄底，橙色字)
        self.open_settings_button = tkinter.Button(
            self.main_application_window,
            text="⚙️ 设置",
            command=self.open_settings_window_action,
            font=("微软雅黑", 10, "bold"),
            bg="#E1B400",
            fg="#333333",
            pady=8,
            relief="flat",
            bd=0,
            activebackground="#C49A00"
        )
        self.open_settings_button.pack(pady=(5, 3), fill="x", padx=20)

        self.close_app_button = tkinter.Button(
            self.main_application_window,
            text="❌ 关闭程序",
            command=self.close_application_action,
            font=("微软雅黑", 10, "bold"),
            bg="#16A34A",
            fg="white",
            pady=8,
            relief="flat",
            bd=0,
            activebackground="#0F7A38"
        )
        self.close_app_button.pack(pady=(0, 10), fill="x", padx=20)

        # 5. 日志文本框
        log_label = tkinter.Label(
            self.main_application_window,
            text="📋 运行日志：",
            font=("微软雅黑", 9, "bold"),
            fg="#333333"
        )
        log_label.pack(pady=(5, 0), padx=20, anchor="w")

        self.log_text_widget = tkinter.Text(
            self.main_application_window,
            height=8,
            font=("Consolas", 8),
            bg="#F5F5F5",
            state="disabled",
            relief="solid",
            bd=1
        )
        self.log_text_widget.pack(pady=5, padx=20, fill="both", expand=True)

        self.gpl_license_label = tkinter.Label(
            self.main_application_window,
            text="本程序受 GPL v3 保护 | 只读诊断与学习用途",
            fg="#999999",
            font=("Arial", 7)
        )
        self.gpl_license_label.pack(side="bottom", pady=5)

    def write_log_message(self, message_string):
        """向日志框写入信息并自动滚动"""
        print(message_string)
        if not hasattr(self, 'log_text_widget'):
            return
        try:
            self.log_text_widget.config(state="normal")
            self.log_text_widget.insert("end", message_string + "\n")
            self.log_text_widget.see("end")
            self.log_text_widget.config(state="disabled")
        except Exception:
            pass

    def scan_and_report_jiyu_status_action(self):
        """只读扫描目标进程，展示 PID、状态和路径，不修改任何外部程序。"""
        self.last_detected_processes_list = []
        self.detected_process_path_string = "尚未检测到极域进程路径"

        try:
            for proc in psutil.process_iter(['pid', 'name', 'exe', 'status', 'username']):
                try:
                    if proc.info['name'] == self.target_process_name_string:
                        process_info = {
                            "pid": proc.info.get('pid'),
                            "exe": proc.info.get('exe') or "无法读取路径",
                            "status": proc.info.get('status') or "未知",
                            "username": proc.info.get('username') or "未知用户",
                        }
                        self.last_detected_processes_list.append(process_info)
                except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.Error):
                    pass
        except Exception as ex:
            self.write_log_message(f"[错误] 扫描进程时出错：{ex}")
            return

        if not self.last_detected_processes_list:
            self.write_log_message("[信息] 没有检测到 StudentMain.exe 正在运行")
            return

        first_path = self.last_detected_processes_list[0]["exe"]
        if first_path != "无法读取路径":
            self.detected_process_path_string = first_path

        self.write_log_message(
            f"[✓] 检测到 {len(self.last_detected_processes_list)} 个 StudentMain.exe 进程")
        for item in self.last_detected_processes_list:
            self.write_log_message(
                f"    PID={item['pid']} 状态={item['status']} 用户={item['username']}")
            self.write_log_message(f"    路径={item['exe']}")

    def open_windows_uninstall_panel_action(self):
        """打开 Windows 已安装应用入口，让用户使用系统提供的卸载流程。"""
        try:
            subprocess.Popen(["control.exe", "appwiz.cpl"])
            self.write_log_message("[✓] 已打开 Windows 程序和功能")
        except Exception as ex:
            tkinter.messagebox.showerror("打开失败", f"无法打开卸载入口：\n{ex}")

    def copy_detected_path_action(self):
        """复制最近一次检测到的可执行文件路径。"""
        if self.detected_process_path_string == "尚未检测到极域进程路径":
            self.scan_and_report_jiyu_status_action()

        if self.detected_process_path_string == "尚未检测到极域进程路径":
            tkinter.messagebox.showinfo("没有路径", "还没有检测到可复制的进程路径。")
            return

        self.main_application_window.clipboard_clear()
        self.main_application_window.clipboard_append(self.detected_process_path_string)
        self.write_log_message(f"[✓] 已复制路径：{self.detected_process_path_string}")

    def show_safe_uninstall_guidance_action(self):
        """展示合规处理建议，不直接修改目标软件。"""
        message = (
            "建议优先使用 Windows 设置、控制面板或软件官方卸载程序处理。\n\n"
            "如果这是机构管理设备，请先确认你有权更改相关软件。"
        )
        tkinter.messagebox.showinfo("安全处理建议", message)

    # =========================================================================
    # 设置窗口相关逻辑
    # =========================================================================
    def open_settings_window_action(self):
        """弹出独立的设置窗口"""
        settings_window = tkinter.Toplevel(self.main_application_window)
        settings_window.title("⚙️ JiYu Slayer 设置")
        settings_window.geometry("320x400")
        settings_window.attributes("-topmost", True)
        settings_window.resizable(False, False)

        # 标题
        title_label = tkinter.Label(
            settings_window,
            text="🔧 详细设置",
            font=("微软雅黑", 12, "bold"),
            fg="#333333"
        )
        title_label.pack(pady=10)

        # 1. 快捷键修改
        hotkey_frame = tkinter.Frame(settings_window)
        hotkey_frame.pack(pady=(10, 5), fill="x", padx=15)
        tkinter.Label(hotkey_frame, text="快捷键:", font=(
            "微软雅黑", 9, "bold")).pack(side="left")
        self.settings_hotkey_button = tkinter.Button(
            hotkey_frame,
            text=self.current_hotkey_string,
            command=self.open_hotkey_capture_window,
            bg="#4A90E2",
            fg="white",
            font=("Consolas", 10, "bold"),
            relief="raised",
            bd=1
        )
        self.settings_hotkey_button.pack(side="left", padx=10)

        # 2. 自动检测时间
        time_frame = tkinter.Frame(settings_window)
        time_frame.pack(pady=5, fill="x", padx=15)
        tkinter.Label(time_frame, text="检测间隔(秒):",
                      font=("微软雅黑", 9)).pack(side="left")

        self.interval_entry_widget = tkinter.Entry(
            time_frame, width=8, font=("Consolas", 9))
        self.interval_entry_widget.insert(
            0, str(self.detection_interval_seconds_integer))
        self.interval_entry_widget.pack(side="left", padx=5)

        # 3. 重新扫描状态按钮
        tkinter.Button(
            settings_window,
            text="🔍 重新扫描状态",
            command=self.scan_and_report_jiyu_status_action,
            bg="#2196F3",
            fg="white",
            font=("微软雅黑", 9, "bold"),
            pady=3,
            relief="raised",
            bd=1
        ).pack(pady=8, fill="x", padx=15)

        # 4. 安全处理建议
        tkinter.Button(
            settings_window,
            text="ℹ️ 安全处理建议",
            command=self.show_safe_uninstall_guidance_action,
            bg="#4B5563",
            fg="white",
            font=("微软雅黑", 9, "bold"),
            pady=3,
            relief="raised",
            bd=1
        ).pack(pady=5, fill="x", padx=15)

        # 5. 灰框勾选区域
        options_frame = tkinter.LabelFrame(
            settings_window,
            text="额外选项",
            font=("微软雅黑", 9, "bold"),
            fg="#666666",
            relief="groove",
            bd=2
        )
        options_frame.pack(pady=15, fill="x", padx=15)

        # 开机自启动
        tkinter.Checkbutton(
            options_frame,
            text="🚀 开机自启动",
            variable=self.is_startup_enabled_boolean,
            command=self.toggle_startup_registry_action,
            font=("微软雅黑", 9),
            relief="flat"
        ).pack(anchor="w", padx=10, pady=5)

        # 保持在最上层
        tkinter.Checkbutton(
            options_frame,
            text="📌 保持在最上层",
            variable=self.is_topmost_enabled_boolean,
            command=self.toggle_topmost_window_action,
            font=("微软雅黑", 9),
            relief="flat"
        ).pack(anchor="w", padx=10, pady=(0, 5))

    def toggle_topmost_window_action(self):
        """将主窗口强制锁定在最上层或解除"""
        topmost = self.is_topmost_enabled_boolean.get()
        self.main_application_window.attributes('-topmost', topmost)
        status = "已启用" if topmost else "已禁用"
        self.write_log_message(f"[✓] 窗口置顶: {status}")

    def check_startup_registry_status(self):
        """检查注册表，看当前是否已经开启了自启动"""
        try:
            key = winreg.OpenKey(
                winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Run", 0, winreg.KEY_READ)
            winreg.QueryValueEx(key, "JiYuSlayer")
            winreg.CloseKey(key)
            self.is_startup_enabled_boolean.set(True)
        except WindowsError:
            self.is_startup_enabled_boolean.set(False)

    def toggle_startup_registry_action(self):
        """修改注册表，添加或移除开机自启动项"""
        try:
            key = winreg.OpenKey(
                winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Run", 0, winreg.KEY_ALL_ACCESS)
            if self.is_startup_enabled_boolean.get():
                exe_path = os.path.abspath(sys.argv[0])
                winreg.SetValueEx(key, "JiYuSlayer", 0,
                                  winreg.REG_SZ, f'"{exe_path}"')
                self.write_log_message("[✓] 已成功添加开机自启")
            else:
                winreg.DeleteValue(key, "JiYuSlayer")
                self.write_log_message("[✓] 已移除开机自启")
            winreg.CloseKey(key)
        except Exception as ex:
            tkinter.messagebox.showerror(
                "权限错误", f"无法修改注册表，请尝试以管理员身份运行。\n错误: {ex}")
            # 回滚勾选框状态
            self.is_startup_enabled_boolean.set(
                not self.is_startup_enabled_boolean.get())

    # 快捷键、后台隐藏与系统托盘逻辑
    def open_hotkey_capture_window(self):
        """打开快捷键捕获窗口"""
        self.capture_window = tkinter.Toplevel()
        self.capture_window.title("⌨️ 设置快捷键")
        self.capture_window.geometry("300x200")
        self.capture_window.attributes("-topmost", True)
        self.capture_window.resizable(False, False)

        tkinter.Label(
            self.capture_window,
            text="请在键盘上按下你想设置的快捷键\n然后按下 Enter 确认\n（或按 Escape 取消）",
            font=("微软雅黑", 10)
        ).pack(pady=15)

        self.captured_keys_label = tkinter.Label(
            self.capture_window,
            text="等待按键...",
            fg="blue",
            font=("Consolas", 14, "bold")
        )
        self.captured_keys_label.pack(pady=10)

        self.pressed_keys = []
        self.max_pressed_keys = []

        self.capture_window.bind("<KeyPress>", self.on_key_press)
        self.capture_window.bind("<KeyRelease>", self.on_key_release)
        self.capture_window.focus_set()

    def on_key_press(self, event):
        key = event.keysym.lower()
        if key == "return":
            keys = self.max_pressed_keys or self.pressed_keys
            if keys:
                new_hotkey = "+".join(keys)
                self.update_global_hotkey(new_hotkey)
            self.capture_window.destroy()
            return

        if key == "escape":
            self.capture_window.destroy()
            return

        # 标准化修饰键
        if "control" in key:
            key = "ctrl"
        elif "shift" in key:
            key = "shift"
        elif "alt" in key:
            key = "alt"
        elif "win" in key or "super" in key:
            key = "win"

        if key in ["backspace", "delete"]:
            if self.pressed_keys:
                self.pressed_keys.pop()
            self.captured_keys_label.config(
                text="+".join(self.pressed_keys) if self.pressed_keys else "等待按键...")
            return

        if key not in self.pressed_keys:
            self.pressed_keys.append(key)
            if len(self.pressed_keys) > len(self.max_pressed_keys):
                self.max_pressed_keys = self.pressed_keys.copy()

        self.captured_keys_label.config(
            text="+".join(self.pressed_keys), fg="#2196F3")

    def on_key_release(self, event):
        key = event.keysym.lower()
        if "control" in key:
            key = "ctrl"
        elif "shift" in key:
            key = "shift"
        elif "alt" in key:
            key = "alt"
        elif "win" in key or "super" in key:
            key = "win"

        if key in self.pressed_keys:
            self.pressed_keys.remove(key)

    def update_global_hotkey(self, new_hotkey):
        try:
            keyboard.unhook_all()
            self.current_hotkey_string = new_hotkey
            if hasattr(self, 'settings_hotkey_button'):
                self.settings_hotkey_button.config(
                    text=self.current_hotkey_string)
            self.register_global_hotkey()
            self.write_log_message(f"[✓] 快捷键已更新为：{new_hotkey}")
        except Exception as ex:
            tkinter.messagebox.showerror("设置失败", f"无法设置该快捷键：\n{ex}")

    def register_global_hotkey(self):
        """注册全局快捷键"""
        try:
            keyboard.add_hotkey(self.current_hotkey_string,
                                self.toggle_window_visibility)
        except Exception as ex:
            self.write_log_message(
                f"[警告] 快捷键 {self.current_hotkey_string} 注册失败：{ex}")

    def toggle_window_visibility(self):
        """切换窗口可见性（显示/隐藏）"""
        if self.main_application_window.state() == "normal":
            self.hide_window_to_background()
        else:
            self.show_window_from_background()

    def hide_window_to_background(self):
        """隐藏窗口到后台"""
        self.main_application_window.withdraw()

    def show_window_from_background(self):
        """从后台显示窗口"""
        self.main_application_window.deiconify()
        if self.is_topmost_enabled_boolean.get():
            self.main_application_window.lift()
            self.main_application_window.attributes('-topmost', True)
        self.main_application_window.focus_force()

    def create_image_for_tray_icon(self):
        """创建系统托盘图标"""
        img = Image.new('RGB', (64, 64), (0, 0, 0))
        draw = ImageDraw.Draw(img)
        # 绘制红色方块作为图标
        draw.rectangle((16, 16, 48, 48), fill=(255, 0, 0))
        return img

    def on_tray_icon_clicked(self, icon, item):
        self.show_window_from_background()

    def on_tray_icon_quit(self, icon, item):
        if hasattr(self, 'system_tray_icon_object') and self.system_tray_icon_object is not None:
            self.system_tray_icon_object.stop()
        self.main_application_window.after(
            0, self.main_application_window.destroy)
        os._exit(0)

    def close_application_action(self):
        if hasattr(self, 'system_tray_icon_object') and self.system_tray_icon_object is not None:
            try:
                self.system_tray_icon_object.stop()
            except Exception:
                pass
        self.main_application_window.after(
            0, self.main_application_window.destroy)
        os._exit(0)

    def run_system_tray_icon_thread(self):
        """在系统托盘创建应用图标"""
        menu = pystray.Menu(
            pystray.MenuItem(
                "📂 打开控制面板", self.on_tray_icon_clicked, default=True),
            pystray.MenuItem("❌ 彻底退出程序", self.on_tray_icon_quit)
        )
        self.system_tray_icon_object = pystray.Icon(
            "JiYu_Slayer", self.create_image_for_tray_icon(), "OMG 工具：JiYu Slayer", menu)
        self.system_tray_icon_object.run()

    def create_system_tray_icon(self):
        """在后台线程中创建系统托盘图标"""
        thread = threading.Thread(
            target=self.run_system_tray_icon_thread, daemon=True)
        thread.start()

    def start_application(self):
        """启动应用主循环"""
        self.main_application_window.mainloop()


if __name__ == "__main__":
    application_instance = Ji_Yu_Slayer_Application()
    application_instance.start_application()
