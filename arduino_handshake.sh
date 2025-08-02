#!/bin/bash
# Arduino Handshake Script
# Proper connection with handshake mechanism

PORT=${1:-/dev/ttyACM0}
BAUDRATE=${2:-9600}

echo "Arduino Handshake Connection Script"
echo "Port: $PORT, Baudrate: $BAUDRATE"
echo "=================================="

# Function to wait for Arduino ready signal
wait_for_ready() {
    echo "Waiting for Arduino to boot and send READY signal..."
    
    timeout=10
    start_time=$(date +%s)
    
    while IFS= read -r line; do
        current_time=$(date +%s)
        elapsed=$((current_time - start_time))
        
        echo "[BOOT] $line"
        
        if [[ "$line" == "ARDUINO_READY" ]]; then
            echo "✓ Arduino is ready!"
            return 0
        fi
        
        if [ $elapsed -gt $timeout ]; then
            echo "⚠ Timeout waiting for READY signal"
            return 1
        fi
    done < <(timeout $timeout cat "$PORT")
    
    return 1
}

# Function to send command and wait for response
send_command() {
    local command="$1"
    echo "Sending: $command"
    echo "$command" > "$PORT"
    
    # Wait for response
    timeout 2 head -1 "$PORT" 2>/dev/null
}

# Main execution
main() {
    # Check if port exists
    if [ ! -e "$PORT" ]; then
        echo "Error: Port $PORT not found"
        exit 1
    fi
    
    # Set up serial port
    stty -F "$PORT" "$BAUDRATE" cs8 -cstopb -parenb -echo raw
    
    # Wait for Arduino to be ready
    if wait_for_ready; then
        echo "Connection established successfully!"
        echo ""
        echo "Testing commands:"
        echo "=================="
        
        # Test PING
        echo -n "PING test: "
        response=$(send_command "PING")
        if [[ "$response" == "PONG" ]]; then
            echo "✓ PASSED"
        else
            echo "✗ FAILED ($response)"
        fi
        
        # Test STATUS
        echo -n "STATUS test: "
        response=$(send_command "STATUS")
        if [[ -n "$response" ]]; then
            echo "✓ $response"
        else
            echo "✗ FAILED"
        fi
        
        echo ""
        echo "Entering monitor mode (Ctrl+C to exit):"
        echo "======================================="
        
        # Monitor mode
        cat "$PORT" &
        cat_pid=$!
        
        # Handle user input
        while true; do
            read -r user_input
            if [[ -n "$user_input" ]]; then
                echo "$user_input" > "$PORT"
            fi
        done
        
        # Cleanup
        kill $cat_pid 2>/dev/null
        
    else
        echo "Failed to establish proper connection"
        exit 1
    fi
}

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    kill $cat_pid 2>/dev/null
    exit 0
}

# Set up signal handlers
trap cleanup INT TERM

# Run main function
main
