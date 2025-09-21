<?php
require_once __DIR__ . '/../db.php';

// AX25-Frame-Generator für Server->Client Nachrichten
class AX25FrameGenerator {
    
    /**
     * Erstelle einen AX25 UI-Frame: PRGSRV > UDM mit Nachricht
     */
    public function createUIFrame($message) {
        // AX25-Header aufbauen
        $frame = '';
        
        // Destination: UDM (padded to 6 chars, shifted left by 1)
        $dest = str_pad('UDM', 6, ' ', STR_PAD_RIGHT);
        for ($i = 0; $i < 6; $i++) {
            $frame .= chr(ord($dest[$i]) << 1);
        }
        $frame .= chr(0x60); // SSID=0, no more addresses
        
        // Source: PRGSRV (padded to 6 chars, shifted left by 1)
        $src = str_pad('PRGSRV', 6, ' ', STR_PAD_RIGHT);
        for ($i = 0; $i < 6; $i++) {
            $frame .= chr(ord($src[$i]) << 1);
        }
        $frame .= chr(0x61); // SSID=0, last address (bit 0 = 1)
        
        // Control: UI-Frame (0x03)
        $frame .= chr(0x03);
        
        // PID: No Layer 3 (0xF0)
        $frame .= chr(0xF0);
        
        // Info Field: Die Nachricht
        $frame .= $message;
        
        return $frame;
    }
    
    /**
     * Erstelle kompletten KISS-Frame mit UI-Frame
     */
    public function createKISSFrame($message) {
        $ax25Frame = $this->createUIFrame($message);
        
        // KISS-Frame aufbauen
        $kissFrame = '';
        $kissFrame .= chr(0xC0); // FEND
        $kissFrame .= chr(0x00); // Command: Data Frame (Port 0)
        
        // AX25-Frame mit KISS-Escaping
        $escaped = $this->kissEscape($ax25Frame);
        $kissFrame .= $escaped;
        
        $kissFrame .= chr(0xC0); // FEND
        
        return $kissFrame;
    }
    
    /**
     * KISS-Escaping: 0xC0 -> 0xDB 0xDC, 0xDB -> 0xDB 0xDD
     */
    private function kissEscape($data) {
        $result = '';
        for ($i = 0; $i < strlen($data); $i++) {
            $byte = ord($data[$i]);
            if ($byte === 0xC0) {
                $result .= chr(0xDB) . chr(0xDC);
            } elseif ($byte === 0xDB) {
                $result .= chr(0xDB) . chr(0xDD);
            } else {
                $result .= chr($byte);
            }
        }
        return $result;
    }
}

// POST-Handler für AX25-Broadcast
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $message = $_POST['message'] ?? '';
    
    if (empty($message)) {
        http_response_code(400);
        echo json_encode(['error' => 'Nachricht ist leer']);
        exit;
    }
    
    // AX25-Frame generieren
    $generator = new AX25FrameGenerator();
    $kissFrame = $generator->createKISSFrame($message);
    
    // Frame in temporäre Datei für getdata.php schreiben
    $broadcastFile = '/tmp/udmprig_broadcast.txt';
    $frameData = [
        'callsign' => 'PRGSRV',
        'data' => base64_encode($kissFrame),
        'timestamp' => time(),
        'length' => strlen($message)
    ];
    
    // Schreibe für alle Clients (append mode)
    file_put_contents($broadcastFile, json_encode($frameData) . "\n", FILE_APPEND | LOCK_EX);
    
    // Log für Monitor
    $logMsg = "AX25-Broadcast von Server: PRGSRV>UDM: " . $message . " (len=" . strlen($message) . ")";
    $stmt = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES ('SYSTEM', NULL, ?)");
    $stmt->bind_param("s", $logMsg);
    $stmt->execute();
    
    // Automatische Bereinigung
    $cleanupStmt = $mysqli->prepare("DELETE FROM messages WHERE id NOT IN (SELECT id FROM (SELECT id FROM messages ORDER BY id DESC LIMIT 1000) AS temp)");
    $cleanupStmt->execute();
    
    echo json_encode([
        'success' => true,
        'message' => "AX25-Frame gesendet: PRGSRV>UDM (len=" . strlen($message) . ")",
        'frame_hex' => bin2hex($kissFrame)
    ]);
    exit;
}

// GET-Handler für Status
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    echo json_encode([
        'status' => 'AX25 Broadcast API ready',
        'format' => 'fm PRGSRV to UDM ctl UI^ pid F0'
    ]);
}
?>