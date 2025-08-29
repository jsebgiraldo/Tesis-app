# ESP-IDF build container with optional OpenThread and Ethernet support
FROM espressif/idf:latest

# Install extra tools (mosquitto-clients for local tests)
RUN apt-get update && apt-get install -y --no-install-recommends \
    mosquitto-clients tcpdump net-tools iproute2 \
 && rm -rf /var/lib/apt/lists/*

# Set workdir for mounted project
WORKDIR /project

# Default command shows idf version
CMD ["bash", "-lc", "idf.py --version && bash"]
