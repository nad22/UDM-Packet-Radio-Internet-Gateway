<?php
require_once __DIR__ . '/../db.php';

/**
 * Hoststar-kompatibles Smart-Broadcast-System
 * Erstellt DB-basierte Notifications für schnelle Zustellung
 */

// Erstelle notifications-Tabelle falls nicht vorhanden  
try {
    global $mysqli;
    $mysqli->query("CREATE TABLE IF NOT EXISTS notifications (
        id INTEGER PRIMARY KEY AUTO_INCREMENT,
        callsign VARCHAR(255),
        message TEXT,
        created_at INTEGER,
        delivered_at INTEGER DEFAULT NULL,
        type VARCHAR(50) DEFAULT 'broadcast'
    )");
} catch (Exception $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Database setup failed: ' . $e->getMessage()]);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $message = $_POST['message'] ?? 'test';
    
    try {
        // Smart-Polling: Erstelle KISS-Frame exakt wie TSTHost sie sendet
        $kissFrame = "";
        
        // KISS Frame Start (wie TSTHost es macht)
        $kissFrame .= chr(0xC0); // FEND - Frame Start
        $kissFrame .= chr(0x00); // Command: Data Frame
        
        // AX25 Header - Destination (wie TSTHost): AA 88 9A 40 40 40 
        $kissFrame .= chr(0xAA); // TSTHost Destination byte 1
        $kissFrame .= chr(0x88); // TSTHost Destination byte 2
        $kissFrame .= chr(0x9A); // TSTHost Destination byte 3
        $kissFrame .= chr(0x40); // TSTHost Destination byte 4
        $kissFrame .= chr(0x40); // TSTHost Destination byte 5
        $kissFrame .= chr(0x40); // TSTHost Destination byte 6
        
        // Source Address (aus TSTHost-Frame extrahiert): E0 A0 A4 8E A6 A4 AC
        $kissFrame .= chr(0xE0); // TSTHost Source byte 1
        $kissFrame .= chr(0xA0); // TSTHost Source byte 2
        $kissFrame .= chr(0xA4); // TSTHost Source byte 3
        $kissFrame .= chr(0x8E); // TSTHost Source byte 4
        $kissFrame .= chr(0xA6); // TSTHost Source byte 5
        $kissFrame .= chr(0xA4); // TSTHost Source byte 6
        $kissFrame .= chr(0xAC); // TSTHost Source byte 7
        $kissFrame .= chr(0x61); // SSID + End bit (last address)
        
        // Control field
        $kissFrame .= chr(0x03); // UI Frame
        
        // Protocol ID  
        $kissFrame .= chr(0xF0); // No Layer 3 protocol
        
        // Information field (actual message)
        $kissFrame .= $message;
        
        // Add CR like TSTHost does
        $kissFrame .= chr(0x0D); // CR
        
        // KISS Frame End (wie TSTHost es macht)
        $kissFrame .= chr(0xC0); // FEND - Frame End
        
        // Base64-kodiere den kompletten KISS-Frame
        $frameContent = base64_encode($kissFrame);
        
        // Erstelle Notification-Einträge für alle aktiven Clients
        $stmt = $mysqli->prepare("SELECT callsign FROM clients WHERE status = 1");
        $stmt->execute();
        $result = $stmt->get_result();
        $activeUsers = [];
        while ($row = $result->fetch_assoc()) {
            $activeUsers[] = $row;
        }
        
        
        $timestamp = time();
        
        $stmt = $mysqli->prepare("INSERT INTO notifications (callsign, message, created_at, type) VALUES (?, ?, ?, 'instant')");
        
        $notificationCount = 0;
        foreach ($activeUsers as $user) {
            $stmt->bind_param("ssi", $user['callsign'], $frameContent, $timestamp);
            $stmt->execute();
            $notificationCount++;
        }
        
        // Log the broadcast (optional - skip if log table doesn't exist)
        try {
            $logStmt = $mysqli->prepare("INSERT INTO log (callsign, direction, data, timestamp) VALUES (?, ?, ?, ?)");
            $logStmt->bind_param("ssss", $logCallsign, $direction, $logData, $timestamp_str);
            $logCallsign = 'PRGSRV';
            $direction = 'OUT';
            $logData = "Smart-Broadcast: " . $message . " (" . $notificationCount . " clients)";
            $timestamp_str = date('Y-m-d H:i:s');
            $logStmt->execute();
        } catch (Exception $logError) {
            // Ignore log errors - not critical
        }
        
        echo json_encode([
            'success' => true,
            'message' => "Broadcast sent to $notificationCount clients", 
            'frame_size' => strlen($frameContent),
            'kiss_frame_size' => strlen($kissFrame),
            'timestamp' => $timestamp
        ]);
        
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['error' => $e->getMessage()]);
    }
    
} else {
    // GET: Status-Abfrage für Monitor
    try {
        $stmt = $mysqli->prepare("SELECT COUNT(*) as pending FROM notifications WHERE delivered_at IS NULL");
        $stmt->execute();
        $result = $stmt->get_result();
        $pending = $result->fetch_assoc()['pending'];
        
        $stmt = $mysqli->prepare("SELECT COUNT(*) as total FROM notifications WHERE created_at > ?");
        $oneHourAgo = time() - 3600;
        $stmt->bind_param("i", $oneHourAgo);
        $stmt->execute();
        $result = $stmt->get_result();
        $total = $result->fetch_assoc()['total'];
        
        $stmt = $mysqli->prepare("SELECT COUNT(*) as active_clients FROM clients WHERE status = 1");
        $stmt->execute();
        $result = $stmt->get_result();
        $activeClients = $result->fetch_assoc()['active_clients'];
        
        echo json_encode([
            'pending_notifications' => $pending,
            'sent_last_hour' => $total,
            'active_clients' => $activeClients,
            'system' => 'Smart-Polling (Hoststar)',
            'timestamp' => time()
        ]);
        
    } catch (Exception $e) {
        echo json_encode(['error' => $e->getMessage()]);
    }
}
?>