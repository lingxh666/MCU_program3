package com.blue.ota

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView

class DeviceAdapter(
    private val onClick: (BleDevice) -> Unit
) : RecyclerView.Adapter<DeviceAdapter.DeviceViewHolder>() {

    private val items = mutableListOf<BleDevice>()

    fun upsert(device: BleDevice) {
        val index = items.indexOfFirst { it.mac == device.mac }
        if (index >= 0) {
            items[index] = device
            notifyItemChanged(index)
        } else {
            items.add(device)
            notifyItemInserted(items.size - 1)
        }
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DeviceViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_device, parent, false)
        return DeviceViewHolder(view)
    }

    override fun onBindViewHolder(holder: DeviceViewHolder, position: Int) {
        val item = items[position]
        holder.bind(item)
        holder.itemView.setOnClickListener { onClick(item) }
    }

    override fun getItemCount(): Int = items.size

    class DeviceViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val textName: TextView = itemView.findViewById(R.id.textName)
        private val textMac: TextView = itemView.findViewById(R.id.textMac)
        private val textRssi: TextView = itemView.findViewById(R.id.textRssi)

        fun bind(device: BleDevice) {
            textName.text = device.name
            textMac.text = device.mac
            textRssi.text = "${device.rssi} dBm"
        }
    }
}
