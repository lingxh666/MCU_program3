package com.example.sampling.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CloudUpload
import androidx.compose.material.icons.filled.Send
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
import com.example.sampling.ui.theme.BackgroundGrey
import com.example.sampling.ui.theme.CloudGrey
import com.example.sampling.ui.theme.PrimaryBlue
import com.example.sampling.ui.theme.TextBlack
import com.example.sampling.viewmodel.MqttViewModel

@Composable
fun MqttScreen(
    viewModel: MqttViewModel = viewModel()
) {
    val isConnected by viewModel.connectionState.collectAsState()
    val logs by viewModel.logs.collectAsState()
    val otaProgress by viewModel.otaProgress.collectAsState()
    val otaStatus by viewModel.otaStatus.collectAsState()

    var upgradeVersion by remember { mutableStateOf("") }
    var commandText by remember { mutableStateOf("") }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(bottom = 0.dp)
    ) {
        // Colored Header
        Surface(
            color = CloudGrey,
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
                    text = stringResource(R.string.mqtt_title), 
                    style = MaterialTheme.typography.titleLarge,
                    color = Color.White
                )
                Button(
                    onClick = { 
                        if (isConnected) {
                            viewModel.disconnect()
                        } else {
                            viewModel.connect()
                        }
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (isConnected) MaterialTheme.colorScheme.error else PrimaryBlue
                    )
                ) {
                    Text(if (isConnected) stringResource(R.string.btn_disconnect_short) else stringResource(R.string.btn_connect))
                }
            }
        }

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp)
        ) {
            Spacer(modifier = Modifier.height(8.dp))

            // OTA Section
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = BackgroundGrey),
                elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.CloudUpload, contentDescription = null, tint = CloudGrey)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(stringResource(R.string.title_ota_remote), style = MaterialTheme.typography.titleMedium, color = TextBlack)
                    }
                    
                    Spacer(modifier = Modifier.height(8.dp))
                    
                    OutlinedTextField(
                        value = upgradeVersion,
                        onValueChange = { upgradeVersion = it },
                        label = { Text(stringResource(R.string.label_upgrade_id)) },
                        placeholder = { Text(stringResource(R.string.hint_upgrade_id)) },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = CloudGrey,
                            unfocusedBorderColor = MaterialTheme.colorScheme.outline
                        )
                    )
                    
                    Spacer(modifier = Modifier.height(8.dp))
                    
                    if (otaProgress > 0) {
                        LinearProgressIndicator(
                            progress = { otaProgress }, 
                            modifier = Modifier.fillMaxWidth(),
                            color = CloudGrey
                        )
                        Text("${(otaProgress * 100).toInt()}% - $otaStatus", modifier = Modifier.align(Alignment.End))
                    } else if (otaStatus.isNotEmpty()) {
                        Text(otaStatus, style = MaterialTheme.typography.bodySmall, modifier = Modifier.align(Alignment.End))
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    Button(
                        onClick = { 
                            viewModel.startOta(
                                firmwareUrl = "http://124.222.59.221:1880/OTA/sampling.bin",
                                upgradeId = upgradeVersion
                            )
                        },
                        modifier = Modifier.fillMaxWidth(),
                        enabled = isConnected && upgradeVersion.isNotEmpty() && otaProgress == 0f,
                        colors = ButtonDefaults.buttonColors(containerColor = CloudGrey)
                    ) {
                        Text(stringResource(R.string.btn_start_ota_upgrade))
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // Command Section
            Text(stringResource(R.string.title_command_center), style = MaterialTheme.typography.titleMedium, color = CloudGrey)
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                OutlinedTextField(
                    value = commandText,
                    onValueChange = { commandText = it },
                    label = { Text(stringResource(R.string.label_send_command)) },
                    modifier = Modifier.weight(1f),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = CloudGrey,
                        unfocusedBorderColor = MaterialTheme.colorScheme.outline
                    )
                )
                Spacer(modifier = Modifier.width(8.dp))
                IconButton(
                    onClick = { 
                        if (commandText.isNotEmpty()) {
                            viewModel.sendCommand(commandText)
                            commandText = ""
                        }
                    },
                    enabled = isConnected
                ) {
                    Icon(Icons.Default.Send, contentDescription = "Send", tint = if(isConnected) PrimaryBlue else Color.Gray)
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // Logs
            Text(stringResource(R.string.title_system_logs), style = MaterialTheme.typography.titleSmall)
            Card(
                modifier = Modifier.weight(1f).fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = BackgroundGrey),
                elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
            ) {
                LazyColumn(
                    modifier = Modifier.padding(8.dp),
                    reverseLayout = true
                ) {
                    items(logs.reversed()) { log ->
                        Text(log, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(vertical = 2.dp), color = TextBlack)
                    }
                }
            }
        }
    }
}

@Preview(showBackground = true)
@Composable
fun MqttScreenPreview() {
    MaterialTheme {
        Box(modifier = Modifier.fillMaxSize().padding(16.dp), contentAlignment = Alignment.Center) {
             Text("MQTT Screen Preview")
        }
    }
}
