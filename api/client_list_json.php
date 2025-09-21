<?php
require_once("../db.php");
$res = $mysqli->query("SELECT * FROM clients ORDER BY id ASC");
$list = [];
while ($row = $res->fetch_assoc()) $list[] = $row;
header("Content-Type: application/json");
echo json_encode($list);
?>