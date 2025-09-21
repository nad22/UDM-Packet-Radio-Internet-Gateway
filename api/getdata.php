<?php
require_once("../db.php");

header('Content-Type: application/json');

// 1. Callsign prüfen
$callsign = $_GET['callsign'] ?? '';

function isCallsignAllowed($callsign) {
    global $mysqli;
    if (empty($callsign)) return false;
    
    $stmt = $mysqli->prepare("SELECT status FROM clients WHERE callsign = ?");
    $stmt->bind_param("s", $callsign);
    $stmt->execute();
    $result = $stmt->get_result();
    
    if ($row = $result->fetch_assoc()) {
        // Client existiert - prüfe ob Status aktiv ist (1 = aktiv)
        return $row['status'] == 1;
    }
    
    return false; // Client nicht gefunden
}

if (!isCallsignAllowed($callsign)) {
    // Log in messages Tabelle mit automatischer Bereinigung
    $msg = "Nicht autorisierter Anmeldeversuch von Callsign '$callsign' (getdata.php)";
    $stmt = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES ('SYSTEM', NULL, ?)");
    $stmt->bind_param("s", $msg);
    $stmt->execute();
    
    // Automatische Bereinigung: Behalte nur die letzten 1000 Einträge
    $cleanupStmt = $mysqli->prepare("DELETE FROM messages WHERE id NOT IN (SELECT id FROM (SELECT id FROM messages ORDER BY id DESC LIMIT 1000) AS temp)");
    $cleanupStmt->execute();
    
    http_response_code(403); // 403 Forbidden
    echo json_encode(["error" => "DENY"]);
    exit;
}

// 2. Prüfe auf Broadcast-Frames vom Server
$broadcastData = '';
$broadcastFile = '/tmp/udmprig_broadcast.txt';
if (file_exists($broadcastFile)) {
    $broadcastData = file_get_contents($broadcastFile);
    // Leere Broadcast-Datei nach dem Lesen
    file_put_contents($broadcastFile, '');
}

// 3. Daten weitergeben wie gehabt
$lines = @file('/tmp/udmprig_buffer.txt', FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
$result = [];

foreach ($lines as $line) {
    $entry = json_decode($line, true);
    if (!$entry) continue;
    $result[] = $entry;
}

// 4. Füge Broadcast-Frames hinzu
if (!empty($broadcastData)) {
    $broadcastLines = explode("\n", trim($broadcastData));
    foreach ($broadcastLines as $line) {
        if (empty($line)) continue;
        $entry = json_decode($line, true);
        if ($entry) {
            // Dekodiere base64-kodierte AX25-Daten
            $entry['data'] = base64_decode($entry['data']);
            $result[] = $entry;
        }
    }
}

// Nach dem Abruf die Datei leeren
file_put_contents('/tmp/udmprig_buffer.txt', '');

// 5. Gebe Daten als Raw-String zurück (nicht JSON) für ESP32-Client
if (!empty($result)) {
    $output = '';
    foreach ($result as $entry) {
        $output .= $entry['data'];
    }
    header('Content-Type: application/octet-stream');
    echo $output;
} else {
    header('Content-Type: application/json');
    echo json_encode([]);
}
?>