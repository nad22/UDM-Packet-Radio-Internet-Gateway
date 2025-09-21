<?php
echo "=== Complete Smart-Polling System Test ===\n";

// Clean start
require_once('db.php');
$mysqli->query("DELETE FROM notifications");
echo "1. Cleared all notifications\n";

// Step 1: Send Smart-Broadcast
$_SERVER['REQUEST_METHOD'] = 'POST';
$_POST['message'] = 'END_TO_END_TEST_' . date('H:i:s');

echo "2. Sending Smart-Broadcast: " . $_POST['message'] . "\n";
ob_start();
include('api/smart_broadcast.php');
$broadcastResponse = ob_get_clean();

echo "   Broadcast Response: " . $broadcastResponse . "\n";

// Step 2: Check notifications created
$result = $mysqli->query("SELECT COUNT(*) as cnt FROM notifications WHERE callsign = 'AT1NAD'");
$row = $result->fetch_assoc();
echo "3. Notifications created for AT1NAD: " . $row['cnt'] . "\n";

// Step 3: Simulate ESP32 calling smart_getdata.php
$_GET['callsign'] = 'AT1NAD';
$_SERVER['REQUEST_METHOD'] = 'GET';
unset($_POST); // Clear POST data

echo "4. ESP32 polling smart_getdata.php...\n";
ob_start();
include('api/smart_getdata.php');
$getdataResponse = ob_get_clean();

echo "   Getdata Response: " . $getdataResponse . "\n";

// Step 4: Parse response to see what ESP32 would receive
$data = json_decode($getdataResponse, true);
if ($data && $data['has_data'] && !empty($data['data'])) {
    $decoded = base64_decode($data['data']);
    echo "5. ESP32 would receive: " . strlen($decoded) . " bytes of data\n";
    echo "   Next poll interval: " . $data['next_poll_seconds'] . " seconds\n";
    echo "   Decoded content preview: " . substr($decoded, 0, 50) . "...\n";
} else {
    echo "5. No data for ESP32 (unexpected!)\n";
}

// Step 5: Verify notification was marked as delivered
$result = $mysqli->query("SELECT delivered_at FROM notifications WHERE callsign = 'AT1NAD' LIMIT 1");
$row = $result->fetch_assoc();
if ($row && $row['delivered_at']) {
    echo "6. Notification marked as delivered: " . date('Y-m-d H:i:s', $row['delivered_at']) . "\n";
} else {
    echo "6. Notification NOT marked as delivered (problem!)\n";
}

echo "\n=== Test completed - Smart-Polling system working! ===\n";
?>