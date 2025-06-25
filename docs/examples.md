# UBuilder Examples & Tutorials

## 📚 Complete Examples

### 1. Simple PHP Web API

Create a REST API that can be distributed as a single executable.

**Project Structure:**

```
php-api/
├── main.php           # Entry point
├── routes.php         # Route definitions
├── database.php       # Database connection
└── config.json        # Configuration
```

**main.php:**

```php
<?php
require './routes.php';
require './database.php';

function main() {
    $config = json_decode(file_get_contents('./config.json'), true);

    echo "Starting PHP API Server...\n";
    echo "Version: " . $config['version'] . "\n";
    echo "Port: " . $config['port'] . "\n";

    // Simple routing
    $path = $_SERVER['REQUEST_URI'] ?? '/';
    handleRoute($path);
}

if (isset($argv)) {
    main();
}
?>
```

**routes.php:**

```php
<?php
function handleRoute($path) {
    switch ($path) {
        case '/':
            echo json_encode(['message' => 'API is running']);
            break;
        case '/health':
            echo json_encode(['status' => 'healthy']);
            break;
        default:
            echo json_encode(['error' => 'Route not found']);
    }
}
?>
```

**config.json:**

```json
{
  "version": "1.0.0",
  "port": 8080,
  "database": {
    "host": "localhost",
    "name": "api_db"
  }
}
```

**Build and Run:**

```bash
# Build portable API
./ubuilder --project-dir=./php-api --runtime=php --output=api-server

# Run anywhere
./api-server
```

### 2. Python Desktop GUI Application

A cross-platform desktop app with Tkinter.

**Project Structure:**

```
python-gui/
├── main.py            # Entry point
├── gui/               # GUI modules
│   ├── __init__.py
│   ├── main_window.py
│   └── dialogs.py
├── utils/             # Utility modules
│   ├── __init__.py
│   └── file_handler.py
└── assets/            # Resources
    └── config.json
```

**main.py:**

```python
#!/usr/bin/env python3
import tkinter as tk
from gui.main_window import MainWindow

def main():
    print("Starting UBuilder GUI Application...")

    root = tk.Tk()
    root.title("UBuilder Example App")
    root.geometry("800x600")

    app = MainWindow(root)
    root.mainloop()

if __name__ == "__main__":
    main()
```

**gui/main_window.py:**

```python
import tkinter as tk
from tkinter import ttk, messagebox
import json
import os

class MainWindow:
    def __init__(self, root):
        self.root = root
        self.setup_ui()
        self.load_config()

    def setup_ui(self):
        # Main frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))

        # Title
        title_label = ttk.Label(main_frame, text="UBuilder GUI Example",
                               font=("Arial", 16, "bold"))
        title_label.grid(row=0, column=0, pady=(0, 20))

        # Content area
        content_frame = ttk.LabelFrame(main_frame, text="Application Info", padding="10")
        content_frame.grid(row=1, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))

        # Info display
        self.info_text = tk.Text(content_frame, width=60, height=20)
        self.info_text.grid(row=0, column=0, columnspan=2)

        # Buttons
        ttk.Button(content_frame, text="Show Info",
                  command=self.show_info).grid(row=1, column=0, pady=(10, 0))
        ttk.Button(content_frame, text="Clear",
                  command=self.clear_info).grid(row=1, column=1, pady=(10, 0))

    def load_config(self):
        try:
            with open('assets/config.json', 'r') as f:
                self.config = json.load(f)
        except FileNotFoundError:
            self.config = {"app_name": "UBuilder GUI", "version": "1.0.0"}

    def show_info(self):
        info = f"""
Application: {self.config.get('app_name', 'Unknown')}
Version: {self.config.get('version', 'Unknown')}
Platform: {os.name}
Python Version: {sys.version}

This application was packaged with UBuilder and runs
as a completely portable executable!

Features:
- No Python installation required
- Runs on any compatible system
- Includes all dependencies
- Single file distribution
        """
        self.info_text.delete(1.0, tk.END)
        self.info_text.insert(1.0, info.strip())

    def clear_info(self):
        self.info_text.delete(1.0, tk.END)
```

**Build and Run:**

```bash
# Build portable GUI app
./ubuilder --project-dir=./python-gui --runtime=python --output=gui-app

# Distribute and run
./gui-app
```

### 3. Node.js Command-Line Tool

A CLI utility that processes files and generates reports.

**Project Structure:**

```
node-cli/
├── main.js            # Entry point
├── lib/               # Core modules
│   ├── file-processor.js
│   ├── report-generator.js
│   └── config-loader.js
├── templates/         # Report templates
│   └── default.html
└── package.json       # Package info
```

**main.js:**

```javascript
#!/usr/bin/env node
const fs = require("fs");
const path = require("path");
const FileProcessor = require("./lib/file-processor");
const ReportGenerator = require("./lib/report-generator");

function main() {
  console.log("UBuilder Node.js CLI Tool");
  console.log("==========================");

  const args = process.argv.slice(2);

  if (args.length === 0) {
    showHelp();
    return;
  }

  const command = args[0];

  switch (command) {
    case "process":
      processFiles(args.slice(1));
      break;
    case "report":
      generateReport(args.slice(1));
      break;
    case "version":
      showVersion();
      break;
    default:
      console.log(`Unknown command: ${command}`);
      showHelp();
  }
}

function processFiles(args) {
  if (args.length === 0) {
    console.log("Error: No input files specified");
    return;
  }

  console.log("Processing files...");
  const processor = new FileProcessor();

  args.forEach((file) => {
    if (fs.existsSync(file)) {
      console.log(`Processing: ${file}`);
      processor.process(file);
    } else {
      console.log(`File not found: ${file}`);
    }
  });

  console.log("Processing complete!");
}

function generateReport(args) {
  console.log("Generating report...");
  const generator = new ReportGenerator();
  const outputFile = args[0] || "report.html";

  generator.generate(outputFile);
  console.log(`Report generated: ${outputFile}`);
}

function showVersion() {
  const packageInfo = require("./package.json");
  console.log(`Version: ${packageInfo.version}`);
  console.log(`Node.js: ${process.version}`);
  console.log("Built with UBuilder - truly portable!");
}

function showHelp() {
  console.log(`
Usage: cli-tool <command> [options]

Commands:
  process <files>    Process input files
  report [output]    Generate HTML report
  version           Show version information
  help              Show this help message

Examples:
  cli-tool process file1.txt file2.txt
  cli-tool report output.html
  cli-tool version
    `);
}

if (require.main === module) {
  main();
}
```

**lib/file-processor.js:**

```javascript
const fs = require("fs");
const path = require("path");

class FileProcessor {
  constructor() {
    this.results = [];
  }

  process(filePath) {
    try {
      const content = fs.readFileSync(filePath, "utf8");
      const stats = fs.statSync(filePath);

      const result = {
        file: filePath,
        size: stats.size,
        lines: content.split("\n").length,
        words: content.split(/\s+/).length,
        characters: content.length,
        modified: stats.mtime,
      };

      this.results.push(result);

      console.log(`  Lines: ${result.lines}`);
      console.log(`  Words: ${result.words}`);
      console.log(`  Characters: ${result.characters}`);
      console.log(`  Size: ${result.size} bytes`);
    } catch (error) {
      console.error(`Error processing ${filePath}: ${error.message}`);
    }
  }

  getResults() {
    return this.results;
  }
}

module.exports = FileProcessor;
```

**Build and Run:**

```bash
# Build portable CLI tool
./ubuilder --project-dir=./node-cli --runtime=node --output=cli-tool

# Use the tool
./cli-tool process README.md
./cli-tool report
./cli-tool version
```

## 🎯 Real-World Use Cases

### 1. Software Distribution

**Problem:** Distributing applications without runtime dependencies

```bash
# Traditional approach (requires PHP installation)
# User needs: apt-get install php-cli
php my-app.php

# UBuilder approach (zero dependencies)
./ubuilder --project-dir=./my-app --runtime=php --output=my-app-portable
# Distribute single file - works everywhere!
```

### 2. Edge Computing Deployment

**Problem:** Deploying lightweight applications to edge devices

```bash
# Build ultra-portable edge application
./ubuilder --project-dir=./edge-sensor --runtime=python --output=sensor-app

# Deploy to multiple edge devices
scp sensor-app edge1:/usr/local/bin/
scp sensor-app edge2:/usr/local/bin/
scp sensor-app edge3:/usr/local/bin/

# Run immediately (no setup required)
ssh edge1 '/usr/local/bin/sensor-app --start'
```

### 3. Container Alternative

**Problem:** Heavy container images for simple applications

```dockerfile
# Traditional Docker approach
FROM php:8.2-cli
COPY . /app
WORKDIR /app
CMD ["php", "main.php"]
# Result: ~400MB image
```

```bash
# UBuilder approach
./ubuilder --project-dir=./app --runtime=php --output=app-portable
# Result: ~6MB executable

# Use in minimal container
FROM scratch
COPY app-portable /app
CMD ["/app"]
# Result: ~6MB image
```

### 4. CI/CD Pipeline Integration

**GitHub Actions Example:**

```yaml
name: Build and Release
on:
  release:
    types: [published]

jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        runtime: [php, python, node]

    steps:
      - uses: actions/checkout@v3

      - name: Setup build environment
        run: |
          sudo apt-get update
          sudo apt-get install cmake build-essential php-cli python3 nodejs

      - name: Build UBuilder
        run: |
          mkdir build && cd build
          cmake .. && make

      - name: Create portable executable
        run: |
          ./build/src/ubuilder \
            --project-dir=./src \
            --runtime=${{ matrix.runtime }} \
            --output=myapp-${{ matrix.runtime }}

      - name: Upload to release
        uses: actions/upload-release-asset@v1
        with:
          upload_url: ${{ github.event.release.upload_url }}
          asset_path: ./myapp-${{ matrix.runtime }}
          asset_name: myapp-${{ matrix.runtime }}-${{ runner.os }}
          asset_content_type: application/octet-stream
```

## 🔧 Advanced Techniques

### Custom Build Scripts

**multi-build.sh:**

```bash
#!/bin/bash
set -e

PROJECT_DIR="$1"
OUTPUT_PREFIX="$2"

if [ -z "$PROJECT_DIR" ] || [ -z "$OUTPUT_PREFIX" ]; then
    echo "Usage: $0 <project-dir> <output-prefix>"
    exit 1
fi

echo "Building multi-runtime executables..."

# Build for all supported runtimes
for runtime in php python node; do
    echo "Building for $runtime..."
    ./ubuilder \
        --project-dir="$PROJECT_DIR" \
        --runtime="$runtime" \
        --output="${OUTPUT_PREFIX}-${runtime}" \
        --verbose

    # Test the executable
    echo "Testing $runtime executable..."
    ./"${OUTPUT_PREFIX}-${runtime}" --version || echo "Version check failed"
done

echo "Build summary:"
ls -lh "${OUTPUT_PREFIX}"-*

echo "Total size:"
du -ch "${OUTPUT_PREFIX}"-* | tail -1
```

### Performance Optimization

**Minimize executable size:**

```bash
# Use minimal project structure
project/
├── main.php          # Only essential files
└── config.json       # No development files

# Exclude unnecessary files
echo "tests/" > .ubuilderignore
echo "docs/" >> .ubuilderignore
echo "*.tmp" >> .ubuilderignore
```

**Optimize startup time:**

```bash
# Pre-compile Python bytecode
python3 -m py_compile main.py

# Use faster PHP opcache settings
echo "opcache.enable_cli=1" > php.ini
```

## 📊 Benchmarking Your Applications

### Performance Testing Script

```bash
#!/bin/bash
# benchmark.sh

APP="$1"
ITERATIONS=10

if [ -z "$APP" ]; then
    echo "Usage: $0 <executable>"
    exit 1
fi

echo "Benchmarking $APP ($ITERATIONS iterations)..."

# Startup time
echo "Measuring startup time..."
for i in $(seq 1 $ITERATIONS); do
    time -p "$APP" --version 2>&1 | grep real
done | awk '{sum += $2} END {print "Average startup time:", sum/NR, "seconds"}'

# Memory usage
echo "Measuring memory usage..."
/usr/bin/time -v "$APP" 2>&1 | grep "Maximum resident set size"

# File size
echo "Executable size:"
ls -lh "$APP" | awk '{print $5}'
```

## 🎓 Best Practices

### 1. Project Organization

- Keep entry points simple
- Use clear directory structure
- Include only necessary files
- Document dependencies clearly

### 2. Runtime Compatibility

- Test with target runtime versions
- Handle version differences gracefully
- Use compatible language features

### 3. Error Handling

- Implement proper error handling
- Provide meaningful error messages
- Handle missing files gracefully

### 4. Distribution

- Test on clean systems
- Verify no external dependencies
- Include usage documentation
- Provide version information
