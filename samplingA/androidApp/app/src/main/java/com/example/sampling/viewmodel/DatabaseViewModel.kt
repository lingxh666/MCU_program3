package com.example.sampling.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.sampling.R
import com.example.sampling.data.DatabaseManager
import com.example.sampling.data.DbRecord
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class DatabaseViewModel(application: Application) : AndroidViewModel(application) {

    private val context = application.applicationContext
    private val dbManager = DatabaseManager()

    private val _records = MutableStateFlow<List<DbRecord>>(emptyList())
    val records = _records.asStateFlow()

    private val _isLoading = MutableStateFlow(false)
    val isLoading = _isLoading.asStateFlow()

    private val _connectionStatus = MutableStateFlow(context.getString(R.string.db_status_unknown))
    val connectionStatus = _connectionStatus.asStateFlow()

    private val _deviceId = MutableStateFlow("")
    val deviceId = _deviceId.asStateFlow()

    fun updateDeviceId(id: String) {
        _deviceId.value = id
    }

    fun testConnection() {
        viewModelScope.launch {
            _isLoading.value = true
            _connectionStatus.value = context.getString(R.string.db_status_connecting)
            val (success, errorMsg) = dbManager.testConnection()
            _connectionStatus.value = if (success) {
                context.getString(R.string.db_status_connected)
            } else {
                "${context.getString(R.string.db_status_failed)}: $errorMsg"
            }
            _isLoading.value = false
        }
    }

    fun loadRecords(type: String) {
        val currentDeviceId = _deviceId.value
        if (currentDeviceId.isBlank()) {
            _records.value = emptyList()
            return
        }
        viewModelScope.launch {
            _isLoading.value = true
            val result = dbManager.queryRecords(type, currentDeviceId)
            _records.value = result
            _isLoading.value = false
        }
    }
}
