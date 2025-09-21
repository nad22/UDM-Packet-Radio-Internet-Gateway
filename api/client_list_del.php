<?php
require_once("../db.php");
$id = intval($_GET["id"]);
if ($id > 0) {
    $callsign = "";
    $res = $mysqli->query("SELECT callsign FROM clients WHERE id=$id LIMIT 1");
    if ($row = $res->fetch_assoc()) $callsign = $row["callsign"];
    $mysqli->query("DELETE FROM clients WHERE id=$id");

    $msg = "Client gelöscht: Callsign='$callsign' (id=$id)";
    $stmt = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES ('SYSTEM', NULL, ?)");
    $stmt->bind_param("s", $msg);
    $stmt->execute();
}
echo "OK";
?>