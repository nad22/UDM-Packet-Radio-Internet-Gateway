<?php
$base64 = "Zm0gUFJHU1JWIHRvIFVETSBjdGwgVUleIHBpZCBGMElNTUVESUFURV9URVNUXzIxOjEwOjQy";
$decoded = base64_decode($base64);
echo "Decoded data: '" . $decoded . "'\n";
echo "Length: " . strlen($decoded) . " bytes\n";
echo "Hex: " . bin2hex($decoded) . "\n";
?>