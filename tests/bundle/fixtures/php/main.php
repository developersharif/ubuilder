<?php
$args = array_slice($argv, 1);
echo "ubuilder:php:hello\n";
echo "argv:" . implode(",", $args) . "\n";
$sum = 0;
for ($i = 1; $i <= 10; $i++) { $sum += $i; }
echo "sum:" . $sum . "\n";
