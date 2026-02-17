package com.example.sampling.data

import android.app.Activity
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.nfc.tech.NfcA
import android.nfc.tech.NfcB
import android.nfc.tech.NfcF
import android.nfc.tech.NfcV
import android.nfc.tech.IsoDep
import android.nfc.tech.Ndef
import android.util.Log

class NfcManager(private val context: Context) {

    private val nfcAdapter: NfcAdapter? = NfcAdapter.getDefaultAdapter(context)

    fun isNfcEnabled(): Boolean {
        return nfcAdapter?.isEnabled == true
    }

    fun enableForegroundDispatch(activity: Activity) {
        if (nfcAdapter != null && nfcAdapter.isEnabled) {
            val intent = Intent(activity, activity.javaClass).apply {
                addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
            }
            val pendingIntent = PendingIntent.getActivity(
                activity, 0, intent,
                PendingIntent.FLAG_MUTABLE
            )
            val filters = arrayOf(IntentFilter(NfcAdapter.ACTION_TAG_DISCOVERED))
            nfcAdapter.enableForegroundDispatch(activity, pendingIntent, filters, null)
        }
    }

    fun disableForegroundDispatch(activity: Activity) {
        nfcAdapter?.disableForegroundDispatch(activity)
    }

    fun parseTag(tag: Tag): Map<String, String> {
        val result = mutableMapOf<String, String>()
        
        // 1. Get UID
        val uid = tag.id
        result["UID"] = bytesToHex(uid)

        // 2. Get Tech List
        val techList = tag.techList.map { it.substringAfterLast(".") }
        result["Technologies"] = techList.joinToString(", ")

        // 3. Try to read generic info based on tech
        // This is a basic reader. For specific chip data, custom commands might be needed.
        return result
    }

    private fun bytesToHex(bytes: ByteArray): String {
        return bytes.joinToString(":") { "%02X".format(it) }
    }
}
