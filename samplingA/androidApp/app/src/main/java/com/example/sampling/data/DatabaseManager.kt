package com.example.sampling.data

import android.util.Log
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.util.concurrent.TimeUnit

data class DbRecord(
    val col1: String,
    val col2: String,
    val col3: String,
    val col4: String,
    val col5: String = "",
    val type: String
)

class DatabaseManager {

    companion object {
        private const val TAG = "DatabaseManager"
        private const val BASE_URL = "http://124.222.59.221:1880/api/db"
        
        // Sampling/Sending modes
        private val SAMPLING_MODES = arrayOf(
            "\u65f6\u95f4\u7b49\u6bd4",  // 时间等比
            "\u5b9a\u65f6\u91c7\u6837",  // 定时采样
            "\u901a\u8baf\u89e6\u53d1",  // 通讯触发
            "\u6d41\u91cf\u89e6\u53d1",  // 流量触发
            "\u5f00\u5173\u89e6\u53d1"   // 开关触发
        )
        
        // Retention modes
        private val RETENTION_MODES = arrayOf(
            "\u8d85\u6807\u7559\u6837",  // 超标留样
            "\u76f4\u63a5\u7559\u6837",  // 直接留样
            "\u6bd4\u5bf9\u7559\u6837",  // 比对留样
            "\u901a\u4fe1\u89e6\u53d1",  // 通信触发
            "\u540c\u6b65\u7559\u6837",  // 同步留样
            "\u53ea\u9001\u4e0d\u7559",  // 只送不留
            "\u5f00\u5173\u89e6\u53d1"   // 开关触发
        )
    }

    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(15, TimeUnit.SECONDS)
        .build()

    suspend fun testConnection(): Pair<Boolean, String> = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder()
                .url("$BASE_URL/test")
                .get()
                .build()

            val response = client.newCall(request).execute()
            if (response.isSuccessful) {
                val body = response.body?.string() ?: ""
                val json = JsonParser.parseString(body).asJsonObject
                val success = json.get("success")?.asBoolean ?: false
                val message = json.get("message")?.asString ?: ""
                if (success) {
                    Pair(true, message.ifEmpty { "OK" })
                } else {
                    Pair(false, message.ifEmpty { "Failed" })
                }
            } else {
                Pair(false, "HTTP ${response.code}")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Connection test error: ${e.message}")
            Pair(false, e.message ?: "Error")
        }
    }

    suspend fun queryRecords(type: String, deviceId: String): List<DbRecord> = withContext(Dispatchers.IO) {
        val records = mutableListOf<DbRecord>()
        val endpoint = when (type) {
            "Sampling" -> "sampling"
            "Sending" -> "sending"
            "Retention" -> "retention"
            "Door" -> "door"
            else -> "sampling"
        }

        try {
            val request = Request.Builder()
                .url("$BASE_URL/$endpoint?device_id=$deviceId")
                .get()
                .build()

            val response = client.newCall(request).execute()
            if (response.isSuccessful) {
                val body = response.body?.string() ?: ""
                Log.d(TAG, "Raw response: ${body.take(300)}")
                
                val json = JsonParser.parseString(body).asJsonObject
                val success = json.get("success")?.asBoolean ?: false
                val dataArray = json.getAsJsonArray("data")
                
                if (success && dataArray != null && dataArray.size() > 0) {
                    Log.d(TAG, "Received ${dataArray.size()} records for $type")
                    for (i in 0 until dataArray.size()) {
                        val item = dataArray.get(i).asJsonObject
                        val record = parseJsonRecord(item, type)
                        records.add(record)
                    }
                    Log.d(TAG, "First parsed: ${records.firstOrNull()}")
                } else {
                    Log.w(TAG, "API response: success=$success, dataSize=${dataArray?.size()}")
                }
            } else {
                records.add(DbRecord("ERR", "-", "-", "HTTP ${response.code}", "", type))
            }
        } catch (e: Exception) {
            Log.e(TAG, "Query error for $type: ${e.message}")
            records.add(DbRecord("ERR", "-", "-", e.message ?: "Error", "", type))
        }
        records
    }

    private fun parseJsonRecord(item: JsonObject, type: String): DbRecord {
        fun getInt(key: String): Int? = try { item.get(key)?.asInt } catch (e: Exception) { null }
        fun getString(key: String): String? = try { item.get(key)?.asString } catch (e: Exception) { null }
        
        return when (type) {
            "Sampling" -> {
                val mode = parseSamplingModeInt(getInt("mode"))
                val bucket = parseBucketInt(getInt("bucket"))
                val amount = getInt("amount")?.toString() ?: "-"
                val time = formatUnixTimeStr(getString("start_time"))
                val result = parseResultInt(getInt("result"))
                DbRecord(mode, bucket, amount, time, result, type)
            }
            "Sending" -> {
                val mode = parseSamplingModeInt(getInt("mode"))
                val bucket = parseBucketInt(getInt("bucket"))
                val amount = getInt("amount")?.toString() ?: "-"
                val time = formatUnixTimeStr(getString("start_time"))
                val result = parseResultInt(getInt("result"))
                DbRecord(mode, bucket, amount, time, result, type)
            }
            "Retention" -> {
                val mode = parseRetentionModeInt(getInt("mode"))
                val bottle = getInt("bottle")?.toString() ?: "-"
                val amount = getInt("amount")?.toString() ?: "-"
                val time = formatUnixTimeStr(getString("start_time"))
                val result = parseResultInt(getInt("result"))
                DbRecord(mode, bottle, amount, time, result, type)
            }
            "Door" -> {
                val eventType = parseDoorEventInt(getInt("event_type"))
                val time = formatUnixTimeStr(getString("timestamp"))
                DbRecord(eventType, "-", "-", time, "", type)
            }
            else -> DbRecord("-", "-", "-", "-", "", type)
        }
    }
    
    private fun parseBucketInt(bucket: Int?): String = when (bucket) {
        0 -> "A桶"  // A桶
        1 -> "B桶"  // B桶
        else -> "-"
    }
    
    private fun parseSamplingModeInt(mode: Int?): String {
        if (mode == null || mode < 0 || mode >= SAMPLING_MODES.size) return "-"
        return SAMPLING_MODES[mode]
    }
    
    private fun parseRetentionModeInt(mode: Int?): String {
        if (mode == null || mode < 0 || mode >= RETENTION_MODES.size) return "-"
        return RETENTION_MODES[mode]
    }
    
    private fun parseResultInt(result: Int?): String = when (result) {
        1 -> "\u6210\u529f"  // 成功
        0 -> "\u5931\u8d25"  // 失败
        else -> "-"
    }
    
    private fun parseDoorEventInt(event: Int?): String = when (event) {
        0 -> "\u5f00\u95e8"  // 开门
        1 -> "\u5173\u95e8"  // 关门
        else -> "-"
    }
    
    private fun formatUnixTimeStr(timeStr: String?): String {
        if (timeStr == null) return "-"
        try {
            val timestamp = timeStr.toLongOrNull() ?: return "-"
            val sdf = java.text.SimpleDateFormat("MM-dd HH:mm", java.util.Locale.getDefault())
            sdf.timeZone = java.util.TimeZone.getDefault()
            return sdf.format(java.util.Date(timestamp * 1000))
        } catch (e: Exception) {
            return "-"
        }
    }
}
