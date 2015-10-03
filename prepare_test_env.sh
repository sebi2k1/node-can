#!/bin/sh

# Load kernel modules
sudo modprobe can
sudo modprobe can_raw
sudo modprobe vcan

# Config virtual CAN device (vcan0)
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
