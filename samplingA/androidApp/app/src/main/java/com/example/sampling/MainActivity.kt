package com.example.sampling

import android.content.Intent
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import com.example.sampling.data.NfcManager
import com.example.sampling.ui.MainScreen
import com.example.sampling.ui.theme.SamplingTheme
import com.example.sampling.viewmodel.NfcViewModel
import com.example.sampling.viewmodel.MqttViewModel
import com.example.sampling.viewmodel.DatabaseViewModel

class MainActivity : ComponentActivity() {
    
    private lateinit var nfcManager: NfcManager
    private val nfcViewModel: NfcViewModel by viewModels()
    private val mqttViewModel: MqttViewModel by viewModels()
    private val databaseViewModel: DatabaseViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        
        nfcManager = NfcManager(this)
        
        setContent {
            SamplingTheme {
                MainScreen(
                    nfcViewModel = nfcViewModel,
                    mqttViewModel = mqttViewModel,
                    databaseViewModel = databaseViewModel
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        nfcManager.enableForegroundDispatch(this)
        nfcViewModel.setNfcEnabledStatus(nfcManager.isNfcEnabled())
    }

    override fun onPause() {
        super.onPause()
        nfcManager.disableForegroundDispatch(this)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (NfcAdapter.ACTION_TAG_DISCOVERED == intent.action ||
            NfcAdapter.ACTION_TECH_DISCOVERED == intent.action ||
            NfcAdapter.ACTION_NDEF_DISCOVERED == intent.action) {
            
            val tag = intent.getParcelableExtra<Tag>(NfcAdapter.EXTRA_TAG)
            if (tag != null) {
                nfcViewModel.onTagDiscovered(tag, nfcManager)
            }
        }
    }
}