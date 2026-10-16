package it.mondodelrisparmio.palmare;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import androidx.core.content.FileProvider;
import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import java.io.File;

/**
 * Apre l'installatore di Android su un APK già scaricato e verificato dall'app (aggiornamenti.js).
 * Android chiede sempre la conferma dell'operatore: un'app fuori dal Play Store non può
 * aggiornarsi in silenzio.
 */
@CapacitorPlugin(name = "Installatore")
public class Installatore extends Plugin {

    /** { permesso: boolean } — se l'app può proporre installazioni (Android 8+: "origini sconosciute"). */
    @PluginMethod
    public void permesso(PluginCall call) {
        JSObject ret = new JSObject();
        ret.put("permesso", puoInstallare(getContext()));
        call.resolve(ret);
    }

    /** { percorso: file:// o percorso assoluto nella cache dell'app } */
    @PluginMethod
    public void installa(PluginCall call) {
        Context c = getContext();
        String percorso = call.getString("percorso", "");
        File apk = new File(percorso.startsWith("file:") ? Uri.parse(percorso).getPath() : percorso);
        if (!apk.isFile()) {
            call.reject("APK non trovato: " + percorso);
            return;
        }
        if (!puoInstallare(c)) {
            // Una volta per palmare: Impostazioni → "Installa app sconosciute" → consenti
            Intent impostazioni = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + c.getPackageName()));
            impostazioni.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            c.startActivity(impostazioni);
            call.reject("Consenti l'installazione di app da questa origine, poi riprova", "PERMESSO");
            return;
        }
        Uri uri = FileProvider.getUriForFile(c, c.getPackageName() + ".fileprovider", apk);
        Intent installa = new Intent(Intent.ACTION_VIEW);
        installa.setDataAndType(uri, "application/vnd.android.package-archive");
        installa.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        c.startActivity(installa);
        call.resolve();
    }

    private static boolean puoInstallare(Context c) {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.O || c.getPackageManager().canRequestPackageInstalls();
    }
}
