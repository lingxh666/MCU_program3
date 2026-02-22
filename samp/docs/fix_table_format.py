import re

with open('山东大岳采样器协议_V2.md', 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
in_table = False

for line in lines:
    if line.startswith('| **自动序号**'):
        in_table = True
        # Original header is currently:
        # | **自动序号** | 寄存器地址 | **读写** | **变量类型** | **单位** | **寄存器含义** | **寄存器值的含义** |
        # Separator is currently:
        # | --- | --- | --- | --- | --- | --- | --- |
        new_lines.append(line)
    elif in_table and line.startswith('| ---'):
        new_lines.append('| --- | --- | --- | --- | --- | --- | --- |\n')
    elif in_table and line.startswith('|'):
        parts = [p.strip() for p in line.split('|')]
        # Filter out empty strings from start and end created by split('|')
        if len(parts) >= 9: # ['', '1', '40001', '只读', 'uint16', '', '当前混样...', '...', '']
            # We want to make sure there are exactly 7 columns, which means 9 elements after split
            row_content = parts[1:-1]
            if len(row_content) > 7:
                row_content = row_content[:7]
            elif len(row_content) < 7:
                row_content.extend([''] * (7 - len(row_content)))
            
            # Reconstruct row
            formatted_row = f"| {' | '.join(row_content)} |\n"
            new_lines.append(formatted_row)
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
