# -*- coding: utf-8 -*-
"""
MQTT客户端封装
"""
import paho.mqtt.client as mqtt
import time
import threading
from utils.config import MQTT_CONFIG


class MQTTClient:
    """MQTT客户端封装类"""

    def __init__(self, host=None, port=None, keepalive=None):
        """
        初始化MQTT客户端

        Args:
            host: MQTT服务器地址
            port: MQTT服务器端口
            keepalive: 保活时间
        """
        self.host = host or MQTT_CONFIG['host']
        self.port = port or MQTT_CONFIG['port']
        self.keepalive = keepalive or MQTT_CONFIG['keepalive']

        self.client = mqtt.Client()
        self.connected = False
        self.lock = threading.Lock()

        # 回调函数注册
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_publish = self._on_publish
        self.client.on_message = self._on_message

        # 消息回调字典
        self.message_callbacks = {}

    def _on_connect(self, client, userdata, flags, rc):
        """连接回调"""
        with self.lock:
            if rc == 0:
                self.connected = True
                print(f"成功连接到MQTT服务器 {self.host}:{self.port}")
            else:
                self.connected = False
                error_messages = {
                    1: "协议版本不正确",
                    2: "客户端ID无效",
                    3: "服务器不可用",
                    4: "用户名或密码错误",
                    5: "未授权"
                }
                print(f"连接失败: {error_messages.get(rc, f'未知错误码: {rc}')}")

    def _on_disconnect(self, client, userdata, rc):
        """断开连接回调"""
        with self.lock:
            self.connected = False
        if rc != 0:
            print(f"意外断开连接，尝试重连...")

    def _on_publish(self, client, userdata, mid):
        """发布消息回调"""
        pass

    def _on_message(self, client, userdata, msg):
        """接收消息回调"""
        topic = msg.topic
        payload = msg.payload

        # 调用注册的回调函数
        if topic in self.message_callbacks:
            self.message_callbacks[topic](topic, payload)

    def connect(self, timeout=10):
        """
        连接MQTT服务器

        Args:
            timeout: 连接超时时间

        Returns:
            bool: 连接是否成功
        """
        try:
            self.client.connect(self.host, self.port, self.keepalive)
            self.client.loop_start()

            # 等待连接完成
            start_time = time.time()
            while not self.connected and (time.time() - start_time) < timeout:
                time.sleep(0.1)

            return self.connected
        except Exception as e:
            print(f"连接MQTT服务器失败: {e}")
            return False

    def disconnect(self):
        """断开连接"""
        if self.connected:
            self.client.loop_stop()
            self.client.disconnect()

    def publish(self, topic, payload, qos=None):
        """
        发布消息

        Args:
            topic: 主题
            payload: 消息内容
            qos: 服务质量等级

        Returns:
            bool: 发布是否成功
        """
        if not self.connected:
            print("MQTT未连接，无法发布消息")
            return False

        qos = qos or MQTT_CONFIG['qos']

        try:
            result = self.client.publish(topic, payload, qos)
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                return True
            else:
                print(f"发布消息失败: {result.rc}")
                return False
        except Exception as e:
            print(f"发布消息异常: {e}")
            return False

    def subscribe(self, topic, callback=None, qos=None):
        """
        订阅主题

        Args:
            topic: 主题
            callback: 消息回调函数
            qos: 服务质量等级

        Returns:
            bool: 订阅是否成功
        """
        if not self.connected:
            print("MQTT未连接，无法订阅主题")
            return False

        qos = qos or MQTT_CONFIG['qos']

        # 注册回调函数
        if callback:
            self.message_callbacks[topic] = callback

        try:
            result, mid = self.client.subscribe(topic, qos)
            if result == mqtt.MQTT_ERR_SUCCESS:
                print(f"成功订阅主题: {topic}")
                return True
            else:
                print(f"订阅主题失败: {result}")
                return False
        except Exception as e:
            print(f"订阅主题异常: {e}")
            return False

    def unsubscribe(self, topic):
        """
        取消订阅主题

        Args:
            topic: 主题
        """
        if topic in self.message_callbacks:
            del self.message_callbacks[topic]

        try:
            self.client.unsubscribe(topic)
            print(f"取消订阅主题: {topic}")
        except Exception as e:
            print(f"取消订阅异常: {e}")

    def is_connected(self):
        """检查是否已连接"""
        return self.connected