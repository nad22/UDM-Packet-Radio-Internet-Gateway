<?php

class BroadcastManager {
    private $mysqli;
    private $broadcastInterval = 300; // 5 Minuten in Sekunden  
    private $lastBroadcast = 0;
    private $serverCallsign = 'HOSTSTAR';

    public function __construct($mysqli) {
        $this->mysqli = $mysqli;
        $this->lastBroadcast = time(); // Startet mit aktueller Zeit
        $this->loadServerCallsign();
    }

    public function checkAndSendBroadcast() {
        $currentTime = time();
        
        if ($currentTime - $this->lastBroadcast >= $this->broadcastInterval) {
            $this->sendAutoBroadcast();
            $this->lastBroadcast = $currentTime;
            return true;
        } else {
            return false;
        }
    }

    private function sendAutoBroadcast() {
        try {
            $activeClients = $this->getActiveClients();
            if (empty($activeClients)) {
                // error_log("[BROADCAST] Keine aktiven Clients gefunden");
                return;
            }

            $currentTime = date('H:i');
            $messages = [
                "📻 5-Min Check $currentTime - Alle Stationen OK?",
                "🔄 Automatischer Check $currentTime - Status bitte!",
                "📡 System-Check $currentTime - Bitte antworten!"
            ];
            
            $message = $messages[array_rand($messages)];
            $this->broadcastToClients($activeClients, $message);
            
        } catch (Exception $e) {
            error_log("[BROADCAST] Fehler: " . $e->getMessage());
        }
    }

    private function broadcastToClients($clients, $message) {
        try {
            $timestamp = date('Y-m-d H:i:s');
            $stmt = $this->mysqli->prepare("INSERT INTO notifications (callsign, message, created_at) VALUES (?, ?, ?)");
            
            foreach ($clients as $callsign) {
                $stmt->bind_param("sss", $callsign, $message, $timestamp);
                $stmt->execute();
            }
            $stmt->close();
            
            error_log("[BROADCAST] Nachricht gesendet an " . count($clients) . " Clients: " . $message);
            
        } catch (Exception $e) {
            error_log("[BROADCAST] Fehler: " . $e->getMessage());
        }
    }

    private function getActiveClients() {
        try {
            $stmt = $this->mysqli->prepare("SELECT callsign FROM clients WHERE status = 1");
            $stmt->execute();
            $result = $stmt->get_result();
            
            $clients = [];
            while ($row = $result->fetch_assoc()) {
                $clients[] = $row['callsign'];
            }
            $stmt->close();
            
            return $clients;
        } catch (Exception $e) {
            error_log("[BROADCAST] Fehler beim Abrufen aktiver Clients: " . $e->getMessage());
            return [];
        }
    }

    public function queueMessageForClient($callsign, $message) {
        try {
            // Prüfe ob Client online ist
            $stmt = $this->mysqli->prepare("SELECT status FROM clients WHERE callsign = ?");
            $stmt->bind_param("s", $callsign);
            $stmt->execute();
            $result = $stmt->get_result();
            
            if ($row = $result->fetch_assoc()) {
                if ($row['status'] != 1) {
                    // Client ist offline - Log aber queue trotzdem
                    error_log("[BROADCAST] Client $callsign ist offline - Nachricht verpasst! 📻");
                }
            }
            $stmt->close();
            
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

    private function loadServerCallsign() {
        try {
            $stmt = $this->mysqli->prepare("SELECT callsign FROM server_config WHERE id = 1 LIMIT 1");
            $stmt->execute();
            $result = $stmt->get_result();
            
            if ($row = $result->fetch_assoc()) {
                $this->serverCallsign = $row['callsign'];
            }
            $stmt->close();
            
        } catch (Exception $e) {
            error_log("[BROADCAST] Fehler beim Laden des Server Callsigns: " . $e->getMessage());
        }
    }
}

?>