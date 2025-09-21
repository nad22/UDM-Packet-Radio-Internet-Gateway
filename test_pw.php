<?php
require_once("db.php");
$res = $mysqli->query("SELECT password_hash FROM admin_user WHERE username='admin'");
$row = $res->fetch_assoc();
$hash = $row['password_hash'];
echo "Länge: " . strlen($hash) . "\n";
echo "Hash aus DB: '$hash'\n";
echo "Hash hex: " . bin2hex($hash) . "\n";
for ($i = 0; $i < strlen($hash); $i++) {
    echo $i . ": " . ord($hash[$i]) . "\n";
}
echo "Prüfung: " . (password_verify("geheim", $hash) ? "OK" : "FAILED") . "\n";
?>