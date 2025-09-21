<?php
require_once("session_check.php");
session_destroy();
header("Location: login.php");
exit;
?>