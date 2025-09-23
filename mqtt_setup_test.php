<?php
/**
 * MQTT Setup und Konfiguration Test
 * 
 * Dieses Script hilft beim Setup und Test der MQTT-Konfiguration
 */

// Prüfe ob Credentials-Datei existiert
if (!file_exists(__DIR__ . '/mqtt_credentials.php')) {
    echo "❌ FEHLER: mqtt_credentials.php nicht gefunden!\n";
    echo "📝 AKTION: Kopieren Sie mqtt_credentials.template.php zu mqtt_credentials.php\n";
    echo "✏️  Tragen Sie dann Ihre HiveMQ-Daten ein\n";
    exit(1);
}

// Lade Credentials
require_once __DIR__ . '/mqtt_credentials.php';

echo "🔧 MQTT Konfiguration Test\n";
echo "=" . str_repeat("=", 50) . "\n\n";

// Teste Konfiguration
echo "📊 Konfiguration:\n";
echo "  Broker: " . MQTT_BROKER . "\n";
echo "  Port: " . MQTT_PORT . "\n";
echo "  Username: " . MQTT_USERNAME . "\n";
echo "  Password: " . str_repeat("*", strlen(MQTT_PASSWORD)) . "\n";
echo "  SSL: " . (MQTT_USE_SSL ? "✅ Aktiviert" : "❌ Deaktiviert") . "\n\n";

// Validiere Konfiguration
if (validateMqttConfig()) {
    echo "✅ Konfiguration ist vollständig\n\n";
} else {
    echo "❌ Konfiguration unvollständig!\n";
    echo "🔍 Prüfen Sie mqtt_credentials.php\n\n";
}

// Teste Topic-Generierung
echo "📡 Topic-Tests:\n";
$testCallsign = "OE1ABC";
echo "  TX Topic: " . getMqttTopic($testCallsign, 'tx') . "\n";
echo "  RX Topic: " . getMqttTopic($testCallsign, 'rx') . "\n";
echo "  Status Topic: " . getMqttTopic($testCallsign, 'status') . "\n";
echo "  Broadcast Topic: " . getMqttBroadcastTopic() . "\n\n";

// Network Test (falls verfügbar)
echo "🌐 Netzwerk-Test:\n";
$brokerHost = MQTT_BROKER;
$brokerPort = MQTT_PORT;

// Teste DNS Auflösung
$ip = gethostbyname($brokerHost);
if ($ip !== $brokerHost) {
    echo "  ✅ DNS: $brokerHost → $ip\n";
} else {
    echo "  ❌ DNS: Kann $brokerHost nicht auflösen\n";
}

// Teste Port-Erreichbarkeit (vereinfacht)
$connection = @fsockopen($brokerHost, $brokerPort, $errno, $errstr, 5);
if ($connection) {
    echo "  ✅ Port: $brokerPort ist erreichbar\n";
    fclose($connection);
} else {
    echo "  ❌ Port: $brokerPort nicht erreichbar ($errstr)\n";
}

echo "\n🚀 Nächste Schritte:\n";
echo "1. ESP32-Code mit MQTT-Bibliothek erweitern\n";
echo "2. PHP MQTT-Client implementieren\n";
echo "3. Topic-basierte Kommunikation testen\n";
echo "4. HTTP→MQTT Migration durchführen\n\n";

/**
 * Test MQTT Message Format
 */
function testMessageFormat() {
    echo "📨 Nachrichten-Format Test:\n";
    
    // Simuliere ESP32→Server Nachricht
    $rxMessage = [
        'timestamp' => time(),
        'callsign' => 'OE1ABC',
        'type' => 'data',
        'payload' => 'Hello World via Packet Radio',
        'rssi' => -65,
        'gateway' => 'ESP32-Gateway-01'
    ];
    
    echo "  RX (ESP32→Server): " . json_encode($rxMessage, JSON_PRETTY_PRINT) . "\n\n";
    
    // Simuliere Server→ESP32 Nachricht  
    $txMessage = [
        'timestamp' => time(),
        'target' => 'OE1XYZ',
        'type' => 'data', 
        'payload' => 'Message from Internet to Radio',
        'priority' => 'normal'
    ];
    
    echo "  TX (Server→ESP32): " . json_encode($txMessage, JSON_PRETTY_PRINT) . "\n\n";
}

testMessageFormat();

echo "✨ Setup-Test abgeschlossen!\n";
?>