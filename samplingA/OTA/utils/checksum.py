# -*- coding: utf-8 -*-
"""
校验和计算模块
"""


def checksum16(data):
    """
    16位累加和算法（与MCU端OTA_Checksum16一致）

    算法：将所有字节累加，取低16位

    Args:
        data: bytes或bytearray类型的数据

    Returns:
        int: 16位校验和
    """
    if not data:
        return 0

    total = 0
    for byte in data:
        total += byte

    # 返回低16位
    return total & 0xFFFF


def calculate_checksum(data):
    """
    计算数据的16位累加和校验

    Args:
        data: bytes或bytearray类型的数据

    Returns:
        int: 16位校验和
    """
    return checksum16(data)


def calculate_checksum_8bit(data):
    """
    计算数据的8位累加和（用于数据包校验）

    Args:
        data: bytes或bytearray类型的数据

    Returns:
        int: 8位校验和值（0-255）
    """
    if not data:
        return 0

    checksum = 0
    for byte in data:
        checksum = (checksum + byte) & 0xFF

    return checksum


def verify_checksum(data, expected_checksum):
    """
    验证数据的CRC16校验和

    Args:
        data: 要验证的数据
        expected_checksum: 期望的校验和值

    Returns:
        bool: 校验结果
    """
    return calculate_checksum(data) == expected_checksum


def format_checksum(checksum):
    """
    格式化CRC16校验和为4位十六进制字符串

    Args:
        checksum: 校验和值

    Returns:
        str: 格式化后的字符串（如 "D658"）
    """
    return f"{checksum:04X}"