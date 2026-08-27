import ctypes
import json
import threading
import time
import tkinter as tk
from tkinter import ttk
from urllib.parse import urlencode
from urllib.request import Request, urlopen


BASE_URL = "http://123.60.188.246"
TARGET_USER = "张弛"
REFRESH_INTERVAL_MS = 10 * 60 * 1000


def enable_high_dpi_awareness():
    try:
        ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except Exception:
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(2)
        except Exception:
            pass


class OIRankClient:
    def __init__(self, base_url=BASE_URL):
        self.base_url = base_url.rstrip("/")

    def _get_json(self, path, params):
        url = f"{self.base_url}{path}?{urlencode(params)}"
        req = Request(url, headers={"User-Agent": "OMG-Tools-WebScraper/1.0"})
        with urlopen(req, timeout=30) as response:
            charset = response.headers.get_content_charset() or "utf-8"
            return json.loads(response.read().decode(charset, errors="replace"))

    def get_oi_rank(self, username):
        params = {
            "currentPage": 1,
            "limit": 1,
            "type": 1,
            "searchUser": "",
        }
        summary = self._get_json("/api/get-rank-list", params)
        total = int(summary["data"]["total"])

        params["limit"] = total
        rank_data = self._get_json("/api/get-rank-list", params)
        records = rank_data["data"]["records"]
        for index, record in enumerate(records, start=1):
            if record.get("username") == username:
                return {
                    "rank": index,
                    "top_percent": index / total * 100,
                    "total_users": total,
                    "user": record,
                    "loaded_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                }
        return {
            "rank": None,
            "top_percent": None,
            "total_users": total,
            "user": None,
            "loaded_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        }


class RippleButton(tk.Canvas):
    def __init__(self, master, text, command, **kwargs):
        super().__init__(
            master,
            height=48,
            highlightthickness=0,
            bd=0,
            bg=kwargs.get("bg", "#f7faf8"),
        )
        self.command = command
        self.text = text
        self.enabled = True
        self.normal_color = "#16a34a"
        self.hover_color = "#15803d"
        self.disabled_color = "#82c99b"
        self.ripple_color = "#bbf7d0"
        self.configure(cursor="hand2")
        self.bind("<Configure>", lambda _event: self.draw())
        self.bind("<Enter>", lambda _event: self.draw(self.hover_color))
        self.bind("<Leave>", lambda _event: self.draw())
        self.bind("<Button-1>", self.on_click)

    def draw(self, color=None):
        self.delete("button")
        fill = self.disabled_color if not self.enabled else color or self.normal_color
        width = max(180, self.winfo_width())
        self.create_rectangle(0, 0, width, 48, fill=fill, outline="", tags="button")
        self.create_text(
            width / 2,
            24,
            text=self.text,
            fill="white",
            font=("Microsoft YaHei UI", 13, "bold"),
            tags="button",
        )

    def set_enabled(self, enabled):
        self.enabled = enabled
        self.configure(cursor="hand2" if enabled else "arrow")
        self.draw()

    def on_click(self, event):
        if not self.enabled:
            return
        self.ripple(event.x, event.y)
        self.command()

    def ripple(self, x, y):
        max_radius = max(self.winfo_width(), self.winfo_height())
        ripple_id = self.create_oval(x, y, x, y, fill=self.ripple_color, outline="")
        self.tag_lower(ripple_id, "button")

        def step(radius=0):
            if radius > max_radius:
                self.delete(ripple_id)
                return
            self.coords(ripple_id, x - radius, y - radius, x + radius, y + radius)
            self.after(12, lambda: step(radius + 12))

        step()


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.client = OIRankClient()
        self.refreshing = False
        self.title("OI Rank Scraper")
        self.geometry("560x470")
        self.minsize(500, 420)
        self.configure(bg="#f7faf8")
        self.set_tk_scaling()
        self.create_widgets()
        self.bind_all("<Control-r>", lambda _event: self.refresh())
        self.bind_all("<Control-R>", lambda _event: self.refresh())
        self.bind_all("<F5>", lambda _event: self.refresh())
        self.after(100, self.refresh)
        self.after(REFRESH_INTERVAL_MS, self.auto_refresh)

    def set_tk_scaling(self):
        try:
            dpi = self.winfo_fpixels("1i")
            self.tk.call("tk", "scaling", dpi / 72)
        except tk.TclError:
            pass

    def create_widgets(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("Root.TFrame", background="#f7faf8")
        style.configure("Title.TLabel", background="#f7faf8", foreground="#0f172a", font=("Microsoft YaHei UI", 22, "bold"))
        style.configure("Subtle.TLabel", background="#f7faf8", foreground="#64748b", font=("Microsoft YaHei UI", 10))
        style.configure("Value.TLabel", background="#ffffff", foreground="#0f172a", font=("Microsoft YaHei UI", 16, "bold"))
        style.configure("Key.TLabel", background="#ffffff", foreground="#64748b", font=("Microsoft YaHei UI", 10))
        style.configure("Card.TFrame", background="#ffffff", relief="flat")

        root = ttk.Frame(self, style="Root.TFrame", padding=24)
        root.pack(fill="both", expand=True)

        ttk.Label(root, text="OI Rank Scraper", style="Title.TLabel").pack(anchor="w")
        ttk.Label(root, text=f"目标用户：{TARGET_USER}    数据源：{BASE_URL}/oi-rank", style="Subtle.TLabel").pack(anchor="w", pady=(4, 18))

        self.refresh_button = RippleButton(root, "刷新", self.refresh)
        self.refresh_button.pack(fill="x", pady=(0, 18))

        card = ttk.Frame(root, style="Card.TFrame", padding=20)
        card.pack(fill="both", expand=True)

        self.rank_var = tk.StringVar(value="--")
        self.top_percent_var = tk.StringVar(value="--")
        self.score_var = tk.StringVar(value="--")
        self.ac_var = tk.StringVar(value="--")
        self.total_var = tk.StringVar(value="--")
        self.nickname_var = tk.StringVar(value="--")
        self.status_var = tk.StringVar(value="启动中，正在准备加载...")

        grid = ttk.Frame(card, style="Card.TFrame")
        grid.pack(fill="x")
        self.metric(grid, "排名", self.rank_var, 0, 0)
        self.metric(grid, "前百分比", self.top_percent_var, 0, 1)
        self.metric(grid, "OI 分数", self.score_var, 1, 0)
        self.metric(grid, "AC 数", self.ac_var, 1, 1)
        self.metric(grid, "总提交", self.total_var, 2, 0)

        ttk.Label(card, text="昵称", style="Key.TLabel").pack(anchor="w", pady=(18, 4))
        ttk.Label(card, textvariable=self.nickname_var, style="Value.TLabel", wraplength=460).pack(anchor="w")

        ttk.Label(root, textvariable=self.status_var, style="Subtle.TLabel").pack(anchor="w", pady=(14, 0))

    def metric(self, parent, label, variable, row, column):
        frame = ttk.Frame(parent, style="Card.TFrame", padding=(0, 0, 24, 14))
        frame.grid(row=row, column=column, sticky="ew")
        parent.columnconfigure(column, weight=1)
        ttk.Label(frame, text=label, style="Key.TLabel").pack(anchor="w")
        ttk.Label(frame, textvariable=variable, style="Value.TLabel").pack(anchor="w", pady=(4, 0))

    def refresh(self):
        if self.refreshing:
            return
        self.refreshing = True
        self.refresh_button.set_enabled(False)
        self.status_var.set("正在重新加载...")
        thread = threading.Thread(target=self.load_rank, daemon=True)
        thread.start()

    def load_rank(self):
        try:
            result = self.client.get_oi_rank(TARGET_USER)
            self.after(0, lambda: self.apply_result(result))
        except Exception as exc:
            self.after(0, lambda: self.apply_error(exc))

    def apply_result(self, result):
        user = result["user"]
        if user is None:
            self.rank_var.set("未找到")
            self.top_percent_var.set("--")
            self.score_var.set("--")
            self.ac_var.set("--")
            self.total_var.set("--")
            self.nickname_var.set("--")
            self.status_var.set(f"{result['loaded_at']} 加载完成，但未找到用户。")
        else:
            self.rank_var.set(f"第 {result['rank']} / {result['total_users']} 名")
            self.top_percent_var.set(f"前 {result['top_percent']:.2f}%")
            self.score_var.set(str(user.get("score", "--")))
            self.ac_var.set(str(user.get("ac", "--")))
            self.total_var.set(str(user.get("total", "--")))
            self.nickname_var.set(user.get("nickname") or "--")
            self.status_var.set(f"{result['loaded_at']} 已加载。下次自动刷新在 10 分钟后。")
        self.finish_refresh()

    def apply_error(self, exc):
        self.status_var.set(f"加载失败：{exc}")
        self.finish_refresh()

    def finish_refresh(self):
        self.refreshing = False
        self.refresh_button.set_enabled(True)

    def auto_refresh(self):
        self.refresh()
        self.after(REFRESH_INTERVAL_MS, self.auto_refresh)


if __name__ == "__main__":
    enable_high_dpi_awareness()
    App().mainloop()
