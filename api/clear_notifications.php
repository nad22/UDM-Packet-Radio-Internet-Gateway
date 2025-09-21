<?php
require_once __DIR__ . '/../db.php';

try {
    global $mysqli;
    
    // Lösche alle notifications
    $result = $mysqli->query("DELETE FROM notifications");
    
    echo "Gelöscht: " . $mysqli->affected_rows . " notifications\n";
    
    // Zeige verbleibende notifications
    $result = $mysqli->query("SELECT COUNT(*) as count FROM notifications");
    $row = $result->fetch_assoc();
    echo "Verbleibende notifications: " . $row['count'] . "\n";
    
} catch (Exception $e) {
    echo "Fehler: " . $e->getMessage() . "\n";
}
?>