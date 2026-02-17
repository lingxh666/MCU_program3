# -*- coding: utf-8 -*-
"""
数据包处理模块
"""
import struct
import base64
from utils.config import OTA_CONFIG
from utils.checksum import calculate_checksum


class PacketHandler:
    """数据包处理器"""

    def __init__(self):
        """初始化数据包处理器"""
        self.packet_size = OTA_CONFIG['packet_size']
        self.packet_header = OTA_CONFIG['packet_header']

    def create_start_command(self, device_id, file_checksum, file_size):
        """
        创建升级开始指令

        Args:
            device_id: 设备编号
            file_checksum: 文件CRC16校验和（4位十六进制字符串）
            file_size: 固件大小（字节）

        Returns:
            str: 升级开始指令字符串
            格式: ML307OTA_{设备ID}_{CRC16}_{固件大小}
        """
        return f"ML307OTA_{device_id}_{file_checksum}_{file_size}"

    def parse_start_command(self, command):
        """
        解析升级开始指令

        Args:
            command: 升级开始指令字符串

        Returns:
            dict or None: 解析结果或None（解析失败）
        """
        try:
            # 示例: ML307OTA_CYJ_2512121H1001B_19646541CRC
            parts = command.split('_')
            if len(parts) >= 4 and parts[0] == 'ML307OTA':
                device_id = '_'.join(parts[1:-1])  # 设备编号可能包含下划线
                checksum_part = parts[-1]
                if checksum_part.endswith('CRC'):
                    checksum = checksum_part[:-3]  # 去掉CRC后缀
                    return {
                        'device_id': device_id,
                        'checksum': int(checksum)
                    }
        except Exception as e:
            print(f"解析升级指令失败: {e}")
        return None

    def create_data_packet(self, packet_id, total_packets, data):
        """
        创建数据包（固定数据大小 + 9字节包头）

        Args:
            packet_id: 包序号
            total_packets: 总包数
            data: 数据内容

        Returns:
            bytes: 序列化后的数据包
        """
        # 每个包包含固定大小的数据
        data_per_packet = self.packet_size

        # 确保数据不超过每包最大值
        if len(data) > data_per_packet:
            data = data[:data_per_packet]

        # 使用0xFF填充剩余部分到固定大小
        while len(data) < data_per_packet:
            data = data + b'\xFF'

        # 手动构建数据包（小端序）
        packet = bytearray()

        # 包头 (0xAA55)
        packet.extend(self.packet_header.to_bytes(2, 'little'))

        # 包序号 (小端序)
        packet.extend(packet_id.to_bytes(2, 'little'))

        # 总包数 (小端序)
        packet.extend(total_packets.to_bytes(2, 'little'))

        # 数据长度 (小端序) - 实际数据长度（固定大小）
        packet.extend(len(data).to_bytes(2, 'little'))

        # 计算校验和（8位累加和）
        checksum = sum(data) & 0xFF
        packet.append(checksum)

        # 数据内容（固定大小）
        packet.extend(data)

        return bytes(packet)

    def create_data_packet_base64(self, packet_id, total_packets, data):
        """
        创建Base64编码的数据包

        Args:
            packet_id: 包序号
            total_packets: 总包数
            data: 数据内容

        Returns:
            str: Base64编码的数据包字符串
        """
        # 创建二进制数据包
        packet = self.create_data_packet(packet_id, total_packets, data)

        # 转换为Base64编码
        packet_base64 = base64.b64encode(packet).decode('utf-8')

        return packet_base64

    def create_buffer_packets_base64(self, start_packet_id, total_packets, file_data):
        """
        创建单包/多包数据的Base64编码

        Args:
            start_packet_id: 起始包序号
            total_packets: 总包数
            file_data: 完整文件数据

        Returns:
            str: 包含多个数据包的Base64编码字符串
        """
        packets_per_buffer = OTA_CONFIG['packets_per_buffer']
        packet_size = OTA_CONFIG['packet_size']
        buffer_data = bytearray()

        # 创建多个数据包
        for i in range(packets_per_buffer):
            packet_id = start_packet_id + i
            if packet_id > total_packets:
                break

            # 计算数据偏移
            offset = (packet_id - 1) * packet_size
            if offset >= len(file_data):
                break

            # 获取当前包的数据
            end_pos = min(offset + packet_size, len(file_data))
            packet_data = file_data[offset:end_pos]

            # 如果最后一包数据不足，用0xFF填充
            if len(packet_data) < packet_size:
                packet_data += b'\xFF' * (packet_size - len(packet_data))

            # 创建数据包
            packet = self.create_data_packet(packet_id, total_packets, packet_data)
            buffer_data.extend(packet)

        # Base64编码整个缓冲区
        buffer_base64 = base64.b64encode(buffer_data).decode('utf-8')
        return buffer_base64

    def parse_data_packet(self, packet):
        """
        解析数据包

        Args:
            packet: 接收到的数据包

        Returns:
            dict or None: 解析结果或None（解析失败）
        """
        try:
            # 解包数据
            packet_format = '<HHHHB' + str(self.packet_size) + 's'
            unpacked = struct.unpack(packet_format, packet)

            return {
                'header': unpacked[0],
                'packet_id': unpacked[1],
                'total_packets': unpacked[2],
                'data_length': unpacked[3],
                'checksum': unpacked[4],
                'data': unpacked[5]
            }
        except Exception as e:
            print(f"解析数据包失败: {e}")
            return None

    def create_ack_packet(self, packet_id, status='OK'):
        """
        创建ACK响应包

        Args:
            packet_id: 确认的包ID
            status: 状态（OK或ERROR）

        Returns:
            dict: ACK包字典
        """
        return {
            'header': self.packet_header,
            'ack_type': 'PACKET_ACK',
            'packet_id': packet_id,
            'status': status
        }

    def serialize_ack(self, ack_packet):
        """
        序列化ACK包

        Args:
            ack_packet: ACK包字典

        Returns:
            str: 序列化后的ACK字符串
        """
        return f"ACK_{ack_packet['packet_id']}_{ack_packet['status']}"

    def parse_ack(self, ack_str):
        """
        解析ACK字符串

        Args:
            ack_str: ACK字符串

        Returns:
            dict or None: 解析结果或None（解析失败）
        """
        try:
            # 示例: ACK_1_OK 或 ACK_1_ERROR
            parts = ack_str.split('_')
            if len(parts) >= 3 and parts[0] == 'ACK':
                packet_id = int(parts[1])
                status = parts[2]
                return {
                    'packet_id': packet_id,
                    'status': status
                }
        except Exception as e:
            print(f"解析ACK失败: {e}")
        return None

    def verify_packet(self, packet):
        """
        验证数据包完整性

        Args:
            packet: 数据包字典

        Returns:
            bool: 验证结果
        """
        try:
            # 检查包头
            if packet['header'] != self.packet_header:
                print(f"包头错误: 0x{packet['header']:04X}")
                return False

            # 验证校验和
            calculated_checksum = calculate_checksum(packet['data'])
            if calculated_checksum != packet['checksum']:
                print(f"校验和错误: 期望{packet['checksum']}, 实际{calculated_checksum}")
                return False

            return True
        except Exception as e:
            print(f"验证数据包失败: {e}")
            return False
