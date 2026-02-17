-- 采样机数据库表初始化脚本
-- 数据库: dydb
-- 在PostgreSQL中执行此脚本创建表

-- 采样记录表
CREATE TABLE IF NOT EXISTS sampling_records (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,           -- 设备ID
    trigger_source VARCHAR(50) NOT NULL,      -- 触发来源
    bucket_no INTEGER NOT NULL,               -- 桶号
    sample_amount DECIMAL(10,2) NOT NULL,     -- 采样量
    sample_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP  -- 采样时间
);

-- 送样记录表
CREATE TABLE IF NOT EXISTS sending_records (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,           -- 设备ID
    trigger_source VARCHAR(50) NOT NULL,      -- 触发来源
    bucket_no INTEGER NOT NULL,               -- 桶号
    send_amount DECIMAL(10,2) NOT NULL,       -- 送样量
    send_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP    -- 送样时间
);

-- 留样记录表
CREATE TABLE IF NOT EXISTS retention_records (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,           -- 设备ID
    trigger_source VARCHAR(50) NOT NULL,      -- 触发来源
    bottle_no INTEGER NOT NULL,               -- 留样瓶号
    retain_amount DECIMAL(10,2) NOT NULL,     -- 留样量
    retain_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP  -- 留样时间
);

-- 开关门记录表
CREATE TABLE IF NOT EXISTS door_records (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,           -- 设备ID
    door_action VARCHAR(20) NOT NULL,         -- 开/关
    door_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,   -- 操作时间
    operator VARCHAR(50)                      -- 操作人
);

-- 创建索引提升查询性能
CREATE INDEX IF NOT EXISTS idx_sampling_time ON sampling_records(sample_time DESC);
CREATE INDEX IF NOT EXISTS idx_sending_time ON sending_records(send_time DESC);
CREATE INDEX IF NOT EXISTS idx_retention_time ON retention_records(retain_time DESC);
CREATE INDEX IF NOT EXISTS idx_door_time ON door_records(door_time DESC);

-- 创建设备ID索引
CREATE INDEX IF NOT EXISTS idx_sampling_device ON sampling_records(device_id);
CREATE INDEX IF NOT EXISTS idx_sending_device ON sending_records(device_id);
CREATE INDEX IF NOT EXISTS idx_retention_device ON retention_records(device_id);
CREATE INDEX IF NOT EXISTS idx_door_device ON door_records(device_id);
