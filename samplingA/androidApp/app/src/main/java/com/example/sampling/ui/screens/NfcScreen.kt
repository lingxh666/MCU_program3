package com.example.sampling.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Nfc
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.sampling.R
import com.example.sampling.ui.theme.BackgroundGrey
import com.example.sampling.ui.theme.Teal
import com.example.sampling.ui.theme.TextBlack
import com.example.sampling.viewmodel.NfcViewModel

@Composable
fun NfcScreen(
    viewModel: NfcViewModel = viewModel()
) {
    val nfcStatus by viewModel.nfcStatus.collectAsState()
    val tagInfo by viewModel.tagInfo.collectAsState()
    val isScanning by viewModel.isScanning.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(bottom = 0.dp)
    ) {
        // Colored Header
        Surface(
            color = Teal,
            modifier = Modifier.fillMaxWidth()
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = stringResource(R.string.nfc_title),
                    style = MaterialTheme.typography.titleLarge,
                    color = Color.White
                )
            }
        }

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Spacer(modifier = Modifier.height(32.dp))

            // NFC Icon Area
            Box(
                modifier = Modifier.size(200.dp),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    Icons.Default.Nfc,
                    contentDescription = "NFC",
                    modifier = Modifier.size(150.dp),
                    tint = if (isScanning) Teal else Color.Gray
                )
                if (isScanning) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(200.dp),
                        color = Teal
                    )
                }
            }

            Spacer(modifier = Modifier.height(32.dp))

            // Status Text
            Text(
                text = nfcStatus,
                style = MaterialTheme.typography.titleMedium,
                textAlign = TextAlign.Center,
                color = TextBlack
            )

            Spacer(modifier = Modifier.height(24.dp))

            // Scan Button
            Button(
                onClick = {
                    viewModel.setScanning(!isScanning)
                },
                modifier = Modifier.fillMaxWidth(0.6f),
                colors = ButtonDefaults.buttonColors(containerColor = Teal)
            ) {
                Text(if (isScanning) stringResource(R.string.btn_stop_read) else stringResource(R.string.btn_start_read))
            }

            Spacer(modifier = Modifier.height(24.dp))

            // Result Card
            if (tagInfo.isNotEmpty()) {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
                    colors = CardDefaults.cardColors(containerColor = BackgroundGrey)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(stringResource(R.string.card_title_tag_info), style = MaterialTheme.typography.titleSmall, color = Teal)
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        tagInfo.forEach { (key, value) ->
                            Text("$key: $value", style = MaterialTheme.typography.bodyMedium, color = TextBlack)
                        }
                    }
                }
            }
        }
    }
}

@Preview(showBackground = true)
@Composable
fun NfcScreenPreview() {
    MaterialTheme {
        // Mock ViewModel requires DI or complex mocking, skipping logic for preview
        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text("NFC Screen Preview")
        }
    }
}
