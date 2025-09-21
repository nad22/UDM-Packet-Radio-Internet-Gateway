<?php
$data = "wABmbSBQUkdTUlYgdG8gVURNIGN0bCBVSV4gcGlkIEYwRklOQUxfVEVTVF8yMDo1OMA=";
$decoded = base64_decode($data);
echo "Decoded data (" . strlen($decoded) . " bytes):\n";
echo $decoded . "\n";
?>