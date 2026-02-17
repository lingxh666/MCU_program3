package com.example.sampling.ui.screens

import android.Manifest
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.sampling.R
import com.example.sampling.data.ScannedDevice
import com.example.sampling.ui.theme.BackgroundGrey
import com.example.sampling.ui.theme.PrimaryDarkBlue
import com.example.sampling.ui.theme.TextBlack
import com.example.sampling.viewmodel.BluetoothViewModel

@Composable
fun BluetoothScreen(
    viewModel: BluetoothViewModel = viewModel()
) {
    val scannedDevices by viewModel.scannedDevices.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()
    val logs by viewModel.logs.collectAsState()
    val otaProgress by viewModel.otaProgress.collectAsState()
    val otaStatus by viewModel.otaStatus.collectAsState()
    
    var isScanning by remember { mutableStateOf(false) }
    val isConnected = connectionState == android.bluetooth.BluetoothProfile.STATE_CONNECTED

    val permissionsToRequest = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        arrayOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.ACCESS_FINE_LOCATION
        )
    } else {
        arrayOf(
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.ACCESS_COARSE_LOCATION
        )
    }

    val multiplePermissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions()
    ) { perms ->
        val allGranted = perms.values.all { it }
        if (allGranted) {
            viewModel.startScan()
            isScanning = true
        }
    }

    BluetoothScreenContent(
        scannedDevices = scannedDevices,
        isConnected = isConnected,
        connectionState = connectionState,
        logs = logs,
        isScanning = isScanning,
        otaProgress = otaProgress,
        otaStatus = otaStatus,
        onScanClick = {
            if (!isScanning) {
                multiplePermissionLauncher.launch(permissionsToRequest)
            } else {
                viewModel.stopScan()
                isScanning = false
            }
        },
        onConnectClick = { device ->
            viewModel.stopScan()
            isScanning = false
            viewModel.connect(device.address)
        },
        onDisconnectClick = { viewModel.disconnect() },
        onStartOtaClick = { 
            viewModel.startOta("http://124.222.59.221:1880/OTA/sampling.bin")
        }
    )
}

@Composable
fun BluetoothScreenContent(
    scannedDevices: List<ScannedDevice>,
    isConnected: Boolean,
    connectionState: Int,
    logs: List<String>,
    isScanning: Boolean,
    otaProgress: Float,
    otaStatus: String,
    onScanClick: () -> Unit,
    onConnectClick: (ScannedDevice) -> Unit,
    onDisconnectClick: () -> Unit,
    onStartOtaClick: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(bottom = 0.dp)
    ) {
        // Colored Header
        Surface(
            color = PrimaryDarkBlue,
            modifier = Modifier.fillMaxWidth()
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = stringResource(R.string.bluetooth_title), 
                    style = MaterialTheme.typography.titleLarge,
                    color = Color.White
                )
                
                Button(
                    onClick = onScanClick,
                    enabled = !isConnected,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color.White,
                        contentColor = PrimaryDarkBlue,
                        disabledContainerColor = Color.White.copy(alpha = 0.5f),
                        disabledContentColor = PrimaryDarkBlue.copy(alpha = 0.5f)
                    )
                ) {
                    Icon(Icons.Default.Refresh, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(if (isScanning) stringResource(R.string.btn_stop_scan) else stringResource(R.string.btn_scan))
                }
            }
        }

        // Content Body
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp)
        ) {
            if (isConnected || connectionState == android.bluetooth.BluetoothProfile.STATE_CONNECTING) {
                Card(
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
                    modifier = Modifier.fillMaxWidth(),
                    elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp).fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Icon(Icons.Default.BluetoothConnected, contentDescription = null)
                                Spacer(modifier = Modifier.width(8.dp))
                                val stateText = when(connectionState) {
                                    android.bluetooth.BluetoothProfile.STATE_CONNECTING -> stringResource(R.string.status_connecting)
                                    android.bluetooth.BluetoothProfile.STATE_CONNECTED -> stringResource(R.string.status_connected)
                                    else -> stringResource(R.string.status_disconnected)
                                }
                                Text(stateText, style = MaterialTheme.typography.labelMedium)
                            }
                        }
                        if (isConnected) {
                            Button(
                                onClick = onDisconnectClick,
                                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
                            ) {
                                Text(stringResource(R.string.btn_disconnect))
                            }
                        }
                    }
                }
            } else {
                Text(stringResource(R.string.label_available_devices), style = MaterialTheme.typography.titleMedium, color = PrimaryDarkBlue)
                LazyColumn(
                    modifier = Modifier.weight(1f).fillMaxWidth(),
                    contentPadding = PaddingValues(vertical = 8.dp)
                ) {
                    if (scannedDevices.isEmpty()) {
                        item {
                            Box(modifier = Modifier.fillMaxWidth().padding(32.dp), contentAlignment = Alignment.Center) {
                                Text(if (isScanning) stringResource(R.string.msg_scanning) else stringResource(R.string.msg_no_devices), color = MaterialTheme.colorScheme.secondary)
                            }
                        }
                    }
                    items(scannedDevices) { device ->
                        DeviceItem(device = device) { onConnectClick(device) }
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            Spacer(modifier = Modifier.height(16.dp))

            Text(stringResource(R.string.title_ota), style = MaterialTheme.typography.titleMedium, color = PrimaryDarkBlue)
            Spacer(modifier = Modifier.height(8.dp))
            
            OutlinedTextField(
                value = "http://124.222.59.221:1880/OTA/sampling.bin",
                onValueChange = {},
                label = { Text(stringResource(R.string.label_firmware_url)) },
                readOnly = true,
                modifier = Modifier.fillMaxWidth(),
                textStyle = MaterialTheme.typography.bodySmall,
                colors = OutlinedTextFieldDefaults.colors(
                    focusedBorderColor = PrimaryDarkBlue,
                    unfocusedBorderColor = MaterialTheme.colorScheme.outline
                )
            )
            
            Spacer(modifier = Modifier.height(16.dp))
            
            if (otaProgress > 0 || otaStatus.isNotEmpty()) {
                LinearProgressIndicator(
                    progress = { otaProgress },
                    modifier = Modifier.fillMaxWidth(),
                    color = PrimaryDarkBlue
                )
                Text("${(otaProgress * 100).toInt()}% - $otaStatus", modifier = Modifier.align(Alignment.End))
            }

            Button(
                onClick = onStartOtaClick,
                enabled = isConnected && otaProgress == 0.0f,
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.buttonColors(containerColor = PrimaryDarkBlue)
            ) {
                Text(stringResource(R.string.btn_start_ota))
            }

            Spacer(modifier = Modifier.height(16.dp))
            
            Text(stringResource(R.string.label_logs), style = MaterialTheme.typography.titleSmall)
            Card(
                modifier = Modifier.fillMaxWidth().height(120.dp),
                colors = CardDefaults.cardColors(containerColor = BackgroundGrey),
                elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
            ) {
                LazyColumn(
                    modifier = Modifier.padding(8.dp),
                    reverseLayout = true
                ) {
                    items(logs.reversed()) { log ->
                        Text(text = log, style = MaterialTheme.typography.bodySmall, color = TextBlack)
                    }
                }
            }
        }
    }
}

@Composable
fun DeviceItem(device: ScannedDevice, onClick: () -> Unit) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
            .clickable(onClick = onClick),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
        colors = CardDefaults.cardColors(containerColor = Color.White)
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(Icons.Default.Bluetooth, contentDescription = null, tint = PrimaryDarkBlue)
            Spacer(modifier = Modifier.width(16.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(text = device.name.ifEmpty { stringResource(R.string.device_unknown) }, style = MaterialTheme.typography.bodyLarge)
                Text(text = device.address, style = MaterialTheme.typography.bodyMedium, color = Color.Gray)
            }
            Text(text = "${device.rssi} dBm", style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Preview(showBackground = true)
@Composable
fun BluetoothScreenPreview() {
    MaterialTheme {
        BluetoothScreenContent(
            scannedDevices = emptyList(),
            isConnected = false,
            connectionState = android.bluetooth.BluetoothProfile.STATE_DISCONNECTED,
            logs = listOf("Ready to scan...", "Bluetooth initialized"),
            isScanning = false,
            otaProgress = 0f,
            otaStatus = "",
            onScanClick = {},
            onConnectClick = {},
            onDisconnectClick = {},
            onStartOtaClick = {}
        )
    }
}
