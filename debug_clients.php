<?php
require_once('db.php');

echo "=== Debug Clients Table ===\n";

// Prüfe ob Tabelle existiert
$tables = $mysqli->query("SHOW TABLES LIKE 'clients'");
if ($tables->num_rows == 0) {
    echo "ERROR: Table 'clients' does not exist!\n";
    echo "Creating table...\n";
    
    $createTable = "CREATE TABLE clients (
        id INT AUTO_INCREMENT PRIMARY KEY,
        callsign VARCHAR(32) NOT NULL UNIQUE,
        status TINYINT DEFAULT 1,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )";
    
    if ($mysqli->query($createTable)) {
        echo "Table 'clients' created successfully!\n";
    } else {
        echo "Error creating table: " . $mysqli->error . "\n";
        exit;
    }
}

// Zeige aktuelle Einträge
echo "\n=== Current entries in clients table ===\n";
$result = $mysqli->query("SELECT * FROM clients");
if ($result->num_rows == 0) {
    echo "No entries found!\n";
} else {
    while ($row = $result->fetch_assoc()) {
        echo "ID: " . $row['id'] . ", Callsign: '" . $row['callsign'] . "', Status: " . $row['status'] . "\n";
    }
}

// Suche speziell nach AT1NAD
echo "\n=== Searching for AT1NAD ===\n";
$stmt = $mysqli->prepare("SELECT * FROM clients WHERE callsign = ?");
$callsign = "AT1NAD";
$stmt->bind_param("s", $callsign);
$stmt->execute();
$result = $stmt->get_result();

if ($result->num_rows == 0) {
    echo "AT1NAD not found! Adding it...\n";
    $insertStmt = $mysqli->prepare("INSERT INTO clients (callsign, status) VALUES (?, 1)");
    $insertStmt->bind_param("s", $callsign);
    if ($insertStmt->execute()) {
        echo "AT1NAD added successfully!\n";
    } else {
        echo "Error adding AT1NAD: " . $mysqli->error . "\n";
    }
} else {
    $row = $result->fetch_assoc();
    echo "AT1NAD found! Status: " . $row['status'] . "\n";
    if ($row['status'] != 1) {
        echo "Status is not 1, updating...\n";
        $updateStmt = $mysqli->prepare("UPDATE clients SET status = 1 WHERE callsign = ?");
        $updateStmt->bind_param("s", $callsign);
        $updateStmt->execute();
        echo "Status updated to 1\n";
    } else {
        echo "Status is correct (1)\n";
    }
}

echo "\n=== Final verification ===\n";
$stmt = $mysqli->prepare("SELECT status FROM clients WHERE callsign = ?");
$stmt->bind_param("s", $callsign);
$stmt->execute();
$result = $stmt->get_result();
if ($row = $result->fetch_assoc()) {
    echo "AT1NAD authentication check: " . ($row['status'] == 1 ? "ALLOWED" : "DENIED") . "\n";
} else {
    echo "AT1NAD not found in final check!\n";
}
?>