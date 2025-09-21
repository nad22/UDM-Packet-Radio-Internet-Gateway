<?php
require_once("../session_check.php");
require_admin();
require_once("../db.php");

$username = trim($_POST["username"]);
$newpass = $_POST["newpass"] ?? "";

if ($username === "") {
    http_response_code(400); echo "Benutzername fehlt"; exit;
}

if ($newpass) {
    $hash = password_hash($newpass, PASSWORD_DEFAULT);
    $stmt = $mysqli->prepare("UPDATE admin_user SET username=?, password_hash=? WHERE id=1");
    $stmt->bind_param("ss", $username, $hash);
    $stmt->execute();
    $msg = "Admin-Benutzername und Passwort geändert!";
} else {
    $stmt = $mysqli->prepare("UPDATE admin_user SET username=? WHERE id=1");
    $stmt->bind_param("s", $username);
    $stmt->execute();
    $msg = "Admin-Benutzername geändert!";
}
// Log ins Monitor:
$stmt2 = $mysqli->prepare("INSERT INTO messages (sender, receiver, payload) VALUES ('SYSTEM', NULL, ?)");
$stmt2->bind_param("s", $msg);
$stmt2->execute();

echo "OK";
?>