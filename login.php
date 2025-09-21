<?php
require_once("session_check.php");
$msg = "";

if ($_SERVER["REQUEST_METHOD"] === "POST") {
    $user = trim($_POST["user"] ?? "");
    $pass = trim($_POST["pass"] ?? "");

    require_once("db.php");
    $stmt = $mysqli->prepare("SELECT id, password_hash FROM admin_user WHERE username=? LIMIT 1");
    $stmt->bind_param("s", $user);
    $stmt->execute();
    $res = $stmt->get_result();
    if ($row = $res->fetch_assoc()) {
        $hash = trim($row['password_hash'], " \r\n\0\x0B");
        if (password_verify($pass, $hash)) {
            $_SESSION['admin_id'] = $row["id"];
            header("Location: index.php");
            exit;
        }
    }
    $msg = "Login fehlgeschlagen!";
}
?>
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Login UDMPRIG-Admin</title>
    <link href="https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css" rel="stylesheet">
    <style>
        body {padding:40px;}
        .card {max-width:400px; margin:auto;}
    </style>
</head>
<body>
<div class="card">
    <div class="card-content">
        <span class="card-title">Admin Login</span>
        <?php if($msg): ?><div class="red-text" style="margin-bottom:1em;"><?php echo $msg; ?></div><?php endif; ?>
        <form method="post" autocomplete="off">
            <div class="input-field">
                <input type="text" name="user" id="user" required autocomplete="username">
                <label for="user" class="active">Benutzername</label>
            </div>
            <div class="input-field">
                <input type="password" name="pass" id="pass" required autocomplete="current-password">
                <label for="pass" class="active">Passwort</label>
            </div>
            <button class="btn teal" type="submit">Login</button>
        </form>
    </div>
</div>
<script src="https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js"></script>
<script>
document.addEventListener('DOMContentLoaded', function() {
    M.updateTextFields();
});
</script>
</body>
</html>