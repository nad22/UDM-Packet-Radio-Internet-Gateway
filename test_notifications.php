<?php
require_once('db.php');

echo "Testing notifications table...\n";

// Test insert
$result = $mysqli->query("INSERT INTO notifications (callsign, message, created_at, type) VALUES ('AT1NAD', 'dGVzdA==', " . time() . ", 'test')");
echo "Insert result: " . ($mysqli->error ? $mysqli->error : 'OK') . "\n";

// Count notifications
$result = $mysqli->query("SELECT COUNT(*) as cnt FROM notifications");
$row = $result->fetch_assoc();
echo "Total notifications: " . $row['cnt'] . "\n";

// Show recent notifications
$result = $mysqli->query("SELECT * FROM notifications ORDER BY created_at DESC LIMIT 3");
while ($row = $result->fetch_assoc()) {
    echo "Notification: ID=" . $row['id'] . ", Callsign=" . $row['callsign'] . ", Type=" . $row['type'] . "\n";
}
?>