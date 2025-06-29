#!/usr/bin/env python3
"""
Orus Playground Server
A simple HTTP server that serves the playground interface and executes Orus code.
"""

import json
import os
import subprocess
import sys
import tempfile
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import parse_qs, urlparse
import threading
import time

class OrusPlaygroundHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        # Set the directory to serve files from
        self.directory = os.path.join(os.path.dirname(__file__), '..', 'web')
        super().__init__(*args, directory=self.directory, **kwargs)

    def do_POST(self):
        """Handle POST requests for code execution"""
        if self.path == '/api/execute':
            self.handle_execute()
        else:
            self.send_error(404, "Not Found")

    def handle_execute(self):
        """Execute Orus code and return results"""
        try:
            # Read the request body
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            
            # Parse JSON data
            data = json.loads(post_data.decode('utf-8'))
            code = data.get('code', '').strip()
            
            if not code:
                self.send_json_response({
                    'success': False,
                    'error': 'No code provided'
                })
                return
            
            # Execute the code
            result = self.execute_orus_code(code)
            self.send_json_response(result)
            
        except json.JSONDecodeError:
            self.send_json_response({
                'success': False,
                'error': 'Invalid JSON in request'
            })
        except Exception as e:
            self.send_json_response({
                'success': False,
                'error': f'Server error: {str(e)}'
            })

    def execute_orus_code(self, code):
        """Execute Orus code using the orus binary"""
        try:
            # Find the orus binary
            orus_binary = self.find_orus_binary()
            if not orus_binary:
                return {
                    'success': False,
                    'error': 'Orus binary not found. Please build the project first.'
                }
            
            # Create a temporary file for the code
            with tempfile.NamedTemporaryFile(mode='w', suffix='.orus', delete=False) as f:
                f.write(code)
                temp_file = f.name
            
            try:
                # Execute the orus binary with the temporary file
                result = subprocess.run(
                    [orus_binary, temp_file],
                    capture_output=True,
                    text=True,
                    timeout=10  # 10 second timeout
                )
                
                return {
                    'success': result.returncode == 0,
                    'output': result.stdout,
                    'error': result.stderr if result.returncode != 0 else None
                }
                
            finally:
                # Clean up the temporary file
                try:
                    os.unlink(temp_file)
                except OSError:
                    pass
                    
        except subprocess.TimeoutExpired:
            return {
                'success': False,
                'error': 'Code execution timed out (10 seconds limit)'
            }
        except Exception as e:
            return {
                'success': False,
                'error': f'Execution error: {str(e)}'
            }

    def find_orus_binary(self):
        """Find the orus binary in the project"""
        # Get the project root (3 levels up from this script)
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.join(script_dir, '..', '..')
        
        # Possible locations for the orus binary
        possible_paths = [
            os.path.join(project_root, 'orus'),
            os.path.join(project_root, 'build', 'orus'),
            os.path.join(project_root, 'bin', 'orus'),
            'orus'  # Try system PATH
        ]
        
        for path in possible_paths:
            if os.path.isfile(path) and os.access(path, os.X_OK):
                return path
        
        # Try to find orus in PATH
        try:
            result = subprocess.run(['which', 'orus'], capture_output=True, text=True)
            if result.returncode == 0:
                return result.stdout.strip()
        except:
            pass
        
        return None

    def send_json_response(self, data):
        """Send a JSON response"""
        response = json.dumps(data).encode('utf-8')
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(response)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()
        
        self.wfile.write(response)

    def do_OPTIONS(self):
        """Handle CORS preflight requests"""
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def log_message(self, format, *args):
        """Override to provide custom logging"""
        print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {format % args}")

def main():
    """Main function to start the server"""
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    
    server_address = ('', port)
    httpd = HTTPServer(server_address, OrusPlaygroundHandler)
    
    print(f"🚀 Orus Playground Server starting on port {port}")
    print(f"   Open your browser to: http://localhost:{port}")
    print(f"   Press Ctrl+C to stop the server")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n🛑 Server stopped by user")
        httpd.server_close()

if __name__ == '__main__':
    main()