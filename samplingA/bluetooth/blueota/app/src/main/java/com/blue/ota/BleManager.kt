package com.blue.ota

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.*
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.core.app.ActivityCompat
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

data class BleDevice(val name: String, val mac: String, val rssi: Int)

/**
 * 简单的 BLE 管理器，负责扫描、连接、通知和分片写入。
 */
object BleManager {
    private const val TAG = "BleManager"
    private const val WRITE_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"
    private const val NOTIFY_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"

    private var bluetoothAdapter: BluetoothAdapter? = null
    private var bluetoothGatt: BluetoothGatt? = null
    private var writeCharacteristic: BluetoothGattCharacteristic? = null
    private var notifyCallback: ((ByteArray) -> Unit)? = null
    private var stateCallback: ((Boolean, String) -> Unit)? = null
    private val mainHandler = Handler(Looper.getMainLooper())

    private val deviceMap = ConcurrentHashMap<String, BluetoothDevice>()
    private var isScanning = false
    var connectedDeviceName: String? = null
        private set

    fun isReady(context: Context): Boolean {
        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
        return bluetoothAdapter?.isEnabled == true &&
                hasPermission(context, Manifest.permission.BLUETOOTH_CONNECT)
    }

    private fun hasPermission(context: Context, permission: String): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ActivityCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED
        } else {
            true
        }
    }

    private var onDeviceFound: ((BleDevice) -> Unit)? = null

    @SuppressLint("MissingPermission")
    fun startScan(context: Context, onFound: (BleDevice) -> Unit) {
        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
        if (bluetoothAdapter == null || isScanning) return
        if (bluetoothAdapter?.isEnabled != true) return

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (!hasPermission(context, Manifest.permission.BLUETOOTH_SCAN)) return
        }

        onDeviceFound = onFound
        deviceMap.clear()
        bluetoothAdapter?.startLeScan(leScanCallback)
        isScanning = true
    }

    @SuppressLint("MissingPermission")
    fun stopScan(context: Context) {
        if (!isScanning) return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (!hasPermission(context, Manifest.permission.BLUETOOTH_SCAN)) return
        }
        bluetoothAdapter?.stopLeScan(leScanCallback)
        isScanning = false
    }

    @SuppressLint("MissingPermission")
    fun connect(
        context: Context,
        mac: String,
        onState: (Boolean, String) -> Unit,
        onNotify: (ByteArray) -> Unit
    ) {
        stateCallback = onState
        notifyCallback = onNotify

        val adapter = BluetoothAdapter.getDefaultAdapter() ?: run {
            onState(false, "未找到蓝牙适配器")
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            !hasPermission(context, Manifest.permission.BLUETOOTH_CONNECT)
        ) {
            onState(false, "缺少蓝牙权限")
            return
        }

        val device = deviceMap[mac] ?: adapter.getRemoteDevice(mac)
        bluetoothGatt?.close()
        bluetoothGatt = device.connectGatt(context, false, gattCallback)
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        try {
            bluetoothGatt?.disconnect()
            bluetoothGatt?.close()
        } catch (_: Throwable) {
        }
        connectedDeviceName = null
        writeCharacteristic = null
        bluetoothGatt = null
    }

    @SuppressLint("MissingPermission")
    fun write(bytes: ByteArray) {
        val characteristic = writeCharacteristic ?: return
        bluetoothGatt?.let { gatt ->
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            characteristic.value = bytes
            try {
                gatt.writeCharacteristic(characteristic)
            } catch (e: Throwable) {
                Log.e(TAG, "write failed", e)
            }
        }
    }

    suspend fun writeChunked(bytes: ByteArray, chunk: Int = 20, gapMs: Long = 120) {
        var offset = 0
        while (offset < bytes.size) {
            val end = minOf(offset + chunk, bytes.size)
            write(bytes.copyOfRange(offset, end))
            offset = end
            if (offset < bytes.size) {
                kotlinx.coroutines.delay(gapMs)
            }
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS || newState != BluetoothProfile.STATE_CONNECTED) {
                connectedDeviceName = null
                mainHandler.post { stateCallback?.invoke(false, "连接断开($status)") }
                gatt.close()
                return
            }
            connectedDeviceName = gatt.device.name ?: gatt.device.address
            mainHandler.post { stateCallback?.invoke(true, "已连接") }
            gatt.discoverServices()
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                mainHandler.post { stateCallback?.invoke(false, "服务发现失败") }
                return
            }
            val writeUuid = UUID.fromString(WRITE_UUID)
            val notifyUuid = UUID.fromString(NOTIFY_UUID)
            gatt.services.forEach { service ->
                service.characteristics.forEach { characteristic ->
                    if (characteristic.uuid == notifyUuid) {
                        enableNotify(gatt, characteristic)
                    }
                    if (characteristic.uuid == writeUuid) {
                        writeCharacteristic = characteristic
                    }
                }
            }
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    gatt.requestMtu(247)
                }
            } catch (_: Throwable) {
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            val value = characteristic.value
            mainHandler.post { notifyCallback?.invoke(value) }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            Log.d(TAG, "MTU updated $mtu status=$status")
        }
    }

    @SuppressLint("MissingPermission")
    private fun enableNotify(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        val success = gatt.setCharacteristicNotification(characteristic, true)
        if (!success) {
            Log.w(TAG, "setCharacteristicNotification failed")
            return
        }
        val descriptor = characteristic.getDescriptor(UUID.fromString("00002902-0000-1000-8000-00805f9b34fb"))
        descriptor?.let {
            it.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            try {
                gatt.writeDescriptor(it)
            } catch (e: Throwable) {
                Log.e(TAG, "writeDescriptor failed", e)
            }
        }
    }

    private val leScanCallback = BluetoothAdapter.LeScanCallback { device, rssi, _ ->
        try {
            val name = device.name ?: return@LeScanCallback
            val mac = device.address ?: return@LeScanCallback
            if (!deviceMap.containsKey(mac)) {
                deviceMap[mac] = device
            }
            onDeviceFound?.invoke(BleDevice(name = name, mac = mac, rssi = rssi))
        } catch (e: Throwable) {
            Log.e(TAG, "scan error", e)
        }
    }
}
