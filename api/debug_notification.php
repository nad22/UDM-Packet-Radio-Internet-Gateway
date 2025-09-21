<?php
require_once __DIR__ . '/../db.php';

try {
    global $mysqli;
    
    // Zeige die neueste notification
    $result = $mysqli->query("SELECT id, callsign, message, created_at FROM notifications ORDER BY id DESC LIMIT 1");
    
    if ($row = $result->fetch_assoc()) {
        echo "ID: " . $row['id'] . "\n";
        echo "Callsign: " . $row['callsign'] . "\n";
        echo "Created: " . date('Y-m-d H:i:s', $row['created_at']) . "\n";
        echo "Base64 Message: " . $row['message'] . "\n";
        
        // Dekodiere Base64
        $decoded = base64_decode($row['message']);
        echo "Decoded Message: " . $decoded . "\n";
        echo "Decoded Length: " . strlen($decoded) . "\n";
        
        // Zeige Hex-Dump
        echo "Hex Dump: " . bin2hex($decoded) . "\n";
        
    } else {
        echo "Keine notifications gefunden\n";
    }
    
} catch (Exception $e) {
    echo "Fehler: " . $e->getMessage() . "\n";
}
?>