<?php
echo "=== Clean Smart-Polling Test ===\n";

// Test the ESP32 endpoint directly via HTTP simulation
echo "1. Testing direct HTTP call to smart_getdata.php\n";

// Use curl to simulate real ESP32 request
$url = 'http://localhost/udm-prig/api/smart_getdata.php?callsign=AT1NAD';
$ch = curl_init();
curl_setopt($ch, CURLOPT_URL, $url);
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_FOLLOWLOCATION, true);
$response = curl_exec($ch);
$httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
curl_close($ch);

echo "   HTTP Status: " . $httpCode . "\n";
echo "   Response: " . $response . "\n";

if ($httpCode == 200) {
    $data = json_decode($response, true);
    if ($data && isset($data['has_data'])) {
        echo "2. JSON parsed successfully\n";
        echo "   Has data: " . ($data['has_data'] ? 'YES' : 'NO') . "\n";
        echo "   Next poll: " . ($data['next_poll_seconds'] ?? 'unknown') . " seconds\n";
        
        if ($data['has_data'] && !empty($data['data'])) {
            $decoded = base64_decode($data['data']);
            echo "   Data size: " . strlen($decoded) . " bytes\n";
            echo "   Content preview: " . substr($decoded, 0, 50) . "...\n";
        }
    } else {
        echo "2. JSON parsing failed or invalid format\n";
    }
} else {
    echo "2. HTTP request failed\n";
}

echo "\n=== Test completed ===\n";
?>