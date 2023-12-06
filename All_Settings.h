StoreStruct storage = {
		'%',                  // chkDigit;
		"MARODEKWiFi2",       // ESP_SSID[16];
		"MAROWiFi19052004!",  // ESP_PASS[27];
    "192.168.5.134",      // energyIP;
		2,                    // beeperCnt;
    10000,                // maxPower (W);
    25,                   // dayPower (KW);
    3,                    // dayGas (m3);
		0,                    // dispScreen;
    -1,                   // prefDay
    0,                    // lastPower
    0                     // lastGas
};

wlanSSID wifiNetworks[] {
    {"PI4RAZ","PI4RAZ_Zoetermeer"},
    {"Loretz_Gast", "Lor_Steg_98"},
    {"MARODEKWiFi", "MAROWiFi19052004!"}
};

int screenRotation          = 0; // 0=0, 1=90, 2=180, 3=270
