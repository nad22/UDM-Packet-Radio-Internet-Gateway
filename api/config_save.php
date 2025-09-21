<?php
require_once("../db.php");
$callsign = $mysqli->real_escape_string($_POST["callsign"]);
$loglevel = intval($_POST["loglevel"]);
$mysqli->query("UPDATE server_config SET callsign='$callsign', loglevel=$loglevel WHERE id=1");

$msg = "Server-Konfiguration geändert: Callsign='$callsign', Loglevel=$loglevel";
$stmt = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES ('SYSTEM', NULL, ?)");
$stmt->bind_param("s", $msg);
$stmt->execute();

echo "OK";
?>