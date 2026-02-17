package com.example.sampling

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.activity.ComponentActivity
import android.widget.LinearLayout
import android.view.Gravity
import android.view.ViewGroup
import android.graphics.Color

class CrashActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val error = intent.getStringExtra("error") ?: "Unknown error"

        // Extract root cause
        val lines = error.lines()
        val rootCause = lines.findLast { it.trim().startsWith("Caused by:") } ?: "See details below"

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
            setPadding(32, 32, 32, 32)
            gravity = Gravity.CENTER
            setBackgroundColor(Color.WHITE)
        }

        val titleView = TextView(this).apply {
            text = getString(R.string.crash_title)
            textSize = 24f
            setTextColor(Color.RED)
            gravity = Gravity.CENTER
            setPadding(0, 0, 0, 16)
        }

        val rootCauseView = TextView(this).apply {
            text = getString(R.string.label_root_cause, rootCause)
            textSize = 16f
            setTextColor(Color.MAGENTA)
            setPadding(0, 0, 0, 16)
        }

        val scrollView = android.widget.ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        }

        val errorView = TextView(this).apply {
            text = error
            textSize = 12f
            setTextColor(Color.BLACK)
        }

        scrollView.addView(errorView)

        val button = Button(this).apply {
            text = getString(R.string.btn_restart_app)
            setOnClickListener {
                val intent = packageManager.getLaunchIntentForPackage(packageName)
                intent?.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_NEW_TASK)
                startActivity(intent)
                finish()
            }
        }

        layout.addView(titleView)
        layout.addView(rootCauseView)
        layout.addView(scrollView)
        layout.addView(button)

        setContentView(layout)
    }
}
