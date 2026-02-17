package com.blue.ota

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.widget.*
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.net.toFile
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import pub.devrel.easypermissions.EasyPermissions
import java.io.File
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.ceil
import kotlin.math.min
import kotlin.math.roundToInt

class MainActivity : AppCompatActivity(), EasyPermissions.PermissionCallbacks {

    private val uiScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val devicesAdapter = DeviceAdapter { device ->
        selectedDevice = device
        updateSelectedDevice()
    }

    private var selectedDevice: BleDevice? = null
    private var firmwareData: ByteArray? = null
    private var firmwareName: String = ""
    private var packets: List<ByteArray> = emptyList()
    private var expectedPacketId = 1
    private var totalPackets = 0
    private var ackTimeoutJob: Job? = null
    private var isSending = false

    private lateinit var textStatus: TextView
    private lateinit var textConnected: TextView
    private lateinit var textFileName: TextView
    private lateinit var textFileInfo: TextView
    private lateinit var textHandshake: TextView
    private lateinit var textProgress: TextView
    private lateinit var textLog: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var inputDeviceId: EditText
    private lateinit var inputPrefix: EditText
    private lateinit var recycler: RecyclerView

    private val blePermissions: Array<String> by lazy {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.BLUETOOTH_ADVERTISE
            )
        } else {
            arrayOf(
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.ACCESS_COARSE_LOCATION,
                Manifest.permission.BLUETOOTH,
                Manifest.permission.BLUETOOTH_ADMIN
            )
        }
    }

    private val firmwarePicker = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri ?: return@registerForActivityResult
        contentResolver.takePersistableUriPermission(
            uri,
            Intent.FLAG_GRANT_READ_URI_PERMISSION
        )
        readFirmware(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        bindViews()
        setupRecycler()
        setupActions()
        refreshHandshakePreview()
    }

    private fun bindViews() {
        textStatus = findViewById(R.id.textStatus)
        textConnected = findViewById(R.id.textConnectedDevice)
        textFileName = findViewById(R.id.textFileName)
        textFileInfo = findViewById(R.id.textFileInfo)
        textHandshake = findViewById(R.id.textHandshake)
        textProgress = findViewById(R.id.textProgress)
        textLog = findViewById(R.id.textLog)
        progressBar = findViewById(R.id.progressBar)
        inputDeviceId = findViewById(R.id.inputDeviceId)
        inputPrefix = findViewById(R.id.inputPrefix)
        recycler = findViewById(R.id.recyclerDevices)
    }

    private fun setupRecycler() {
        recycler.layoutManager = LinearLayoutManager(this)
        recycler.adapter = devicesAdapter
    }

    private fun setupActions() {
        findViewById<Button>(R.id.btnScan).setOnClickListener { ensurePermissionThen { startScan() } }
        findViewById<Button>(R.id.btnConnect).setOnClickListener { connectSelected() }
        findViewById<Button>(R.id.btnDisconnect).setOnClickListener {
            BleManager.disconnect()
            textStatus.text = "已断开"
            textConnected.text = "设备: -"
        }
        findViewById<Button>(R.id.btnPickFile).setOnClickListener { pickFirmware() }
        findViewById<Button>(R.id.btnStartOta).setOnClickListener { startOta() }

        inputDeviceId.addTextChangedListener(SimpleTextWatcher { refreshHandshakePreview() })
        inputPrefix.addTextChangedListener(SimpleTextWatcher { refreshHandshakePreview() })
    }

    private fun ensurePermissionThen(onGranted: () -> Unit) {
        if (EasyPermissions.hasPermissions(this, *blePermissions)) {
            onGranted()
        } else {
            EasyPermissions.requestPermissions(
                this,
                "需要蓝牙和位置信息权限以扫描并连接设备",
                0xA0,
                *blePermissions
            )
        }
    }

    private fun startScan() {
        logLine("开始扫描...")
        BleManager.startScan(this) { device ->
            devicesAdapter.upsert(device)
        }
    }

    private fun connectSelected() {
        val target = selectedDevice
        if (target == null) {
            toast("请先在列表中点选设备")
            return
        }
        logLine("连接 ${target.name}")
        BleManager.stopScan(this)
        BleManager.connect(
            this,
            target.mac,
            onState = { ok, msg ->
                textStatus.text = msg
                if (ok) {
                    textConnected.text = "设备: ${BleManager.connectedDeviceName ?: target.name}"
                } else {
                    textConnected.text = "设备: -"
                }
            },
            onNotify = { bytes -> handleNotify(bytes) }
        )
    }

    private fun pickFirmware() {
        firmwarePicker.launch(arrayOf("*/*"))
    }

    private fun readFirmware(uri: Uri) {
        try {
            val bytes = contentResolver.openInputStream(uri)?.use(InputStream::readBytes)
            if (bytes == null || bytes.isEmpty()) {
                toast("读取文件失败")
                return
            }
            firmwareData = bytes
            firmwareName = guessName(uri)
            val crc = checksum16(bytes)
            val sizeText = String.format("大小：%.2f KB | CRC16: %04X", bytes.size / 1024f, crc)
            textFileName.text = firmwareName
            textFileInfo.text = sizeText
            refreshHandshakePreview()
            logLine("载入固件 ${firmwareName}, ${bytes.size} bytes")
        } catch (e: Throwable) {
            toast("读取文件失败: ${e.message}")
        }
    }

    private fun guessName(uri: Uri): String {
        return try {
            uri.toFile().name
        } catch (_: Throwable) {
            uri.lastPathSegment?.substringAfterLast('/') ?: "firmware.bin"
        }
    }

    private fun refreshHandshakePreview() {
        val handshake = buildHandshake() ?: "-"
        textHandshake.text = "握手码预览：$handshake"
    }

    private fun buildHandshake(): String? {
        val data = firmwareData ?: return null
        val prefix = inputPrefix.text?.toString()?.ifBlank { "ML307OTA" } ?: "ML307OTA"
        val id = inputDeviceId.text?.toString()?.ifBlank { "DEVICE" } ?: "DEVICE"
        val crcHex = checksum16(data).toString(16).uppercase().padStart(4, '0')
        return "${prefix}_${id}_${crcHex}_${data.size}"
    }

    private fun startOta() {
        if (isSending) {
            toast("正在发送 OTA 数据，请稍候")
            return
        }
        if (!EasyPermissions.hasPermissions(this, *blePermissions)) {
            toast("请先授予蓝牙权限")
            return
        }
        if (BleManager.connectedDeviceName == null) {
            toast("请先连接目标设备")
            return
        }
        val data = firmwareData
        if (data == null) {
            toast("请先选择固件文件")
            return
        }
        val handshake = buildHandshake()
        if (handshake.isNullOrBlank()) {
            toast("握手码不能为空")
            return
        }
        packets = buildPackets(data)
        totalPackets = packets.size
        expectedPacketId = 1
        isSending = true
        updateProgress()
        logLine("发送握手码: $handshake")
        uiScope.launch {
            BleManager.writeChunked(handshake.toByteArray(), gapMs = 120)
            kotlinx.coroutines.delay(300)
            sendPacket(0)
        }
    }

    private fun sendPacket(index: Int) {
        if (index >= packets.size) {
            return
        }
        expectedPacketId = index + 1
        logLine("发送数据包 ${expectedPacketId}/$totalPackets")
        ackTimeoutJob?.cancel()
        uiScope.launch {
            BleManager.writeChunked(packets[index], gapMs = 120)
            startAckTimeout(index)
        }
    }

    private fun startAckTimeout(index: Int) {
        ackTimeoutJob = uiScope.launch {
            kotlinx.coroutines.delay(5000)
            logLine("ACK 超时，重发包 ${index + 1}")
            sendPacket(index)
        }
    }

    private fun handleNotify(bytes: ByteArray) {
        val text = try {
            String(bytes).trim()
        } catch (_: Throwable) {
            return
        }
        if (text.isBlank()) return
        logLine("RX: $text")
        if (text.startsWith("ACK_")) {
            handleAck(text)
        }
    }

    private fun handleAck(text: String) {
        val parts = text.replace("\r", "").replace("\n", "").split("_")
        if (parts.size < 3) return
        val id = parts[1].toIntOrNull() ?: return
        val ok = parts[2].contains("OK", ignoreCase = true)

        ackTimeoutJob?.cancel()
        if (!ok) {
            logLine("包 $id 返回 ERROR，重发")
            sendPacket(id - 1)
            return
        }

        if (id == 65535) {
            logLine("升级完成确认")
            isSending = false
            updateProgress(100)
            toast("OTA 完成")
            return
        }

        if (id == totalPackets) {
            logLine("数据发送完毕，等待最终确认...")
            updateProgress(100)
            isSending = false
            return
        }

        if (id == expectedPacketId) {
            updateProgress()
            sendPacket(id)
        }
    }

    private fun buildPackets(data: ByteArray): List<ByteArray> {
        val total = ceil(data.size / 256.0).toInt()
        val list = mutableListOf<ByteArray>()
        for (i in 0 until total) {
            val offset = i * 256
            val len = min(256, data.size - offset)
            val slice = data.copyOfRange(offset, offset + len)
            val checksum = slice.fold(0) { acc, b -> (acc + (b.toInt() and 0xFF)) and 0xFF }
            val buffer = ByteBuffer.allocate(265).order(ByteOrder.LITTLE_ENDIAN)
            buffer.putShort(0xAA55.toShort())
            buffer.putShort((i + 1).toShort())
            buffer.putShort(total.toShort())
            buffer.putShort(len.toShort())
            buffer.put(checksum.toByte())
            buffer.put(slice)
            if (len < 256) {
                buffer.put(ByteArray(256 - len) { 0xFF.toByte() })
            }
            list.add(buffer.array())
        }
        return list
    }

    private fun checksum16(data: ByteArray): Int {
        var sum = 0
        data.forEach { sum = (sum + (it.toInt() and 0xFF)) and 0xFFFF }
        return sum
    }

    private fun updateSelectedDevice() {
        textStatus.text = "待连接"
        textConnected.text = "设备: ${selectedDevice?.name ?: "-"}"
    }

    private fun updateProgress(forcePercent: Int? = null) {
        val percent = forcePercent ?: run {
            if (totalPackets == 0) 0 else ((expectedPacketId - 1).toFloat() / totalPackets * 100).roundToInt()
        }
        progressBar.progress = percent
        textProgress.text = "$percent%"
    }

    private fun logLine(msg: String) {
        val existing = textLog.text?.toString() ?: ""
        val merged = (existing + "\n" + msg).takeLast(1800)
        textLog.text = merged.trim()
    }

    private fun toast(msg: String) {
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        EasyPermissions.onRequestPermissionsResult(requestCode, permissions, grantResults, this)
    }

    override fun onPermissionsGranted(requestCode: Int, perms: MutableList<String>) {
        if (requestCode == 0xA0) {
            startScan()
        }
    }

    override fun onPermissionsDenied(requestCode: Int, perms: MutableList<String>) {
        toast("缺少权限，无法使用蓝牙")
    }

    override fun onDestroy() {
        super.onDestroy()
        ackTimeoutJob?.cancel()
        BleManager.stopScan(this)
        BleManager.disconnect()
    }
}
