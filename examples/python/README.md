# Python Hello World Example

This is a simple Python application that demonstrates UBuilder's capability to package Python scripts as standalone executables.

## Files

- `main.py` - Main application entry point
- `ubuilder.json` - UBuilder configuration file

## Building

```bash
# From the project root
./build/ubuilder --project-dir=./examples/python-hello --runtime=python --output=python-hello-app
```

## Running

```bash
./python-hello-app
```
