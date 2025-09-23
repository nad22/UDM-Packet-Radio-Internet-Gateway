<?php
/**
 * MQTT Credentials Template
 * 
 * 1. Kopieren Sie diese Datei zu 'mqtt_credentials.php'
 * 2. Tragen Sie Ihre HiveMQ Cloud Daten ein
 * 3. Löschen Sie diese Template-Datei
 */

// ANLEITUNG: Ersetzen Sie die Placeholder mit Ihren echten HiveMQ-Daten

// HiveMQ Cloud Configuration
define('MQTT_BROKER', 'your-cluster.s1.eu.hivemq.cloud'); // ← Ihr HiveMQ Cluster URL
define('MQTT_PORT', 8883); // SSL Port (normalerweise 8883)
define('MQTT_USERNAME', 'your-username'); // ← Ihr HiveMQ Username  
define('MQTT_PASSWORD', 'your-password'); // ← Ihr HiveMQ Password

// MQTT Topic Configuration  
define('MQTT_TOPIC_PREFIX', 'udmprig'); // Basis Topic für alle Nachrichten
define('MQTT_QOS_LEVEL', 1); // Quality of Service (1 = at least once delivery)

// SSL/TLS Configuration
define('MQTT_USE_SSL', true);
define('MQTT_CA_CERT_PATH', __DIR__ . '/certs/hivemq-ca.crt'); // CA Certificate Pfad

// Connection Settings
define('MQTT_KEEPALIVE', 60); // Keepalive Interval in Sekunden
define('MQTT_CLEAN_SESSION', true); // Clean Session Flag
define('MQTT_TIMEOUT', 10); // Connection Timeout

/**
 * BEISPIEL-KONFIGURATION:
 * 
 * Falls Ihr HiveMQ Cluster "abc123.s1.eu.hivemq.cloud" ist:
 * define('MQTT_BROKER', 'abc123.s1.eu.hivemq.cloud');
 * 
 * Falls Username "gateway-user" und Password "mySecurePassword123":
 * define('MQTT_USERNAME', 'gateway-user');
 * define('MQTT_PASSWORD', 'mySecurePassword123');
 */

// Restliche Funktionen bleiben unverändert...
function getMqttTopic($callsign, $direction) {
    return MQTT_TOPIC_PREFIX . '/' . strtolower($callsign) . '/' . $direction;
}

function getMqttBroadcastTopic() {
    return MQTT_TOPIC_PREFIX . '/broadcast/all';
}

function validateMqttConfig() {
    return !empty(MQTT_BROKER) && 
           !empty(MQTT_USERNAME) && 
           !empty(MQTT_PASSWORD) &&
           MQTT_BROKER !== 'your-cluster.s1.eu.hivemq.cloud';
}

function getMqttClientConfig() {
    return [
        'broker' => MQTT_BROKER,
        'port' => MQTT_PORT,
        'username' => MQTT_USERNAME,
        'password' => MQTT_PASSWORD,
        'ssl' => MQTT_USE_SSL,
        'ca_cert' => MQTT_CA_CERT_PATH,
        'keepalive' => MQTT_KEEPALIVE,
        'clean_session' => MQTT_CLEAN_SESSION,
        'timeout' => MQTT_TIMEOUT,
        'qos' => MQTT_QOS_LEVEL
    ];
}

if (basename($_SERVER['PHP_SELF']) === basename(__FILE__)) {
    http_response_code(403);
    die('Direct access not allowed');
}
?>