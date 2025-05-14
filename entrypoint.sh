#!/bin/bash
set -e

# Start Redis server
echo "entrypoint.sh : Starting Redis server..."
redis-server /etc/redis/redis.conf --daemonize yes || {
    echo "Failed to start Redis server"
    exit 1
}

# Wait for Redis to be ready
echo "entrypoint.sh : Waiting for Redis to be ready..."
until redis-cli -h localhost -p 6379 ping 2>/dev/null; do
    echo "Redis is not ready - sleeping 1s"
    sleep 1
done
echo "entrypoint.sh : Redis is ready!"

# Execute the main application
echo "entrypoint.sh : Starting main()..."
exec /app/build/backendify "$@"