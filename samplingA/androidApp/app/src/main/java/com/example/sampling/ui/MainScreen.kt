package com.example.sampling.ui

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.example.sampling.ui.navigation.Screen
import com.example.sampling.ui.screens.BluetoothScreen
import com.example.sampling.ui.screens.DatabaseScreen
import com.example.sampling.ui.screens.MqttScreen
import com.example.sampling.ui.screens.NfcScreen
import com.example.sampling.viewmodel.NfcViewModel
import com.example.sampling.viewmodel.MqttViewModel
import com.example.sampling.viewmodel.DatabaseViewModel

@Composable
fun MainScreen(
    nfcViewModel: NfcViewModel = viewModel(),
    mqttViewModel: MqttViewModel = viewModel(),
    databaseViewModel: DatabaseViewModel = viewModel()
) {
    val navController = rememberNavController()
    val screens = listOf(
        Screen.Bluetooth,
        Screen.Nfc,
        Screen.Mqtt,
        Screen.Database
    )

    Scaffold(
        bottomBar = {
            NavigationBar {
                val navBackStackEntry by navController.currentBackStackEntryAsState()
                val currentRoute = navBackStackEntry?.destination?.route

                screens.forEach { screen ->
                    NavigationBarItem(
                        icon = { Icon(screen.icon, contentDescription = stringResource(screen.titleResId)) },
                        label = { Text(stringResource(screen.titleResId)) },
                        selected = currentRoute == screen.route,
                        onClick = {
                            navController.navigate(screen.route) {
                                popUpTo(navController.graph.findStartDestination().id) {
                                    saveState = true
                                }
                                launchSingleTop = true
                                restoreState = true
                            }
                        }
                    )
                }
            }
        }
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = Screen.Bluetooth.route,
            modifier = Modifier.padding(innerPadding)
        ) {
            composable(Screen.Bluetooth.route) { BluetoothScreen() }
            composable(Screen.Nfc.route) { NfcScreen(viewModel = nfcViewModel) }
            composable(Screen.Mqtt.route) { MqttScreen(viewModel = mqttViewModel) }
            composable(Screen.Database.route) { DatabaseScreen(viewModel = databaseViewModel) }
        }
    }
}
