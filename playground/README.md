# 🚀 Orus Playground

A cross-platform web-based playground for experimenting with the Orus programming language. The playground provides an interactive code editor, real-time execution, and example programs to help you get started with Orus.

## Features

- **🌐 Web-based Interface**: Clean, modern editor with syntax highlighting
- **⚡ Real-time Execution**: Run Orus code instantly in your browser
- **📚 Built-in Examples**: Pre-loaded examples showcasing Orus features
- **🔧 Cross-platform**: Works on Windows, macOS, and Linux
- **📊 Performance Metrics**: Execution time tracking
- **🎨 Dark Theme**: Developer-friendly dark interface

## Quick Start

### Prerequisites

- Python 3.x
- Orus binary (automatically built if missing)

### Running the Playground

#### Option 1: Using the launcher script (Recommended)

**Linux/macOS:**
```bash
cd playground
./scripts/start.sh
```

**Windows:**
```cmd
cd playground
scripts\start.bat
```

#### Option 2: Using Make

```bash
cd playground
make run
```

#### Option 3: Manual startup

```bash
cd playground
python3 scripts/server.py 8000
```

The playground will be available at `http://localhost:8000`

## Build System

The playground includes a comprehensive build system for development and distribution:

```bash
# Build the playground
make build

# Create distribution packages
make package

# Run tests
make test

# Development server with auto-reload
make dev

# Clean build artifacts
make clean

# Show help
make help
```

## Project Structure

```
playground/
├── web/
│   └── index.html          # Main playground interface
├── scripts/
│   ├── server.py           # Python web server
│   ├── start.sh            # Unix launcher script
│   └── start.bat           # Windows launcher script
├── examples/
│   ├── hello.orus          # Hello World example
│   ├── variables.orus      # Variables and types
│   ├── arithmetic.orus     # Mathematical operations
│   └── showcase.orus       # Feature showcase
├── build/                  # Build output directory
├── dist/                   # Distribution packages
├── Makefile               # Build system
└── README.md              # This file
```

## Example Programs

The playground comes with several example programs:

### Hello World
```orus
print("Hello, World!");
print("Welcome to Orus programming language!");

let greeting = "Hello";
let name = "Orus";
print(greeting + ", " + name + "!");
```

### Variables and Arithmetic
```orus
let a = 10;
let b = 5;
let sum = a + b;
let product = a * b;

print("Sum: " + sum);
print("Product: " + product);
```

### Complex Expressions
```orus
let result = (10 + 5) * 2 - 3;
print("Result: " + result);

// Chain operations
let chain = 1 + 2 * 3 - 4 / 2;
print("Chain: " + chain);
```

## API Reference

The playground server provides a simple REST API:

### Execute Code
```http
POST /api/execute
Content-Type: application/json

{
  "code": "print(\"Hello, World!\");"
}
```

**Response:**
```json
{
  "success": true,
  "output": "Hello, World!\n",
  "error": null
}
```

## Development

### Adding New Examples

1. Create a new `.orus` file in the `examples/` directory
2. Add the example to the HTML dropdown in `web/index.html`
3. Update the JavaScript `examples` object with your new example

### Customizing the Interface

The playground interface is a single HTML file (`web/index.html`) with embedded CSS and JavaScript. You can customize:

- **Themes**: Modify the CSS variables for colors
- **Editor Features**: Add syntax highlighting, autocomplete, etc.
- **UI Layout**: Adjust the responsive layout and panels

### Server Configuration

The Python server (`scripts/server.py`) can be extended to:

- Add authentication
- Implement file saving/loading
- Add collaborative editing
- Integrate with version control

## Deployment

### Self-contained Distribution

Create a portable package:

```bash
make package
```

This creates platform-specific archives in the `dist/` directory:
- `orus-playground-windows.zip`
- `orus-playground-macos.tar.gz`
- `orus-playground-linux.tar.gz`

### Web Server Deployment

To deploy on a web server:

1. Build the playground: `make build`
2. Copy the `build/` directory to your web server
3. Configure your server to proxy `/api/execute` requests to the Python server
4. Start the Python server: `python3 scripts/server.py`

## Troubleshooting

### Common Issues

**"Orus binary not found"**
- The playground will automatically build Orus if missing
- Ensure you have a C compiler (GCC/Clang) installed
- Run `make` in the project root directory

**"Python 3 is required but not found"**
- Install Python 3.6 or later
- Ensure `python3` is in your PATH

**"Port already in use"**
- Change the port: `./scripts/start.sh 8001`
- Kill existing processes: `pkill -f server.py`

**"Failed to connect to Orus runtime"**
- Check that the Orus binary is executable
- Verify the server is running and accessible
- Look for error messages in the server console

### Performance Tips

- The playground includes a 10-second timeout for code execution
- Long-running or infinite loops will be terminated
- Use the built-in performance timing to optimize your code

## Contributing

To contribute to the playground:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test across platforms
5. Submit a pull request

Please ensure your changes work on all supported platforms (Windows, macOS, Linux).

## License

The Orus Playground is part of the Orus project and follows the same license terms.

---

**Happy coding with Orus! 🎉**

For more information about the Orus language, see the main project documentation and the [Language Guide](../docs/LANGUAGE.md).