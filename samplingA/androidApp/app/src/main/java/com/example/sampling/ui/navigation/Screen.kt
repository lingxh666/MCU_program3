package com.example.sampling.ui.navigation

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.Nfc
import androidx.compose.material.icons.filled.Storage
import androidx.compose.ui.graphics.vector.ImageVector
import com.example.sampling.R

sealed class Screen(val route: String, val titleResId: Int, val icon: ImageVector) {
    object Bluetooth : Screen("bluetooth", R.string.nav_bluetooth, Icons.Filled.Bluetooth)
    object Nfc : Screen("nfc", R.string.nav_nfc, Icons.Filled.Nfc)
    object Mqtt : Screen("mqtt", R.string.nav_mqtt, Icons.Filled.Cloud)
    object Database : Screen("database", R.string.nav_database, Icons.Filled.Storage)
}
