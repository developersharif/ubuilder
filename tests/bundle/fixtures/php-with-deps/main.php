<?php
// M1-D fixture: requires composer-installed psr/log to be present at runtime.
// If `vendor/autoload.php` is missing the require_once below fatals; if the
// composer install path is wired correctly the bundle prints a deterministic
// summary that matches expected.txt.
require_once __DIR__ . '/vendor/autoload.php';

$args = array_slice($argv, 1);
echo "ubuilder:m1d-php:hello\n";
echo "argv:" . implode(",", $args) . "\n";

$logger = new \Psr\Log\NullLogger();
echo "psr-log-nulllogger:" . ($logger instanceof \Psr\Log\LoggerInterface ? "true" : "false") . "\n";
