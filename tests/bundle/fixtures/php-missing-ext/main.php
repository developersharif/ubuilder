<?php
// Unreachable: this fixture is built to fail at embed_runtime, before any
// PHP code would ever execute. The file exists so php_validate_project
// passes (it requires main.php or index.php).
echo "unreachable\n";
