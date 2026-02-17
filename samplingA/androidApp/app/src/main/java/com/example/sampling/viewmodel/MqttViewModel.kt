package com.example.sampling.viewmodel

import android.util.Base64
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.sampling.data.MqttManager
import com.example.sampling.data.OtaHelper
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull

class MqttViewModel : ViewModel() {

    private val mqttManager = MqttManager()

    val connectionState = mqttManager.connectionState
    val logs = mqttManager.logs

    private val _otaProgress = MutableStateFlow(0.0f)
    val otaProgress = _otaProgress.asStateFlow()

    private val _otaStatus = MutableStateFlow("")
    val otaStatus = _otaStatus.asStateFlow()

    // Command topic from config
    private val COMMAND_TOPIC = "CYJSET"
    // Data/ACK topic from config
    private val DATA_TOPIC = "CYJOTA"

    init {
        // Automatically connect on init or let user trigger? User request implies manual control or auto.
        // Let's keep it manual for now via UI button, but we could auto-connect.
    }

    fun connect() {
        mqttManager.connect()
    }

    fun disconnect() {
        mqttManager.disconnect()
    }

    fun sendCommand(command: String) {
        if (mqttManager.isConnected()) {
            mqttManager.publish(COMMAND_TOPIC, command)
            // Log handled by manager, but we could add "Sent: ..." here if needed
        }
    }

    fun startOta(firmwareUrl: String, upgradeId: String) {
        viewModelScope.launch {
            if (!mqttManager.isConnected()) {
                _otaStatus.value = "MQTT not connected"
                return@launch
            }

            _otaProgress.value = 0.01f
            _otaStatus.value = "Downloading firmware..."

            val firmwareData = OtaHelper.downloadFirmware(firmwareUrl)
            if (firmwareData == null) {
                _otaStatus.value = "Download failed"
                _otaProgress.value = 0f
                return@launch
            }

            val fileSize = firmwareData.size
            val crc16 = OtaHelper.crc16(firmwareData)
            // Device ID logic: Assuming upgradeId is the device ID or part of it.
            // If upgradeId is just a version number, we might need a target device ID.
            // For now, let's assume upgradeId IS the target device ID as per "ÊäÈëÉý¼¶µÄ±àºÅ".
            val deviceId = if (upgradeId.isNotBlank()) upgradeId else "CYJ_2512121H1001B"

            _otaStatus.value = "Sending Start Command..."
            
            // Trigger command: ML307OTA_{DeviceID}_{CRC}_{Size}
            val startCmd = "ML307OTA_${deviceId}_${String.format("%04X", crc16)}_${fileSize}"
            mqttManager.publish(COMMAND_TOPIC, startCmd)

            _otaStatus.value = "Waiting for device..."
            delay(5000) // Wait for flash erase

            val packetSize = 256
            val totalPackets = (fileSize + packetSize - 1) / packetSize

            for (i in 0 until totalPackets) {
                val packetId = i + 1
                val start = i * packetSize
                val end = minOf(start + packetSize, fileSize)
                val chunk = firmwareData.copyOfRange(start, end)

                // 1. Create Raw Packet (Header + Data + Checksum)
                val rawPacket = OtaHelper.createPacket(packetId, totalPackets, chunk)

                // 2. Base64 Encode
                val base64Message = Base64.encodeToString(rawPacket, Base64.NO_WRAP)

                // 3. Publish
                _otaStatus.value = "Sending packet $packetId/$totalPackets"
                mqttManager.publish(DATA_TOPIC, base64Message)

                // 4. Wait for ACK
                val ackReceived = waitForAck(packetId)
                if (!ackReceived) {
                    _otaStatus.value = "OTA Failed: No ACK for packet $packetId"
                    _otaProgress.value = 0f
                    return@launch
                }

                _otaProgress.value = packetId.toFloat() / totalPackets
            }

            _otaStatus.value = "OTA Complete"
            _otaProgress.value = 0f
        }
    }

    private suspend fun waitForAck(packetId: Int): Boolean {
        return withTimeoutOrNull(5000) {
            mqttManager.messageReceived.collect { (topic, payload) ->
                if (topic == DATA_TOPIC) {
                    // Expecting ACK_{PacketID}_OK
                    if (payload.contains("ACK_${packetId}_OK")) {
                        return@collect
                    }
                    // Handle ERROR?
                    if (payload.contains("ACK_${packetId}_ERROR")) {
                        // Logic to resend could be added here
                    }
                }
            }
            true
        } ?: false
    }
}
