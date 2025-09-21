<?php
require_once("../db.php");

// Lösche sowohl Messages als auch Notifications
$mysqli->query("DELETE FROM messages");
$mysqli->query("DELETE FROM notifications");

echo "OK";
?>