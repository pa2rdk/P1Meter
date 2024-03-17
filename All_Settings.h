StoreStruct storage = {
		'!',                  // chkDigit;
		"YourSSID",           // ESP_SSID[16];
		"YourWiFiPass",       // ESP_PASS[27];
    "192.168.5.134",      // energyIP;
    "192.168.5.104",      // waterIP;
		2,                    // beeperCnt;
    3680,                 // maxFasePower;
    10000,                // maxPower (W);
    25,                   // dayPower (KW);
    5,                    // dayGas (m3);
    500,                  // dayWater (l);
    1,                    // useYesterdayAsMax;
		0,                    // dispScreen;
    -1,                   // prefDay
    0,                    // lastPower
    0,                    // lastGas
    0                     // lastWater
};

wlanSSID wifiNetworks[] {
    {"PI4RAZ","PI4RAZ_Zoetermeer"},
    {"Loretz_Gast", "Lor_Steg_98"},
    {"YourWiFi", "YourPass!"}
};

int screenRotation          = 0; // 0=0, 1=90, 2=180, 3=270
