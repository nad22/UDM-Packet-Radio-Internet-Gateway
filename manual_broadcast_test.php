<?php
require_once('db.php');

echo "=== Manual Smart-Broadcast Test ===\n";

// Simulate POST request to smart_broadcast.php
$testMessage = "MANUAL_TEST_" . date('H:i:s');

echo "Testing Smart-Broadcast with message: '$testMessage'\n";

// Clear old notifications
$mysqli->query("DELETE FROM notifications");
echo "Cleared old notifications\n";

// Manual AX25 frame creation (simplified)
$base64Frame = base64_encode("KISS_FRAME_DATA_FOR_" . $testMessage);

// Get active clients
$stmt = $mysqli->prepare("SELECT callsign FROM clients WHERE status = 1");
$stmt->execute();
$result = $stmt->get_result();
$activeUsers = [];
while ($row = $result->fetch_assoc()) {
    $activeUsers[] = $row;
    echo "Found active client: " . $row['callsign'] . "\n";
}

// Create notifications
$timestamp = time();
$stmt = $mysqli->prepare("INSERT INTO notifications (callsign, message, created_at, type) VALUES (?, ?, ?, 'instant')");

$notificationCount = 0;
foreach ($activeUsers as $user) {
    $stmt->bind_param("ssi", $user['callsign'], $base64Frame, $timestamp);
    if ($stmt->execute()) {
        $notificationCount++;
        echo "Created notification for: " . $user['callsign'] . "\n";
    } else {
        echo "Failed to create notification for: " . $user['callsign'] . " - Error: " . $mysqli->error . "\n";
    }
}

echo "Total notifications created: $notificationCount\n";

// Verify notifications in database
$result = $mysqli->query("SELECT * FROM notifications ORDER BY created_at DESC");
echo "\n=== Notifications in database ===\n";
while ($row = $result->fetch_assoc()) {
    echo "ID: " . $row['id'] . ", Callsign: " . $row['callsign'] . ", Type: " . $row['type'] . "\n";
}

// Test smart_getdata for AT1NAD
echo "\n=== Testing smart_getdata for AT1NAD ===\n";
$stmt = $mysqli->prepare("SELECT * FROM notifications WHERE callsign = ? AND delivered_at IS NULL");
$callsign = 'AT1NAD';
$stmt->bind_param("s", $callsign);
$stmt->execute();
$result = $stmt->get_result();

$notifications = [];
while ($row = $result->fetch_assoc()) {
    $notifications[] = $row;
    echo "Found notification for AT1NAD: ID=" . $row['id'] . "\n";
}

if (count($notifications) > 0) {
    echo "AT1NAD should receive: " . count($notifications) . " notifications\n";
    
    // Simulate delivery
    foreach ($notifications as $notification) {
        echo "Notification message (base64): " . substr($notification['message'], 0, 50) . "...\n";
        echo "Decoded: " . base64_decode($notification['message']) . "\n";
    }
} else {
    echo "No pending notifications for AT1NAD!\n";
}

echo "\n=== Test completed ===\n";
?>