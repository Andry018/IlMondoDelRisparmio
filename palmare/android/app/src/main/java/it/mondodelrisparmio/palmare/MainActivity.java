package it.mondodelrisparmio.palmare;

import android.os.Bundle;
import com.getcapacitor.BridgeActivity;

public class MainActivity extends BridgeActivity {

    @Override
    public void onCreate(Bundle savedInstanceState) {
        registerPlugin(Installatore.class);   // aggiornamento dell'APK (aggiornamenti.js)
        super.onCreate(savedInstanceState);
    }
}
