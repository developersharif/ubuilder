# UBuilder CLI Reference

## Command Syntax

```bash
ubuilder [OPTIONS]
```

## Required Arguments

### `--project-dir PATH`

**Description:** Source project directory containing your application  
**Type:** Directory path  
**Example:** `--project-dir=./my-app`

### `--runtime RUNTIME`

**Description:** Target runtime environment  
**Type:** Enum  
**Values:** `php` | `python` | `node`  
**Example:** `--runtime=php`

### `--output PATH`

**Description:** Output executable file path  
**Type:** File path  
**Example:** `--output=my-portable-app`

## Optional Arguments

### `--entry-point FILE`

**Description:** Main application file (if not auto-detected)  
**Type:** File path relative to project directory  
**Default:** Auto-detected (`main.php`, `main.py`, `main.js`, `index.*`)  
**Example:** `--entry-point=bootstrap.php`

### `--exclude PATTERN`

**Description:** Drop files or dependencies from the bundle. Repeatable; entries append to the `exclude` array in `ubuilder.json`.
**Type:** Pattern string  
**Categories accepted:**

- **File / dir glob** (matches paths in the project tree) — `*` (segment), `**` (cross `/`), `?`, `[abc]`, `[a-z]`, `[!abc]`. Leading `/` anchors to project root. Trailing `/` matches only directories. Backslashes are normalized to `/`.
- **PHP extension** — `ext-<name>` or bare `<name>`. Drops the entry from the composer-declared extension list AND passes `--ignore-platform-req=ext-<name>` to `composer install`.
- **Python wheel** — PEP-503-normalized package name. Filters `requirements.txt` line-by-line before `pip install` runs.
- **Node module** — npm package name. Drops the key from `dependencies` / `devDependencies` / `optionalDependencies` / `peerDependencies` of the staged `package.json`; if anything was dropped the staged `package-lock.json` is removed and `npm install` is used instead of `npm ci`.

**Examples:**

```bash
ubuilder --exclude='tests/**' --exclude='*.md'        # drop tests and markdown
ubuilder --exclude=ext-curl                           # drop PHP ext-curl
ubuilder --exclude=six                                # drop Python `six` wheel
ubuilder --exclude=is-number                          # drop Node `is-number`
```

Excluded deps invalidate the install-cache key, so cache hits stay correct across `--exclude` changes.

### `--verbose`

**Description:** Enable verbose output for debugging  
**Type:** Flag  
**Example:** `--verbose`

### `--help`

**Description:** Show help message  
**Type:** Flag  
**Example:** `--help`

### `--version`

**Description:** Show version information  
**Type:** Flag  
**Example:** `--version`

## Runtime-Specific Behavior

### PHP Runtime (`--runtime=php`)

**Auto-detected entry points:**

- `main.php`
- `index.php`
- First `.php` file found

**Supported features:**

- ✅ `require` and `include` statements
- ✅ Multi-file projects
- ✅ JSON configuration files
- ✅ Composer dependencies (if embedded)

**Example:**

```bash
./ubuilder --project-dir=./my-php-app --runtime=php --output=webapp
```

### Python Runtime (`--runtime=python`)

**Auto-detected entry points:**

- `main.py`
- `__main__.py`
- `app.py`

**Supported features:**

- ✅ `import` statements (local modules)
- ✅ Multi-file projects with packages
- ✅ Resource files
- ❌ External pip packages (must be embedded separately)

**Example:**

```bash
./ubuilder --project-dir=./my-python-app --runtime=python --output=pyapp
```

### Node.js Runtime (`--runtime=node`)

**Auto-detected entry points:**

- `main.js`
- `index.js`
- Entry point from `package.json`

**Supported features:**

- ✅ `require()` statements (local modules)
- ✅ Multi-file projects
- ✅ JSON configuration files
- ❌ NPM packages (must be embedded separately)

**Example:**

```bash
./ubuilder --project-dir=./my-node-app --runtime=node --output=nodeapp
```

## Complete Examples

### Basic Usage

```bash
# Minimal PHP application
./ubuilder --project-dir=./hello-world --runtime=php --output=hello

# Python GUI application
./ubuilder --project-dir=./gui-app --runtime=python --output=myapp

# Node.js web server
./ubuilder --project-dir=./server --runtime=node --output=webserver
```

### Advanced Usage

```bash
# Custom entry point with verbose output
./ubuilder \
  --project-dir=./complex-app \
  --runtime=php \
  --entry-point=bootstrap.php \
  --output=app \
  --verbose

# Build multiple versions
./ubuilder --project-dir=./app --runtime=php --output=app-php
./ubuilder --project-dir=./app --runtime=python --output=app-python
./ubuilder --project-dir=./app --runtime=node --output=app-node
```

## Exit Codes

| Code | Meaning           | Description                                  |
| ---- | ----------------- | -------------------------------------------- |
| `0`  | Success           | Executable created successfully              |
| `1`  | Invalid arguments | Missing required arguments or invalid values |
| `2`  | Project not found | Project directory doesn't exist              |
| `3`  | Runtime not found | Target runtime not installed on system       |
| `4`  | Build failed      | Error during executable creation             |
| `5`  | Permission denied | Cannot write to output location              |

## Environment Variables

### `UBUILDER_TEMP_DIR`

**Description:** Custom temporary directory for build process  
**Default:** `/tmp` (Unix) or `%TEMP%` (Windows)  
**Example:** `export UBUILDER_TEMP_DIR=/custom/tmp`

### `UBUILDER_VERBOSE`

**Description:** Enable verbose output globally  
**Values:** `1` or `true`  
**Example:** `export UBUILDER_VERBOSE=1`

## Configuration Files

UBuilder supports optional configuration files for complex projects:

### `.ubuilder.json`

Place in your project root for default settings:

```json
{
  "runtime": "php",
  "entry_point": "app/bootstrap.php",
  "exclude": ["tests/", "docs/", "*.tmp"],
  "compress": true
}
```

## Output Information

### Build Process Output

```
Building executable for runtime: 1
Project directory: ./examples/php-hello
Output path: my-app
Using PHP runtime builder (embeds full PHP interpreter)
Estimated runtime size: 6.0 MB
Embedding PHP runtime: /usr/bin/php8.4
PHP version: PHP 8.4.5 (cli)
Binary size: 5.79 MB
Successfully created modular PHP executable: my-app
Successfully created executable: my-app
```

### Verbose Output

When `--verbose` is enabled, additional information is shown:

- File discovery and processing
- Runtime detection details
- Embedding progress
- Debug information

## Integration Examples

### Build Scripts

```bash
#!/bin/bash
# build.sh
set -e

echo "Building UBuilder applications..."

# Build for all runtimes
./ubuilder --project-dir=./src --runtime=php --output=dist/app-php
./ubuilder --project-dir=./src --runtime=python --output=dist/app-python
./ubuilder --project-dir=./src --runtime=node --output=dist/app-node

echo "Build complete!"
ls -lh dist/
```

### Makefile Integration

```makefile
# Makefile
.PHONY: build clean

UBUILDER := ./build/src/ubuilder
PROJECT_DIR := ./src
OUTPUT_DIR := ./dist

build:
	mkdir -p $(OUTPUT_DIR)
	$(UBUILDER) --project-dir=$(PROJECT_DIR) --runtime=php --output=$(OUTPUT_DIR)/app-php
	$(UBUILDER) --project-dir=$(PROJECT_DIR) --runtime=python --output=$(OUTPUT_DIR)/app-python
	$(UBUILDER) --project-dir=$(PROJECT_DIR) --runtime=node --output=$(OUTPUT_DIR)/app-node

clean:
	rm -rf $(OUTPUT_DIR)
```

### CI/CD Integration

```yaml
# .github/workflows/build.yml
name: Build UBuilder Apps
on: [push]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install cmake build-essential php-cli python3 nodejs

      - name: Build UBuilder
        run: |
          mkdir build && cd build
          cmake .. && make

      - name: Create executables
        run: |
          ./build/src/ubuilder --project-dir=./examples/php-hello --runtime=php --output=hello-php
          ./build/src/ubuilder --project-dir=./examples/python-hello --runtime=python --output=hello-python
          ./build/src/ubuilder --project-dir=./examples/node-hello --runtime=node --output=hello-node

      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: ubuilder-apps
          path: hello-*
```
