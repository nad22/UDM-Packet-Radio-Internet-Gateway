<?php
$jsonResponse = '{"data":"Zm0gUFJHU1JWIHRvIFVETSBjdGwgVUleIHBpZCBGMERFQlVHX1RFU1RfMjE6MTY6MTI=","next_poll_seconds":2,"notifications_count":1,"has_data":true,"timestamp":1758395772}';

$data = json_decode($jsonResponse, true);
if ($data) {
    echo "JSON parsing successful:\n";
    echo "has_data: " . ($data['has_data'] ? 'true' : 'false') . "\n";
    echo "next_poll_seconds: " . $data['next_poll_seconds'] . "\n";
    if ($data['has_data'] && !empty($data['data'])) {
        $decoded = base64_decode($data['data']);
        echo "decoded: " . $decoded . "\n";
    }
} else {
    echo "JSON parsing failed\n";
}
?>