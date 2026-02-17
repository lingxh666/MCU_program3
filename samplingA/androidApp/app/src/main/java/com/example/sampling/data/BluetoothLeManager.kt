package com.example.sampling.data

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.os.Build
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch
import java.util.UUID

data class ScannedDevice(val name: String, val address: String, val rssi: Int, val device: android.bluetooth.BluetoothDevice)

@SuppressLint("MissingPermission")
class BluetoothLeManager(private val context: Context) {

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        try {
            val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            bluetoothManager?.adapter
        } catch (e: Exception) {
            Log.e("BluetoothLeManager", "Failed to get BluetoothAdapter", e)
            null
        }
    }

    private var bluetoothGatt: BluetoothGatt? = null
    private var writeCharacteristic: BluetoothGattCharacteristic? = null

    // UUIDs from Demo
    // Base UUID: 0000xxxx-0000-1000-8000-00805f9b34fb
    private val SERVICE_UUID_PART = "0000-1000-8000-00805f9b34fb"
    private val WRITE_UUID = UUID.fromString("0000fff2-$SERVICE_UUID_PART")
    private val NOTIFY_UUID = UUID.fromString("0000fff1-$SERVICE_UUID_PART")
    private val CLIENT_CHARACTERISTIC_CONFIG = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    // StateFlows for UI
    private val _scannedDevices = MutableStateFlow<List<ScannedDevice>>(emptyList())
    val scannedDevices = _scannedDevices.asStateFlow()

    // SharedFlow for received data events (OTA ACKs)
    private val _receivedData = MutableSharedFlow<ByteArray>()
    val receivedData = _receivedData.asSharedFlow()

    private val _connectionState = MutableStateFlow(BluetoothProfile.STATE_DISCONNECTED)
    val connectionState = _connectionState.asStateFlow()

    private val _logs = MutableStateFlow<List<String>>(emptyList())
    val logs = _logs.asStateFlow()

    private val foundDevicesMap = mutableMapOf<String, ScannedDevice>()

    private fun addLog(msg: String) {
        val currentLogs = _logs.value.toMutableList()
        currentLogs.add(msg)
        if (currentLogs.size > 100) currentLogs.removeAt(0)
        _logs.value = currentLogs
        Log.d("BluetoothLeManager", msg)
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val name = device.name ?: "Unknown"
            val address = device.address
            val rssi = result.rssi
            
            if (!foundDevicesMap.containsKey(address)) {
                val scannedDevice = ScannedDevice(name, address, rssi, device)
                foundDevicesMap[address] = scannedDevice
                _scannedDevices.value = foundDevicesMap.values.toList()
                // addLog("Found: $name ($address)")
            }
        }
        
        override fun onScanFailed(errorCode: Int) {
            addLog("Scan failed with error: $errorCode")
        }
    }

    fun startScan() {
        if (bluetoothAdapter?.isEnabled == true) {
            foundDevicesMap.clear()
            _scannedDevices.value = emptyList()
            bluetoothAdapter?.bluetoothLeScanner?.startScan(scanCallback)
            addLog("Scanning started...")
        } else {
            addLog("Bluetooth is disabled")
        }
    }

    fun stopScan() {
        if (bluetoothAdapter?.isEnabled == true) {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
            addLog("Scanning stopped.")
        }
    }

    fun connect(address: String) {
        stopScan()
        val device = bluetoothAdapter?.getRemoteDevice(address)
        if (device == null) {
            addLog("Device not found: $address")
            return
        }
        
        addLog("Connecting to $address...")
        _connectionState.value = BluetoothProfile.STATE_CONNECTING
        bluetoothGatt = device.connectGatt(context, false, gattCallback)
    }

    fun disconnect() {
        bluetoothGatt?.disconnect()
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            _connectionState.value = newState
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                addLog("Connected to GATT server.")
                gatt.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                addLog("Disconnected from GATT server.")
                bluetoothGatt?.close()
                bluetoothGatt = null
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                addLog("Services discovered.")
                // Find characteristics
                gatt.services.forEach { service ->
                    service.characteristics.forEach { characteristic ->
                        if (characteristic.uuid == WRITE_UUID) {
                            writeCharacteristic = characteristic
                            addLog("Write characteristic found.")
                        }
                        if (characteristic.uuid == NOTIFY_UUID) {
                            enableNotifications(gatt, characteristic)
                            addLog("Notify characteristic found and enabled.")
                        }
                    }
                }
            } else {
                addLog("onServicesDiscovered received: $status")
            }
        }
        
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            val data = characteristic.value
            val hexString = data.joinToString("") { "%02x".format(it) }
            addLog("RX: $hexString")
            
            CoroutineScope(Dispatchers.Default).launch {
                _receivedData.emit(data)
            }
        }
    }

    private fun enableNotifications(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        gatt.setCharacteristicNotification(characteristic, true)
        val descriptor = characteristic.getDescriptor(CLIENT_CHARACTERISTIC_CONFIG)
        if (descriptor != null) {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
        }
    }

    // Send data with 20-byte splitting
    fun sendData(data: ByteArray) {
        if (bluetoothGatt == null || writeCharacteristic == null) {
            addLog("Not connected or characteristic not found.")
            return
        }

        scope.launch {
            val chunkSize = 20
            var offset = 0
            while (offset < data.size) {
                val end = minOf(offset + chunkSize, data.size)
                val chunk = data.copyOfRange(offset, end)
                
                writeCharacteristic?.value = chunk
                writeCharacteristic?.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                val success = bluetoothGatt?.writeCharacteristic(writeCharacteristic) ?: false
                
                if (success) {
                    // addLog("Sent chunk: ${chunk.size} bytes")
                } else {
                    addLog("Failed to send chunk at offset $offset")
                    break // Stop on error
                }
                
                offset += chunkSize
                delay(20) // Small delay between packets to prevent congestion
            }
            addLog("Data sent: ${data.size} bytes")
        }
    }
}
