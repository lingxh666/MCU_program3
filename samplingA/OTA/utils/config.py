# -*- coding: utf-8 -*-
"""
配置文件
"""

# MQTT服务器配置
MQTT_CONFIG = {
    'host': '124.222.59.221',
    'port': 1883,
    'keepalive': 60,
    'qos': 0
}

# OTA配置
OTA_CONFIG = {
    'packet_size': 256,         # 每个数据包的实际数据大小（256字节）
    'packet_header_size': 9,    # 数据包头大小（9字节）
    'total_packet_size': 265,   # 总包大小（256字节数据 + 9字节包头）
    'base64_packet_size': 356,  # Base64编码后的大小
    'buffer_size': 2048,        # 缓冲区大小（2K）
    'packets_per_buffer': 1,    # 每次发送单包，避免超过512字节限制
    'timeout': 120,             # 超时时间(秒)
    'max_retries': 3,           # 最大重试次数
    'retry_interval': 2,        # 重试间隔(秒)
    'progress_interval': 0.1,   # 进度更新间隔(秒)
    'packet_header': 0xAA55,    # 包头标识
    'default_device_id': 'CYJ_2512121H1001B'  # 默认设备编号
}

# 主题配置
TOPIC_CONFIG = {
    'command_topic': 'CYJSET',        # 命令主题
    'data_topic': 'CYJOTA',           # 数据主题
}

# 界面配置
GUI_CONFIG = {
    'title': 'MQTT OTA固件升级工具 v1.0',
    'width': 800,
    'height': 600,
    'log_max_lines': 1000,            # 日志最大行数
}
