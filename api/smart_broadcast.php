<?php
require_once __DIR__ . '/../db.php';

/**
 * Smart-Broadcast-System
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
        // Hole Server Callsign aus Konfiguration
        $serverCallsign = "PRGSRV"; // Fallback
        $stmt = $mysqli->prepare("SELECT callsign FROM server_config WHERE id = 1 LIMIT 1");
        $stmt->execute();
        $result = $stmt->get_result();
        $row = $result->fetch_assoc();
        if ($row && !empty($row['callsign'])) {
            $serverCallsign = $row['callsign'];
        }
        $stmt->close();
        
        // Smart-Polling: Erstelle KISS-Frame mit Server Callsign
        $kissFrame = "";
        
        // KISS Frame Start (wie TSTHost es macht)
        $kissFrame .= chr(0xC0); // FEND - Frame Start
        $kissFrame .= chr(0x00); // Command: Data Frame
        
        // AX25 Header - Destination CQ: 86 A2 40 40 40 40 60
        $kissFrame .= chr(0x86); // 'C' << 1 = 67*2 = 134 = 0x86
        $kissFrame .= chr(0xA2); // 'Q' << 1 = 81*2 = 162 = 0xA2  
        $kissFrame .= chr(0x40); // ' ' << 1 (padding)
        $kissFrame .= chr(0x40); // ' ' << 1 (padding)
        $kissFrame .= chr(0x40); // ' ' << 1 (padding)
        $kissFrame .= chr(0x40); // ' ' << 1 (padding)
        $kissFrame .= chr(0x60); // SSID (no end bit)
        
        // Source Address - Server Callsign
        $sourceBytes = encodeCallsignToAX25($serverCallsign, true); // true = last address
        $kissFrame .= $sourceBytes;
        
        // Control field
        $kissFrame .= chr(0x03); // UI Frame (Standard wie TSTHost UI^)
        
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
            'system' => 'Smart-Polling',
            'timestamp' => time()
        ]);
        
    } catch (Exception $e) {
        echo json_encode(['error' => $e->getMessage()]);
    }
}

/**
 * Kodiert ein Callsign für AX.25 (6 Bytes + 1 SSID Byte)
 */
function encodeCallsignToAX25($callsign, $last = false) {
    // Callsign auf 6 Zeichen padden
    $call = str_pad(substr($callsign, 0, 6), 6, ' ');
    
    $encoded = "";
    for ($i = 0; $i < 6; $i++) {
        $encoded .= chr(ord($call[$i]) << 1);
    }
    
    // SSID Byte (Standard: 0x60, last bit für Ende der Adresse)
    $ssid = 0x60;
    if ($last) {
        $ssid |= 0x01;  // End of address marker
    }
    $encoded .= chr($ssid);
    
    return $encoded;
}
?>