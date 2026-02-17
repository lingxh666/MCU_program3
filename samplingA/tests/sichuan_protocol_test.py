#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
四川管控协议测试工具
支持功能码: 0x03(读保持寄存器), 0x06(写单个寄存器), 0x10(写多个寄存器)
"""

import serial
import struct
import time
from datetime import datetime
from typing import Optional, List, Tuple, Dict, Any

# ============================================================================
# CRC16 校验
# ============================================================================
CRC16_TABLE = [
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
]

def crc16_modbus(data: bytes) -> int:
    """计算Modbus CRC16校验值"""
    crc = 0xFFFF
    for byte in data:
        crc = (crc >> 8) ^ CRC16_TABLE[(crc ^ byte) & 0xFF]
    return crc

def append_crc(data: bytes) -> bytes:
    """添加CRC校验到数据末尾(低字节在前)"""
    crc = crc16_modbus(data)
    return data + struct.pack('<H', crc)

def verify_crc(data: bytes) -> bool:
    """验证CRC校验"""
    if len(data) < 4:
        return False
    received_crc = struct.unpack('<H', data[-2:])[0]
    calculated_crc = crc16_modbus(data[:-2])
    return received_crc == calculated_crc

# ============================================================================
# 寄存器定义
# ============================================================================

# 状态寄存器 (40001-40022) - 只读
STATUS_REGISTERS = {
    40001: ("当前瓶位置", "int", None),
    40002: ("当前采样桶", "int", {1: "A桶供样", 2: "B桶供样"}),
    40003: ("运行状态", "int", {0: "停止", 1: "空闲待机", 2: "运行中", 97: "报警", 98: "故障", 99: "维护"}),
    40004: ("同步供样信号", "int", {0: "等待", 1: "同步供样"}),
    40005: ("留样完成信号", "int", {0: "等待", 1: "留样完成"}),
    40006: ("冰柜温度", "temp", None),  # 扩大10倍
    40007: ("A桶状态", "int", {0: "空闲/待机", 1: "采水", 2: "等待仪器分析", 3: "超标留样", 4: "完成排空"}),
    40008: ("B桶状态", "int", {0: "空闲/待机", 1: "采水", 2: "等待仪器分析", 3: "超标留样", 4: "完成排空"}),
    40009: ("留样瓶状态高16位", "hex", None),
    40010: ("留样瓶状态低16位", "hex", None),
    40011: ("门禁状态", "int", {0: "开", 1: "关"}),
    40012: ("固定密码高16位", "hex", None),
    40013: ("固定密码低16位", "hex", None),
    40014: ("采样模式", "int", {1: "时间等比", 2: "流量触发", 3: "串口控制"}),
    40015: ("门禁操作-年", "int", None),
    40016: ("门禁操作-月", "int", None),
    40017: ("门禁操作-日", "int", None),
    40018: ("门禁操作-时", "int", None),
    40019: ("门禁操作-分", "int", None),
    40020: ("门禁操作-秒", "int", None),
    40021: ("门禁卡号高16位", "hex", None),
    40022: ("门禁卡号低16位", "hex", None),
}

# 留样结果寄存器 (40101-40120) - 只读
RETAIN_REGISTERS = {
    40101: ("留样年", "int", None),
    40102: ("留样月", "int", None),
    40103: ("留样日", "int", None),
    40104: ("留样时", "int", None),
    40105: ("留样分", "int", None),
    40106: ("留样瓶号", "int", None),
    40107: ("留样量(ml)", "int", None),
    40108: ("留样模式", "int", {0: "远程留样", 1: "同步留样", 2: "超标留样", 3: "直接留样"}),
    40109: ("样品编号-年", "int", None),
    40110: ("样品编号-月", "int", None),
    40111: ("样品编号-日", "int", None),
    40112: ("样品编号-起始时", "int", None),
    40113: ("样品编号-起始分", "int", None),
    40114: ("样品编号-结束时", "int", None),
    40115: ("样品编号-结束分", "int", None),
    40116: ("留样结果", "int", {0: "成功", 1: "失败"}),
    40117: ("固定剂类型", "int", {0: "不添加", 1: "硝酸", 2: "盐酸", 3: "氢氧化钠", 4: "硫酸"}),
    40118: ("加药比例", "percent", None),  # 扩大100倍
    40119: ("动态密码高16位", "hex", None),
    40120: ("动态密码低16位", "hex", None),
}

# 控制寄存器 (40301-40309) - 可写
CONTROL_REGISTERS = {
    40301: ("远程留样", "bool", {0: "无动作", 1: "启动远程留样(瞬时样)"}),
    40302: ("远程留样量", "int", None),
    40303: ("立即采水", "bool", {0: "无动作", 1: "启动立即采水"}),
    40304: ("立即供样", "bool", {0: "无动作", 1: "启动立即供样"}),
    40305: ("排空采水桶", "bool", {0: "无动作", 1: "启动排空"}),
    40306: ("超标留样", "bool", {0: "无动作", 1: "启动超标留样"}),
    40307: ("不超标排空", "bool", {0: "无动作", 1: "启动不超标排空"}),
    40308: ("采瞬时样供样", "bool", {0: "无动作", 1: "启动采瞬时样供样"}),
    40309: ("停止并排空", "bool", {0: "无动作", 1: "停止当前动作并排空复位"}),
}

# 合并所有寄存器
ALL_REGISTERS = {**STATUS_REGISTERS, **RETAIN_REGISTERS, **CONTROL_REGISTERS}

# ============================================================================
# 值解析函数
# ============================================================================

def format_register_value(reg_addr: int, value: int) -> str:
    """格式化寄存器值为可读字符串"""
    if reg_addr not in ALL_REGISTERS:
        return f"{value} (0x{value:04X})"

    name, dtype, mapping = ALL_REGISTERS[reg_addr]

    if dtype == "temp":
        return f"{value/10:.1f}°C"
    elif dtype == "percent":
        return f"{value/100:.2f}%"
    elif dtype == "hex":
        return f"0x{value:04X}"
    elif mapping and value in mapping:
        return f"{value} ({mapping[value]})"
    else:
        return str(value)

def parse_bottle_mask(high: int, low: int) -> str:
    """解析留样瓶状态掩码"""
    mask = (high << 16) | low
    bottles = []
    for i in range(32):
        if mask & (1 << (31 - i)):
            bottles.append(str(i + 1))
    if bottles:
        return f"已留样瓶: {', '.join(bottles)}"
    return "无已留样瓶"

def parse_password(high: int, low: int) -> str:
    """解析密码(32位整数)"""
    pwd = (high << 16) | low
    return f"{pwd:06d}"

# ============================================================================
# Modbus帧构建
# ============================================================================

def build_read_holding(slave_addr: int, start_reg: int, count: int) -> bytes:
    """构建读保持寄存器请求(功能码03)"""
    # 寄存器地址转换: 40001 -> 0x0000
    reg_offset = start_reg - 40001
    frame = struct.pack('>BBHH', slave_addr, 0x03, reg_offset, count)
    return append_crc(frame)

def build_write_single(slave_addr: int, reg: int, value: int) -> bytes:
    """构建写单个寄存器请求(功能码06)"""
    reg_offset = reg - 40001
    frame = struct.pack('>BBHH', slave_addr, 0x06, reg_offset, value)
    return append_crc(frame)

def build_write_multiple(slave_addr: int, start_reg: int, values: List[int]) -> bytes:
    """构建写多个寄存器请求(功能码16/0x10)"""
    reg_offset = start_reg - 40001
    count = len(values)
    byte_count = count * 2
    frame = struct.pack('>BBHHB', slave_addr, 0x10, reg_offset, count, byte_count)
    for v in values:
        frame += struct.pack('>H', v)
    return append_crc(frame)

# ============================================================================
# 响应解析
# ============================================================================

MODBUS_EXCEPTIONS = {
    0x01: "非法功能码",
    0x02: "非法数据地址",
    0x03: "非法数据值",
    0x04: "从站设备故障",
}

def parse_response(data: bytes, start_reg: int = 40001) -> Dict[str, Any]:
    """解析Modbus响应"""
    result = {
        "raw": data.hex(' ').upper(),
        "valid_crc": False,
        "error": None,
        "registers": {}
    }

    if len(data) < 4:
        result["error"] = "响应数据过短"
        return result

    result["valid_crc"] = verify_crc(data)
    if not result["valid_crc"]:
        result["error"] = "CRC校验失败"
        return result

    slave_addr = data[0]
    func_code = data[1]
    result["slave_addr"] = slave_addr
    result["func_code"] = func_code

    # 检查异常响应
    if func_code & 0x80:
        exc_code = data[2]
        result["error"] = f"异常响应: {MODBUS_EXCEPTIONS.get(exc_code, f'未知异常 0x{exc_code:02X}')}"
        return result

    if func_code == 0x03:  # 读保持寄存器响应
        byte_count = data[2]
        reg_count = byte_count // 2
        for i in range(reg_count):
            reg_addr = start_reg + i
            value = struct.unpack('>H', data[3 + i*2:5 + i*2])[0]
            result["registers"][reg_addr] = value

    elif func_code == 0x06:  # 写单个寄存器响应
        reg_offset = struct.unpack('>H', data[2:4])[0]
        value = struct.unpack('>H', data[4:6])[0]
        reg_addr = 40001 + reg_offset
        result["registers"][reg_addr] = value

    elif func_code == 0x10:  # 写多个寄存器响应
        reg_offset = struct.unpack('>H', data[2:4])[0]
        count = struct.unpack('>H', data[4:6])[0]
        result["start_reg"] = 40001 + reg_offset
        result["count"] = count

    return result

# ============================================================================
# 串口通信类
# ============================================================================

class SichuanProtocolTester:
    """四川管控协议测试器"""

    def __init__(self, port: str, baudrate: int = 9600, slave_addr: int = 1):
        self.port = port
        self.baudrate = baudrate
        self.slave_addr = slave_addr
        self.serial: Optional[serial.Serial] = None
        self.timeout = 1.0

    def connect(self) -> bool:
        """连接串口"""
        try:
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout
            )
            print(f"[OK] 串口 {self.port} 已连接 (波特率: {self.baudrate})")
            return True
        except Exception as e:
            print(f"[ERROR] 串口连接失败: {e}")
            return False

    def disconnect(self):
        """断开串口"""
        if self.serial and self.serial.is_open:
            self.serial.close()
            print(f"[OK] 串口 {self.port} 已断开")

    def send_and_receive(self, frame: bytes, description: str = "") -> Optional[bytes]:
        """发送帧并接收响应"""
        if not self.serial or not self.serial.is_open:
            print("[ERROR] 串口未连接")
            return None

        self.serial.reset_input_buffer()

        print(f"\n{'='*60}")
        if description:
            print(f"[发送] {description}")
        print(f"[TX] {frame.hex(' ').upper()}")
        self._explain_request(frame)

        self.serial.write(frame)
        time.sleep(0.1)

        response = self.serial.read(256)
        if response:
            print(f"[RX] {response.hex(' ').upper()}")
            return response
        else:
            print("[RX] 无响应 (超时)")
            return None

    def _explain_request(self, frame: bytes):
        """解释请求帧含义"""
        if len(frame) < 4:
            return
        slave = frame[0]
        func = frame[1]
        print(f"    从站地址: {slave}")

        if func == 0x03:
            reg_offset = struct.unpack('>H', frame[2:4])[0]
            count = struct.unpack('>H', frame[4:6])[0]
            start_reg = 40001 + reg_offset
            print(f"    功能码: 0x03 (读保持寄存器)")
            print(f"    起始寄存器: {start_reg}, 数量: {count}")
        elif func == 0x06:
            reg_offset = struct.unpack('>H', frame[2:4])[0]
            value = struct.unpack('>H', frame[4:6])[0]
            reg = 40001 + reg_offset
            print(f"    功能码: 0x06 (写单个寄存器)")
            if reg in ALL_REGISTERS:
                name = ALL_REGISTERS[reg][0]
                print(f"    寄存器: {reg} ({name}), 值: {value}")
            else:
                print(f"    寄存器: {reg}, 值: {value}")
        elif func == 0x10:
            reg_offset = struct.unpack('>H', frame[2:4])[0]
            count = struct.unpack('>H', frame[4:6])[0]
            start_reg = 40001 + reg_offset
            print(f"    功能码: 0x10 (写多个寄存器)")
            print(f"    起始寄存器: {start_reg}, 数量: {count}")

    def _display_response(self, result: Dict[str, Any], start_reg: int):
        """显示响应解析结果"""
        print("\n[解析结果]")
        if result.get("error"):
            print(f"    错误: {result['error']}")
            return

        print(f"    CRC校验: {'通过' if result['valid_crc'] else '失败'}")

        if "registers" in result and result["registers"]:
            print("    寄存器值:")
            for reg, value in sorted(result["registers"].items()):
                if reg in ALL_REGISTERS:
                    name = ALL_REGISTERS[reg][0]
                    formatted = format_register_value(reg, value)
                    print(f"      {reg} ({name}): {formatted}")
                else:
                    print(f"      {reg}: {value}")

            # 特殊组合解析
            regs = result["registers"]
            if 40009 in regs and 40010 in regs:
                print(f"    [组合] {parse_bottle_mask(regs[40009], regs[40010])}")
            if 40012 in regs and 40013 in regs:
                print(f"    [组合] 固定密码: {parse_password(regs[40012], regs[40013])}")
            if 40021 in regs and 40022 in regs:
                print(f"    [组合] 门禁卡号: {parse_password(regs[40021], regs[40022])}")
            if 40119 in regs and 40120 in regs:
                print(f"    [组合] 动态密码: {parse_password(regs[40119], regs[40120])}")

        if "count" in result:
            print(f"    写入成功: 起始{result.get('start_reg')}, 数量{result['count']}")

    # ========== 测试方法 ==========

    def read_status(self) -> Optional[Dict]:
        """读取设备状态 (40001-40022)"""
        frame = build_read_holding(self.slave_addr, 40001, 22)
        resp = self.send_and_receive(frame, "读取设备状态 (40001-40022)")
        if resp:
            result = parse_response(resp, 40001)
            self._display_response(result, 40001)
            return result
        return None

    def read_retain_result(self) -> Optional[Dict]:
        """读取留样结果 (40101-40120)"""
        frame = build_read_holding(self.slave_addr, 40101, 20)
        resp = self.send_and_receive(frame, "读取留样结果 (40101-40120)")
        if resp:
            result = parse_response(resp, 40101)
            self._display_response(result, 40101)
            return result
        return None

    def cmd_sample(self) -> Optional[Dict]:
        """立即采水"""
        frame = build_write_single(self.slave_addr, 40303, 1)
        resp = self.send_and_receive(frame, "立即采水")
        if resp:
            result = parse_response(resp, 40303)
            self._display_response(result, 40303)
            return result
        return None

    def cmd_delivery(self) -> Optional[Dict]:
        """立即供样"""
        frame = build_write_single(self.slave_addr, 40304, 1)
        resp = self.send_and_receive(frame, "立即供样")
        if resp:
            result = parse_response(resp, 40304)
            self._display_response(result, 40304)
            return result
        return None

    def cmd_drain(self) -> Optional[Dict]:
        """排空采水桶"""
        frame = build_write_single(self.slave_addr, 40305, 1)
        resp = self.send_and_receive(frame, "排空采水桶")
        if resp:
            result = parse_response(resp, 40305)
            self._display_response(result, 40305)
            return result
        return None

    def cmd_remote_retain(self, volume: int = 500) -> Optional[Dict]:
        """远程留样"""
        frame = build_write_multiple(self.slave_addr, 40301, [1, volume])
        resp = self.send_and_receive(frame, f"远程留样 (留样量: {volume}ml)")
        if resp:
            result = parse_response(resp, 40301)
            self._display_response(result, 40301)
            return result
        return None

    def send_custom_hex(self, hex_str: str) -> Optional[bytes]:
        """发送自定义十六进制命令"""
        try:
            hex_str = hex_str.replace(' ', '').replace('0x', '').replace(',', '')
            frame = bytes.fromhex(hex_str)
            resp = self.send_and_receive(frame, "自定义命令")
            return resp
        except ValueError as e:
            print(f"[ERROR] 十六进制格式错误: {e}")
            return None

    def read_registers(self, start: int, count: int) -> Optional[Dict]:
        """读取指定寄存器"""
        frame = build_read_holding(self.slave_addr, start, count)
        resp = self.send_and_receive(frame, f"读取寄存器 {start}-{start+count-1}")
        if resp:
            result = parse_response(resp, start)
            self._display_response(result, start)
            return result
        return None

    def write_register(self, reg: int, value: int) -> Optional[Dict]:
        """写单个寄存器"""
        frame = build_write_single(self.slave_addr, reg, value)
        resp = self.send_and_receive(frame, f"写寄存器 {reg} = {value}")
        if resp:
            result = parse_response(resp, reg)
            self._display_response(result, reg)
            return result
        return None

    def cmd_exceed_retain(self) -> Optional[Dict]:
        """超标留样"""
        frame = build_write_single(self.slave_addr, 40306, 1)
        resp = self.send_and_receive(frame, "超标留样")
        if resp:
            result = parse_response(resp, 40306)
            self._display_response(result, 40306)
            return result
        return None

    def cmd_instant_delivery(self) -> Optional[Dict]:
        """采瞬时样供样"""
        frame = build_write_single(self.slave_addr, 40308, 1)
        resp = self.send_and_receive(frame, "采瞬时样供样")
        if resp:
            result = parse_response(resp, 40308)
            self._display_response(result, 40308)
            return result
        return None

    def cmd_stop(self) -> Optional[Dict]:
        """停止并排空"""
        frame = build_write_single(self.slave_addr, 40309, 1)
        resp = self.send_and_receive(frame, "停止当前动作并排空复位")
        if resp:
            result = parse_response(resp, 40309)
            self._display_response(result, 40309)
            return result
        return None

# ============================================================================
# 交互式菜单
# ============================================================================

def print_menu():
    """打印菜单"""
    print("\n" + "="*60)
    print("四川管控协议测试工具")
    print("="*60)
    print("\n[读取命令]")
    print("  1. 读取设备状态 (40001-40022)")
    print("  2. 读取留样结果 (40101-40120)")
    print("  3. 读取指定寄存器")
    print("\n[控制命令]")
    print("  10. 远程留样 (可设置留样量)")
    print("  11. 立即采水")
    print("  12. 立即供样")
    print("  13. 排空采水桶")
    print("  14. 超标留样")
    print("  15. 采瞬时样供样")
    print("  16. 停止并排空复位")
    print("\n[自定义命令]")
    print("  20. 发送自定义十六进制命令")
    print("  21. 写单个寄存器")
    print("\n[设置]")
    print("  30. 修改从站地址")
    print("  31. 显示寄存器定义")
    print("\n  0. 退出")
    print("="*60)

def print_register_definitions():
    """打印寄存器定义"""
    print("\n" + "="*60)
    print("寄存器定义")
    print("="*60)

    print("\n[状态寄存器 40001-40022] (只读)")
    for reg, (name, dtype, mapping) in sorted(STATUS_REGISTERS.items()):
        desc = f"  {reg}: {name}"
        if mapping:
            desc += f" - {mapping}"
        print(desc)

    print("\n[留样结果寄存器 40101-40120] (只读)")
    for reg, (name, dtype, mapping) in sorted(RETAIN_REGISTERS.items()):
        desc = f"  {reg}: {name}"
        if mapping:
            desc += f" - {mapping}"
        print(desc)

    print("\n[控制寄存器 40301-40309] (可写)")
    for reg, (name, dtype, mapping) in sorted(CONTROL_REGISTERS.items()):
        desc = f"  {reg}: {name}"
        if mapping:
            desc += f" - {mapping}"
        print(desc)

def run_interactive(tester: SichuanProtocolTester):
    """运行交互式测试"""
    while True:
        print_menu()
        try:
            choice = input("\n请输入选项: ").strip()
            if not choice:
                continue

            if choice == '0':
                print("退出测试工具")
                break
            elif choice == '1':
                tester.read_status()
            elif choice == '2':
                tester.read_retain_result()
            elif choice == '3':
                start = int(input("起始寄存器地址: "))
                count = int(input("寄存器数量: "))
                tester.read_registers(start, count)
            elif choice == '10':
                vol = input("留样量(ml,默认500): ").strip()
                volume = int(vol) if vol else 500
                tester.cmd_remote_retain(volume)
            elif choice == '11':
                tester.cmd_sample()
            elif choice == '12':
                tester.cmd_delivery()
            elif choice == '13':
                tester.cmd_drain()
            elif choice == '14':
                tester.cmd_exceed_retain()
            elif choice == '15':
                tester.cmd_instant_delivery()
            elif choice == '16':
                tester.cmd_stop()
            elif choice == '20':
                hex_str = input("输入十六进制命令(如: 01 03 00 00 00 16): ")
                tester.send_custom_hex(hex_str)
            elif choice == '21':
                reg = int(input("寄存器地址: "))
                val = int(input("写入值: "))
                tester.write_register(reg, val)
            elif choice == '30':
                addr = int(input("新从站地址(1-247): "))
                if 1 <= addr <= 247:
                    tester.slave_addr = addr
                    print(f"从站地址已修改为: {addr}")
                else:
                    print("地址范围错误")
            elif choice == '31':
                print_register_definitions()
            else:
                print("无效选项")

        except ValueError as e:
            print(f"输入错误: {e}")
        except KeyboardInterrupt:
            print("\n操作已取消")
            break

# ============================================================================
# 主函数
# ============================================================================

def main():
    """主函数"""
    import sys

    print("="*60)
    print("四川管控协议测试工具 v1.0")
    print("支持功能码: 0x03(读), 0x06(写单个), 0x10(写多个)")
    print("="*60)

    # 默认参数
    port = "COM3"
    baudrate = 9600
    slave_addr = 1

    # 命令行参数
    if len(sys.argv) > 1:
        port = sys.argv[1]
    if len(sys.argv) > 2:
        baudrate = int(sys.argv[2])
    if len(sys.argv) > 3:
        slave_addr = int(sys.argv[3])

    # 交互式输入串口
    if len(sys.argv) == 1:
        port = input(f"串口号 (默认 {port}): ").strip() or port
        baud_input = input(f"波特率 (默认 {baudrate}): ").strip()
        if baud_input:
            baudrate = int(baud_input)
        addr_input = input(f"从站地址 (默认 {slave_addr}): ").strip()
        if addr_input:
            slave_addr = int(addr_input)

    print(f"\n配置: 串口={port}, 波特率={baudrate}, 从站地址={slave_addr}")

    # 创建测试器
    tester = SichuanProtocolTester(port, baudrate, slave_addr)

    if not tester.connect():
        print("无法连接串口，请检查配置")
        return

    try:
        run_interactive(tester)
    finally:
        tester.disconnect()


if __name__ == "__main__":
    main()
