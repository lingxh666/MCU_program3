package com.example.sampling.viewmodel

import android.app.Application
import android.nfc.Tag
import androidx.lifecycle.AndroidViewModel
import com.example.sampling.R
import com.example.sampling.data.NfcManager
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class NfcViewModel(application: Application) : AndroidViewModel(application) {

    private val context = application.applicationContext
    private val _nfcStatus = MutableStateFlow(context.getString(R.string.nfc_status_ready))
    val nfcStatus = _nfcStatus.asStateFlow()

    private val _tagInfo = MutableStateFlow<Map<String, String>>(emptyMap())
    val tagInfo = _tagInfo.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning = _isScanning.asStateFlow()

    fun setScanning(scanning: Boolean) {
        _isScanning.value = scanning
        if (scanning) {
            _nfcStatus.value = context.getString(R.string.nfc_status_scanning)
            _tagInfo.value = emptyMap()
        } else {
            _nfcStatus.value = context.getString(R.string.nfc_status_paused)
        }
    }

    fun onTagDiscovered(tag: Tag, manager: NfcManager) {
        if (!_isScanning.value) return

        val info = manager.parseTag(tag)
        _tagInfo.value = info
        _nfcStatus.value = context.getString(R.string.nfc_status_detected)
        
        // Optionally stop scanning after read
        // _isScanning.value = false
    }
    
    fun setNfcEnabledStatus(enabled: Boolean) {
        if (!enabled) {
            _nfcStatus.value = context.getString(R.string.nfc_status_disabled_device)
        }
    }
}
