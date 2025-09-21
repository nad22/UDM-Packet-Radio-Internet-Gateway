<?php
require_once __DIR__ . '/../db.php';

try {
    global $mysqli;
    
    // Direkte Erstellung einer korrekten Notification ohne Base64-Probleme
    $testMessage = "TEST_MSG";
    $messageLength = strlen($testMessage);
    $ax25Header = "fm PRGSRV to UDM ctl UI^ pid F0 len=" . sprintf("%03d", $messageLength);
    $frameContent = $ax25Header . "\r\n" . $testMessage . "\r\n";
    
    echo "Frame Content: " . addcslashes($frameContent, "\r\n") . "\n";
    echo "Frame Length: " . strlen($frameContent) . "\n";
    
    $base64Frame = base64_encode($frameContent);
    echo "Base64: " . $base64Frame . "\n";
    
    // Test: Dekodiere wieder
    $decoded = base64_decode($base64Frame);
    echo "Decoded: " . addcslashes($decoded, "\r\n") . "\n";
    echo "Decoded matches: " . ($decoded === $frameContent ? "YES" : "NO") . "\n";
    
    // Schreibe in DB
    $timestamp = time();
    $stmt = $mysqli->prepare("INSERT INTO notifications (callsign, message, created_at, type) VALUES (?, ?, ?, 'direct_test')");
    $stmt->bind_param("ssi", $callsign, $base64Frame, $timestamp);
    $callsign = 'AT1NAD';
    $stmt->execute();
    
    echo "Notification ID: " . $mysqli->insert_id . "\n";
    
} catch (Exception $e) {
    echo "Fehler: " . $e->getMessage() . "\n";
}
?>