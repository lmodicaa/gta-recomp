package com.gta.reversed

import android.app.AlertDialog
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import androidx.annotation.Keep
import org.json.JSONArray
import androidx.appcompat.app.AppCompatActivity

class REGTA : GTASA() {

    private external fun initreGTA()
    private external fun nativeMtaConnect(host: String, port: Int, nick: String)
    private external fun nativeMtaDisconnect()
    private external fun nativeMtaSendChat(text: String)
    private external fun nativeMtaGetState(): Int
    private external fun nativeMtaStartLocalServer(port: Int): Boolean
    private external fun nativeMtaStopLocalServer()
    private external fun nativeMtaIsLocalServerRunning(): Boolean
    private external fun nativeMtaSpawnVehicle(modelId: Int): Boolean
    private external fun nativeMtaSetSkin(skinId: Int): Boolean
    private external fun nativeMtaGetPlayerList(): String

    private var tvStatus: TextView? = null
    private var tvChatLog: TextView? = null
    private var svChat: ScrollView? = null
    private var etChatInput: EditText? = null
    private var chatBox: View? = null
    private var scoreboardBox: View? = null
    private var tvScoreboardList: TextView? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        activity = this

        try {
            initreGTA()
        } catch (e: UnsatisfiedLinkError) {
            e.message?.let { Log.e("REGTA", it) }
        }

        super.onCreate(savedInstanceState)

        initMtaHud()
    }

    private fun initMtaHud() {
        try {
            val inflater = LayoutInflater.from(this)
            val hudView = inflater.inflate(R.layout.layout_mta_hud, null)
            addContentView(
                hudView,
                ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )
            )

            tvStatus = hudView.findViewById(R.id.tvMtaStatus)
            tvChatLog = hudView.findViewById(R.id.tvMtaChatLog)
            svChat = hudView.findViewById(R.id.svMtaChat)
            etChatInput = hudView.findViewById(R.id.etMtaChatInput)
            chatBox = hudView.findViewById(R.id.mtaChatBox)
            scoreboardBox = hudView.findViewById(R.id.mtaScoreboardBox)
            tvScoreboardList = hudView.findViewById(R.id.tvMtaScoreboardList)

            val btnConnect = hudView.findViewById<Button>(R.id.btnMtaOpenConnect)
            val btnToggleChat = hudView.findViewById<Button>(R.id.btnMtaToggleChat)
            val btnVehicles = hudView.findViewById<Button>(R.id.btnMtaVehicles)
            val btnScoreboard = hudView.findViewById<Button>(R.id.btnMtaScoreboard)
            val btnCloseScoreboard = hudView.findViewById<Button>(R.id.btnMtaCloseScoreboard)
            val btnSend = hudView.findViewById<Button>(R.id.btnMtaSendChat)

            btnConnect?.setOnClickListener { showConnectDialog() }
            btnToggleChat?.setOnClickListener { toggleChatBox() }
            btnVehicles?.setOnClickListener { showVehicleSpawnerDialog() }
            btnScoreboard?.setOnClickListener { toggleScoreboard() }
            btnCloseScoreboard?.setOnClickListener { scoreboardBox?.visibility = View.GONE }
            btnSend?.setOnClickListener { sendChatMessage() }

            Log.i("REGTA", "MTA HUD Overlay initialized.")
        } catch (e: Exception) {
            Log.e("REGTA", "Failed to init MTA HUD: ${e.message}", e)
        }
    }

    private fun toggleChatBox() {
        chatBox?.let { box ->
            if (box.visibility == View.VISIBLE) {
                box.visibility = View.GONE
            } else {
                box.visibility = View.VISIBLE
                etChatInput?.requestFocus()
            }
        }
    }

    private fun sendChatMessage() {
        val text = etChatInput?.text?.toString()?.trim() ?: return
        if (text.isNotEmpty()) {
            nativeMtaSendChat(text)
            appendChatMessage("Tú", text)
            etChatInput?.setText("")
        }
    }

    @Keep
    fun showConnectDialogFromNative() {
        runOnUiThread {
            showConnectDialog()
        }
    }

    private fun showConnectDialog() {
        val prefs = getSharedPreferences("MTA_Config", MODE_PRIVATE)
        val savedHost = prefs.getString("server_host", "127.0.0.1") ?: "127.0.0.1"
        val savedPort = prefs.getInt("server_port", 22003)
        val savedNick = prefs.getString("nickname", "Player_${(100..999).random()}") ?: "Player"

        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_mta_connect, null)
        val etHost = dialogView.findViewById<EditText>(R.id.etMtaHost)
        val etPort = dialogView.findViewById<EditText>(R.id.etMtaPort)
        val etNick = dialogView.findViewById<EditText>(R.id.etMtaNick)
        val btnHostLocal = dialogView.findViewById<Button>(R.id.btnMtaHostLocal)
        val btnCancel = dialogView.findViewById<Button>(R.id.btnMtaCancel)
        val btnConnect = dialogView.findViewById<Button>(R.id.btnMtaConnect)

        etHost.setText(savedHost)
        etPort.setText(savedPort.toString())
        etNick.setText(savedNick)

        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .setCancelable(true)
            .create()

        btnCancel.setOnClickListener { dialog.dismiss() }

        // HOST LOCAL SERVER
        btnHostLocal?.setOnClickListener {
            val port = etPort.text.toString().trim().toIntOrNull() ?: 22003
            val nick = etNick.text.toString().trim()
            val nickFinal = if (nick.isNotEmpty()) nick else "HostPlayer"

            Log.d("DEBUG", "[DEBUG] [UI] Click en CREAR SERVIDOR LOCAL en puerto $port como '$nickFinal'")

            val started = nativeMtaStartLocalServer(port)
            if (started) {
                appendChatMessage("HOST", "¡Servidor Local MTA iniciado en puerto $port!")
                appendChatMessage("HOST", "Conectando al servidor local (127.0.0.1:$port)...")
                chatBox?.visibility = View.VISIBLE
                nativeMtaConnect("127.0.0.1", port, nickFinal)
                dialog.dismiss()
            } else {
                appendChatMessage("HOST", "ERROR: No se pudo levantar el servidor local en puerto $port.")
            }
        }

        btnConnect.setOnClickListener {
            val host = etHost.text.toString().trim()
            val port = etPort.text.toString().trim().toIntOrNull() ?: 22003
            val nick = etNick.text.toString().trim()

            Log.d("DEBUG", "[DEBUG] [UI] Boton Conectar pulsado: Host='$host', Port=$port, Nick='$nick'")

            if (host.isNotEmpty() && nick.isNotEmpty()) {
                prefs.edit()
                    .putString("server_host", host)
                    .putInt("server_port", port)
                    .putString("nickname", nick)
                    .apply()

                // Abrir la ventana de chat para ver los logs en pantalla
                chatBox?.visibility = View.VISIBLE

                appendChatMessage("MTA", "Conectando a $host:$port como $nick...")
                Log.d("DEBUG", "[DEBUG] [UI] Llamando a nativeMtaConnect('$host', $port, '$nick')...")
                nativeMtaConnect(host, port, nick)
                dialog.dismiss()
            } else {
                Log.d("DEBUG", "[DEBUG] [UI] Host o Nick vacio. No se inicia conexion.")
            }
        }

        dialog.show()
    }

    private fun showVehicleSpawnerDialog() {
        val vehicles = arrayOf(
            "🏎️ Infernus (411)",
            "🏍️ NRG-500 (522)",
            "🚗 Sultan (560)",
            "🏎️ Bullet (541)",
            "🚗 Elegy (562)",
            "🏎️ Turismo (451)",
            "🏎️ Cheetah (415)",
            "🏍️ Sanchez (468)",
            "🚲 BMX (481)",
            "✈️ Hydra (520)",
            "🚁 Hunter (425)",
            "🛻 Monster Truck (444)",
            "🪖 Tanque Rhino (432)",
            "🔢 Ingresar ID manual..."
        )
        val modelIds = intArrayOf(
            411, 522, 560, 541, 562, 451, 415, 468, 481, 520, 425, 444, 432, -1
        )

        AlertDialog.Builder(this)
            .setTitle("🚗 MTA - Selector de Vehículos")
            .setItems(vehicles) { _, which ->
                val selectedId = modelIds[which]
                if (selectedId == -1) {
                    showCustomVehicleDialog()
                } else {
                    spawnVehicleById(selectedId)
                }
            }
            .setNegativeButton("Cerrar", null)
            .show()
    }

    private fun showCustomVehicleDialog() {
        val input = EditText(this)
        input.hint = "Ejemplo: 411 (400 - 611)"
        input.inputType = android.text.InputType.TYPE_CLASS_NUMBER

        AlertDialog.Builder(this)
            .setTitle("🔢 Ingresar ID de Vehículo")
            .setView(input)
            .setPositiveButton("Crear") { _, _ ->
                val id = input.text.toString().trim().toIntOrNull()
                if (id != null && id in 400..611) {
                    spawnVehicleById(id)
                } else {
                    appendChatMessage("MTA", "ID inválido. Los vehículos van del 400 al 611.")
                }
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun spawnVehicleById(modelId: Int) {
        val success = nativeMtaSpawnVehicle(modelId)
        if (success) {
            appendChatMessage("MTA", "Generando vehículo $modelId...")
        } else {
            appendChatMessage("MTA", "No se pudo solicitar el vehículo $modelId.")
        }
    }

    private fun toggleScoreboard() {
        scoreboardBox?.let { box ->
            if (box.visibility == View.VISIBLE) {
                box.visibility = View.GONE
            } else {
                updateScoreboardContent()
                box.visibility = View.VISIBLE
            }
        }
    }

    private fun updateScoreboardContent() {
        try {
            val jsonStr = nativeMtaGetPlayerList()
            val prefs = getSharedPreferences("MTA_Config", MODE_PRIVATE)
            val myNick = prefs.getString("nickname", "Tú") ?: "Tú"

            val sb = StringBuilder()
            // Local player row
            sb.append(String.format("%-4s  %-16s  %4s  %4s  %4s\n", "0", "$myNick (Tú)", "100%", "100%", "0ms"))

            if (jsonStr.isNotEmpty() && jsonStr != "[]") {
                val jsonArray = JSONArray(jsonStr)
                for (i in 0 until jsonArray.length()) {
                    val obj = jsonArray.getJSONObject(i)
                    val id = obj.optInt("id", i + 1)
                    val nick = obj.optString("nick", "Player")
                    val health = obj.optInt("health", 100)
                    val armour = obj.optInt("armour", 0)
                    val ping = obj.optInt("ping", 25)
                    sb.append(String.format("%-4d  %-16s  %3d%%  %3d%%  %3dms\n", id, nick, health, armour, ping))
                }
            }
            tvScoreboardList?.text = sb.toString()
        } catch (e: Exception) {
            Log.e("REGTA", "Error actualizando scoreboard: ${e.message}")
        }
    }

    private fun appendChatMessage(sender: String, message: String) {
        Log.d("DEBUG", "[DEBUG] [UI] appendChatMessage: [$sender] $message")
        runOnUiThread {
            tvChatLog?.append("[$sender] $message\n")
            svChat?.post {
                svChat?.fullScroll(View.FOCUS_DOWN)
            }
        }
    }

    @Keep
    fun onMtaChatMessage(sender: String, message: String) {
        Log.d("DEBUG", "[DEBUG] [UI] onMtaChatMessage JNI Callback: [$sender] $message")
        appendChatMessage(sender, message)
    }

    @Keep
    fun onMtaStateChanged(state: Int, statusText: String) {
        Log.d("DEBUG", "[DEBUG] [UI] onMtaStateChanged JNI Callback: state=$state text='$statusText'")
        runOnUiThread {
            tvStatus?.text = statusText
            when (state) {
                0 -> tvStatus?.setTextColor(Color.parseColor("#A0A0B0")) // DISCONNECTED
                1 -> tvStatus?.setTextColor(Color.parseColor("#F6D20F")) // CONNECTING
                2, 3 -> tvStatus?.setTextColor(Color.parseColor("#4CC9F0")) // JOINING / MAP
                4 -> tvStatus?.setTextColor(Color.parseColor("#4ADE80")) // INGAME
                else -> tvStatus?.setTextColor(Color.WHITE)
            }
        }
    }

    companion object {
        @JvmStatic
        lateinit var activity: AppCompatActivity
    }
}