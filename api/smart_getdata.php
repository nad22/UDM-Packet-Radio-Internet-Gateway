<?php
require_once __DIR__ . '/../db.php';
require_once __DIR__ . '/broadcast_manager.php';

/**
 * Smart-Polling-API für ESP32 Clients
 * Liefert Daten + intelligente Polling-Intervall-Anweisungen
 * Automatische Broadcasts alle 30 Sekunden
 */

header('Content-Type: application/json');

// Debug: Log nur bei Fehlern (nicht bei jedem Request)
// error_log("[" . date('Y-m-d H:i:s') . "] Smart-Polling request from: " . ($_SERVER['REMOTE_ADDR'] ?? 'unknown') . " for callsign: " . ($_GET['callsign'] ?? 'none'));

$callsign = $_GET['callsign'] ?? '';
if (empty($callsign)) {
    error_log("[" . date('Y-m-d H:i:s') . "] ERROR: Smart-Polling missing callsign from: " . ($_SERVER['REMOTE_ADDR'] ?? 'unknown'));
    http_response_code(400);
    echo json_encode(['error' => 'Missing callsign parameter']);
    exit;
}

try {
    global $mysqli;
    
    // **OPTIMIERT: Broadcast nur alle 30 Sekunden statt bei jedem Request**
    static $lastBroadcastCheck = 0;
    $currentTime = time();
    
    if ($currentTime - $lastBroadcastCheck >= 30) {
        try {
            $broadcaster = new BroadcastManager($mysqli);
            $broadcastSent = $broadcaster->checkAndSendBroadcast();
            if ($broadcastSent) {
                error_log("[AUTO-BROADCAST] 5-Minuten Broadcast gesendet");
            }
            $lastBroadcastCheck = $currentTime;
        } catch (Exception $broadcastError) {
            error_log("[AUTO-BROADCAST] Fehler: " . $broadcastError->getMessage());
        }
    }
    
    // OPTIMIERT: Ein einziger DB-Query für Auth + Data
    $stmt = $mysqli->prepare("
        SELECT c.status, 
               COUNT(n.id) as notification_count,
               GROUP_CONCAT(n.id ORDER BY n.created_at ASC) as notification_ids,
               GROUP_CONCAT(n.message ORDER BY n.created_at ASC SEPARATOR '|') as messages
        FROM clients c 
        LEFT JOIN notifications n ON c.callsign = n.callsign AND n.delivered_at IS NULL 
        WHERE c.callsign = ? 
        GROUP BY c.callsign, c.status
    ");
    $stmt->bind_param("s", $callsign);
    $stmt->execute();
    $result = $stmt->get_result();
    $data = $result->fetch_assoc();
    
    if (!$data) {
        http_response_code(403);
        echo json_encode(['error' => 'UNKNOWN']);
        exit;
    }
    
    if ($data['status'] != 1) {
        http_response_code(403);
        echo json_encode(['error' => 'DENY']);
        exit;
    }

    // Standard-Response aufbauen
    $response = [
        'data' => '',
        'next_poll_seconds' => 2.0, // Ultra-fast: 2s baseline
        'notifications_count' => (int)$data['notification_count'],
        'has_data' => false,
        'timestamp' => time()
    ];
    
    if ($data['notification_count'] > 0) {
        // Neue Notifications gefunden!
        $messages = explode('|', $data['messages']);
        $notificationIds = explode(',', $data['notification_ids']);
        
        $allData = '';
        foreach ($messages as $message) {
            if (!empty($message)) {
                // Messages sind bereits base64-kodiert in der DB
                $frameData = base64_decode($message);
                $allData .= $frameData;
            }
        }
        
        $response['data'] = base64_encode($allData);
        $response['has_data'] = true;
        $response['next_poll_seconds'] = 0.5; // Ultra-fast: 0.5s after receiving data
        
        // OPTIMIERT: Batch-Update aller Notifications auf einmal
        if (!empty($notificationIds) && !empty($notificationIds[0])) {
            $placeholders = str_repeat('?,', count($notificationIds) - 1) . '?';
            $stmt = $mysqli->prepare("UPDATE notifications SET delivered_at = ? WHERE id IN ($placeholders)");
            $params = array_merge([time()], $notificationIds);
            $types = 'i' . str_repeat('i', count($notificationIds));
            $stmt->bind_param($types, ...$params);
            $stmt->execute();
        }
        
    } else {
        // Keine neuen Notifications - einfache Response
        // ENTFERNT: Unnötige DB-Query für recent activity check
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