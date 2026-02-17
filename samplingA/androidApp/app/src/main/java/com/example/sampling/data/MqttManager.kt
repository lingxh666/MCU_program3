package com.example.sampling.data

import android.util.Log
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import org.eclipse.paho.client.mqttv3.*
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence

class MqttManager {

    private var mqttClient: MqttAsyncClient? = null
    private val serverUri = "tcp://124.222.59.221:1883"
    private val clientId = "AndroidApp_" + System.currentTimeMillis()

    private val _connectionState = MutableStateFlow(false)
    val connectionState = _connectionState.asStateFlow()

    private val _messageReceived = MutableSharedFlow<Pair<String, String>>(
        replay = 0,
        extraBufferCapacity = 64,
        onBufferOverflow = BufferOverflow.DROP_OLDEST
    )
    val messageReceived = _messageReceived.asSharedFlow()

    private val _logs = MutableStateFlow<List<String>>(emptyList())
    val logs = _logs.asStateFlow()

    private fun addLog(msg: String) {
        val current = _logs.value.toMutableList()
        current.add(msg)
        if (current.size > 100) current.removeAt(0)
        _logs.value = current
        Log.d("MqttManager", msg)
    }

    fun connect() {
        try {
            if (mqttClient != null && mqttClient!!.isConnected) {
                return
            }

            mqttClient = MqttAsyncClient(serverUri, clientId, MemoryPersistence())
            val options = MqttConnectOptions()
            options.isCleanSession = true
            options.connectionTimeout = 30
            options.keepAliveInterval = 60
            options.isAutomaticReconnect = true

            mqttClient!!.setCallback(object : MqttCallbackExtended {
                override fun connectionLost(cause: Throwable?) {
                    _connectionState.value = false
                    addLog("Connection lost: ${cause?.message}")
                }

                override fun messageArrived(topic: String?, message: MqttMessage?) {
                    val payload = message?.toString() ?: return
                    topic?.let {
                        _messageReceived.tryEmit(it to payload)
                        // addLog("RX [$topic]: $payload")
                    }
                }

                override fun deliveryComplete(token: IMqttDeliveryToken?) {
                    // Delivery complete
                }

                override fun connectComplete(reconnect: Boolean, serverURI: String?) {
                    _connectionState.value = true
                    addLog("Connected to $serverURI (Reconnect: $reconnect)")
                    subscribe("CYJOTA") // Subscribe to data/ACK topic
                }
            })

            mqttClient!!.connect(options, null, object : IMqttActionListener {
                override fun onSuccess(asyncActionToken: IMqttToken?) {
                    // connectComplete will handle state update
                }

                override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                    _connectionState.value = false
                    addLog("Connect failed: ${exception?.message}")
                }
            })

        } catch (e: Exception) {
            addLog("Connect error: ${e.message}")
            e.printStackTrace()
        }
    }

    fun disconnect() {
        try {
            mqttClient?.disconnect()
            _connectionState.value = false
            addLog("Disconnected")
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    fun subscribe(topic: String, qos: Int = 0) {
        try {
            mqttClient?.subscribe(topic, qos)
            addLog("Subscribed to $topic")
        } catch (e: Exception) {
            addLog("Subscribe failed: ${e.message}")
        }
    }

    fun publish(topic: String, message: String, qos: Int = 0) {
        try {
            if (mqttClient?.isConnected == true) {
                val mqttMessage = MqttMessage(message.toByteArray())
                mqttMessage.qos = qos
                mqttClient!!.publish(topic, mqttMessage)
                // addLog("TX [$topic]: $message")
            } else {
                addLog("Not connected, cannot publish")
            }
        } catch (e: Exception) {
            addLog("Publish failed: ${e.message}")
        }
    }
    
    fun isConnected(): Boolean {
        return mqttClient?.isConnected == true
    }
}
