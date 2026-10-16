package com.gta.launcher;

import android.Manifest;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import com.gta.reversed.R;
import com.gta.reversed.REGTA;

import java.util.List;

import pub.devrel.easypermissions.EasyPermissions;

public class MainActivity extends AppCompatActivity implements EasyPermissions.PermissionCallbacks {

    private static final int MAIN_PERMISSIONS_REQUEST_CODE = 300;
    private static final String PREFS_NAME = "reGTA_Settings";
    private static final String PREF_DATA_MODE = "data_mode";
    private static final String PREF_REMEMBER = "remember_choice";

    private final String[] main_permissions = {
            Manifest.permission.WRITE_EXTERNAL_STORAGE,
            Manifest.permission.READ_EXTERNAL_STORAGE
    };

    private CheckBox cbRemember;
    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);

        // Check storage permissions on Android < 11
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R && !EasyPermissions.hasPermissions(this, main_permissions)) {
            EasyPermissions.requestPermissions(this,
                    "La aplicacion necesita permisos de almacenamiento para cargar los datos del juego.",
                    MAIN_PERMISSIONS_REQUEST_CODE, main_permissions);
            return;
        }

        initSelector();
    }

    private void initSelector() {
        setContentView(R.layout.activity_main);

        cbRemember = findViewById(R.id.cbRememberChoice);
        Button btnLite = findViewById(R.id.btnPlayLite);
        Button btnFull = findViewById(R.id.btnPlayFull);
        LinearLayout cardLite = findViewById(R.id.cardLite);
        LinearLayout cardFull = findViewById(R.id.cardFull);

        boolean remember = prefs.getBoolean(PREF_REMEMBER, false);
        if (cbRemember != null) {
            cbRemember.setChecked(remember);
        }

        Button btnMtaConfig = findViewById(R.id.btnMtaLauncherConfig);
        if (btnMtaConfig != null) {
            btnMtaConfig.setOnClickListener(v -> showMtaConfigDialog());
        }

        View.OnClickListener liteListener = v -> launchGame("LITE");
        View.OnClickListener fullListener = v -> launchGame("FULL");

        if (btnLite != null) btnLite.setOnClickListener(liteListener);
        if (cardLite != null) cardLite.setOnClickListener(liteListener);

        if (btnFull != null) btnFull.setOnClickListener(fullListener);
        if (cardFull != null) cardFull.setOnClickListener(fullListener);
    }

    private void showMtaConfigDialog() {
        android.content.SharedPreferences mtaPrefs = getSharedPreferences("MTA_Config", MODE_PRIVATE);
        String savedHost = mtaPrefs.getString("server_host", "127.0.0.1");
        int savedPort = mtaPrefs.getInt("server_port", 22003);
        String savedNick = mtaPrefs.getString("nickname", "Player_" + ((int)(Math.random() * 900) + 100));

        android.view.View dialogView = getLayoutInflater().inflate(R.layout.dialog_mta_connect, null);
        android.widget.EditText etHost = dialogView.findViewById(R.id.etMtaHost);
        android.widget.EditText etPort = dialogView.findViewById(R.id.etMtaPort);
        android.widget.EditText etNick = dialogView.findViewById(R.id.etMtaNick);
        Button btnCancel = dialogView.findViewById(R.id.btnMtaCancel);
        Button btnSave = dialogView.findViewById(R.id.btnMtaConnect);

        etHost.setText(savedHost);
        etPort.setText(String.valueOf(savedPort));
        etNick.setText(savedNick);
        btnSave.setText("GUARDAR");

        androidx.appcompat.app.AlertDialog dialog = new androidx.appcompat.app.AlertDialog.Builder(this)
                .setView(dialogView)
                .setCancelable(true)
                .create();

        btnCancel.setOnClickListener(v -> dialog.dismiss());
        btnSave.setOnClickListener(v -> {
            String host = etHost.getText().toString().trim();
            String portStr = etPort.getText().toString().trim();
            String nick = etNick.getText().toString().trim();
            int port = 22003;
            try { port = Integer.parseInt(portStr); } catch (Exception ignored) {}

            if (!host.isEmpty() && !nick.isEmpty()) {
                mtaPrefs.edit()
                        .putString("server_host", host)
                        .putInt("server_port", port)
                        .putString("nickname", nick)
                        .apply();
                android.widget.Toast.makeText(this, "Configuracion MTA guardada.", android.widget.Toast.LENGTH_SHORT).show();
                dialog.dismiss();
            }
        });

        dialog.show();
    }

    private void launchGame(String mode) {
        boolean remember = cbRemember != null && cbRemember.isChecked();
        prefs.edit()
                .putString(PREF_DATA_MODE, mode)
                .putBoolean(PREF_REMEMBER, remember)
                .apply();

        Intent intent = new Intent(this, REGTA.class);
        intent.putExtra("DATA_MODE", mode);
        startActivity(intent);
        finish();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        EasyPermissions.onRequestPermissionsResult(requestCode, permissions, grantResults, this);
    }

    @Override
    public void onPermissionsGranted(int requestCode, @NonNull List<String> list) {
        if (requestCode == MAIN_PERMISSIONS_REQUEST_CODE) {
            initSelector();
        }
    }

    @Override
    public void onPermissionsDenied(int requestCode, @NonNull List<String> list) {
        // Proceed to selector even if legacy permissions were denied
        initSelector();
    }
}