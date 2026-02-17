package com.example.sampling.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException

object OtaHelper {

    private val client = OkHttpClient()

    // Download firmware from URL
    suspend fun downloadFirmware(url: String): ByteArray? = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder().url(url).build()
            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) return@use null
                response.body?.bytes()
            }
        } catch (e: Exception) {
            e.printStackTrace()
            null
        }
    }

    // CRC16-MODBUS Implementation (matches Python script)
    fun crc16(data: ByteArray): Int {
        var crc = 0xFFFF
        for (b in data) {
            // Convert signed byte to unsigned int (0-255)
            val byteVal = b.toInt() and 0xFF
            crc = crc xor byteVal
            for (i in 0 until 8) {
                if ((crc and 0x0001) != 0) {
                    crc = (crc shr 1) xor 0xA001
                } else {
                    crc = crc shr 1
                }
            }
        }
        return crc
    }

    // Create OTA Data Packet
    // Header (2) + PacketID (2) + TotalPackets (2) + DataLen (2) + Checksum (1) + Data (N)
    fun createPacket(packetId: Int, totalPackets: Int, data: ByteArray): ByteArray {
        val packetSize = data.size
        // 2+2+2+2+1 = 9 bytes header
        val buffer = java.nio.ByteBuffer.allocate(9 + packetSize)
        buffer.order(java.nio.ByteOrder.LITTLE_ENDIAN)

        buffer.putShort(0xAA55.toShort())      // Header
        buffer.putShort(packetId.toShort())    // Packet ID
        buffer.putShort(totalPackets.toShort())// Total Packets
        buffer.putShort(packetSize.toShort())  // Data Length

        // Calculate Checksum (Simple Sum & 0xFF)
        var checksum = 0
        for (b in data) {
            checksum += (b.toInt() and 0xFF)
        }
        buffer.put((checksum and 0xFF).toByte())

        buffer.put(data)

        return buffer.array()
    }
}
