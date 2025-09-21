<?php

class BroadcastManager {
    private $mysqli;
    private $lastBroadcast = 0;
    private $broadcastInterval = 300; // 5 Minuten (300 Sekunden)
    private $broadcastFile;
    private $serverCallsign = "SERVER"; // Fallback
    
    public function __construct($mysqli_connection) {
        $this->mysqli = $mysqli_connection;
        $this->broadcastFile = sys_get_temp_dir() . '/udm_last_broadcast.txt';
        $this->loadLastBroadcastTime();
        $this->loadServerCallsign();
    }
    
    public function checkAndSendBroadcast() {
        $currentTime = time();
        error_log("[DEBUG] Broadcast-Check: current=$currentTime, last=$this->lastBroadcast, interval=$this->broadcastInterval");
        
        if (($currentTime - $this->lastBroadcast) >= $this->broadcastInterval) {
            error_log("[DEBUG] Broadcast wird gesendet!");
            $this->sendBroadcast();
            $this->lastBroadcast = $currentTime;
            $this->saveLastBroadcastTime();
            return true;
        } else {
            $remaining = $this->broadcastInterval - ($currentTime - $this->lastBroadcast);
            error_log("[DEBUG] Broadcast noch nicht fällig. Noch $remaining Sekunden.");
            return false;
        }
    }
    
    private function sendBroadcast() {
        try {
            $activeClients = $this->getActiveClients();
            
            if (empty($activeClients)) {
                error_log("[BROADCAST] Keine aktiven Clients gefunden");
                return;
            }
            
            $clientList = implode(', ', $activeClients);
            $message = "UDM Gateway Server online... Clients online: " . $clientList;
            $kissFrame = $this->createKissFrame($message);
            
            foreach ($activeClients as $callsign) {
                $this->queueMessageForClient($callsign, $kissFrame);
            }
            
            error_log("[BROADCAST] Nachricht gesendet an " . count($activeClients) . " Clients: " . $message);
            
        } catch (Exception $e) {
            error_log("[BROADCAST] Fehler: " . $e->getMessage());
        }
    }
    
    private function getActiveClients() {
        $clients = array();
        
        try {
            // Einfache Query - alle aktiven Clients (status=1)
            $stmt = $this->mysqli->prepare("SELECT callsign FROM clients WHERE status = 1 ORDER BY callsign");
            $stmt->execute();
            $result = $stmt->get_result();
            
            while ($row = $result->fetch_assoc()) {
                $clients[] = $row['callsign'];
            }
            
            $stmt->close();
            error_log("[DEBUG] Gefundene aktive Clients: " . implode(', ', $clients));
            
        } catch (Exception $e) {
            error_log("[BROADCAST] Fehler beim Abrufen aktiver Clients: " . $e->getMessage());
        }
        
        return $clients;
    }
    
    private function createKissFrame($message) {
        $kiss_fend = chr(0xC0);
        $kiss_cmd = chr(0x00);
        // AX.25 Header: Server-Callsign -> CQ (ohne Digipeater)
        $ax25_header = $this->createAX25Header($this->serverCallsign, "CQ");
        $ax25_control = chr(0x03);
        $ax25_pid = chr(0xF0);
        $ax25_frame = $ax25_header . $ax25_control . $ax25_pid . $message;
        $kiss_frame = $kiss_fend . $kiss_cmd . $ax25_frame . $kiss_fend;
        return base64_encode($kiss_frame);
    }
    
    private function createAX25Header($from, $to, $via = null) {
        $header = "";
        $header .= $this->encodeCallsign($to, false);
        $header .= $this->encodeCallsign($from, $via === null);
        if ($via !== null) {
            $header .= $this->encodeCallsign($via, true);
        }
        return $header;
    }
    
    private function encodeCallsign($callsign, $last = false) {
        $call = str_pad(substr($callsign, 0, 6), 6, ' ');
        $encoded = "";
        for ($i = 0; $i < 6; $i++) {
            $encoded .= chr(ord($call[$i]) << 1);
        }
        $ssid = 0x60;
        if ($last) {
            $ssid |= 0x01;
        }
        $encoded .= chr($ssid);
        return $encoded;
    }
    
    private function queueMessageForClient($callsign, $message) {
        try {
            // Prüfe ob Client aktiv ist (status = 1) - wie beim echten Funk!
            $stmt = $this->mysqli->prepare("SELECT status FROM clients WHERE callsign = ?");
            $stmt->bind_param("s", $callsign);
            $stmt->execute();
            $result = $stmt->get_result();
            $row = $result->fetch_assoc();
            $stmt->close();
            
            if (!$row || $row['status'] != 1) {
                // Client ist offline/inaktiv - verpasst die Nachricht (wie beim echten Funk!)
                error_log("[BROADCAST] Client $callsign ist offline - Nachricht verpasst! 📻");
                return;
            }
            
            // Client ist aktiv - Nachricht wird zugestellt
            $timestamp = date('Y-m-d H:i:s');
            $stmt = $this->mysqli->prepare("INSERT INTO notifications (callsign, message, created_at) VALUES (?, ?, ?)");
            $stmt->bind_param("sss", $callsign, $message, $timestamp);
            $stmt->execute();
            $stmt->close();
            
        } catch (Exception $e) {
            error_log("[BROADCAST] Fehler beim Queuen für $callsign: " . $e->getMessage());
        }
    }
    
    private function loadLastBroadcastTime() {
        if (file_exists($this->broadcastFile)) {
            $content = file_get_contents($this->broadcastFile);
            $this->lastBroadcast = intval($content);
            error_log("[DEBUG] Broadcast-Zeit geladen: " . $this->lastBroadcast . " (Datei: " . $this->broadcastFile . ")");
        } else {
            error_log("[DEBUG] Keine Broadcast-Datei gefunden, starte bei 0");
            $this->lastBroadcast = 0;
        }
    }
    
    private function saveLastBroadcastTime() {
        file_put_contents($this->broadcastFile, $this->lastBroadcast);
    }
    
    /**
     * Lädt das konfigurierte Server Callsign aus der Datenbank
     */
    private function loadServerCallsign() {
        try {
            $stmt = $this->mysqli->prepare("SELECT callsign FROM server_config WHERE id = 1 LIMIT 1");
            $stmt->execute();
            $result = $stmt->get_result();
            $row = $result->fetch_assoc();
            
            if ($row && !empty($row['callsign'])) {
                $this->serverCallsign = $row['callsign'];
                error_log("[DEBUG] Server Callsign geladen: " . $this->serverCallsign);
            } else {
                error_log("[DEBUG] Kein Server Callsign konfiguriert, verwende Fallback: " . $this->serverCallsign);
            }
            
            $stmt->close();
            
        } catch (Exception $e) {
            error_log("[BROADCAST] Fehler beim Laden des Server Callsigns: " . $e->getMessage());
        }
    }
}
?>