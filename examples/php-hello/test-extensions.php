<?php
echo "Testing PHP with mbstring extension...\n";
echo "PHP version: " . phpversion() . "\n";

// Test mbstring functions
if (function_exists('mb_strlen')) {
    $test_string = "Hello 世界! 🌍";
    echo "Original string: " . $test_string . "\n";
    echo "String length (mb_strlen): " . mb_strlen($test_string) . "\n";
    echo "String length (strlen): " . strlen($test_string) . "\n";
    echo "mbstring extension is working! ✅\n";
} else {
    echo "mbstring extension is NOT available ❌\n";
}

// Test other common functions
if (function_exists('json_encode')) {
    $data = array('message' => 'JSON works!', 'status' => 'ok');
    echo "JSON test: " . json_encode($data) . "\n";
} else {
    echo "JSON extension is NOT available ❌\n";
}

echo "Test completed.\n";
