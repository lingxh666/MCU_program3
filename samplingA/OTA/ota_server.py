#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
MQTT OTA服务器
用于向MCU发送固件升级数据包
"""

import paho.mqtt.client as mqtt
import time
import base64
import os
import sys
import json
from datetime import datetime
from utils.config import MQTT_CONFIG, TOPIC_CONFIG, OTA_CONFIG


def crc16_modbus(data: bytes) -> int:
    """
    CRC16-MODBUS算法
    多项式: 0x8005
    初始值: 0xFFFF
    输入反转: 是（按字节）
    输出反转: 是
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001  # 0xA001是0x8005的反转
            else:
                crc >>= 1
    return crc


class OTAServer:
    def __init__(self):
        self.client = mqtt.Client()
        self.device_id = OTA_CONFIG['default_device_id']
        self.trigger_topic = TOPIC_CONFIG['command_topic']
        self.data_topic = TOPIC_CONFIG['data_topic']

        # OTA状态
        self.ota_active = False
        self.current_packet_id = 0
        self.total_packets = 0
        self.firmware_data = None
        self.firmware_size = 0         # 原始固件大小
        self.firmware_crc16 = 0        # 固件CRC16校验值
        self.packet_size = OTA_CONFIG['packet_size']  # 每包数据大小

        # 统计信息
        self.start_time = None
        self.last_instruction_time = None
        self.sent_packets = 0
        self.ack_packets = []

        # 设置回调
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message
        self.client.on_publish = self.on_publish

    def on_connect(self, client, userdata, flags, rc):
        """连接成功回调"""
        if rc == 0:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 成功连接到MQTT服务器")
            # 订阅ACK主题
            client.subscribe(self.data_topic)
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 已订阅数据主题: {self.data_topic}")
        else:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 连接失败，错误码: {rc}")

    def on_disconnect(self, client, userdata, rc):
        """连接断开回调"""
        print(f"[{datetime.now().strftime('%H:%M:%S')}] 连接已断开，错误码: {rc}")

    def on_publish(self, client, userdata, mid):
        """消息发布成功回调"""
        pass

    def on_message(self, client, userdata, msg):
        """接收到消息回调"""
        try:
            payload = msg.payload.decode('utf-8')
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 收到消息: {payload}")

            # 解析ACK消息
            if payload.startswith("ACK_"):
                self.handle_ack(payload)
            # 处理重传请求
            elif payload == "DATA_INCOMPLETE":
                self.handle_incomplete_request()
            elif payload == "DATA_REQUEST_RETRANSMIT":
                self.handle_retransmit_request()

            self.last_instruction_time = time.time()
        except Exception as e:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 处理消息错误: {e}")

    def handle_ack(self, ack_msg):
        """处理ACK消息"""
        try:
            # 解析ACK_X_OK或ACK_X_ERROR格式
            if ack_msg.startswith("ACK_"):
                parts = ack_msg.split('_')
                if len(parts) >= 3:
                    packet_id = int(parts[1])
                    status = parts[2]
                    self.ack_packets.append(packet_id)

                    # 检查是否是期望的ACK
                    if status == "OK":
                        if packet_id == self.current_packet_id:
                            print(f"[{datetime.now().strftime('%H:%M:%S')}] 收到正确的ACK: 包{packet_id}")
                            # 发送下一个包
                            self.send_next_packet()
                        else:
                            print(f"[{datetime.now().strftime('%H:%M:%S')}] 收到过期的ACK: 包{packet_id}，当前期望: 包{self.current_packet_id}")
                    elif status == "ERROR":
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] 收到重发请求: 包{packet_id}")
                        if packet_id >= self.total_packets:
                            print(f"[错误] 请求重发的包号超出范围: {packet_id}/{self.total_packets}")
                            return
                        if packet_id < self.current_packet_id:
                            self.current_packet_id = packet_id
                            self.send_next_packet()
                        else:
                            self.send_packet(packet_id)
                    else:
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] 未知ACK状态: {ack_msg}")
            else:
                print(f"[{datetime.now().strftime('%H:%M:%S')}] 未知ACK格式: {ack_msg}")

        except Exception as e:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 解析ACK错误: {e}")

    def handle_incomplete_request(self):
        """处理数据不完整请求"""
        print(f"[{datetime.now().strftime('%H:%M:%S')}] 收到数据不完整请求，重传当前包")
        self.send_packet(self.current_packet_id)

    def handle_retransmit_request(self):
        """处理重传请求"""
        print(f"[{datetime.now().strftime('%H:%M:%S')}] 收到重传请求，重传当前包")
        self.send_packet(self.current_packet_id)

    def load_firmware(self, firmware_path):
        """加载固件文件"""
        if not os.path.exists(firmware_path):
            print(f"[错误] 固件文件不存在: {firmware_path}")
            return False

        try:
            with open(firmware_path, 'rb') as f:
                self.firmware_data = f.read()

            # 保存原始固件大小（用于MCU验证）
            self.firmware_size = len(self.firmware_data)

            # 计算CRC16-MODBUS（与MCU端一致）
            self.firmware_crc16 = crc16_modbus(self.firmware_data)

            # 计算总包数
            self.total_packets = (len(self.firmware_data) + self.packet_size - 1) // self.packet_size

            print(f"[{datetime.now().strftime('%H:%M:%S')}] 固件加载成功")
            print(f"  文件大小: {self.firmware_size} 字节")
            print(f"  CRC16: 0x{self.firmware_crc16:04X}")
            print(f"  数据包大小: {self.packet_size} 字节/包")
            print(f"  总包数: {self.total_packets}")
            print(f"  预计Base64长度: {((self.packet_size + 9) * 4 + 2) // 3} 字符/包")

            return True

        except Exception as e:
            print(f"[错误] 加载固件失败: {e}")
            return False

    def create_data_packet(self, packet_id, data):
        """创建数据包"""
        packet = bytearray()

        # 包头 (0xAA55) - 小端序
        packet.extend((0xAA55).to_bytes(2, 'little'))

        # 包序号 - 小端序
        packet.extend(packet_id.to_bytes(2, 'little'))

        # 总包数 - 小端序
        packet.extend(self.total_packets.to_bytes(2, 'little'))

        # 数据长度 - 小端序（始终为固定大小）
        packet.extend(self.packet_size.to_bytes(2, 'little'))

        # 计算校验和（8位累加和）
        checksum = sum(data) & 0xFF
        packet.append(checksum)

        # 数据内容
        packet.extend(data)

        return bytes(packet)

    def send_packet(self, packet_id):
        """发送指定包号的数据包"""
        if packet_id > self.total_packets:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 包号超出范围: {packet_id}/{self.total_packets}")
            return

        # 计算数据偏移
        offset = (packet_id - 1) * self.packet_size
        data = self.firmware_data[offset:offset + self.packet_size]

        # 如果最后一包数据不足，用0xFF填充
        if len(data) < self.packet_size:
            data += b'\xFF' * (self.packet_size - len(data))

        # 创建数据包
        packet = self.create_data_packet(packet_id, data)

        # Base64编码
        packet_base64 = base64.b64encode(packet).decode('utf-8')

        # 直接发送Base64编码的数据，不需要前缀
        message = packet_base64
        result = self.client.publish(self.data_topic, message)

        # 更新当前包号
        self.current_packet_id = packet_id
        self.sent_packets += 1

        print(f"[{datetime.now().strftime('%H:%M:%S')}] 发送包 {packet_id}/{self.total_packets}")
        print(f"  Base64长度: {len(packet_base64)} 字符")

    def send_next_packet(self):
        """发送下一个数据包"""
        if self.current_packet_id < self.total_packets:
            self.send_packet(self.current_packet_id + 1)
        else:
            # 所有包发送完成
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 所有数据包发送完成")
            self.complete_ota()

    def start_ota(self, firmware_path):
        """开始OTA升级"""
        print("\n" + "="*60)
        print("开始OTA升级")
        print("="*60)

        # 加载固件
        if not self.load_firmware(firmware_path):
            return False

        # 重置状态
        self.ota_active = True
        self.current_packet_id = 0
        self.sent_packets = 0
        self.ack_packets = []
        self.start_time = time.time()
        self.last_instruction_time = time.time()

        # 发送OTA触发指令（新格式：包含CRC16和原始固件大小）
        # 格式: ML307OTA_{设备ID}_{CRC16}_{固件大小}
        trigger_msg = f"ML307OTA_{self.device_id}_{self.firmware_crc16:04X}_{self.firmware_size}"
        self.client.publish(self.trigger_topic, trigger_msg)
        print(f"[{datetime.now().strftime('%H:%M:%S')}] 发送OTA触发指令: {trigger_msg}")

        # 等待设备准备就绪（设备需要擦除252K Flash，约需10-20秒）
        print(f"[{datetime.now().strftime('%H:%M:%S')}] 等待设备擦除Flash并准备就绪...")
        time.sleep(30)  # 增加等待时间，等待设备完成Flash擦除

        # 开始发送第一个包
        self.send_packet(1)

        return True

    def stop_ota(self):
        """停止OTA升级"""
        self.ota_active = False
        elapsed = time.time() - self.start_time if self.start_time else 0
        print(f"\n[{datetime.now().strftime('%H:%M:%S')}] OTA升级中断")
        print(f"  已用时间: {elapsed:.2f} 秒")
        print(f"  已发送包数: {self.sent_packets}")
        print(f"  已确认包数: {len(self.ack_packets)}")

    def complete_ota(self):
        """完成OTA升级"""
        self.ota_active = False
        elapsed = time.time() - self.start_time if self.start_time else 0

        print("\n" + "="*60)
        print("OTA升级完成")
        print("="*60)
        print(f"  设备ID: {self.device_id}")
        print(f"  固件大小: {len(self.firmware_data)} 字节")
        print(f"  总包数: {self.total_packets}")
        print(f"  发送包数: {self.sent_packets}")
        print(f"  确认包数: {len(self.ack_packets)}")
        print(f"  耗时: {elapsed:.2f} 秒")
        print(f"  平均速率: {(len(self.firmware_data) / elapsed if elapsed > 0 else 0):.2f} 字节/秒")
        print("="*60)

    def run(self, firmware_path=None):
        """运行OTA服务器"""
        # 连接到MQTT服务器
        print(f"[{datetime.now().strftime('%H:%M:%S')}] 正在连接到MQTT服务器 {MQTT_CONFIG['host']}:{MQTT_CONFIG['port']}...")

        try:
            self.client.connect(MQTT_CONFIG['host'], MQTT_CONFIG['port'], 60)
            self.client.loop_start()

            # 等待连接完成
            time.sleep(2)

            # 如果指定了固件文件，自动开始OTA
            if firmware_path:
                self.start_ota(firmware_path)
                # 等待OTA完成或中断
                while self.ota_active:
                    if self.last_instruction_time and time.time() - self.last_instruction_time > OTA_CONFIG['timeout']:
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] 120s未收到设备指令，升级失败")
                        self.stop_ota()
                        break
                    time.sleep(1)
            else:
                # 交互模式
                print("\n进入交互模式，输入命令:")
                print("  start <固件文件路径> - 开始OTA升级")
                print("  stop - 停止OTA升级")
                print("  quit - 退出程序")

                while True:
                    try:
                        cmd = input("\nOTA> ").strip()
                        if cmd.startswith("start "):
                            firmware_path = cmd[6:].strip()
                            self.start_ota(firmware_path)
                            while self.ota_active:
                                time.sleep(1)
                        elif cmd == "stop":
                            self.stop_ota()
                        elif cmd == "quit":
                            break
                        else:
                            print("未知命令")
                    except KeyboardInterrupt:
                        print("\n\n用户中断")
                        break

        except Exception as e:
            print(f"[错误] {e}")
        finally:
            # 断开连接
            self.client.loop_stop()
            self.client.disconnect()
            print(f"\n[{datetime.now().strftime('%H:%M:%S')}] 已断开连接")


def main():
    """主函数"""
    import argparse

    parser = argparse.ArgumentParser(description='MQTT OTA服务器')
    parser.add_argument('firmware', nargs='?', help='固件文件路径')
    parser.add_argument('--device-id', default='CYJ_2512121H1001B', help='设备ID')

    args = parser.parse_args()

    # 创建OTA服务器
    server = OTAServer()
    server.device_id = args.device_id

    # 运行服务器
    server.run(args.firmware)


if __name__ == "__main__":
    main()
