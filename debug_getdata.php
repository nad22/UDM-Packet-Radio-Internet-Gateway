<?php
require_once('db.php');

echo "=== Smart-Getdata Debug for AT1NAD ===\n";

// Clear old notifications and create a fresh one
$mysqli->query("DELETE FROM notifications");
echo "Cleared old notifications\n";

// Create a test notification for AT1NAD
$callsign = 'AT1NAD';
$testData = base64_encode("TEST_DATA_FOR_ESP32");
$timestamp = time();

$stmt = $mysqli->prepare("INSERT INTO notifications (callsign, message, created_at, type) VALUES (?, ?, ?, 'test')");
$stmt->bind_param("ssi", $callsign, $testData, $timestamp);
$stmt->execute();
echo "Created test notification for AT1NAD\n";

// Now test smart_getdata.php
$_GET['callsign'] = 'AT1NAD';
$_SERVER['REQUEST_METHOD'] = 'GET';

echo "\n=== Testing smart_getdata.php ===\n";

try {
    ob_start();
    include('api/smart_getdata.php');
    $response = ob_get_clean();
    
    echo "Response from smart_getdata.php:\n";
    echo $response . "\n";
    
    // Parse the JSON to see what we got
    $data = json_decode($response, true);
    if ($data) {
        echo "\nParsed response:\n";
        echo "- has_data: " . ($data['has_data'] ? 'true' : 'false') . "\n";
        echo "- data length: " . strlen($data['data']) . "\n";
        echo "- next_poll_seconds: " . $data['next_poll_seconds'] . "\n";
        echo "- notifications_count: " . $data['notifications_count'] . "\n";
        
        if (!empty($data['data'])) {
            $decoded = base64_decode($data['data']);
            echo "- decoded data: " . $decoded . "\n";
        }
    }
    
    // Check if notification was marked as delivered
    $result = $mysqli->query("SELECT * FROM notifications WHERE callsign = 'AT1NAD'");
    echo "\n=== Notifications after getdata ===\n";
    while ($row = $result->fetch_assoc()) {
        $delivered = $row['delivered_at'] ? date('Y-m-d H:i:s', $row['delivered_at']) : 'NULL';
        echo "ID: " . $row['id'] . ", Delivered: " . $delivered . "\n";
    }
    
} catch (Exception $e) {
    echo "ERROR in smart_getdata.php: " . $e->getMessage() . "\n";
    echo "Trace: " . $e->getTraceAsString() . "\n";
}

echo "\n=== Debug completed ===\n";
?>