<?php
require_once("../session_check.php");
require_admin();
require_once("../db.php");
$res = $mysqli->query("SELECT username FROM admin_user WHERE id=1 LIMIT 1");
$row = $res->fetch_assoc();
header("Content-Type: application/json");
echo json_encode($row ?: ["username"=>""]);
?>