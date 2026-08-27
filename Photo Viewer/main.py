import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from PIL import Image, ImageTk
import io
import os
import re
import xml.etree.ElementTree as ET

"""
图片查看器主类：基于 Tkinter 和 Pillow 构建
采用视口(Viewport)渲染模式，支持无限级无损放大
"""
class ImageViewerApp:
    def __init__(self, root):
        # 绑定根窗口并设置基础属性
        self.root = root
        self.root.title("OMG Photo Viewer")
        self.root.geometry("900x650")
        
        # 初始化核心图像对象变量
        self.orig_img = None
        self.tk_img = None
        
        # 缩放比例，1.0 代表 100%，最大可设为 500.0
        self.scale = 1.0
        # cx 和 cy 记录当前视口中心点在原图上的坐标
        self.cx = 0
        self.cy = 0
        
        # 用于记录鼠标拖拽时的起始坐标
        self.drag_data = {"x": 0, "y": 0}

        # 调用 UI 构建方法
        self._setup_ui()

    def _setup_ui(self):
        # 创建顶部控制面板，放置按钮和下拉框
        control_frame = tk.Frame(self.root)
        control_frame.pack(side=tk.TOP, fill=tk.X, pady=10, padx=10)

        # 放置打开图片按钮，点击触发 open_image 方法
        tk.Button(control_frame, text="打开图片 📂", command=self.open_image).pack(side=tk.LEFT, padx=5)

        # 标签：用于实时显示当前的缩放百分比
        self.scale_label = tk.Label(control_frame, text="缩放: 100%")
        self.scale_label.pack(side=tk.LEFT, padx=15)

        # 格式转换区域的提示标签
        tk.Label(control_frame, text="目标格式:").pack(side=tk.LEFT)
        
        # 下拉框：提供支持的转换格式选项 (去掉了 HEIF，加入了 TIFF)
        self.format_cb = ttk.Combobox(control_frame, values=["JPG", "PNG", "WEBP", "ICO", "BMP", "TIFF"], state="readonly", width=8)
        self.format_cb.set("PNG")
        self.format_cb.pack(side=tk.LEFT, padx=5)
        
        # 放置保存按钮，点击触发 save_image 方法
        tk.Button(control_frame, text="转换并保存", command=self.save_image).pack(side=tk.LEFT, padx=5)

        # 创建主画布，用于渲染图像和捕捉鼠标事件，背景设为深灰色
        self.canvas = tk.Canvas(self.root, bg="#2c2c2c")
        self.canvas.pack(side=tk.BOTH, expand=True)

        # 绑定鼠标滚轮事件，用于不同操作系统的缩放处理
        self.canvas.bind("<MouseWheel>", self.on_zoom)  # 适用 Windows 和 macOS
        self.canvas.bind("<Button-4>", self.on_zoom)    # 适用 Linux 的滚轮上滚
        self.canvas.bind("<Button-5>", self.on_zoom)    # 适用 Linux 的滚轮下滚
        
        # 绑定鼠标左键的按下和拖动事件，实现画面平移
        self.canvas.bind("<ButtonPress-1>", self.on_drag_start)
        self.canvas.bind("<B1-Motion>", self.on_drag_motion)
        
        # 窗口大小改变时，触发重新计算和渲染，保持图像适应
        self.canvas.bind("<Configure>", lambda e: self.update_view())

    def parse_svg_to_image(self, path):
        # 纯 Python 标准库解析 SVG 宽高并转为无损占位/近似图的逻辑
        # 读取 SVG 的文本内容
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            svg_text = f.read()
        
        # 移除可能存在的命名空间以方便解析
        svg_text = re.sub(' xmlns="[^"]+"', '', svg_text, count=1)
        root = ET.fromstring(svg_text)
        
        # 获取 SVG 标签中定义的宽高，如果没有则默认为 500
        width = int(float(root.attrib.get('width', 500)))
        height = int(float(root.attrib.get('height', 500)))
        
        # 创建一个透明的基底图片
        img = Image.new("RGBA", (width, height), (255, 255, 255, 0))
        return img

    def open_image(self):
        # 定义支持的文件过滤器
        filetypes = [("Images", "*.jpg *.jpeg *.png *.svg *.webp *.ico *.bmp *.tiff *.tif")]
        path = filedialog.askopenfilename(filetypes=filetypes)
        
        # 如果用户取消了选择，直接返回，不做处理
        if not path:
            return

        try:
            # 获取文件后缀名并统一转为小写进行判断
            ext = os.path.splitext(path)[1].lower()
            
            # SVG 矢量图特殊处理：改用纯自带标准库解析
            if ext == '.svg':
                self.orig_img = self.parse_svg_to_image(path)
                # 如果标准库解析出的 SVG 是空图，提示用户但依然建立画布
                if self.orig_img is None:
                    raise Exception("SVG 文件解析失败")
            else:
                # 其他常规位图格式直接用 Pillow 读取
                self.orig_img = Image.open(path)

            # 图片加载成功后，重置缩放比例为原始大小 (1.0)
            self.scale = 1.0
            
            # 将视口中心点重置为原图的物理中心
            self.cx = self.orig_img.width / 2
            self.cy = self.orig_img.height / 2
            
            # 调用核心渲染函数，更新画布显示
            self.update_view()
            
        except Exception as e:
            # 捕获异常，比如不支持的格式或文件损坏，弹出错误提示
            messagebox.showerror("错误", f"无法加载图片:\n{e}")

    def update_view(self):
        # 如果还没加载原图，或者获取不到画布尺寸，直接退出
        if self.orig_img is None:
            return
        cw = self.canvas.winfo_width()
        ch = self.canvas.winfo_height()
        if cw <= 1 or ch <= 1:
            return

        # 根据当前的缩放比例，计算画布对应原图的“真实可见宽高”
        view_w = cw / self.scale
        view_h = ch / self.scale

        # 根据中心点和可见宽高，计算原图上需要裁剪的四个边界坐标
        left = self.cx - view_w / 2
        top = self.cy - view_h / 2
        right = self.cx + view_w / 2
        bottom = self.cy + view_h / 2

        # 执行裁剪。即使坐标超出了原图范围，PIL crop 也会自动用黑边填充补齐
        crop_box = (int(left), int(top), int(right), int(bottom))
        cropped = self.orig_img.crop(crop_box)

        # 将裁剪出的小图，强行拉伸到画布的大小
        # 放大时强制使用 NEAREST (最近邻插值)，确保放大 500 倍能看清像素边缘
        resized = cropped.resize((cw, ch), Image.Resampling.NEAREST)

        # 转换为 Tkinter 能够识别的图像对象 (PhotoImage)
        self.tk_img = ImageTk.PhotoImage(resized)
        
        # 清空画布上的旧图像，并在正中心绘制新图像
        self.canvas.delete("all")
        self.canvas.create_image(cw/2, ch/2, image=self.tk_img, anchor=tk.CENTER)
        
        # 格式化更新界面上方的缩放比例文本
        self.scale_label.config(text=f"缩放: {int(self.scale * 100)}%")

    def on_zoom(self, event):
        # 没有图片时忽略滚轮操作
        if self.orig_img is None:
            return

        # 判断滚动方向：Windows 用 delta 判定，Linux 靠事件数字判定
        if event.delta > 0 or event.num == 4:
            self.scale *= 1.25  # 每次放大 25%
        elif event.delta < 0 or event.num == 5:
            self.scale /= 1.25  # 每次缩小 25%

        # 限制极限值：最小缩放 1%，最大放大到 50000%（即 500 倍）
        self.scale = max(0.01, min(self.scale, 500.0))
        
        # 比例更新后，立刻触发重绘
        self.update_view()

    def on_drag_start(self, event):
        # 鼠标左键按下时，记录当前屏幕上的 x 和 y 坐标
        self.drag_data["x"] = event.x
        self.drag_data["y"] = event.y

    def on_drag_motion(self, event):
        # 如果没有图片在显示，忽略拖拽
        if self.orig_img is None:
            return

        # 计算鼠标在屏幕上移动的像素差值
        dx = event.x - self.drag_data["x"]
        dy = event.y - self.drag_data["y"]

        # 将屏幕上的差值除以缩放比例，转换成原图真实的移动距离，反向更新中心点
        self.cx -= dx / self.scale
        self.cy -= dy / self.scale

        # 更新记录的鼠标坐标，以便下一次运动计算
        self.drag_data["x"] = event.x
        self.drag_data["y"] = event.y
        
        # 中心点变化后，实时重绘视图，形成顺滑拖动效果
        self.update_view()

    def save_image(self):
        # 检查是否有需要保存的图像
        if self.orig_img is None:
            return

        # 获取下拉框中选择的目标格式
        fmt = self.format_cb.get().lower()
        # Pillow 中保存 JPG 的名称必须是 JPEG
        save_format = 'JPEG' if fmt == 'jpg' else fmt.upper()
        
        # 弹出保存对话框，限制用户只能保存为选择的后缀
        path = filedialog.asksaveasfilename(defaultextension=f".{fmt}", 
                                            filetypes=[(fmt.upper(), f"*.{fmt}")])
        if not path:
            return

        try:
            # 准备输出图像的数据对象
            out_img = self.orig_img
            
            # 兼容性处理：如果导出 JPG，且原图含有 Alpha 透明通道，强制转为 RGB 模式防止报错
            if save_format == 'JPEG' and out_img.mode in ('RGBA', 'LA', 'P'):
                out_img = out_img.convert('RGB')
            
            # 使用 Pillow 按照指定格式写出到磁盘
            out_img.save(path, format=save_format)
            messagebox.showinfo("成功", "图片已成功转换并保存！")
            
        except Exception as e:
            # 捕获保存过程中可能出现的文件占用或权限报错
            messagebox.showerror("转换失败", f"保存时发生错误:\n{e}")

# 程序的入口执行点
if __name__ == "__main__":
    # 初始化 Tkinter 根对象
    root = tk.Tk()
    # 实例化并挂载我们自定义的应用界面
    app = ImageViewerApp(root)
    # 启动 Tkinter 的事件循环，维持窗口显示
    root.mainloop()