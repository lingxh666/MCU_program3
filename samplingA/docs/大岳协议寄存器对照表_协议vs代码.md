# Dayue protocol register mapping (spec vs code)

- spec: `docs/采样机大岳通讯协议.md` (section 3.5, V1.05)
- code: `middlewares/bsp/freemodbus/mb_reg_dayue.c`, `middlewares/bsp/work.h`
- address: table 0x = Modbus 0-based; holding register = 40001 + 0x

## Summary
- entries: 226 (address 0x0000~0x0258)
- code status counts: implemented=22, partial=60, mismatch=53, missing=91

### Missing ranges (merged, top)
- 0x0035~0x003A(40054~40059)
- 0x003E~0x0050(40063~40081)
- 0x00DD~0x017F(40222~40384)
- 0x0186~0x019A(40391~40411)
- 0x019D~0x01AC(40414~40429)
- 0x01AE~0x01AF(40431~40432)
- 0x01CE~0x01D9(40463~40474)
- 0x01E7~0x0215(40488~40534)
- 0x021D(40542)
- 0x0221~0x0258(40546~40601)

### Mismatch ranges (merged, top)
- 0x0051~0x00C6(40082~40199)
- 0x00D7~0x00DC(40216~40221)
- 0x019B~0x019C(40412~40413)
- 0x01DA~0x01E0(40475~40481)
- 0x021E(40543)

### Partial ranges (merged, top)
- 0x0006~0x000B(40007~40012)
- 0x0025~0x0033(40038~40052)
- 0x003B~0x003D(40060~40062)
- 0x00C7~0x00C8(40200~40201)
- 0x00D5~0x00D6(40214~40215)
- 0x0180~0x0185(40385~40390)
- 0x01AD(40430)
- 0x01B0~0x01CD(40433~40462)
- 0x0216~0x021C(40535~40541)
- 0x021F~0x0220(40544~40545)

## Notes
- mismatch hotspots: exceed-retain block base, instant-retain block range, time-set layout, 0x019B/0x019C meaning

## Mapping table

| Spec 0x | Spec 4xxxx | RW | Type | Len | Spec meaning | Code support | Code var/logic | Status/notes |
|---|---|---|---|---:|---|---|---|---|
| 0x0000 | 40001 | 只写 | 枚举 | 1 | 特殊命 令 | R:yes W:yes | read=0 (dayue_build_bucket_status_block); write logs only (dayue_write_holding) | implemented |
| 0x0001 | 40002 | 只读 | 枚举 | 1 | 当前混 样桶编号 | R:yes W:no | g_State.InletThreeWayValve + ready pulse switch (dayue_build_bucket_status_block) | implemented |
| 0x0002 | 40003 | 只读 | 枚举 | 1 | 混样桶水样准 备好 | R:yes W:no | g_water_sample_ready_A/B latched 60s pulse (dayue_bucket_ready_pulse) | implemented |
| 0x0003 | 40004 | 只读 | 枚举 | 1 | 仪器当前工作状态 | R:yes W:no | g_State.State (dayue_build_device_state_block) | implemented |
| 0x0004 | 40005 | 只读 | 枚举 | 1 | A 混样桶状态 | R:yes W:no | dayue_convert_bucket_state(g_State.ABucketState) | implemented |
| 0x0005 | 40006 | 只读 | 枚举 | 1 | B 混样 桶状态 | R:yes W:no | dayue_convert_bucket_state(g_State.BBucketState) | implemented |
| 0x0006 | 40007 | 只读 | 枚举 | 1 | A 混样桶液位状态 | R:yes W:no | constant 1 (liquid level OK) | partial; hardcoded; spec expects real level input |
| 0x0007 | 40008 | 只读 | 枚举 | 1 | B 混样 桶液位状态 | R:yes W:no | constant 1 (liquid level OK) | partial; hardcoded; spec expects real level input |
| 0x0008 | 40009 | 只读 | 枚举 | 1 | 仪器进 样状态 | R:yes W:no | sampling_get_status() -> sample_state (==3/4 => 1) | partial; may not match spec normal/fail definition |
| 0x0009 | 40010 | 只读 | 枚举 | 1 | 仪器送 样状态 | R:yes W:no | delivery_get_status() -> delivery_state (==3/4 => 1) | partial; may not match spec normal/fail definition |
| 0x000A | 40011 | 只读 | 枚举 | 1 | 仪器留 样状态 | R:yes W:no | constant 0 (retain status) | partial; hardcoded |
| 0x000B | 40012 | 只读 | 枚举 | 1 | 仪器故障状态 | R:yes W:no | constant 0 (fault status) | partial; hardcoded |
| 0x000C | 40013 | 只读 | 整数 | 1 | 当前已 留瓶数 | R:yes W:no | popcount(g_RetainBottleState.usedMask) | implemented |
| 0x000D~0x0024 | 40014~40037 | 只读 | 整数 | 24 | 瓶1~瓶 24 状态及水量 | R:yes W:no | per-bottle: usedMask + SampleVolume (dayue_build_device_state_block) | implemented |
| 0x0025~0x0026 | 40038~40039 | 只读 | 浮点数 | 2 | 冷藏柜温度 | R:yes W:no | constant 4.0C as float (2 regs) | partial; hardcoded |
| 0x0027 | 40040 | 只读 | 枚举 | 1 | A 混样桶进样 阀状态 | R:yes W:no | (g_State.InletThreeWayValve==1)?1:0 | partial; spec has 0/1/2 (fault) |
| 0x0028 | 40041 | 只读 | 枚举 | 1 | B 混样 桶进样阀状态 | R:yes W:no | (g_State.InletThreeWayValve==2)?1:0 | partial; spec has 0/1/2 (fault) |
| 0x0029 | 40042 | 只读 | 枚举 | 1 | A 混样 桶排水阀状态 | R:yes W:no | g_State.DrainA | partial; spec has 0/1/2 (fault) |
| 0x002A | 40043 | 只读 | 枚举 | 1 | B 混样 桶排水阀状态 | R:yes W:no | g_State.DrainB | partial; spec has 0/1/2 (fault) |
| 0x002B | 40044 | 只读 | 枚举 | 1 | A 混样桶搅拌器（带清洗功能）状 态 | R:yes W:no | GPIOB PIN10 (mix A) -> 0/1 | partial; spec has 0/1/2 (fault) |
| 0x002C | 40045 | 只读 | 枚举 | 1 | B 混样桶搅拌器（带清洗功能）状 态 | R:yes W:no | GPIOE PIN15 (mix B) -> 0/1 | partial; spec has 0/1/2 (fault) |
| 0x002D | 40046 | 只读 | 枚举 | 1 | 外接抽水泵状 态 | R:yes W:no | GPIOE PIN11 (external pump) -> 0/1 | partial; spec has 0/1/2 (fault) |
| 0x002E | 40047 | 只读 | 枚举 | 1 | 进样蠕动泵状 态 | R:yes W:no | g_State.SamplingMotor | partial; spec has 0/1/2 (fault) |
| 0x002F | 40048 | 只读 | 枚举 | 1 | 留样阀状态 | R:yes W:no | g_State.SampleThreeWayValve | partial; spec has 0/1/2 (fault) |
| 0x0030 | 40049 | 只读 | 枚举 | 1 | 送样阀状态 | R:yes W:no | g_State.OutletThreeWayValve | partial; spec has 0/1/2 (fault) |
| 0x0031 | 40050 | 只读 | 枚举 | 1 | 留样蠕动泵状 态 | R:yes W:no | constant 0 (retain pump state) | partial; hardcoded |
| 0x0032 | 40051 | 只读 | 枚举 | 1 | 送样蠕 动泵状态 | R:yes W:no | g_State.DeliveryMotor | partial; spec has 0/1/2 (fault) |
| 0x0033 | 40052 | 只读 | 枚举 | 1 | 门禁系统报警状态 | R:yes W:no | constant 0 (door alarm) | partial; hardcoded |
| 0x0034 | 40053 | 只读 | 枚举 | 1 | 门禁系 统开关状态 | R:yes W:no | GPIOE PIN13 (door state) -> 0/1 | implemented |
| 0x0035~0x003A | 40054~40059 | 只读 | BCD | 6 | 最近一次门禁操作记录-时间 | R:yes W:no | constant 0 (door open time record) | missing; spec expects last door time record |
| 0x003B~0x003C | 40060~40061 | 只读 | 无符号整数 | 2 | 最近一次门禁操作记录-门禁卡号或者按键密码 | R:yes W:no | g_SystemSettingConfig.CardId[0] (u32) | partial; spec wants last door record card id/pin |
| 0x003D | 40062 | 只读 |  | 1 | 药品剩余量 （试剂 瓶内剩余的容量） | R:yes W:no | constant 0x270F (no reagent module) | partial |
| 0x003E~0x0041 | 40063~40066 | 只读 |  | 4 | 样品编号 | R:no W:no |  | missing |
| 0x0042~0x0050 | 40067~40081 | 只读 | 预留 | 15 | 保留 | R:no W:no |  | missing |
| 0x0051~0x0056 | 40082~40087 | 只读 | BCD | 6 | 超标留样信息- -留样时间 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0057 | 40088 | 只读 | 枚举 | 1 | 超标留样信息- -执行结 果 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0058 | 40089 | 只读 | 枚举 | 1 | 超标留样信息- -执行失败原因 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0059 | 40090 | 只读 | 整数 | 1 | 超标留样信息- -要求的 留样瓶数 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x005A | 40091 | 只读 | 整数 | 1 | 超标留样信息- -超标因子总个数 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x005B | 40092 | 只读 | 枚举 | 1 | 超标留样信息- -超标因子1 编号 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x005C~0x005D | 40093~40094 | 只读 | 浮点数 | 2 | 超标留样信息- -超标因 子1 值 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x005E~0x005F | 40095~40096 | 只读 | 浮点数 | 2 | 超标留样信息- -因子1 超标下 限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0060~0x0061 | 40097~40098 | 只读 | 浮点数 | 2 | 超标留样信息- -因子1 超标上限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0062 | 40099 | 只读 | 枚举 | 1 | 超标留样信息- -超标因 子2 编 号 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0063~0x0064 | 40100~40101 | 只读 | 浮点数 | 2 | 超标留样信息- -超标因 子2 值 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0065~0x0066 | 40102~40103 | 只读 | 浮点数 | 2 | 超标留样信息- -因子2 超标下限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0067~0x0068 | 40104~40105 | 只读 | 浮点数 | 2 | 超标留样信息- -因子2 超标上限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0069 | 40106 | 只读 | 枚举 | 1 | 超标留样信息- -超标因子3 编 号 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x006A~0x006B | 40107~40108 | 只读 | 浮点数 | 2 | 超标留样信息- -超标因 子3 值 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x006C~0x006D | 40109~40110 | 只读 | 浮点数 | 2 | 超标留样信息- -因子3 超标下限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x006E~0x006F | 40111~40112 | 只读 | 浮点数 | 2 | 超标留样信息- -因子3 超标上限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0070 | 40113 | 只读 | 枚举 | 1 | 超标留样信息- -超标因子4 编 号 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0071~0x0072 | 40114~40115 | 只读 | 浮点数 | 2 | 超标留样信息- -超标因 子4 值 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0073~0x0074 | 40116~40117 | 只读 | 浮点数 | 2 | 超标留样信息- -因子4 超标下限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0075~0x0076 | 40118~40119 | 只读 | 浮点数 | 2 | 超标留样信息- -因子4 超标上限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0077 | 40120 | 只读 | 枚举 | 1 | 超标留样信息- -超标因 子5 编号 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0078~0x0079 | 40121~40122 | 只读 | 浮点数 | 2 | 超标留样信息- -超标因 子5 值 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x007A~0x007B | 40123~40124 | 只读 | 浮点数 | 2 | 超标留样信息- -因子5 超标下限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x007C~0x007D | 40125~40126 | 只读 | 浮点数 | 2 | 超标留样信息- -因子5 超标上 限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x007E | 40127 | 只读 | 枚举 | 1 | 超标留样信息- -超标因 子6 编号 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x007F~0x0080 | 40128~40129 | 只读 | 浮点数 | 2 | 超标留样信息- -超标因 子6 值 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0081~0x0082 | 40130~40131 | 只读 | 浮点数 | 2 | 超标留样信息- -因子6 超标下 限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0083~0x0084 | 40132~40133 | 只读 | 浮点 数 | 2 | 超标留 样信息- -因子6 超标上限 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0085 | 40134 | 只读 | 整数 | 1 | 超标留样信息- -当前留样瓶号 1 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0086 | 40135 | 只读 | 枚举 | 1 | 超标留样信息- -当前瓶留样结 果 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0087 | 40136 | 只读 | 整数 | 1 | 超标留样信息- -当前瓶留样容 积 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0088 | 40137 | 只读 | 枚举 | 1 | 超标留样信息- -当前瓶留样失败原因 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0089 | 40138 | 只读 | 整数 | 1 | 超标留样信息- -当前瓶 为总留样瓶数 中的第几瓶 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x008A~0x008F | 40139~40144 | 只读 | BCD | 6 | 超标留样信息- -当前瓶留样时间 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x0090~0x009A | 40145~40155 | 只读 |  | 11 | 超标留样信息- -当前留样瓶号 2 留样相关信 息 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x009B~0x00A5 | 40156~40166 | 只读 |  | 11 | 超标留样信息- -当前留样瓶号 3 留样相关信 息 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x00A6~0x00B0 | 40167~40177 | 只读 |  | 11 | 超标留样信息- -当前留样瓶号 4 留样相关信 息 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x00B1~0x00BB | 40178~40188 | 只读 |  | 11 | 超标留样信息- -当前留样瓶号 5 留样相关信 息 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x00BC~0x00C6 | 40189~40199 | 只读 |  | 11 | 超标留样信息- -当前留样瓶号 6 留样相关信 息 | R:misaligned W:no | g_LastRetainInfo (dayue_build_retain_exceed_block) | mismatch; code mounts this block at 0x003C (len=118, end=0x00B1), so spec 0x0051 start is shifted and 0x00B2~0x00C6 cannot be read |
| 0x00C7~0x00C8 | 40200~40201 | 只读 | 预留 | 2 | 保留 | R:partial W:no | only 0x00C8 implemented as 0 (dayue_build_retain_normal_block) | partial; 0x00C7 returns illegal address |
| 0x00C9~0x00CE | 40202~40207 | 只读 | BCD | 6 | 其他模式留样信息--留样时间 | R:yes W:no | g_LastRetainInfo (dayue_build_retain_normal_block) | implemented |
| 0x00CF | 40208 | 只读 | 枚举 | 1 | 其他模式留样信息--执行结 果 | R:yes W:no | g_LastRetainInfo (dayue_build_retain_normal_block) | implemented |
| 0x00D0 | 40209 | 只读 | 枚举 | 1 | 其他模式留样信息--执行失败原因 | R:yes W:no | g_LastRetainInfo (dayue_build_retain_normal_block) | implemented |
| 0x00D1 | 40210 | 只读 | 整数 | 1 | 其他模式留样信息--留样起 始瓶号 | R:yes W:no | g_LastRetainInfo (dayue_build_retain_normal_block) | implemented |
| 0x00D2 | 40211 | 只读 | 整数 | 1 | 其他模式留样信息--留样总 瓶数 | R:yes W:no | g_LastRetainInfo (dayue_build_retain_normal_block) | implemented |
| 0x00D3 | 40212 | 只读 | 整数 | 1 | 其他模式留样信息-- 留样量 | R:yes W:no | g_LastRetainInfo (dayue_build_retain_normal_block) | implemented |
| 0x00D4 | 40213 | 只读 | 枚举 | 1 | 其他模式留样信息--留样模式 | R:yes W:no | g_LastRetainInfo (dayue_build_retain_normal_block) | implemented |
| 0x00D5 | 40214 | 只读 | 枚举 | 1 | 留样记录信息 --留样触发条件 | R:yes W:no | g_LastRetainInfo (dayue_build_retain_normal_block) | partial; 0x00D5/0x00D6 are hardcoded 0 |
| 0x00D6 | 40215 | 只读 | 枚举 | 1 | 留样记录信息 --是否添加药 剂 | R:yes W:no | g_LastRetainInfo (dayue_build_retain_normal_block) | partial; 0x00D5/0x00D6 are hardcoded 0 |
| 0x00D7 | 40216 | 只读 | 枚举 | 1 | 留样记录信息 --药剂类型 | R:misaligned W:no | g_LastInstantRetainInfo (dayue_build_retain_instant_block base=0x00D4) | mismatch; spec defines these as reagent/sample-id fields; code returns offset instant-retain data |
| 0x00D8 | 40217 | 只读 | 整数 | 1 | 留样记录信息 --留样瓶加药 比 | R:misaligned W:no | g_LastInstantRetainInfo (dayue_build_retain_instant_block base=0x00D4) | mismatch; spec defines these as reagent/sample-id fields; code returns offset instant-retain data |
| 0x00D9~0x00DC | 40218~40221 | 只读 |  | 4 | 留样记录信息 --样品编号 | R:misaligned W:no | g_LastInstantRetainInfo (dayue_build_retain_instant_block base=0x00D4) | mismatch; spec defines these as reagent/sample-id fields; code returns offset instant-retain data |
| 0x00DD~0x00E2 | 40222~40227 | 只读 | BCD | 6 | 瞬时留样信息- -留样时间 | R:incomplete W:no | g_LastInstantRetainInfo (dayue_build_retain_instant_block) | missing; code read block is 0x00D4~0x00DE; cannot cover up to 0x00E6; spec read pattern (00DD + 6 regs) will exceed range |
| 0x00E3 | 40228 | 只读 | 枚举 | 1 | 瞬时留样信息- -执行结果及失败原因 | R:incomplete W:no | g_LastInstantRetainInfo (dayue_build_retain_instant_block) | missing; code read block is 0x00D4~0x00DE; cannot cover up to 0x00E6; spec read pattern (00DD + 6 regs) will exceed range |
| 0x00E4 | 40229 | 只读 | 整数 | 1 | 瞬时留样信息- -留样起 始瓶号 | R:incomplete W:no | g_LastInstantRetainInfo (dayue_build_retain_instant_block) | missing; code read block is 0x00D4~0x00DE; cannot cover up to 0x00E6; spec read pattern (00DD + 6 regs) will exceed range |
| 0x00E5 | 40230 | 只读 | 整数 | 1 | 瞬时留样信息- -留样总 瓶数 | R:incomplete W:no | g_LastInstantRetainInfo (dayue_build_retain_instant_block) | missing; code read block is 0x00D4~0x00DE; cannot cover up to 0x00E6; spec read pattern (00DD + 6 regs) will exceed range |
| 0x00E6 | 40231 | 只读 | 整数 | 1 | 瞬时留样信息- -留样量 | R:incomplete W:no | g_LastInstantRetainInfo (dayue_build_retain_instant_block) | missing; code read block is 0x00D4~0x00DE; cannot cover up to 0x00E6; spec read pattern (00DD + 6 regs) will exceed range |
| 0x00E7~0x00EC | 40232~40237 | 只读 | BCD | 6 | 弃样记录信息 --弃样时间 | R:no W:no |  | missing |
| 0x00ED | 40238 | 只读 | 枚举 | 1 | 弃样记录信息- -执行结 果 | R:no W:no |  | missing |
| 0x00EE | 40239 | 只读 | 枚举 | 1 | 弃样记录信息- -执行失 败原因 | R:no W:no |  | missing |
| 0x00EF | 40240 | 只读 | 整数 | 1 | 弃样记录信息- -留样起 始瓶号 | R:no W:no |  | missing |
| 0x00F0 | 40241 | 只读 | 整数 | 1 | 弃样记录信息- -留样总 瓶数 | R:no W:no |  | missing |
| 0x00F1~0x00F4 | 40242~40245 | 只读 |  | 4 | 弃样记录信息 --样品编号 | R:no W:no |  | missing |
| 0x00F5~0x0129 | 40246~40298 | 只读 | 预留 | 53 | 保留 | R:no W:no |  | missing |
| 0x012A | 40299 | 只读 | 整数 | 1 | 最后一次制水完成时 间年 | R:no W:no |  | missing |
| 0x012B | 40300 | 只读 | 整数 | 1 | 最后一次制水完成时 间月 | R:no W:no |  | missing |
| 0x012C | 40301 | 只读 | 整数 | 1 | 最后一次制水完成时 间日 | R:no W:no |  | missing |
| 0x012D | 40302 | 只读 | 整数 | 1 | 最后一次制水完成时 间时 | R:no W:no |  | missing |
| 0x012E | 40303 | 只读 | 整数 | 1 | 最后一次制水完成时 间分 | R:no W:no |  | missing |
| 0x012F | 40304 | 只读 | 整数 | 1 | 最后一次制水完成时 间秒 | R:no W:no |  | missing |
| 0x0130 | 40305 | 读写 | 枚举 | 1 | 水质因子1 编号 | R:no W:no |  | missing |
| 0x0131~0x0132 | 40306~40307 | 读写 | 浮点数 | 2 | 水质因子1 超标下限 | R:no W:no |  | missing |
| 0x0133~0x0134 | 40308~40309 | 读写 | 浮点数 | 2 | 水质因子1 超 标上限 | R:no W:no |  | missing |
| 0x0135 | 40310 | 读写 | 整数 | 1 | 水质因子1 超标平行 样数 | R:no W:no |  | missing |
| 0x0136 | 40311 | 读写 | 整数 | 1 | 水质因子1 超标留样 间隔 | R:no W:no |  | missing |
| 0x0137 | 40312 | 读写 | 枚举 | 1 | 水质因子1 超标留样设置--启用/停 用 | R:no W:no |  | missing |
| 0x0138 | 40313 | 读写 | 枚举 | 1 | 水质因子2 编 号 | R:no W:no |  | missing |
| 0x0139~0x013A | 40314~40315 | 读写 | 浮点数 | 2 | 水质因子2 超 标下限 | R:no W:no |  | missing |
| 0x013B~0x013C | 40316~40317 | 读写 | 浮点数 | 2 | 水质因子2 超 标上限 | R:no W:no |  | missing |
| 0x013D | 40318 | 读写 | 整数 | 1 | 水质因子2 超标平行 样数 | R:no W:no |  | missing |
| 0x013E | 40319 | 读写 | 整数 | 1 | 水质因子2 超标留样 间隔 | R:no W:no |  | missing |
| 0x013F | 40320 | 读写 | 枚举 | 1 | 水质因子2 超标留样设置--启用/停 用 | R:no W:no |  | missing |
| 0x0140 | 40321 | 读写 | 枚举 | 1 | 水质因子3 编 号 | R:no W:no |  | missing |
| 0x0141~0x0142 | 40322~40323 | 读写 | 浮点数 | 2 | 水质因 子3 超标下限 | R:no W:no |  | missing |
| 0x0143~0x0144 | 40324~40325 | 读写 | 浮点数 | 2 | 水质因子3 超 标上限 | R:no W:no |  | missing |
| 0x0145 | 40326 | 读写 | 整数 | 1 | 水质因子3 超标平行 样数 | R:no W:no |  | missing |
| 0x0146 | 40327 | 读写 | 整数 | 1 | 水质因 子3 超 标留样 间隔 | R:no W:no |  | missing |
| 0x0147 | 40328 | 读写 | 枚举 | 1 | 水质因子3 超标留样设置--启用/停 用 | R:no W:no |  | missing |
| 0x0148 | 40329 | 读写 | 枚举 | 1 | 水质因 子4 编号 | R:no W:no |  | missing |
| 0x0149~0x014A | 40330~40331 | 读写 | 浮点数 | 2 | 水质因子4 超 标下限 | R:no W:no |  | missing |
| 0x014B~0x014C | 40332~40333 | 读写 | 浮点数 | 2 | 水质因子4 超 标上限 | R:no W:no |  | missing |
| 0x014D | 40334 | 读写 | 整数 | 1 | 水质因子4 超标平行 样数 | R:no W:no |  | missing |
| 0x014E | 40335 | 读写 | 整数 | 1 | 水质因子4 超标留样 间隔 | R:no W:no |  | missing |
| 0x014F | 40336 | 读写 | 枚举 | 1 | 水质因子4 超标留样 设置-- 启用/停用 | R:no W:no |  | missing |
| 0x0150 | 40337 | 读写 | 枚举 | 1 | 水质因 子5 编号 | R:no W:no |  | missing |
| 0x0151~0x0152 | 40338~40339 | 读写 | 浮点数 | 2 | 水质因子5 超 标下限 | R:no W:no |  | missing |
| 0x0153~0x0154 | 40340~40341 | 读写 | 浮点数 | 2 | 水质因 子5 超标上限 | R:no W:no |  | missing |
| 0x0155 | 40342 | 读写 | 整数 | 1 | 水质因子5 超标平行 样数 | R:no W:no |  | missing |
| 0x0156 | 40343 | 读写 | 整数 | 1 | 水质因子5 超标留样 间隔 | R:no W:no |  | missing |
| 0x0157 | 40344 | 读写 | 枚举 | 1 | 水质因子5 超标留样设置--启用/停 用 | R:no W:no |  | missing |
| 0x0158 | 40345 | 读写 | 枚举 | 1 | 水质因 子6 编号 | R:no W:no |  | missing |
| 0x0159~0x015A | 40346~40347 | 读写 | 浮点数 | 2 | 水质因 子6 超标下限 | R:no W:no |  | missing |
| 0x015B~0x015C | 40348~40349 | 读写 | 浮点数 | 2 | 水质因 子6 超标上限 | R:no W:no |  | missing |
| 0x015D | 40350 | 读写 | 整数 | 1 | 水质因子6 超标平行 样数 | R:no W:no |  | missing |
| 0x015E | 40351 | 读写 | 整数 | 1 | 水质因子6 超标留样 间隔 | R:no W:no |  | missing |
| 0x015F | 40352 | 读写 | 枚举 | 1 | 水质因子6 超标留样设置--启用/停 用 | R:no W:no |  | missing |
| 0x0160~0x017F | 40353~40384 | 只读 | 预留 | 32 | 保留 | R:no W:no |  | missing |
| 0x0180 | 40385 | 读写 | 枚举 | 1 | 采样模式 | R:yes W:no | g_SampleConfig + mode remap (dayue_build_sampling_params_block) | partial; spec is R/W; code lacks write |
| 0x0181 | 40386 | 读写 | 整数 | 1 | 单次采 样量 | R:yes W:no | g_SampleConfig + mode remap (dayue_build_sampling_params_block) | partial; spec is R/W; code lacks write |
| 0x0182 | 40387 | 读写 | 整数 | 1 | 时间等比模式参数--间隔时 间 | R:yes W:no | g_SampleConfig + mode remap (dayue_build_sampling_params_block) | partial; spec is R/W; code lacks write |
| 0x0183 | 40388 | 读写 | 整数 | 1 | 排放等比模式参数-- 排放量 | R:yes W:no | g_SampleConfig + mode remap (dayue_build_sampling_params_block) | partial; spec is R/W; code lacks write |
| 0x0184 | 40389 | 读写 | 整数 | 1 | 流量比例模式参数--间隔时 间 | R:yes W:no | g_SampleConfig + mode remap (dayue_build_sampling_params_block) | partial; spec is R/W; code lacks write |
| 0x0185 | 40390 | 读写 | 整数 | 1 | 流量比例模式参数--采样比例 | R:yes W:no | g_SampleConfig + mode remap (dayue_build_sampling_params_block) | partial; spec is R/W; code lacks write |
| 0x0186 | 40391 | 读写 | 整数 | 1 | 流量触 发模式参数-- 触发阈 值 | R:no W:no |  | missing |
| 0x0187 | 40392 | 读写 | 整数 | 1 | 流量触发模式参数--触发间 隔 | R:no W:no |  | missing |
| 0x0188~0x0197 | 40393~40408 | 只读 | 预留 | 16 | 保留 | R:no W:no |  | missing |
| 0x0198 | 40409 | 读写 | 枚举 | 1 | 留样模式 | R:no W:no |  | missing |
| 0x0199 | 40410 | 读写 | 整数 | 1 | 单次留 样量 | R:no W:no |  | missing |
| 0x019A | 40411 | 读写 | 整数 | 1 | 平行样 数 | R:no W:no |  | missing |
| 0x019B | 40412 | 读写 | 整数 | 1 | 单瓶混 合次数 | R:yes W:no | dynamic_code=(hour*100+min)%10000 (+10000 if sec even) | mismatch; spec meaning differs |
| 0x019C | 40413 | 读写 | 枚举 | 1 | 是否加 药 | R:yes W:no | ready=(Mode==3 && used_count<24) | mismatch; spec meaning differs |
| 0x019D | 40414 | 只读 | 枚举 | 1 | 药剂类型 | R:no W:no |  | missing |
| 0x019E | 40415 | 只读 | 整数 | 1 | 留样瓶 加药比 | R:no W:no |  | missing |
| 0x019F~0x01AC | 40416~40429 | 只读 | 预留 | 14 | 保留 | R:no W:no |  | missing |
| 0x01AD | 40430 | 读写 | 枚举 | 1 | 控制仪器运行状态 | R:no W:partial | dayue_write_holding: 0x0001 reset / 0x0002 start / 0x0003 stop | partial; spec defines 0x0003 pause and 0x0004 resume; no readback/handshake |
| 0x01AE | 40431 | 读写 | 枚举 | 1 | 复位瓶信息 | R:no W:no |  | missing |
| 0x01AF | 40432 | 只读 | 预留 | 1 | 保留 | R:no W:no |  | missing |
| 0x01B0 | 40433 | 只读 | 枚举 | 1 | 因子1 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01B1~0x01B2 | 40434~40435 | 读写 | 浮点数 | 2 | 因子1 测量值 （数采定时30秒写 入） | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01B3 | 40436 | 只读 | 枚举 | 1 | 因子2 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01B4~0x01B5 | 40437~40438 | 读写 | 浮点 数 | 2 | 因子2 测量值 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01B6 | 40439 | 只读 | 枚举 | 1 | 因子3 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01B7~0x01B8 | 40440~40441 | 读写 | 浮点 数 | 2 | 因子3 测量值 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01B9 | 40442 | 只读 | 枚举 | 1 | 因子4 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01BA~0x01BB | 40443~40444 | 读写 | 浮点 数 | 2 | 因子4 测量值 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01BC | 40445 | 只读 | 枚举 | 1 | 因子5 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01BD~0x01BE | 40446~40447 | 读写 | 浮点 数 | 2 | 因子5 测量值 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01BF | 40448 | 只读 | 枚举 | 1 | 因子6 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01C0~0x01C1 | 40449~40450 | 读写 | 浮点 数 | 2 | 因子6 测量值 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01C2 | 40451 | 只读 | 枚举 | 1 | 因子7 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01C3~0x01C4 | 40452~40453 | 读写 | 浮点 数 | 2 | 因子7 测量值 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01C5 | 40454 | 只读 | 枚举 | 1 | 因子8 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01C6~0x01C7 | 40455~40456 | 读写 | 浮点 数 | 2 | 因子8 测量值 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01C8 | 40457 | 只读 | 枚举 | 1 | 因子9 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01C9~0x01CA | 40458~40459 | 读写 | 浮点 数 | 2 | 因子9 测量值 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01CB | 40460 | 只读 | 枚举 | 1 | 因子10 编号 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01CC~0x01CD | 40461~40462 | 读写 | 浮点 数 | 2 | 因子10 测量值 | R:no W:yes | g_FactorDataFromHost[] (addr==0x01B0, nregs==3*N) | partial; write-only implemented; readback missing |
| 0x01CE~0x01D9 | 40463~40474 | 只读 | 预留 | 12 | 保留 | R:no W:no |  | missing |
| 0x01DA | 40475 | 只写 | 整数 | 1 | 设置时 间年 | R:no W:partial | rtc_time_set: addr==0x01DA, nregs==7(year,mon,day,weekday,hour,min,sec) | mismatch; spec is year/mon/day/hour/min/sec/cmd; code uses weekday and ignores cmd |
| 0x01DB | 40476 | 只写 | 整数 | 1 | 设置时 间月 | R:no W:partial | rtc_time_set: addr==0x01DA, nregs==7(year,mon,day,weekday,hour,min,sec) | mismatch; spec is year/mon/day/hour/min/sec/cmd; code uses weekday and ignores cmd |
| 0x01DC | 40477 | 只写 | 整数 | 1 | 设置时 间日 | R:no W:partial | rtc_time_set: addr==0x01DA, nregs==7(year,mon,day,weekday,hour,min,sec) | mismatch; spec is year/mon/day/hour/min/sec/cmd; code uses weekday and ignores cmd |
| 0x01DD | 40478 | 只写 | 整数 | 1 | 设置时 间时 | R:no W:partial | rtc_time_set: addr==0x01DA, nregs==7(year,mon,day,weekday,hour,min,sec) | mismatch; spec is year/mon/day/hour/min/sec/cmd; code uses weekday and ignores cmd |
| 0x01DE | 40479 | 只写 | 整数 | 1 | 设置时 间分 | R:no W:partial | rtc_time_set: addr==0x01DA, nregs==7(year,mon,day,weekday,hour,min,sec) | mismatch; spec is year/mon/day/hour/min/sec/cmd; code uses weekday and ignores cmd |
| 0x01DF | 40480 | 只写 | 整数 | 1 | 设置时 间秒 | R:no W:partial | rtc_time_set: addr==0x01DA, nregs==7(year,mon,day,weekday,hour,min,sec) | mismatch; spec is year/mon/day/hour/min/sec/cmd; code uses weekday and ignores cmd |
| 0x01E0 | 40481 | 只写 | 枚举 | 1 | 设置时 间命令 | R:no W:partial | rtc_time_set: addr==0x01DA, nregs==7(year,mon,day,weekday,hour,min,sec) | mismatch; spec is year/mon/day/hour/min/sec/cmd; code uses weekday and ignores cmd |
| 0x01E1 | 40482 | 只读 | 整数 | 1 | 系统当前时间年 | R:yes W:no | rtc_time_get -> calendar (dayue_build_system_time_block) | implemented |
| 0x01E2 | 40483 | 只读 | 整数 | 1 | 系统当 前时间月 | R:yes W:no | rtc_time_get -> calendar (dayue_build_system_time_block) | implemented |
| 0x01E3 | 40484 | 只读 | 整数 | 1 | 系统当 前时间日 | R:yes W:no | rtc_time_get -> calendar (dayue_build_system_time_block) | implemented |
| 0x01E4 | 40485 | 只读 | 整数 | 1 | 系统当 前时间时 | R:yes W:no | rtc_time_get -> calendar (dayue_build_system_time_block) | implemented |
| 0x01E5 | 40486 | 只读 | 整数 | 1 | 系统当 前时间分 | R:yes W:no | rtc_time_get -> calendar (dayue_build_system_time_block) | implemented |
| 0x01E6 | 40487 | 只读 | 整数 | 1 | 系统当 前时间秒 | R:yes W:no | rtc_time_get -> calendar (dayue_build_system_time_block) | implemented |
| 0x01E7~0x01E8 | 40488~40489 | 读写 | 长整数 | 2 | 管理员门禁卡号1 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01E9~0x01EA | 40490~40491 | 读写 | 长整数 | 2 | 管理员 门禁卡号2 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01EB~0x01EC | 40492~40493 | 读写 | 长整数 | 2 | 管理员 门禁卡号3 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01ED~0x01EE | 40494~40495 | 读写 | 长整数 | 2 | 管理员 门禁卡号4 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01EF~0x01F0 | 40496~40497 | 读写 | 长整数 | 2 | 管理员门禁卡 号5 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01F1~0x01F2 | 40498~40499 | 读写 | 长整数 | 2 | 操作员 门禁卡号1 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01F3~0x01F4 | 40500~40501 | 读写 | 长整数 | 2 | 操作员门禁卡 号2 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01F5~0x01F6 | 40502~40503 | 读写 | 长整数 | 2 | 操作员 门禁卡号3 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01F7~0x01F8 | 40504~40505 | 读写 | 长整数 | 2 | 操作员 门禁卡号4 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01F9~0x01FA | 40506~40507 | 读写 | 长整数 | 2 | 操作员 门禁卡号5 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01FB~0x01FC | 40508~40509 | 读写 | 长整数 | 2 | 留样后门禁开启动态密码，密码位数暂时为4 位 （最多可扩展为8 位） | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01FD | 40510 | 读写 | 无符号整 数 | 1 | 管理员密码 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01FE | 40511 | 读写 | 无符号整 数 | 1 | 操作员密码 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x01FF | 40512 | 读写 | 无符 号整数 | 1 | 取样员密码 | R:no W:no |  | missing; door-card/password regs not implemented |
| 0x0200~0x020E | 40513~40527 | 只读 | 预留 | 15 | 保留 | R:no W:no |  | missing |
| 0x020F | 40528 | 读写 | 无符号整数 | 1 | 仪器联网地址 （联网时的唯一编 号） | R:no W:no |  | missing; network addr / serial number regs not implemented |
| 0x0210~0x0215 | 40529~40534 | 只读 | 文本 | 6 | 仪器出厂编号 | R:no W:no |  | missing; network addr / serial number regs not implemented |
| 0x0216 | 40535 | 读写 | 枚举 | 1 | 触发一次A 桶动作 | R:no W:partial | g_comm_trigger_request + g_dayue_cmd_status (dayue_write_holding) | partial; supports 0x0001/0x0002/0x0003 only; no 0x81/0xC1 readback |
| 0x0217 | 40536 | 读写 | 枚举 | 1 | 触发一次B 桶动作 | R:no W:partial | g_comm_trigger_request + g_dayue_cmd_status (dayue_write_holding) | partial; supports 0x0001/0x0002/0x0003 only; no 0x81/0xC1 readback |
| 0x0218 | 40537 | 读写 | 枚举 | 1 | 触发一次桶动作 注意：本指令由仪器自主寻找合适的桶 | R:no W:partial | g_comm_trigger_request + g_dayue_cmd_status (dayue_write_holding) | partial; supports 0x0001/0x0002/0x0003 only; no 0x81/0xC1 readback |
| 0x0219 | 40538 | 读写 | 枚举 | 1 | 留样/排空起始瓶 | R:no W:partial | only supports 0x10 write 3 regs at once (0x0219~0x021B) | partial; single-reg write not supported |
| 0x021A | 40539 | 读写 | 枚举 | 1 | 留样/排空总瓶数 | R:no W:partial | only supports 0x10 write 3 regs at once (0x0219~0x021B) | partial; single-reg write not supported |
| 0x021B | 40540 | 读写 | 枚举 | 1 | 触发一次留样或排空留样可以从 A/B 桶或外管路取水样到瓶 | R:no W:partial | addr==0x0219,nregs==3: type=1..5 (retain/instant/bottle-empty) | Spec doc formatting issue: address column is "5"; inferred as 021B from context; partial; spec expects trigger/readback semantics; code write requires 3-reg write |
| 0x021C | 40541 | 读写 | 枚举 | 1 | 直接瞬时留样 | R:yes W:partial | read: delivery_ready; write: instant_retention_execute(0, bottleCount) | partial; spec table meaning is instant retain; doc appendix also uses 40541 (0x021C) as delivery-ready |
| 0x021D | 40542 | 读写 | 枚举 | 1 | 送样同步信号 | R:no W:no |  | missing |
| 0x021E | 40543 | 读写 | 枚举 | 1 | 超标同步信号 | R:yes W:no | read: channelData[] > 10.0 -> 1 | mismatch; spec is host-written sync signal |
| 0x021F~0x0220 | 40544~40545 | 读写 | 浮点数 | 2 | 流速数值 | R:yes W:no | g_RetainSampleConfig.channelData[7] -> float | partial; spec is R/W; write missing |
| 0x0221~0x0247 | 40546~40584 | 只读 | 预留 | 39 | 保留 | R:no W:no |  | missing |
| 0x0248~0x0257 | 40585~40600 | 读写 | 文本 | 16 | 授权码 | R:no W:no |  | missing |
| 0x0258 | 40601 | - | 预留 | 0 |  | R:no W:no |  | missing |
