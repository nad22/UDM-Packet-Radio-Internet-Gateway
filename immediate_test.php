<?php
require_once('db.php');

echo "=== Immediate Test: Broadcast -> Poll ===\n";

// 1. Send broadcast
$_SERVER['REQUEST_METHOD'] = 'POST';
$_POST['message'] = 'IMMEDIATE_TEST_' . date('H:i:s');

echo "1. Sending: " . $_POST['message'] . "\n";
ob_start();
include('api/smart_broadcast.php');
$broadcastResult = ob_get_clean();
echo "   Broadcast result: " . $broadcastResult . "\n";

// 2. Immediate poll
$_GET['callsign'] = 'AT1NAD';
$_SERVER['REQUEST_METHOD'] = 'GET';
unset($_POST);

echo "2. Immediate ESP32 poll...\n";
ob_start();
include('api/smart_getdata.php');
$pollResult = ob_get_clean();
echo "   Poll result: " . $pollResult . "\n";

// 3. Decode what ESP32 would receive
$data = json_decode($pollResult, true);
if ($data && $data['has_data'] && !empty($data['data'])) {
    $decoded = base64_decode($data['data']);
    echo "3. ESP32 would send to RS232: '" . $decoded . "'\n";
    echo "   Length: " . strlen($decoded) . " bytes\n";
} else {
    echo "3. No data received by ESP32\n";
}

?>