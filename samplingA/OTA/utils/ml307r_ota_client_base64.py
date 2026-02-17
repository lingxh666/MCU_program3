# -*- coding: utf-8 -*-
"""
MQTT OTA升级客户端 - 支持Base64编码
为ML307R模块设计的OTA升级工具，支持Base64编码传输二进制数据
"""

import paho.mqtt.client as mqtt
import time
import os
import base64
from config import MQTT_CONFIG, OTA_CONFIG, TOPIC_CONFIG, GUI_CONFIG


class ML307ROTAClient:
    def __init__(self):
        self.client = mqtt.Client()
        self.device_id = OTA_CONFIG['default_device_id']
        self.connected = False
        self.ota_active = False
        self.current_file = None
        self.file_data = None
        self.total_packets = 0
        self.current_packet = 0
        self.received_acks = set()
        self.last_ack_time = 0
        self.retransmit_request = None

        # 回调函数绑定
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message
        self.client.on_publish = self._on_publish

    def _on_connect(self, client, userdata, flags, rc):
        """连接成功回调"""
        if rc == 0:
            self.connected = True
            print(f"✅ 成功连接到MQTT服务器 {MQTT_CONFIG['host']}:{MQTT_CONFIG['port']}")
            # 订阅命令主题
            client.subscribe(TOPIC_CONFIG['command_topic'])
            print(f"📡 订阅命令主题: {TOPIC_CONFIG['command_topic']}")
        else:
            print(f"❌ 连接失败，错误码: {rc}")

    def _on_disconnect(self, client, userdata, rc):
        """连接断开回调"""
        self.connected = False
        print(f"🔌 连接已断开，错误码: {rc}")

    def _on_message(self, client, userdata, msg):
        """收到消息回调"""
        try:
            payload = msg.payload.decode('utf-8')
            topic = msg.topic
            print(f"\n📨 收到消息 [{topic}]: {payload}")

            # 处理OTA相关消息
            if topic == TOPIC_CONFIG['command_topic']:
                self._handle_command(payload)
            elif topic == TOPIC_CONFIG['data_topic']:
                self._handle_data_response(payload)

        except Exception as e:
            print(f"❌ 消息处理错误: {e}")

    def _on_publish(self, client, userdata, mid):
        """消息发布成功回调"""
        print(f"✅ 消息发布成功: msg_id={mid}")

    def _handle_command(self, payload):
        """处理命令消息"""
        if f"ML307OTA_{self.device_id}" in payload:
            print(f"🎯 收到设备 {self.device_id} 的OTA升级请求")
            self._start_ota_response()
        elif payload.startswith("ACK_"):
            # 解析ACK消息
            try:
                ack_parts = payload.split("_")
                packet_id = int(ack_parts[1])
                status = ack_parts[2] if len(ack_parts) > 2 else "UNKNOWN"
                if status == "OK":
                    print(f"✅ 收到数据包 {packet_id} 的ACK确认")
                    self.received_acks.add(packet_id)
                elif status == "ERROR":
                    print(f"⚠️ 收到重发请求: 包{packet_id}")
                    self.retransmit_request = packet_id
                else:
                    print(f"⚠️ 未知ACK状态: {payload}")
                self.last_ack_time = time.time()
            except:
                pass

    def _handle_data_response(self, payload):
        """处理数据响应"""
        # 这里可以处理设备返回的其他数据
        pass

    def _start_ota_response(self):
        """响应OTA升级请求"""
        # 发送OTA升级指令
        cmd = f"ML307OTA_{self.device_id}_START"
        self.client.publish(TOPIC_CONFIG['command_topic'], cmd)
        print(f"📤 发送OTA升级指令: {cmd}")

        # 延迟后开始发送文件
        time.sleep(2)
        self._prepare_file_transfer()

    def _prepare_file_transfer(self):
        """准备文件传输"""
        if not self.current_file:
            print("❌ 没有选择要升级的文件")
            return

        try:
            # 读取固件文件
            with open(self.current_file, 'rb') as f:
                self.file_data = f.read()

            print(f"📁 固件文件: {self.current_file}")
            print(f"📏 文件大小: {len(self.file_data)} 字节")

            # 计算总包数（每个包固定大小数据）
            packet_size = OTA_CONFIG['packet_size']
            data_per_packet = packet_size
            self.total_packets = (len(self.file_data) + data_per_packet - 1) // data_per_packet

            print(f"📦 每包数据大小: {data_per_packet} 字节")
            print(f"📦 总包数: {self.total_packets}")

            # 重置状态
            self.current_packet = 0
            self.received_acks = set()
            self.retransmit_request = None
            self.ota_active = True

            # 开始发送数据包
            self._send_next_packet()

        except Exception as e:
            print(f"❌ 文件读取失败: {e}")

    def _send_next_packet(self):
        """发送下一个数据包"""
        if not self.ota_active or self.current_packet >= self.total_packets:
            if self.current_packet >= self.total_packets:
                print("\n🎉 所有数据包发送完成！")
                self._verify_completion()
            return

        try:
            # 构建数据包
            packet_data = self._build_packet(self.current_packet + 1)

            # 将二进制数据包转换为Base64编码
            packet_base64 = base64.b64encode(packet_data).decode('utf-8')

            # 构建MQTT消息（仅Base64编码数据）
            message = packet_base64

            # 发送到数据主题
            result = self.client.publish(TOPIC_CONFIG['data_topic'], message, qos=MQTT_CONFIG['qos'])

            print(f"📤 发送数据包 {self.current_packet + 1}/{self.total_packets} "
                  f"(Base64长度: {len(packet_base64)})")

            # 等待ACK确认或重发请求
            timeout = time.time() + OTA_CONFIG['timeout']
            while time.time() < timeout:
                if self.current_packet + 1 in self.received_acks:
                    break
                if self.retransmit_request is not None:
                    break
                time.sleep(0.1)

            if self.current_packet + 1 in self.received_acks:
                # 成功收到ACK，发送下一个包
                self.current_packet += 1
                # 短暂延迟后发送下一包
                time.sleep(OTA_CONFIG['retry_interval'])
                self._send_next_packet()
            elif self.retransmit_request is not None:
                # 设备请求重发
                self.current_packet = min(self.current_packet, self.retransmit_request)
                self.retransmit_request = None
                self._send_next_packet()
            else:
                # 超时未收到任何指令，失败退出
                print("❌ 120s未收到设备指令，升级失败")
                self.ota_active = False

        except Exception as e:
            print(f"❌ 发送数据包失败: {e}")
            self.ota_active = False

    def _build_packet(self, packet_id):
        """构建数据包（二进制格式）"""
        data_per_packet = OTA_CONFIG['packet_size']

        # 计算当前包的数据范围
        start_pos = (packet_id - 1) * data_per_packet
        end_pos = min(start_pos + data_per_packet, len(self.file_data))
        data = self.file_data[start_pos:end_pos]

        # 构建包头（小端序）
        packet = bytearray()

        # 包头标识 (0xAA55)
        packet.extend(OTA_CONFIG['packet_header'].to_bytes(2, 'little'))

        # 包序号 (小端序)
        packet.extend(packet_id.to_bytes(2, 'little'))

        # 总包数 (小端序)
        packet.extend(self.total_packets.to_bytes(2, 'little'))

        if len(data) < data_per_packet:
            data += b'\xFF' * (data_per_packet - len(data))  # 用0xFF填充

        # 数据长度 (小端序)
        packet.extend(data_per_packet.to_bytes(2, 'little'))

        # 计算校验和（8位累加和）
        checksum = sum(data) & 0xFF
        packet.append(checksum)

        # 数据内容
        packet.extend(data)

        return bytes(packet)

    def _verify_completion(self):
        """验证传输完成"""
        time.sleep(2)

        # 发送完成标志
        complete_cmd = f"ML307OTA_{self.device_id}_COMPLETE"
        self.client.publish(TOPIC_CONFIG['command_topic'], complete_cmd)
        print(f"📤 发送完成指令: {complete_cmd}")

        # 发送文件校验和
        file_checksum = sum(self.file_data) & 0xFFFFFFFF
        checksum_cmd = f"ML307OTA_{self.device_id}_CHECKSUM:{file_checksum}"
        self.client.publish(TOPIC_CONFIG['command_topic'], checksum_cmd)
        print(f"📤 发送文件校验和: {file_checksum:#010x}")

        self.ota_active = False
        print("\n✅ OTA升级流程完成！")

    def connect(self):
        """连接到MQTT服务器"""
        try:
            print(f"🔗 正在连接到MQTT服务器 {MQTT_CONFIG['host']}:{MQTT_CONFIG['port']}...")
            self.client.connect(MQTT_CONFIG['host'],
                              MQTT_CONFIG['port'],
                              MQTT_CONFIG['keepalive'])
            self.client.loop_start()
            return True
        except Exception as e:
            print(f"❌ 连接失败: {e}")
            return False

    def disconnect(self):
        """断开连接"""
        self.ota_active = False
        self.client.loop_stop()
        self.client.disconnect()
        print("🔌 已断开连接")

    def start_ota(self, firmware_file):
        """开始OTA升级"""
        if not os.path.exists(firmware_file):
            print(f"❌ 文件不存在: {firmware_file}")
            return False

        self.current_file = firmware_file
        print(f"\n🚀 开始OTA升级流程...")
        print(f"📋 设备ID: {self.device_id}")
        print(f"📡 MQTT主题: {TOPIC_CONFIG['command_topic']}")
        return True


def main():
    """主函数 - 测试用"""
    import sys

    if len(sys.argv) < 2:
        print("用法: python ml307r_ota_client_base64.py <固件文件路径>")
        sys.exit(1)

    firmware_file = sys.argv[1]

    # 创建OTA客户端
    ota_client = ML307ROTAClient()

    # 连接到MQTT服务器
    if ota_client.connect():
        # 准备OTA升级
        if ota_client.start_ota(firmware_file):
            print("\n⏳ 等待设备发起OTA请求...")
            print("   (请让设备发送ML307OTA_<设备ID>命令)")

            # 保持运行
            try:
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                print("\n\n👋 用户中断，退出程序")

        # 断开连接
        ota_client.disconnect()


if __name__ == "__main__":
    main()
