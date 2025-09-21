<?php
require_once('db.php');

echo "=== Notifications Debugging ===\n";

// Check notifications table
$result = $mysqli->query("SELECT * FROM notifications ORDER BY created_at DESC LIMIT 5");
if ($result) {
    echo "Recent notifications:\n";
    while ($row = $result->fetch_assoc()) {
        echo "ID: {$row['id']}, Callsign: {$row['callsign']}, Created: " . date('Y-m-d H:i:s', $row['created_at']) . 
             ", Delivered: " . ($row['delivered_at'] ? date('Y-m-d H:i:s', $row['delivered_at']) : 'NULL') . "\n";
        echo "Message preview: " . substr($row['message'], 0, 50) . "...\n";
    }
} else {
    echo "Error querying notifications: " . $mysqli->error . "\n";
}

// Check clients table
$result = $mysqli->query("SELECT * FROM clients WHERE callsign = 'AT1NAD'");
if ($result && $result->num_rows > 0) {
    $client = $result->fetch_assoc();
    echo "\nClient AT1NAD status: " . $client['status'] . "\n";
} else {
    echo "\nClient AT1NAD not found in database!\n";
}

?>