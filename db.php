<?php
// Datenbankverbindung - bitte ggf. Benutzer/Passwort anpassen!
$host = "localhost";
$user = "root";
$pass = "";
$dbname = "udm-prig";

$mysqli = new mysqli($host, $user, $pass, $dbname);
$mysqli->set_charset("utf8");

if ($mysqli->connect_error) {
    die("DB-Verbindung fehlgeschlagen: " . $mysqli->connect_error);
}
$mysqli->set_charset("utf8mb4");
?>