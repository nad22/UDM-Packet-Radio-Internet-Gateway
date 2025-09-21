<?php
session_start();
require_once(__DIR__."/db.php");

function is_admin() {
    return isset($_SESSION['admin_id']) && $_SESSION['admin_id'] == 1;
}

function require_admin() {
    if (!is_admin()) {
        http_response_code(403);
        echo "Forbidden";
        exit;
    }
}
?>