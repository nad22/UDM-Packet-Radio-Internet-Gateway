<?php
require_once('db.php');

echo "=== Database State ===\n";

// Check recent notifications
$result = $mysqli->query("SELECT id, callsign, created_at, delivered_at, LEFT(message, 50) as message_preview FROM notifications ORDER BY created_at DESC LIMIT 10");
if ($result) {
    echo "Recent notifications:\n";
    while ($row = $result->fetch_assoc()) {
        echo sprintf("ID: %d, Callsign: %s, Created: %s, Delivered: %s\n", 
            $row['id'], 
            $row['callsign'], 
            date('Y-m-d H:i:s', $row['created_at']),
            $row['delivered_at'] ? date('Y-m-d H:i:s', $row['delivered_at']) : 'NULL'
        );
        echo "  Message: " . $row['message_preview'] . "...\n";
    }
} else {
    echo "Error reading notifications: " . $mysqli->error . "\n";
}

// Check pending notifications
$result = $mysqli->query("SELECT COUNT(*) as cnt FROM notifications WHERE delivered_at IS NULL");
$row = $result->fetch_assoc();
echo "\nPending notifications: " . $row['cnt'] . "\n";

// Show active clients
$result = $mysqli->query("SELECT callsign, status FROM clients WHERE status = 1");
echo "Active clients:\n";
while ($row = $result->fetch_assoc()) {
    echo "  " . $row['callsign'] . " (status: " . $row['status'] . ")\n";
}

?>