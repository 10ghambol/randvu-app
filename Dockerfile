FROM debian:bookworm-slim

# Install necessary build tools and SQLite dev libraries
RUN apt-get update && \
    apt-get install -y gcc make libsqlite3-dev && \
    rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy the application files
COPY . .

# Build the C executable
RUN make

# Export default port
EXPOSE 8080

# To preserve SQLite database between restarts, you should mount a volume to /app/data
# and change the server to use data/randvu.db instead. For simplicity, we run it directly here.
CMD ["./randvu_server"]
