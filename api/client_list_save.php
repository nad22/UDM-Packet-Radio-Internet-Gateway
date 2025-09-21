<?php
require_once("../db.php");
$id = intval($_POST["id"]);
$callsign = $mysqli->real_escape_string($_POST["callsign"]);
$status = intval($_POST["status"]);
if ($id < 0) {
    $stmt = $mysqli->prepare("INSERT INTO clients (callsign, status) VALUES (?, ?)");
    $stmt->bind_param("si", $callsign, $status);
    $stmt->execute();

    $msg = "Client hinzugefügt: Callsign='$callsign', Status=$status";
} else {
    $stmt = $mysqli->prepare("UPDATE clients SET callsign=?, status=? WHERE id=?");
    $stmt->bind_param("sii", $callsign, $status, $id);
    $stmt->execute();

    $msg = "Client geändert: Callsign='$callsign', Status=$status";
}
$stmt2 = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES ('SYSTEM', NULL, ?)");
$stmt2->bind_param("s", $msg);
$stmt2->execute();

echo "OK";
?>