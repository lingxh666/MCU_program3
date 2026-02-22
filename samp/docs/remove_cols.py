import re

with open('山东大岳采样器协议_V2.md', 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
in_table = False

for line in lines:
    if line.startswith('| **自动序号**'):
        in_table = True
        # Original header:
        # | **自动序号** | **寄存器地址  <br>（0x）** | **读写** | **变量类型** | **长度** | **下限** | **上限** | **单位** | **寄存器含义** | **寄存器值的含义** | **备注** |
        # Indices:
        # 0: empty
        # 1: 自动序号
        # 2: 寄存器地址
        # 3: 读写
        # 4: 变量类型
        # 5: 长度 -> Remove
        # 6: 下限 -> Remove
        # 7: 上限 -> Remove
        # 8: 单位
        # 9: 寄存器含义
        # 10: 寄存器值的含义
        # 11: 备注 -> Remove
        # 12: empty
        # Keep indices: 1, 2, 3, 4, 8, 9, 10
        parts = line.split('|')
        new_parts = [parts[0], parts[1], parts[2], parts[3], parts[4], parts[8], parts[9], parts[10], parts[12]]
        new_lines.append('|'.join(new_parts))
    elif in_table and line.startswith('| ---'):
        parts = line.split('|')
        if len(parts) >= 12:
            new_parts = [parts[0], parts[1], parts[2], parts[3], parts[4], parts[8], parts[9], parts[10], parts[-1]]
            new_lines.append('|'.join(new_parts))
        else:
            new_lines.append(line)
    elif in_table and line.startswith('|'):
        parts = line.split('|')
        if len(parts) >= 12: # Standard row
            # Same keep indices: 1, 2, 3, 4, 8, 9, 10
            new_parts = [parts[0], parts[1], parts[2], parts[3], parts[4], parts[8], parts[9], parts[10], parts[-1]]
            new_lines.append('|'.join(new_parts))
        else:
            new_lines.append(line)
            if not line.strip():
                in_table = False
    else:
        if not line.strip() and in_table:
            in_table = False
        new_lines.append(line)

with open('山东大岳采样器协议_V2.md', 'w', encoding='utf-8') as f:
    f.writelines(new_lines)
