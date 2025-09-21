<?php
require_once('db.php');

echo "=== Smart-Polling Debugging ===\n";

// 1. Check database connection
if ($mysqli->connect_error) {
    echo "ERROR: Database connection failed: " . $mysqli->connect_error . "\n";
    exit;
}
echo "1. Database connection: OK\n";

// 2. Check clients table
$result = $mysqli->query("SELECT callsign, status FROM clients WHERE callsign = 'AT1NAD'");
if ($result && $result->num_rows > 0) {
    $client = $result->fetch_assoc();
    echo "2. Client AT1NAD found - Status: " . $client['status'] . "\n";
} else {
    echo "2. ERROR: Client AT1NAD not found in database!\n";
    echo "   Creating client entry...\n";
    $stmt = $mysqli->prepare("INSERT INTO clients (callsign, status) VALUES (?, ?) ON DUPLICATE KEY UPDATE status = ?");
    $callsign = 'AT1NAD';
    $status = 1;
    $stmt->bind_param("sii", $callsign, $status, $status);
    $stmt->execute();
    echo "   Client created with status=1\n";
}

// 3. Test smart_broadcast.php
echo "3. Testing smart_broadcast.php...\n";
$_SERVER['REQUEST_METHOD'] = 'POST';
$_POST['message'] = 'DEBUG_TEST_' . date('H:i:s');

ob_start();
include('api/smart_broadcast.php');
$broadcastResult = ob_get_clean();
echo "   Broadcast result: " . $broadcastResult . "\n";

// 4. Check notifications created
$result = $mysqli->query("SELECT COUNT(*) as cnt FROM notifications WHERE callsign = 'AT1NAD' AND delivered_at IS NULL");
$row = $result->fetch_assoc();
echo "4. Pending notifications for AT1NAD: " . $row['cnt'] . "\n";

if ($row['cnt'] > 0) {
    echo "5. Testing smart_getdata.php...\n";
    $_GET['callsign'] = 'AT1NAD';
    $_SERVER['REQUEST_METHOD'] = 'GET';
    unset($_POST);
    
    ob_start();
    include('api/smart_getdata.php');
    $getdataResult = ob_get_clean();
    echo "   Getdata result: " . $getdataResult . "\n";
    
    // Parse JSON
    $data = json_decode($getdataResult, true);
    if ($data) {
        echo "6. JSON parsed successfully:\n";
        echo "   has_data: " . ($data['has_data'] ? 'true' : 'false') . "\n";
        echo "   next_poll_seconds: " . $data['next_poll_seconds'] . "\n";
        if ($data['has_data'] && !empty($data['data'])) {
            $decoded = base64_decode($data['data']);
            echo "   decoded: " . $decoded . "\n";
        }
    } else {
        echo "6. ERROR: JSON parsing failed\n";
    }
} else {
    echo "5. No notifications to test with\n";
}

echo "\n=== Debugging completed ===\n";
?>