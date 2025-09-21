<?php
require_once("../db.php");
$id = intval($_POST["id"]);
$callsign = $mysqli->real_escape_string($_POST["callsign"]);
$port = intval($_POST["port"]);
$status = intval($_POST["status"]);
$loglevel = intval($_POST["loglevel"]);
if ($id < 0) {
    $stmt = $mysqli->prepare("INSERT INTO clients (callsign, port, status, loglevel) VALUES (?, ?, ?, ?)");
    $stmt->bind_param("siii", $callsign, $port, $status, $loglevel);
    $stmt->execute();

    $msg = "Client hinzugefügt: Callsign='$callsign', Port=$port, Status=$status, Loglevel=$loglevel";
} else {
    $stmt = $mysqli->prepare("UPDATE clients SET callsign=?, port=?, status=?, loglevel=? WHERE id=?");
    $stmt->bind_param("siiii", $callsign, $port, $status, $loglevel, $id);
    $stmt->execute();

    $msg = "Client geändert: Callsign='$callsign', Port=$port, Status=$status, Loglevel=$loglevel";
}
$stmt2 = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES ('SYSTEM', NULL, ?)");
$stmt2->bind_param("s", $msg);
$stmt2->execute();

echo "OK";
?>