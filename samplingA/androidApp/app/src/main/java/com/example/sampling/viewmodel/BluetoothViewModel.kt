package com.example.sampling.viewmodel

import android.app.Application
import android.bluetooth.BluetoothProfile
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.sampling.data.BluetoothLeManager
import com.example.sampling.data.ScannedDevice
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.first

import com.example.sampling.data.OtaHelper
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.nio.charset.Charset

class BluetoothViewModel(application: Application) : AndroidViewModel(application) {

    private val bluetoothManager = BluetoothLeManager(application)

    val scannedDevices = bluetoothManager.scannedDevices
    val connectionState = bluetoothManager.connectionState
    val logs = bluetoothManager.logs
    val receivedData = bluetoothManager.receivedData

    private val _otaProgress = MutableStateFlow(0.0f)
    val otaProgress = _otaProgress.asStateFlow()

    private val _otaStatus = MutableStateFlow("")
    val otaStatus = _otaStatus.asStateFlow()

    fun startScan() {
        bluetoothManager.startScan()
    }

    fun stopScan() {
        bluetoothManager.stopScan()
    }

    fun connect(address: String) {
        bluetoothManager.connect(address)
    }

    fun disconnect() {
        bluetoothManager.disconnect()
    }

    fun startOta(firmwareUrl: String) {
        viewModelScope.launch {
            if (connectionState.value != BluetoothProfile.STATE_CONNECTED) {
                _otaStatus.value = "Not connected"
                return@launch
            }

            _otaProgress.value = 0.01f
            _otaStatus.value = "Downloading firmware..."

            val firmwareData = withContext(Dispatchers.IO) {
                OtaHelper.downloadFirmware(firmwareUrl)
            }
            
            if (firmwareData == null) {
                _otaStatus.value = "Download failed"
                _otaProgress.value = 0f
                return@launch
            }

            val fileSize = firmwareData.size
            val crc16 = OtaHelper.crc16(firmwareData)
            val deviceId = "CYJ_2512121H1001B"

            _otaStatus.value = "Sending Start Command..."

            // Send Start Command: ML307OTA_{DeviceID}_{CRC}_{Size}
            val startCmd = "ML307OTA_${deviceId}_${String.format("%04X", crc16)}_${fileSize}"
            bluetoothManager.sendData(startCmd.toByteArray(Charsets.UTF_8))

            // Wait a bit for device to be ready (erase flash)
            _otaStatus.value = "Waiting for device..."
            kotlinx.coroutines.delay(5000)

            val packetSize = 256
            val totalPackets = (fileSize + packetSize - 1) / packetSize

            for (i in 0 until totalPackets) {
                val packetId = i + 1
                val start = i * packetSize
                val end = minOf(start + packetSize, fileSize)
                val chunk = firmwareData.copyOfRange(start, end)

                // Create packet with header/checksum
                val packetData = OtaHelper.createPacket(packetId, totalPackets, chunk)

                // Send packet (manager handles 20-byte splitting)
                _otaStatus.value = "Sending packet $packetId/$totalPackets"
                bluetoothManager.sendData(packetData)

                // Wait for ACK
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
            receivedData.filter { data ->
                val msg = String(data, Charsets.UTF_8)
                msg.contains("ACK_${packetId}_OK")
            }.first()
            true
        } ?: false
    }
}
