<?php

class AX25Parser {
    
    private $ax25_ctrl_codes = [
        // I-Frames
        'I' => 'Information Frame',
        
        // S-Frames  
        'RR' => 'Receive Ready',
        'RNR' => 'Receive Not Ready', 
        'REJ' => 'Reject',
        'SREJ' => 'Selective Reject',
        
        // U-Frames
        'SABM+' => 'Set Asynchronous Balanced Mode Extended',
        'DISC+' => 'Disconnect Extended', 
        'UA+' => 'Unnumbered Acknowledgement Extended',
        'DM+' => 'Disconnected Mode Extended',
        'SABM' => 'Set Asynchronous Balanced Mode',
        'DISC' => 'Disconnect',
        'UA' => 'Unnumbered Acknowledgement',
        'DM' => 'Disconnected Mode',
        'UI' => 'Unnumbered Information',
        'FRMR' => 'Frame Reject',
        'XID' => 'Exchange Identification',
        'TEST' => 'Test Frame'
    ];
    
    /**
     * Hauptfunktion: Parse AX25-Daten vom Client
     */
    public function parseClientData($rawData) {
        $result = [
            'frames' => [],
            'raw_hex' => bin2hex($rawData),
            'parsed_count' => 0
        ];
        
        // Suche nach KISS-Frames (0xC0 Start/End)
        $frames = $this->extractKissFrames($rawData);
        
        foreach ($frames as $frame) {
            $parsedFrame = $this->parseKissFrame($frame);
            if ($parsedFrame !== null) {
                $result['frames'][] = $parsedFrame;
                $result['parsed_count']++;
            }
        }
        
        return $result;
    }
    
    /**
     * Extrahiere KISS-Frames aus Rohdaten (0xC0 delimited)
     */
    private function extractKissFrames($data) {
        $frames = [];
        $len = strlen($data);
        $start = 0;
        
        while ($start < $len) {
            // Suche nach 0xC0 Start
            while ($start < $len && ord($data[$start]) !== 0xC0) {
                $start++;
            }
            if ($start >= $len) break;
            
            // Suche nach 0xC0 Ende
            $end = $start + 1;
            while ($end < $len && ord($data[$end]) !== 0xC0) {
                $end++;
            }
            if ($end >= $len) break;
            
            // Frame extrahieren (ohne die 0xC0 Delimiter)
            if ($end - $start > 2) {
                $frameData = substr($data, $start + 1, $end - $start - 1);
                $frames[] = $frameData;
            }
            
            $start = $end;
        }
        
        return $frames;
    }
    
    /**
     * Parse einen einzelnen KISS-Frame
     */
    private function parseKissFrame($kissFrame) {
        if (strlen($kissFrame) < 2) {
            return null;
        }
        
        // KISS Command Byte (erstes Byte)
        $kissCmd = ord($kissFrame[0]);
        
        // AX25 Daten (Rest nach KISS Command)
        $ax25Data = substr($kissFrame, 1);
        
        // KISS Unescaping
        $ax25Frame = $this->kissUnescape($ax25Data);
        
        return $this->parseAX25Frame($ax25Frame, $kissCmd);
    }
    
    /**
     * KISS Unescaping: 0xDB 0xDC -> 0xC0, 0xDB 0xDD -> 0xDB
     */
    private function kissUnescape($data) {
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
    
    /**
     * Parse AX25-Frame (Adresse, Control, Info)
     */
    private function parseAX25Frame($frame, $kissCmd = 0) {
        if (strlen($frame) < 15) {
            return [
                'error' => 'AX25 Frame zu kurz',
                'length' => strlen($frame),
                'hex' => bin2hex($frame)
            ];
        }
        
        // Destination Address (7 bytes)
        $destAddr = $this->decodeAX25Address(substr($frame, 0, 7));
        
        // Source Address (7 bytes) 
        $srcAddr = $this->decodeAX25Address(substr($frame, 7, 7));
        
        // Control Byte
        $control = ord($frame[14]);
        $controlDesc = $this->formatControlByte($control);
        
        // Info Field (if present)
        $infoStart = 15;
        if ($control === 0x03 && strlen($frame) >= 16) {
            $infoStart = 16; // UI-Frame with PID
        }
        
        $infoLen = strlen($frame) - $infoStart;
        $info = '';
        $infoHex = '';
        
        if ($infoLen > 0) {
            $infoData = substr($frame, $infoStart);
            $infoHex = bin2hex($infoData);
            
            // Konvertiere zu lesbarem Text
            for ($i = 0; $i < strlen($infoData); $i++) {
                $byte = ord($infoData[$i]);
                if ($byte >= 32 && $byte <= 126) {
                    $info .= chr($byte);
                } else {
                    $info .= '.';
                }
            }
        }
        
        return [
            'source' => $srcAddr,
            'destination' => $destAddr,
            'control' => $control,
            'control_desc' => $controlDesc,
            'info' => $info,
            'info_hex' => $infoHex,
            'info_length' => $infoLen,
            'frame_length' => strlen($frame),
            'frame_hex' => bin2hex($frame),
            'kiss_cmd' => $kissCmd,
            'timestamp' => date('Y-m-d H:i:s')
        ];
    }
    
    /**
     * Dekodiere AX25-Adresse (6 bytes + SSID)
     */
    private function decodeAX25Address($addrBytes) {
        if (strlen($addrBytes) < 7) {
            return 'INVALID';
        }
        
        $callsign = '';
        
        // Dekodiere Callsign (6 bytes, jeweils >> 1)
        for ($i = 0; $i < 6; $i++) {
            $char = chr((ord($addrBytes[$i]) >> 1));
            if ($char !== ' ') {
                $callsign .= $char;
            }
        }
        
        // SSID (7. Byte)
        $ssidByte = ord($addrBytes[6]);
        $ssid = ($ssidByte >> 1) & 0x0F;
        
        if ($ssid > 0) {
            $callsign .= '-' . $ssid;
        }
        
        return trim($callsign);
    }
    
    /**
     * Formatiere Control Byte zu lesbarer Beschreibung
     */
    private function formatControlByte($ctrl) {
        // I-Frame: LSB ist 0
        if (($ctrl & 0x01) === 0) {
            $ns = ($ctrl >> 1) & 0x07;
            $nr = ($ctrl >> 5) & 0x07;
            return "I (N(S)=$ns, N(R)=$nr)";
        }
        
        // S-Frames: Bits 1-2 bestimmen Typ
        if (($ctrl & 0x03) === 1) {
            $nr = ($ctrl >> 5) & 0x07;
            switch ($ctrl & 0x0F) {
                case 0x01: return "RR (N(R)=$nr)";
                case 0x05: return "RNR (N(R)=$nr)";
                case 0x09: return "REJ (N(R)=$nr)"; 
                case 0x0D: return "SREJ (N(R)=$nr)";
            }
        }
        
        // U-Frames: verschiedene Kommandos
        switch ($ctrl & 0xEF) { // P/F-Bit ignorieren
            case 0x2F: return "SABM+ (Set Async Balanced Mode Ext)";
            case 0x43: return "DISC+ (Disconnect Ext)";
            case 0x63: return "UA+ (Unnumbered Ack Ext)";
            case 0x0F: return "DM+ (Disconnected Mode Ext)";
            case 0x87: return "SABM (Set Async Balanced Mode)";
            case 0x43: return "DISC (Disconnect)";
            case 0x63: return "UA (Unnumbered Ack)";
            case 0x0F: return "DM (Disconnected Mode)";
            case 0x03: return "UI (Unnumbered Information)";
            case 0x87: return "FRMR (Frame Reject)";
            case 0xAF: return "XID (Exchange ID)";
            case 0xE3: return "TEST (Test Frame)";
        }
        
        return sprintf("Unknown (0x%02X)", $ctrl);
    }
    
    /**
     * Generiere lesbare Zusammenfassung
     */
    public function generateSummary($parseResult) {
        $summary = [];
        
        if ($parseResult['parsed_count'] === 0) {
            $summary[] = "Keine AX25-Frames erkannt";
            $summary[] = "Raw Data: " . substr($parseResult['raw_hex'], 0, 100) . "...";
            return $summary;
        }
        
        foreach ($parseResult['frames'] as $frame) {
            if (isset($frame['error'])) {
                $summary[] = "ERROR: " . $frame['error'];
                continue;
            }
            
            $line = $frame['source'] . " > " . $frame['destination'];
            
            if (!empty($frame['info'])) {
                $line .= ": " . $frame['info'];
            } else {
                $line .= " [" . $frame['control_desc'] . "]";
            }
            
            $summary[] = $line;
        }
        
        return $summary;
    }
}

?>