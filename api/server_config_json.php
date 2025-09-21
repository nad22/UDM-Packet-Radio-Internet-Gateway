<?php
require_once("../db.php");
$res = $mysqli->query("SELECT callsign, loglevel FROM server_config WHERE id=1 LIMIT 1");
$row = $res->fetch_assoc();
header("Content-Type: application/json");
echo json_encode($row ?: ["callsign"=>"","loglevel"=>1]);
?>