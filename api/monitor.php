<?php
require_once("../db.php");

// Parameter für Filterung
$filter = $_GET['filter'] ?? 'all'; // 'all', 'ax25', 'system', 'broadcasts'
$limit = intval($_GET['limit'] ?? 100);

$log = "";

// Zunächst Smart-Broadcast-Notifications anzeigen (die neuesten)
if ($filter === 'all' || $filter === 'broadcasts') {
    $sql = "SELECT callsign, message, created_at FROM notifications ORDER BY id DESC LIMIT 10";
    $res = $mysqli->query($sql);
    
    while ($row = $res->fetch_assoc()) {
        $ts = date('Y-m-d H:i:s', $row['created_at']);
        $callsign = htmlspecialchars($row['callsign']);
        
        // Dekodiere Base64-Daten für Anzeige
        $decodedData = base64_decode($row['message']);
        $hexData = '';
        $asciiData = '';
        
        for ($i = 0; $i < strlen($decodedData); $i++) {
            $hexData .= sprintf('%02X ', ord($decodedData[$i]));
            $asciiData .= (ord($decodedData[$i]) >= 32 && ord($decodedData[$i]) <= 126) ? $decodedData[$i] : '.';
        }
        
        $log .= "[$ts] [SMART-BROADCAST] [$callsign] RAW: " . trim($hexData) . "\n";
        $log .= "[$ts] [SMART-BROADCAST] [$callsign] ASCII: $asciiData\n";
        $log .= "[$ts] [SMART-BROADCAST] [$callsign] Base64: " . substr($row['message'], 0, 50) . "...\n";
    }
}

// Dann normale Messages anzeigen
if ($filter !== 'broadcasts') {
    // SQL Query je nach Filter
    switch ($filter) {
        case 'ax25':
            $sql = "SELECT sender, receiver, payload, created_at FROM messages WHERE payload LIKE 'AX25%' ORDER BY id DESC LIMIT ?";
            break;
        case 'system':
            $sql = "SELECT sender, receiver, payload, created_at FROM messages WHERE payload NOT LIKE 'AX25%' ORDER BY id DESC LIMIT ?";
            break;
        default:
            $sql = "SELECT sender, receiver, payload, created_at FROM messages ORDER BY id DESC LIMIT ?";
            break;
    }

    $stmt = $mysqli->prepare($sql);
    $stmt->bind_param("i", $limit);
    $stmt->execute();
    $res = $stmt->get_result();

    while ($row = $res->fetch_assoc()) {
        $ts = $row['created_at'];
        $sender = htmlspecialchars($row['sender']);
        $receiver = $row['receiver'] ? htmlspecialchars($row['receiver']) : "ALL";
        $payload = htmlspecialchars($row['payload']);
        
        // Spezielle Formatierung für AX25-Nachrichten
        if (strpos($payload, 'AX25 von') === 0) {
            // Extrahiere Callsign und AX25-Content
            if (preg_match('/AX25 von ([^:]+): (.+)/', $payload, $matches)) {
                $clientCall = $matches[1];
                $ax25Content = $matches[2];
                $log .= "[$ts] [AX25] [$clientCall] $ax25Content\n";
            } else {
                $log .= "[$ts] [AX25] $payload\n";
            }
        } else {
            // Standard-Formatierung für andere Nachrichten
            $log .= "[$ts] [$sender -> $receiver] $payload\n";
        }
    }
}

header("Content-Type: text/plain");
echo $log;
?>