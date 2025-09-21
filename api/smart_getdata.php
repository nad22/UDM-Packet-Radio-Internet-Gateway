<?php
require_once __DIR__ . '/../db.php';
require_once __DIR__ . '/broadcast_manager.php';

/**
 * Smart-Polling-API für ESP32 Clients
 * Liefert Daten + intelligente Polling-Intervall-Anweisungen
 * Automatische Broadcasts alle 30 Sekunden
 */

header('Content-Type: application/json');

// Debug: Log all requests
error_log("[" . date('Y-m-d H:i:s') . "] Smart-Polling request from: " . ($_SERVER['REMOTE_ADDR'] ?? 'unknown') . " for callsign: " . ($_GET['callsign'] ?? 'none'));

$callsign = $_GET['callsign'] ?? '';
if (empty($callsign)) {
    http_response_code(400);
    echo json_encode(['error' => 'Missing callsign parameter']);
    exit;
}

try {
    global $mysqli;
    
    // **AUTOMATISCHER 5-MINUTEN BROADCAST**
    try {
        $broadcaster = new BroadcastManager($mysqli);
        $broadcastSent = $broadcaster->checkAndSendBroadcast();
        if ($broadcastSent) {
            error_log("[AUTO-BROADCAST] 5-Minuten Broadcast gesendet");
        }
    } catch (Exception $broadcastError) {
        error_log("[AUTO-BROADCAST] Fehler: " . $broadcastError->getMessage());
    }
    
    // Authentifizierung prüfen
    $stmt = $mysqli->prepare("SELECT status FROM clients WHERE callsign = ?");
    $stmt->bind_param("s", $callsign);
    $stmt->execute();
    $result = $stmt->get_result();
    $user = $result->fetch_assoc();
    
    if (!$user) {
        http_response_code(403);
        echo json_encode(['error' => 'UNKNOWN']);
        exit;
    }
    
    if ($user['status'] != 1) {
        http_response_code(403);
        echo json_encode(['error' => 'DENY']);
        exit;
    }
    
    // Standard-Response aufbauen
    $response = [
        'data' => '',
        'next_poll_seconds' => 2.0, // Ultra-fast: 2s baseline
        'notifications_count' => 0,
        'has_data' => false,
        'timestamp' => time()
    ];
    
    // Prüfe auf neue Notifications für diesen Client
    $stmt = $mysqli->prepare("SELECT id, message, created_at FROM notifications WHERE callsign = ? AND delivered_at IS NULL ORDER BY created_at ASC LIMIT 10");
    $stmt->bind_param("s", $callsign);
    $stmt->execute();
    $result = $stmt->get_result();
    $notifications = [];
    while ($row = $result->fetch_assoc()) {
        $notifications[] = $row;
    }
    
    $response['notifications_count'] = count($notifications);
    
    if (count($notifications) > 0) {
        // Neue Notifications gefunden!
        $allData = '';
        $notificationIds = [];
        
        foreach ($notifications as $notification) {
            // Notifications sind bereits base64-kodiert in der DB
            $frameData = base64_decode($notification['message']);
            $allData .= $frameData;
            $notificationIds[] = $notification['id'];
        }
        
        $response['data'] = base64_encode($allData);
        $response['has_data'] = true;
        $response['next_poll_seconds'] = 0.5; // Ultra-fast: 0.5s after receiving data
        
        // Markiere Notifications als zugestellt
        if (!empty($notificationIds)) {
            $placeholders = str_repeat('?,', count($notificationIds) - 1) . '?';
            $stmt = $mysqli->prepare("UPDATE notifications SET delivered_at = ? WHERE id IN ($placeholders)");
            $params = array_merge([time()], $notificationIds);
            $types = 'i' . str_repeat('i', count($notificationIds));
            $stmt->bind_param($types, ...$params);
            $stmt->execute();
        }
        
        // Log successful delivery (optional)
        try {
            $logStmt = $mysqli->prepare("INSERT INTO log (callsign, direction, data, timestamp) VALUES (?, ?, ?, ?)");
            $logStmt->bind_param("ssss", $callsign, $direction, $logData, $timestamp);
            $direction = 'IN';
            $logData = "Smart-Poll delivered: " . count($notifications) . " notifications";
            $timestamp = date('Y-m-d H:i:s');
            $logStmt->execute();
        } catch (Exception $logError) {
            // Ignore log errors - not critical
        }
        
    } else {
        // Keine neuen Notifications
        
        // Prüfe ob kürzlich Broadcasts für andere Clients gesendet wurden
        $stmt = $mysqli->prepare("SELECT COUNT(*) as recent FROM notifications WHERE created_at > ? AND type = 'instant'");
        $recentTime = time() - 30;
        $stmt->bind_param("i", $recentTime);
        $stmt->execute();
        $result = $stmt->get_result();
        $recentActivity = $result->fetch_assoc()['recent'];
        
        if ($recentActivity > 0) {
            // Es gab kürzlich Aktivität -> häufiger prüfen
            $response['next_poll_seconds'] = 1;
        }
    }
    
    echo json_encode($response);
    
} catch (Exception $e) {
    http_response_code(500);
    echo json_encode([
        'error' => 'Server error: ' . $e->getMessage(),
        'next_poll_seconds' => 2,
        'has_data' => false
    ]);
}
?>