<?php
require_once('db.php');

echo "=== ESP32 Data Debug Monitor ===\n";
echo "Listening for ESP32 senddata.php calls...\n\n";

// Monitor Smart-Broadcast notifications (server-generated)
echo "Smart-Broadcast Notifications (Server Generated):\n";
echo "=================================================\n";
$result = $mysqli->query("SELECT * FROM notifications ORDER BY id DESC LIMIT 5");
if ($result->num_rows > 0) {
    while ($row = $result->fetch_assoc()) {
        echo "ID: " . $row['id'] . "\n";
        echo "Callsign: " . $row['callsign'] . "\n";
        echo "Base64 Data: " . substr($row['data'], 0, 50) . "...\n";
        
        // Dekodiere Base64 und zeige HEX + ASCII
        $decodedData = base64_decode($row['data']);
        $hexData = '';
        $asciiData = '';
        for ($i = 0; $i < strlen($decodedData); $i++) {
            $hexData .= sprintf('%02X ', ord($decodedData[$i]));
            $asciiData .= (ord($decodedData[$i]) >= 32 && ord($decodedData[$i]) <= 126) ? $decodedData[$i] : '.';
        }
        echo "HEX: " . trim($hexData) . "\n";
        echo "ASCII: " . $asciiData . "\n";
        echo "Length: " . strlen($decodedData) . " bytes\n";
        echo "Created: " . $row['created_at'] . "\n";
        echo "-------------------------------------\n";
    }
} else {
    echo "No Smart-Broadcast notifications found.\n";
}

// Monitor the last few senddata.php calls (client-generated)
echo "\nRecent ESP32 senddata.php activity (Client Generated):\n";
echo "======================================================\n";
$result = $mysqli->query("SELECT * FROM messages WHERE payload LIKE '%senddata.php%' OR sender != 'SYSTEM' ORDER BY id DESC LIMIT 10");

if ($result->num_rows > 0) {
    while ($row = $result->fetch_assoc()) {
        echo "ID: " . $row['id'] . "\n";
        echo "Sender: " . $row['sender'] . "\n";
        echo "Receiver: " . $row['receiver'] . "\n";
        echo "Payload: " . $row['payload'] . "\n";
        echo "Timestamp: " . $row['created_at'] . "\n";
        echo "-------------------------------------\n";
    }
} else {
    echo "No recent ESP32 activity found.\n";
}

// Also check for any AX25 parsing results
echo "\nAX25 Parsing Results:\n";
echo "=====================\n";
$result = $mysqli->query("SELECT * FROM messages WHERE payload LIKE 'AX25%' ORDER BY id DESC LIMIT 5");
while ($row = $result->fetch_assoc()) {
    echo "AX25: " . $row['payload'] . "\n";
    echo "Time: " . $row['created_at'] . "\n";
    echo "-----\n";
}
?>