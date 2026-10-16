package com.gta.reversed;

import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import android.content.SharedPreferences;
import com.wardrumstudios.utils.WarMedia;
import java.io.File;

import com.bytedance.shadowhook.ShadowHook;

public class GTASA extends WarMedia {
    private static final String TAG = "GTASA";
    public static GTASA gtasaSelf;
    static String vmVersion;
    boolean UseExpansionPack = false;

    static {
        ShadowHook.init(new ShadowHook.ConfigBuilder()
                .setMode(ShadowHook.Mode.SHARED)
                .build());

        Log.i(TAG, "**** Loading SO's");
        try {
            vmVersion = System.getProperty("java.vm.version");
            Log.i(TAG, "vmVersion " + vmVersion);
            try { System.loadLibrary("ImmEmulatorJ"); } catch (Throwable ignored) {}
            try { System.loadLibrary("OpenAL32"); } catch (Throwable ignored) {}
            try { System.loadLibrary("OpenAL64"); } catch (Throwable ignored) {}
            System.loadLibrary("SCAnd");
            System.loadLibrary("GTASA");
            System.loadLibrary("reGTA");
        } catch (Throwable e) {
            Log.e(TAG, "Failed to load native libraries: " + e.getMessage(), e);
        }
    }
    @Override
    public void onCreate(Bundle bundle) {
        Log.i(TAG, "**** onCreate");
        gtasaSelf = this;
        this.apkFileName = GetPackageName(getPackageName());
        Log.i(TAG, "apkFileName " + this.apkFileName);
        this.baseDirectory = GetGameBaseDirectory();
        this.AllowLongPressForExit = true;

        // Resolve data mode (Intent extra > SharedPreferences > Auto-detection)
        String mode = getIntent() != null ? getIntent().getStringExtra("DATA_MODE") : null;
        if (mode == null) {
            android.content.SharedPreferences prefs = getSharedPreferences("reGTA_Settings", MODE_PRIVATE);
            mode = prefs.getString("data_mode", null);
        }

        if ("LITE".equalsIgnoreCase(mode)) {
            Log.i(TAG, "Selected Mode: LITE (Data files only, no OBB)");
            this.UseExpansionPack = false;
            this.xAPKS = null;
            this.expansionFileName = "";
            this.patchFileName = "";
            Toast.makeText(this, "reGTA: Modo Lite (Data)", Toast.LENGTH_SHORT).show();
        } else if ("FULL".equalsIgnoreCase(mode)) {
            Log.i(TAG, "Selected Mode: FULL (OBB Expansion Packs)");
            this.UseExpansionPack = true;
            this.expansionFileName = "main.8." + getPackageName() + ".obb";
            this.patchFileName = "patch.8." + getPackageName() + ".obb";
            this.xAPKS = new WarMedia.XAPKFile[2];
            this.xAPKS[0] = new WarMedia.XAPKFile(true, 8, 1967561852L);
            this.xAPKS[1] = new WarMedia.XAPKFile(false, 8, 625313014L);
            Toast.makeText(this, "reGTA: Modo Full (OBB)", Toast.LENGTH_SHORT).show();
        } else {
            // Auto fallback: check if loose data directories exist
            boolean hasLiteFolders = false;
            String[] dataDirs = {"anim", "audio", "data", "models", "texdb"};
            for (String dir : dataDirs) {
                File file = new File(this.baseDirectory, dir);
                if (file.exists() && file.isDirectory()) {
                    hasLiteFolders = true;
                    break;
                }
            }

            if (hasLiteFolders) {
                Log.i(TAG, "Auto-detected LITE data in: " + this.baseDirectory);
                this.UseExpansionPack = false;
                this.xAPKS = null;
                this.expansionFileName = "";
                this.patchFileName = "";
                Toast.makeText(this, "reGTA: Modo Lite detectado", Toast.LENGTH_SHORT).show();
            } else {
                Log.i(TAG, "Auto-detected FULL (OBB)");
                this.UseExpansionPack = true;
                this.expansionFileName = "main.8." + getPackageName() + ".obb";
                this.patchFileName = "patch.8." + getPackageName() + ".obb";
                this.xAPKS = new WarMedia.XAPKFile[2];
                this.xAPKS[0] = new WarMedia.XAPKFile(true, 8, 1967561852L);
                this.xAPKS[1] = new WarMedia.XAPKFile(false, 8, 625313014L);
                Toast.makeText(this, "reGTA: Modo Full (OBB)", Toast.LENGTH_SHORT).show();
            }
        }

        this.wantsMultitouch = true;
        this.wantsAccelerometer = true;
        RestoreCurrentLanguage();
        super.onCreate(bundle);
        SetReportPS3As360(false);

    }

    @Override
    public boolean ServiceAppCommand(String str, String str2) {
        if (str.equalsIgnoreCase("SetLocale")) {
            SetLocale(str2);
        }
        return false;
    }

    @Override
    public int ServiceAppCommandValue(String str, String str2) {
        if (str.equalsIgnoreCase("GetDownloadBytes")) {
            return 0;
        }
        if (str.equalsIgnoreCase("GetDownloadState")) {
            return 4;
        }
        return (str.equalsIgnoreCase("GetNetworkState") && isNetworkAvailable()) ? 1 : 0;
    }

    @Override
    public void onStart() {
        Log.i(TAG, "**** onStart");
        super.onStart();
    }

    @Override
    public void onRestart() {
        Log.i(TAG, "**** onRestart");
        super.onRestart();
    }

    @Override
    public void onResume() {
        Log.i(TAG, "**** onResume");
        super.onResume();
    }

    @Override
    public void onPause() {
        Log.i(TAG, "**** onPause");
        super.onPause();
    }

    @Override
    public void onStop() {
        Log.i(TAG, "**** onStop");
        super.onStop();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "**** onDestroy");
        super.onDestroy();
    }

    @Override
    public boolean CustomLoadFunction() {
        return CheckIfNeedsReadPermission(gtasaSelf);
    }

    public static void staticEnterSocialClub() {
        gtasaSelf.EnterSocialClub();
    }

    public static void staticExitSocialClub() {
        gtasaSelf.ExitSocialClub();
    }

    public void EnterSocialClub() {
        Log.i(TAG, "**** EnterSocialClub");
    }

    public void ExitSocialClub() {
        Log.i(TAG, "**** ExitSocialClub");
    }
}
