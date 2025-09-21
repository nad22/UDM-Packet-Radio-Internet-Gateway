<?php
require_once('db.php');

echo "=== Smart-Broadcast Debug ===\n";

// Prüfe notifications table
$tables = $mysqli->query("SHOW TABLES LIKE 'notifications'");
if ($tables->num_rows == 0) {
    echo "ERROR: Table 'notifications' does not exist!\n";
    
    // Erstelle notifications-Tabelle
    $createTable = "CREATE TABLE IF NOT EXISTS notifications (
        id INTEGER PRIMARY KEY AUTO_INCREMENT,
        callsign VARCHAR(255),
        message TEXT,
        created_at INTEGER,
        delivered_at INTEGER DEFAULT NULL,
        type VARCHAR(50) DEFAULT 'broadcast'
    )";
    
    if ($mysqli->query($createTable)) {
        echo "Table 'notifications' created successfully!\n";
    } else {
        echo "Error creating table: " . $mysqli->error . "\n";
        exit;
    }
} else {
    echo "Table 'notifications' exists!\n";
}

// Zeige aktuelle Notifications
echo "\n=== Current Notifications ===\n";
$result = $mysqli->query("SELECT * FROM notifications ORDER BY created_at DESC LIMIT 10");
if ($result->num_rows == 0) {
    echo "No notifications found!\n";
} else {
    while ($row = $result->fetch_assoc()) {
        $delivered = $row['delivered_at'] ? date('Y-m-d H:i:s', $row['delivered_at']) : 'NULL';
        echo "ID: " . $row['id'] . ", Callsign: '" . $row['callsign'] . "', Created: " . date('Y-m-d H:i:s', $row['created_at']) . ", Delivered: " . $delivered . ", Type: " . $row['type'] . "\n";
    }
}

// Test Smart-Broadcast API direkt
echo "\n=== Testing Smart-Broadcast API ===\n";
$testMessage = "TEST " . date('H:i:s');

// Simulate POST request
$_SERVER['REQUEST_METHOD'] = 'POST';
$_POST['message'] = $testMessage;

echo "Sending test message: '$testMessage'\n";

// Include and execute smart_broadcast.php
ob_start();
include('api/smart_broadcast.php');
$response = ob_get_clean();

echo "Smart-Broadcast Response: $response\n";

// Prüfe erstellte Notifications
echo "\n=== Notifications after broadcast ===\n";
$result = $mysqli->query("SELECT * FROM notifications WHERE message LIKE '%$testMessage%' ORDER BY created_at DESC LIMIT 5");
if ($result->num_rows == 0) {
    echo "No new notifications found!\n";
} else {
    while ($row = $result->fetch_assoc()) {
        $delivered = $row['delivered_at'] ? date('Y-m-d H:i:s', $row['delivered_at']) : 'NULL';
        echo "ID: " . $row['id'] . ", Callsign: '" . $row['callsign'] . "', Created: " . date('Y-m-d H:i:s', $row['created_at']) . ", Delivered: " . $delivered . "\n";
    }
}

// Test Smart-Getdata für AT1NAD
echo "\n=== Testing Smart-Getdata for AT1NAD ===\n";
$_GET['callsign'] = 'AT1NAD';
$_SERVER['REQUEST_METHOD'] = 'GET';

ob_start();
include('api/smart_getdata.php');
$getResponse = ob_get_clean();

echo "Smart-Getdata Response: $getResponse\n";

echo "\n=== Debug completed ===\n";
?>