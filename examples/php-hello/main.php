<?php
require './sub.php';
function main()
{
    echo "Hello from UBuilder PHP Application!\n";
    echo "This application was packaged as a single executable.\n";

    echo "PHP version: " . phpversion() . "\n";

    global $argv;
    echo "Arguments: " . implode(" ", $argv) . "\n";
}

if (isset($argv)) {
    main();
}
