<?php
require_once __DIR__ . '/../db.php';

// Empfange Daten vom Client
$callsign = $_POST['callsign'] ?? '';
$data = $_POST['data'] ?? '';

// --- Callsign-Prüfung ---
function isCallsignAllowed($callsign) {
    global $mysqli;
    if (empty($callsign)) return false;
    
    $stmt = $mysqli->prepare("SELECT status FROM clients WHERE callsign = ?");
    $stmt->bind_param("s", $callsign);
    $stmt->execute();
    $result = $stmt->get_result();
    
    if ($row = $result->fetch_assoc()) {
        // Client existiert - prüfe ob Status aktiv ist (1 = aktiv)
        return $row['status'] == 1;
    }
    
    return false; // Client nicht gefunden
}

if (!isCallsignAllowed($callsign)) {
    // Log in messages Tabelle mit automatischer Bereinigung
    $msg = "Nicht autorisierter Anmeldeversuch von Callsign '$callsign' (senddata.php)";
    $stmt = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES ('SYSTEM', NULL, ?)");
    $stmt->bind_param("s", $msg);
    $stmt->execute();
    
    // Automatische Bereinigung: Behalte nur die letzten 1000 Einträge
    $cleanupStmt = $mysqli->prepare("DELETE FROM messages WHERE id NOT IN (SELECT id FROM (SELECT id FROM messages ORDER BY id DESC LIMIT 1000) AS temp)");
    $cleanupStmt->execute();
    
    http_response_code(403); // 403 Forbidden
    echo "DENY";
    exit;
}

// AX25 Parsing - exakte Portierung der alten server.ino
if (!empty($data)) {
    // Debug: Log rohe Daten vom ESP32
    $debugMsg = "ESP32 RAW DATA from $callsign: Length=" . strlen($data) . " HEX=" . bin2hex($data) . " ASCII=" . addslashes($data);
    $stmt = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES (?, ?, ?)");
    $debugSender = "ESP32_DEBUG";
    $stmt->bind_param("sss", $debugSender, $callsign, $debugMsg);
    $stmt->execute();
    
    // Globaler kontinuierlicher Buffer pro Callsign (wie rxbuf in server.ino)
    $bufferFile = "/tmp/udm_rxbuf_" . preg_replace('/[^a-zA-Z0-9]/', '_', $callsign) . ".dat";
    $countFile = "/tmp/udm_rxcnt_" . preg_replace('/[^a-zA-Z0-9]/', '_', $callsign) . ".dat";
    
    // Lade vorhandenen Buffer und Counter
    $rxbuf = '';
    $rxcnt = 0;
    if (file_exists($bufferFile)) {
        $rxbuf = file_get_contents($bufferFile);
        $rxcnt = strlen($rxbuf);
    }
    
    // Füge neue Daten zum Buffer hinzu (wie in der loop() der server.ino)
    foreach (str_split($data) as $byte) {
        if ($rxcnt < 1024) { // RX_BUF_SIZE aus server.ino
            $rxbuf .= $byte;
            $rxcnt++;
        }
    }
    
    // --- AX25-Parsing für empfangene Frames (exakt wie in server.ino) ---
    $start = 0;
    $end = 0;
    $parsed_any = false;
    
    while ($start < $rxcnt) {
        // Suche nach 0xC0 Start
        while ($start < $rxcnt && ord($rxbuf[$start]) !== 0xC0) {
            $start++;
        }
        if ($start >= $rxcnt) break;
        
        // Suche nach 0xC0 Ende
        $end = $start + 1;
        while ($end < $rxcnt && ord($rxbuf[$end]) !== 0xC0) {
            $end++;
        }
        if ($end >= $rxcnt) break;
        
        // Frame gefunden (wie in server.ino)
        if ($end - $start > 2) {
            $kiss_payload = substr($rxbuf, $start + 1, $end - $start - 1);
            $kiss_len = strlen($kiss_payload);
            
            if ($kiss_len >= 2) {
                $kiss_cmd = ord($kiss_payload[0]);
                $ax25_data = substr($kiss_payload, 1);
                $ax25_frame = kissUnescapeExact($ax25_data);
                
                // printAX25Packet equivalent
                $ax25_info = printAX25PacketPHP($ax25_frame, true, $callsign);
                if ($ax25_info !== null) {
                    $parsed_any = true;
                    
                    // Log wie in der alten server.ino
                    $stmt = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES ('SYSTEM', NULL, ?)");
                    $stmt->bind_param("s", $ax25_info);
                    $stmt->execute();
                }
            }
        }
        $start = $end;
    }
    
    // Buffer-Management (exakt wie in server.ino)
    if ($start >= $rxcnt) {
        $rxcnt = 0;
        $rxbuf = '';
    } else if ($start > 0) {
        // memmove equivalent in PHP
        $rxbuf = substr($rxbuf, $start);
        $rxcnt -= $start;
    }
    
    // Speichere Buffer zurück
    if ($rxcnt > 0) {
        file_put_contents($bufferFile, $rxbuf);
    } else {
        if (file_exists($bufferFile)) unlink($bufferFile);
    }
}

// Helper-Funktionen (exakte Portierung aus server.ino)
function kissUnescapeExact($data) {
    $result = '';
    $len = strlen($data);
    $i = 0;
    
    while ($i < $len) {
        $byte = ord($data[$i]);
        if ($byte === 0xDB && $i + 1 < $len) {
            $nextByte = ord($data[$i + 1]);
            if ($nextByte === 0xDC) {
                $result .= chr(0xC0);
            } elseif ($nextByte === 0xDD) {
                $result .= chr(0xDB);
            } else {
                $result .= chr($nextByte);
            }
            $i += 2;
        } else {
            $result .= chr($byte);
            $i++;
        }
    }
    return $result;
}

function printAX25PacketPHP($buf, $incoming, $callsign) {
    $len = strlen($buf);
    if ($len < 15) {
        return "AX25 zu kurz von $callsign (" . $len . " bytes)";
    }
    
    // decode_ax25_addr equivalent
    $dest = decodeAX25AddressExact(substr($buf, 0, 7));
    $src = decodeAX25AddressExact(substr($buf, 7, 7));
    
    $control = ord($buf[14]);
    $infoStart = 15;
    if ($control === 0x03 && $len >= 16) {
        $infoStart = 16; // UI-Frame mit PID
    }
    
    $infoLen = ($len > $infoStart) ? ($len - $infoStart) : 0;
    
    $sLine = $src . ">" . $dest . ($incoming ? ": " : ":\n");
    
    if ($infoLen === 0) {
        $sLine .= "[" . ax25CtrlFormatExact($control) . "]";
    } else {
        $info = substr($buf, $infoStart);
        for ($i = 0; $i < strlen($info); $i++) {
            $byte = ord($info[$i]);
            if ($byte >= 32 && $byte <= 126) {
                $sLine .= chr($byte);
            }
        }
    }
    
    return "AX25 von $callsign: $sLine";
}

function decodeAX25AddressExact($addrBytes) {
    if (strlen($addrBytes) < 7) return 'INVALID';
    
    $callsign = '';
    // Dekodiere Callsign (6 bytes, jeweils >> 1)
    for ($i = 0; $i < 6; $i++) {
        $char = chr((ord($addrBytes[$i]) >> 1));
        if ($char !== ' ') $callsign .= $char;
    }
    
    // SSID (7. Byte)
    $ssidByte = ord($addrBytes[6]);
    $ssid = ($ssidByte >> 1) & 0x0F;
    
    if ($ssid > 0) {
        $callsign .= '-' . $ssid;
    }
    
    return trim($callsign);
}

function ax25CtrlFormatExact($ctrl) {
    // I-Frame: LSB ist 0
    if (($ctrl & 0x01) === 0) {
        return "I (Information Frame)";
    }
    
    // S-Frames und U-Frames (vereinfacht)
    switch ($ctrl & 0xEF) {
        case 0x2F: return "SABM+";
        case 0x43: return "DISC+";
        case 0x63: return "UA+";
        case 0x0F: return "DM+";
        case 0x03: return "UI";
        case 0x13: return "UI";
        default: return sprintf("0x%02X", $ctrl);
    }
}

// Automatische Bereinigung: Behalte nur die letzten 1000 Einträge
$cleanupStmt = $mysqli->prepare("DELETE FROM messages WHERE id NOT IN (SELECT id FROM (SELECT id FROM messages ORDER BY id DESC LIMIT 1000) AS temp)");
$cleanupStmt->execute();

// Schreibe die empfangenen Daten in eine temporäre Datei
file_put_contents('/tmp/udmprig_buffer.txt', json_encode([
    'callsign' => $callsign,
    'data' => $data,
    'timestamp' => time()
]) . "\n", FILE_APPEND);

echo "OK";
?>