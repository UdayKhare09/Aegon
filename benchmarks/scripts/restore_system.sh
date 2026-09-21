#!/usr/bin/env bash
# ==============================================================================
# Aegon Benchmark System Restore Script
# Restores original system baseline sysctl parameters.
# ==============================================================================

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Notice: Must be run with sudo to restore kernel sysctl settings:"
    echo "  sudo $0"
    exit 1
fi

echo "==> Restoring original kernel sysctl settings..."
sysctl -w net.core.somaxconn=4096
sysctl -w net.ipv4.tcp_max_syn_backlog=2048
sysctl -w net.ipv4.tcp_tw_reuse=2
sysctl -w net.ipv4.tcp_fin_timeout=60
sysctl -w net.core.netdev_max_backlog=1000
sysctl -w net.ipv4.ip_local_port_range="32768 60999"

echo "✓ Kernel sysctl successfully restored to baseline."
