package com.example.sampling.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.*
import androidx.compose.material3.TabRowDefaults.tabIndicatorOffset
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.sampling.R
import com.example.sampling.data.DbRecord
import com.example.sampling.ui.theme.Slate
import com.example.sampling.ui.theme.SurfaceWhite
import com.example.sampling.viewmodel.DatabaseViewModel

@Composable
fun DatabaseScreen(
    viewModel: DatabaseViewModel = viewModel()
) {
    var selectedTabIndex by remember { mutableIntStateOf(0) }
    val tabTitles = listOf(
        stringResource(R.string.db_tab_sampling),
        stringResource(R.string.db_tab_sending),
        stringResource(R.string.db_tab_retention),
        stringResource(R.string.db_tab_door)
    )
    val queryTypes = listOf("Sampling", "Sending", "Retention", "Door")
    
    val records by viewModel.records.collectAsState()
    val isLoading by viewModel.isLoading.collectAsState()
    val connectionStatus by viewModel.connectionStatus.collectAsState()
    val deviceId by viewModel.deviceId.collectAsState()
    val focusManager = LocalFocusManager.current

    LaunchedEffect(selectedTabIndex, deviceId) {
        viewModel.loadRecords(queryTypes[selectedTabIndex])
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(bottom = 0.dp)
    ) {
        Surface(
            color = Slate,
            modifier = Modifier.fillMaxWidth()
        ) {
            Column {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = stringResource(R.string.db_title),
                        style = MaterialTheme.typography.titleLarge,
                        color = Color.White
                    )
                    
                    Button(
                        onClick = { viewModel.testConnection() },
                        modifier = Modifier.height(36.dp),
                        contentPadding = PaddingValues(horizontal = 8.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = Color.White,
                            contentColor = Slate
                        )
                    ) {
                        Text(stringResource(R.string.btn_test_conn), style = MaterialTheme.typography.labelSmall)
                    }
                }
                
                Text(
                    text = stringResource(R.string.label_db_status, connectionStatus), 
                    style = MaterialTheme.typography.bodySmall, 
                    color = Color.White.copy(alpha = 0.8f),
                    modifier = Modifier.padding(start = 16.dp, bottom = 8.dp)
                )

                OutlinedTextField(
                    value = deviceId,
                    onValueChange = { viewModel.updateDeviceId(it) },
                    label = { Text(stringResource(R.string.label_device_id)) },
                    placeholder = { Text(stringResource(R.string.hint_device_id)) },
                    singleLine = true,
                    leadingIcon = {
                        Icon(Icons.Default.Search, contentDescription = null, tint = Color.White)
                    },
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedTextColor = Color.White,
                        unfocusedTextColor = Color.White,
                        focusedBorderColor = Color.White,
                        unfocusedBorderColor = Color.White.copy(alpha = 0.6f),
                        focusedLabelColor = Color.White,
                        unfocusedLabelColor = Color.White.copy(alpha = 0.8f),
                        cursorColor = Color.White,
                        focusedPlaceholderColor = Color.White.copy(alpha = 0.5f),
                        unfocusedPlaceholderColor = Color.White.copy(alpha = 0.5f)
                    ),
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                    keyboardActions = KeyboardActions(
                        onSearch = {
                            focusManager.clearFocus()
                            viewModel.loadRecords(queryTypes[selectedTabIndex])
                        }
                    ),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 8.dp)
                )
            }
        }

        TabRow(
            selectedTabIndex = selectedTabIndex,
            containerColor = SurfaceWhite,
            contentColor = Slate,
            indicator = { tabPositions ->
                TabRowDefaults.SecondaryIndicator(
                    Modifier.tabIndicatorOffset(tabPositions[selectedTabIndex]),
                    color = Slate
                )
            }
        ) {
            tabTitles.forEachIndexed { index, title ->
                Tab(
                    selected = selectedTabIndex == index,
                    onClick = { selectedTabIndex = index },
                    text = { 
                        Text(
                            title, 
                            maxLines = 1, 
                            color = if (selectedTabIndex == index) Slate else Color.Gray
                        ) 
                    }
                )
            }
        }

        Column(modifier = Modifier.padding(8.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = tabTitles[selectedTabIndex], 
                    style = MaterialTheme.typography.titleMedium,
                    color = Slate
                )
                if (isLoading) {
                    CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 2.dp, color = Slate)
                } else {
                    IconButton(onClick = { viewModel.loadRecords(queryTypes[selectedTabIndex]) }) {
                        Icon(Icons.Default.Refresh, contentDescription = stringResource(R.string.btn_refresh_data), tint = Slate)
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(8.dp))

            val headers = when (queryTypes[selectedTabIndex]) {
                "Sampling" -> listOf("模式", "桶号", "采样量", "时间", "结果")
                "Sending" -> listOf("模式", "桶号", "送样量", "时间", "结果")
                "Retention" -> listOf("模式", "瓶号", "留样量", "时间", "结果")
                "Door" -> listOf("事件", "-", "-", "时间", "")
                else -> listOf("列1", "列2", "列3", "列4", "列5")
            }

            TableHeader(headers)

            LazyColumn(
                verticalArrangement = Arrangement.spacedBy(0.dp)
            ) {
                if (records.isEmpty() && !isLoading) {
                    item {
                        Text(
                            if (deviceId.isBlank()) stringResource(R.string.msg_enter_device_id) 
                            else stringResource(R.string.msg_no_records), 
                            style = MaterialTheme.typography.bodyMedium, 
                            color = MaterialTheme.colorScheme.secondary,
                            modifier = Modifier.padding(16.dp)
                        )
                    }
                }
                items(records) { record ->
                    TableRow(record)
                }
            }
        }
    }
}

@Composable
fun TableHeader(headers: List<String>) {
    val weights = listOf(1.2f, 0.6f, 0.7f, 1.3f, 0.6f)
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFFE0E0E0))
            .border(1.dp, Color.Gray)
    ) {
        headers.forEachIndexed { index, header ->
            Text(
                text = if (header == "-") "" else header,
                modifier = Modifier
                    .weight(weights.getOrElse(index) { 1f })
                    .padding(vertical = 8.dp, horizontal = 2.dp),
                textAlign = TextAlign.Center,
                fontSize = 11.sp,
                color = Color.Black
            )
        }
    }
}

@Composable
fun TableRow(record: DbRecord) {
    val weights = listOf(1.2f, 0.6f, 0.7f, 1.3f, 0.6f)
    val values = listOf(record.col1, record.col2, record.col3, record.col4, record.col5)
    
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color.White)
            .border(0.5.dp, Color.LightGray)
    ) {
        values.forEachIndexed { index, value ->
            Text(
                text = value,
                modifier = Modifier
                    .weight(weights.getOrElse(index) { 1f })
                    .padding(vertical = 8.dp, horizontal = 2.dp),
                textAlign = TextAlign.Center,
                fontSize = 10.sp,
                color = if (value == "成功") Color(0xFF4CAF50) 
                       else if (value == "失败") Color.Red 
                       else Color.DarkGray
            )
        }
    }
}
