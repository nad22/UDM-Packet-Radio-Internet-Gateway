<?php
echo "=== Direct Smart-Broadcast API Test ===\n";

// Simulate calling smart_broadcast.php via HTTP POST
$_SERVER['REQUEST_METHOD'] = 'POST';
$_POST['message'] = 'DIRECT_API_TEST_' . date('H:i:s');

echo "Testing smart_broadcast.php with message: " . $_POST['message'] . "\n";

// Clear old notifications first
require_once('db.php');
$mysqli->query("DELETE FROM notifications");

// Call smart_broadcast.php
ob_start();
include('api/smart_broadcast.php');
$response = ob_get_clean();

echo "Smart-Broadcast Response:\n";
echo $response . "\n";

// Check notifications created
$result = $mysqli->query("SELECT COUNT(*) as cnt FROM notifications");
$row = $result->fetch_assoc();
echo "Notifications created: " . $row['cnt'] . "\n";

if ($row['cnt'] > 0) {
    $result = $mysqli->query("SELECT * FROM notifications ORDER BY created_at DESC LIMIT 3");
    while ($notification = $result->fetch_assoc()) {
        echo "Notification: Callsign=" . $notification['callsign'] . ", Type=" . $notification['type'] . "\n";
    }
}
?>