#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
MQTT OTA固件升级工具
主程序入口
"""
import sys
import os

# 添加当前目录到Python路径
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gui.main_window import MainWindow


def main():
    """主函数"""
    try:
        # 创建并运行主窗口
        app = MainWindow()
        app.run()
    except Exception as e:
        import traceback
        print(f"程序运行异常: {e}")
        traceback.print_exc()
        input("按任意键退出...")


if __name__ == "__main__":
    main()