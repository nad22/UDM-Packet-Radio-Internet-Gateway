<?php
require_once __DIR__ . '/../db.php';

try {
    global $mysqli;
    
    // Einfacher Test: Nur Text ohne AX25-Header
    $simpleMessage = "HELLO TERMINAL";
    
    // Nur die Nachricht mit einem Zeilenumbruch
    $frameContent = $simpleMessage . "\r\n";
    
    echo "Simple Message: " . $simpleMessage . "\n";
    echo "Frame Content: " . addcslashes($frameContent, "\r\n") . "\n";
    echo "Frame Length: " . strlen($frameContent) . " bytes\n";
    
    $base64Frame = base64_encode($frameContent);
    echo "Base64: " . $base64Frame . "\n";
    
    // Test: Dekodiere wieder
    $decoded = base64_decode($base64Frame);
    echo "Decoded: " . addcslashes($decoded, "\r\n") . "\n";
    
    // Schreibe in DB
    $timestamp = time();
    $stmt = $mysqli->prepare("INSERT INTO notifications (callsign, message, created_at, type) VALUES (?, ?, ?, 'simple_test')");
    $stmt->bind_param("ssi", $callsign, $base64Frame, $timestamp);
    $callsign = 'AT1NAD';
    $stmt->execute();
    
    echo "Simple Test Notification ID: " . $mysqli->insert_id . "\n";
    
} catch (Exception $e) {
    echo "Fehler: " . $e->getMessage() . "\n";
}
?>