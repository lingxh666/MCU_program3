# -*- coding: utf-8 -*-
"""
GUI主窗口
"""
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import threading
import time
import os
from datetime import datetime
from core.ota_manager import OTAManager
from utils.config import GUI_CONFIG, OTA_CONFIG
from utils.checksum import calculate_checksum, format_checksum


class MainWindow:
    """主窗口类"""

    def __init__(self):
        """初始化主窗口"""
        self.root = tk.Tk()
        self.root.title(GUI_CONFIG['title'])
        self.root.geometry(f"{GUI_CONFIG['width']}x{GUI_CONFIG['height']}")
        self.root.resizable(True, True)

        # OTA管理器
        self.ota_manager = OTAManager(
            progress_callback=self.update_progress,
            log_callback=self.append_log
        )

        # 文件信息
        self.file_path = None
        self.file_size = 0
        self.file_checksum = 0

        # 设置UI
        self.setup_ui()

        # 绑定关闭事件
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

    def setup_ui(self):
        """设置UI布局"""
        # 创建主框架
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))

        # 配置网格权重
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        main_frame.rowconfigure(4, weight=1)

        # 设备信息框架
        info_frame = ttk.LabelFrame(main_frame, text="设备信息", padding="5")
        info_frame.grid(row=0, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=(0, 10))
        info_frame.columnconfigure(1, weight=1)

        ttk.Label(info_frame, text="设备编号:").grid(row=0, column=0, sticky=tk.W)
        self.device_id_var = tk.StringVar(value=OTA_CONFIG['default_device_id'])
        ttk.Entry(info_frame, textvariable=self.device_id_var, width=30).grid(row=0, column=1, sticky=(tk.W, tk.E), padx=(5, 0))

        ttk.Label(info_frame, text="状态:").grid(row=1, column=0, sticky=tk.W, pady=(5, 0))
        self.status_var = tk.StringVar(value="未连接")
        self.status_label = ttk.Label(info_frame, textvariable=self.status_var, foreground="gray")
        self.status_label.grid(row=1, column=1, sticky=tk.W, padx=(5, 0), pady=(5, 0))

        # 文件选择框架
        file_frame = ttk.LabelFrame(main_frame, text="文件选择", padding="5")
        file_frame.grid(row=1, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=(0, 10))
        file_frame.columnconfigure(0, weight=1)

        # 文件路径显示
        self.file_path_var = tk.StringVar(value="请选择bin文件")
        self.file_entry = ttk.Entry(file_frame, textvariable=self.file_path_var, state="readonly")
        self.file_entry.grid(row=0, column=0, sticky=(tk.W, tk.E), padx=(0, 5))

        ttk.Button(file_frame, text="选择文件", command=self.on_select_file).grid(row=0, column=1)

        # 文件信息显示
        self.file_info_var = tk.StringVar(value="")
        self.file_info_label = ttk.Label(file_frame, textvariable=self.file_info_var, foreground="blue")
        self.file_info_label.grid(row=1, column=0, columnspan=2, sticky=tk.W, pady=(5, 0))

        # 进度显示框架
        progress_frame = ttk.LabelFrame(main_frame, text="升级进度", padding="5")
        progress_frame.grid(row=2, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=(0, 10))
        progress_frame.columnconfigure(0, weight=1)

        # 进度条
        self.progress_var = tk.DoubleVar(value=0)
        self.progress_bar = ttk.Progressbar(progress_frame, variable=self.progress_var, maximum=100)
        self.progress_bar.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 5))

        # 进度文本
        self.progress_text_var = tk.StringVar(value="0/0 包 (0%)")
        self.progress_text_label = ttk.Label(progress_frame, textvariable=self.progress_text_var)
        self.progress_text_label.grid(row=1, column=0, sticky=tk.W)

        # 日志框架
        log_frame = ttk.LabelFrame(main_frame, text="日志信息", padding="5")
        log_frame.grid(row=4, column=0, columnspan=2, sticky=(tk.W, tk.E, tk.N, tk.S), pady=(0, 10))
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)

        # 日志文本框
        self.log_text = tk.Text(log_frame, wrap=tk.WORD, height=15)
        log_scrollbar = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=log_scrollbar.set)

        self.log_text.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        log_scrollbar.grid(row=0, column=1, sticky=(tk.N, tk.S))

        # 配置日志文本标签
        self.log_text.tag_config("time", foreground="gray")
        self.log_text.tag_config("info", foreground="black")
        self.log_text.tag_config("error", foreground="red")
        self.log_text.tag_config("success", foreground="green")

        # 控制按钮框架
        button_frame = ttk.Frame(main_frame)
        button_frame.grid(row=5, column=0, columnspan=2, pady=(0, 0))

        self.start_button = ttk.Button(button_frame, text="开始升级", command=self.on_start_upgrade)
        self.start_button.grid(row=0, column=0, padx=(0, 5))

        self.stop_button = ttk.Button(button_frame, text="停止", command=self.on_stop_upgrade, state="disabled")
        self.stop_button.grid(row=0, column=1, padx=(0, 5))

        ttk.Button(button_frame, text="清空日志", command=self.on_clear_log).grid(row=0, column=2, padx=(0, 5))

        ttk.Button(button_frame, text="关于", command=self.on_about).grid(row=0, column=3)

    def on_select_file(self):
        """选择文件事件"""
        file_path = filedialog.askopenfilename(
            title="选择固件文件",
            filetypes=[("BIN文件", "*.bin"), ("所有文件", "*.*")]
        )

        if file_path:
            self.file_path = file_path
            self.file_path_var.set(file_path)

            # 获取文件信息
            self.file_size = os.path.getsize(file_path)
            self.file_info_var.set(f"文件大小: {self.file_size:,} 字节")

            # 计算校验和
            try:
                with open(file_path, 'rb') as f:
                    data = f.read()
                self.file_checksum = calculate_checksum(data)
                self.file_info_var.set(f"文件大小: {self.file_size:,} 字节  校验和: {format_checksum(self.file_checksum)}")
            except Exception as e:
                self.append_log(f"计算校验和失败: {e}", "error")

    def on_start_upgrade(self):
        """开始升级事件"""
        if not self.file_path:
            messagebox.showwarning("警告", "请先选择固件文件")
            return

        # 确认对话框
        result = messagebox.askyesno(
            "确认升级",
            f"确定要开始升级吗？\n\n设备编号: {self.device_id_var.get()}\n固件文件: {os.path.basename(self.file_path)}"
        )
        if not result:
            return

        # 更新UI状态
        self.start_button.config(state="disabled")
        self.stop_button.config(state="normal")
        self.status_var.set("升级中...")
        self.status_label.config(foreground="blue")

        # 清空进度
        self.update_progress(0, 0)

        # 开始升级
        if self.ota_manager.start_upgrade(self.file_path, self.device_id_var.get()):
            self.append_log("升级任务已启动", "info")
        else:
            self.upgrade_finished(False, "启动升级失败")

    def on_stop_upgrade(self):
        """停止升级事件"""
        result = messagebox.askyesno("确认停止", "确定要停止当前升级吗？")
        if result:
            self.ota_manager.stop_upgrade()
            self.upgrade_finished(False, "用户停止升级")

    def on_clear_log(self):
        """清空日志"""
        self.log_text.delete(1.0, tk.END)

    def on_about(self):
        """关于对话框"""
        messagebox.showinfo(
            "关于",
            f"{GUI_CONFIG['title']}\n\n"
            "MQTT固件升级工具\n"
            "支持通过MQTT协议进行远程OTA升级\n\n"
            "功能特点:\n"
            "• 友好的图形界面\n"
            "• 实时进度显示\n"
            "• 完整的日志记录\n"
            "• 自动重试机制"
        )

    def on_closing(self):
        """窗口关闭事件"""
        if self.ota_manager.state != self.ota_manager.STATE_IDLE:
            result = messagebox.askyesno("确认退出", "升级正在进行中，确定要退出吗？")
            if result:
                self.ota_manager.stop_upgrade()
            else:
                return

        self.root.destroy()

    def update_progress(self, current, total):
        """更新进度显示"""
        if total > 0:
            percent = (current / total) * 100
            self.progress_var.set(percent)
            self.progress_text_var.set(f"{current}/{total} 包 ({percent:.1f}%)")
        else:
            self.progress_var.set(0)
            self.progress_text_var.set("0/0 包 (0%)")

    def append_log(self, message, level="info"):
        """添加日志信息"""
        # 获取当前时间
        timestamp = datetime.now().strftime("%H:%M:%S")

        # 插入日志
        self.log_text.insert(tk.END, f"[{timestamp}] ", "time")
        self.log_text.insert(tk.END, f"{message}\n", level)

        # 限制日志行数
        lines = self.log_text.get(1.0, tk.END).split('\n')
        if len(lines) > GUI_CONFIG['log_max_lines']:
            # 删除多余的行
            self.log_text.delete(1.0, f"{len(lines) - GUI_CONFIG['log_max_lines'] + 1}.0")

        # 自动滚动到底部
        self.log_text.see(tk.END)

    def upgrade_finished(self, success, message=None):
        """升级完成"""
        # 更新UI状态
        self.start_button.config(state="normal")
        self.stop_button.config(state="disabled")

        if success:
            self.status_var.set("升级成功")
            self.status_label.config(foreground="green")
            self.append_log("升级完成！", "success")
            messagebox.showinfo("成功", "固件升级成功完成！")
        else:
            self.status_var.set("升级失败")
            self.status_label.config(foreground="red")
            self.append_log(f"升级失败: {message}", "error")
            if message:
                messagebox.showerror("失败", f"升级失败:\n{message}")

    def run(self):
        """运行主循环"""
        self.root.mainloop()