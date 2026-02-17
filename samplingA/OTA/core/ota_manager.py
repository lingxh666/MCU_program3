# -*- coding: utf-8 -*-
"""
OTA升级管理器
"""
import os
import time
import threading
import queue
from core.mqtt_client import MQTTClient
from core.packet_handler import PacketHandler
from utils.config import OTA_CONFIG, TOPIC_CONFIG
from utils.checksum import calculate_checksum, format_checksum


class OTAManager:
    """OTA升级管理器"""

    # 状态定义
    STATE_IDLE = 0
    STATE_CONNECTING = 1
    STATE_SEND_START = 2
    STATE_WAIT_ACK = 3
    STATE_SENDING_DATA = 4
    STATE_WAIT_PACKET_ACK = 5
    STATE_FINISHED = 6
    STATE_ERROR = 7

    def __init__(self, progress_callback=None, log_callback=None):
        """
        初始化OTA管理器

        Args:
            progress_callback: 进度更新回调函数
            log_callback: 日志输出回调函数
        """
        self.state = self.STATE_IDLE
        self.progress_callback = progress_callback
        self.log_callback = log_callback

        self.mqtt_client = MQTTClient()
        self.packet_handler = PacketHandler()

        self.device_id = OTA_CONFIG['default_device_id']
        self.file_path = None
        self.file_data = None
        self.file_checksum = 0
        self.total_packets = 0
        self.current_packet = 0

        self.event_queue = queue.Queue()
        self.worker_thread = None
        self.stop_flag = False

    def set_callbacks(self, progress_callback, log_callback):
        """设置回调函数"""
        self.progress_callback = progress_callback
        self.log_callback = log_callback

    def _log(self, message):
        """输出日志"""
        if self.log_callback:
            self.log_callback(message)
        else:
            print(message)

    def _update_progress(self, current, total):
        """更新进度"""
        if self.progress_callback:
            self.progress_callback(current, total)

    def start_upgrade(self, file_path, device_id=None):
        """
        开始OTA升级

        Args:
            file_path: 固件文件路径
            device_id: 设备编号

        Returns:
            bool: 是否成功启动
        """
        if self.state != self.STATE_IDLE:
            self._log("升级正在进行中，请勿重复启动")
            return False

        # 检查文件是否存在
        if not os.path.exists(file_path):
            self._log(f"文件不存在: {file_path}")
            return False

        # 设置设备编号
        if device_id:
            self.device_id = device_id

        # 读取文件
        try:
            with open(file_path, 'rb') as f:
                raw_data = f.read()
            self.file_path = file_path

            # 计算总包数和填充后的大小
            data_per_packet = OTA_CONFIG['packet_size']
            self.total_packets = (len(raw_data) + data_per_packet - 1) // data_per_packet
            padded_size = self.total_packets * data_per_packet

            # 最后一包不足256字节时，用0xFF填充到256字节
            padding_bytes = padded_size - len(raw_data)
            if padding_bytes > 0:
                self.file_data = raw_data + b'\xFF' * padding_bytes
            else:
                self.file_data = raw_data

            # 基于填充后的数据计算校验和
            self.file_checksum = calculate_checksum(self.file_data)

            self._log(f"文件加载成功: {os.path.basename(file_path)}")
            self._log(f"原始文件大小: {len(raw_data)} 字节")
            self._log(f"填充后大小: {len(self.file_data)} 字节")
            self._log(f"校验和: {format_checksum(self.file_checksum)}")
            self._log(f"每包数据: {data_per_packet} 字节")
            self._log(f"总包数: {self.total_packets}")
            self._log(f"每次发送: {OTA_CONFIG['packets_per_buffer']} 包")
        except Exception as e:
            self._log(f"读取文件失败: {e}")
            return False

        # 启动工作线程
        self.stop_flag = False
        self.worker_thread = threading.Thread(target=self._worker_loop)
        self.worker_thread.start()

        # 发送开始事件
        self.event_queue.put(('start', None))

        return True

    def stop_upgrade(self):
        """停止OTA升级"""
        self.stop_flag = True
        if self.worker_thread and self.worker_thread.is_alive():
            self.worker_thread.join(timeout=5)

        # 断开MQTT连接
        self.mqtt_client.disconnect()

        # 重置状态
        self.state = self.STATE_IDLE
        self._log("升级已停止")

    def _worker_loop(self):
        """工作线程主循环"""
        while not self.stop_flag:
            try:
                # 处理事件
                event, data = self.event_queue.get(timeout=1)
                self._handle_event(event, data)
            except queue.Empty:
                continue
            except Exception as e:
                self._log(f"处理事件异常: {e}")
                self.state = self.STATE_ERROR

    def _handle_event(self, event, data):
        """处理事件"""
        if event == 'start':
            self._on_start()
        elif event == 'connected':
            self._on_connected()
        elif event == 'start_ack':
            self._on_start_ack(data)
        elif event == 'packet_ack':
            self._on_packet_ack(data)
        elif event == 'timeout':
            self._on_timeout()
        elif event == 'error':
            self._on_error(data)

    def _on_start(self):
        """处理开始事件"""
        self.state = self.STATE_CONNECTING
        self._log("连接MQTT服务器...")

        # 连接MQTT服务器
        if self.mqtt_client.connect():
            self.event_queue.put(('connected', None))
        else:
            self.event_queue.put(('error', '连接MQTT服务器失败'))

    def _on_connected(self):
        """处理连接成功事件"""
        self.state = self.STATE_SEND_START
        self._log(f"连接成功，设备ID: {self.device_id}")

        # 订阅ACK主题（使用CYJOTA主题接收ACK）
        self.mqtt_client.subscribe(TOPIC_CONFIG['data_topic'], self._on_ack_message)

        # 发送升级开始指令（包含CRC16和固件大小）
        start_command = self.packet_handler.create_start_command(
            self.device_id,
            format_checksum(self.file_checksum),
            len(self.file_data)
        )
        self._log(f"发送升级指令: {start_command}")

        if self.mqtt_client.publish(TOPIC_CONFIG['command_topic'], start_command):
            self.state = self.STATE_WAIT_ACK
            self._start_timeout_timer()
        else:
            self.event_queue.put(('error', '发送升级指令失败'))

    def _on_ack_message(self, topic, payload):
        """处理收到的消息（ACK或数据）"""
        try:
            # 检查是否是ACK消息（文本格式）
            if isinstance(payload, bytes):
                # 尝试解码为文本
                try:
                    ack_str = payload.decode('utf-8')
                    # 如果成功解码且符合ACK格式，则处理为ACK
                    if ack_str.startswith('ACK_'):
                        self._log(f"收到ACK: {ack_str}")

                        # 解析ACK
                        ack = self.packet_handler.parse_ack(ack_str)
                        if ack:
                            if self.state == self.STATE_WAIT_ACK:
                                self.event_queue.put(('start_ack', ack))
                            elif self.state == self.STATE_WAIT_PACKET_ACK:
                                self.event_queue.put(('packet_ack', ack))
                        return
                except:
                    # 解码失败，说明是二进制数据包，不是ACK
                    pass

            # 如果不是ACK，忽略（因为我们只在这个主题上等待ACK）
            # 数据包是通过另一个方向发送的，我们不会在CYJOTA主题上收到数据包
            pass
        except Exception as e:
            self._log(f"处理消息异常: {e}")

    def _on_start_ack(self, ack):
        """处理开始指令的ACK"""
        self._cancel_timeout_timer()
        if ack['status'] == 'OK':
            self.state = self.STATE_SENDING_DATA
            self.current_packet = 0
            self._log("设备准备就绪，开始发送数据")
            self._send_next_packet()
        else:
            self.event_queue.put(('error', f'设备拒绝升级: {ack["status"]}'))

    def _send_next_packet(self):
        """发送下一个数据包（单包Base64发送）"""
        if self.current_packet >= self.total_packets:
            # 所有包发送完成
            self.state = self.STATE_FINISHED
            self._log("所有数据包发送完成")
            self._update_progress(self.total_packets, self.total_packets)
            time.sleep(2)  # 等待设备处理
            self.mqtt_client.disconnect()
            return

        # 获取当前要发送的包的起始序号
        start_packet_id = self.current_packet + 1  # 包序号从1开始

        # 创建2K缓冲区的多个数据包的Base64编码
        buffer_base64 = self.packet_handler.create_buffer_packets_base64(
            start_packet_id,
            self.total_packets,
            self.file_data
        )

        # 计算这批发送了多少包
        packets_per_buffer = OTA_CONFIG['packets_per_buffer']
        actual_packets = min(packets_per_buffer, self.total_packets - self.current_packet)

        # 直接发送Base64编码的数据，不需要前缀
        message = buffer_base64

        # 发送数据包
        self.state = self.STATE_WAIT_PACKET_ACK
        self._log(f"发送数据包批次 {start_packet_id}-{start_packet_id + actual_packets - 1}/{self.total_packets}")
        self._log(f"Base64数据大小: {len(buffer_base64)} 字符")
        self._log(f"包含包数: {actual_packets}")

        if self.mqtt_client.publish(TOPIC_CONFIG['data_topic'], message):
            self._start_timeout_timer()
        else:
            self.event_queue.put(('error', f'发送数据包批次失败'))

    def _on_packet_ack(self, ack):
        """处理数据包ACK"""
        self._cancel_timeout_timer()

        if ack['status'] == 'OK':
            # 发送成功 - 更新当前包号
            # ack['packet_id'] 是最后一个成功接收的包号
            self.current_packet = ack['packet_id']
            self._update_progress(self.current_packet, self.total_packets)

            # 检查是否还有未发送的包
            if self.current_packet < self.total_packets:
                self.state = self.STATE_SENDING_DATA
                # 【关键修改】添加延时，等待MCU处理完成（写Flash等）后再发送下一包
                # 避免发送太快导致MCU来不及处理
                time.sleep(0.3)  # 300ms延时
                self._send_next_packet()
            else:
                self.state = self.STATE_FINISHED
                self._log("OTA升级成功完成！")
                self.mqtt_client.disconnect()
        else:
            # 设备请求重发：ACK_x_ERROR 表示最后成功包号为 x
            if ack['packet_id'] > self.total_packets:
                self.event_queue.put(('error', f"设备返回异常ACK: {ack['packet_id']}_{ack['status']}"))
                return
            self._log(f"收到设备重发请求: ACK_{ack['packet_id']}_{ack['status']}")
            self.current_packet = min(self.current_packet, ack['packet_id'])
            self.state = self.STATE_SENDING_DATA
            self._send_next_packet()

    def _on_timeout(self):
        """处理超时"""
        self.event_queue.put(('error', '120s未收到设备指令，升级失败'))

    def _on_error(self, error_msg):
        """处理错误"""
        self._log(f"错误: {error_msg}")
        self.state = self.STATE_ERROR
        self.mqtt_client.disconnect()

    def _start_timeout_timer(self):
        """启动超时定时器"""
        def timeout_callback():
            if not self.stop_flag:
                self.event_queue.put(('timeout', None))

        timer = threading.Timer(OTA_CONFIG['timeout'], timeout_callback)
        timer.daemon = True
        timer.start()
        self.timeout_timer = timer

    def _cancel_timeout_timer(self):
        """取消超时定时器"""
        if hasattr(self, 'timeout_timer'):
            self.timeout_timer.cancel()
