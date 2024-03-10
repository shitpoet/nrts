#!/bin/bash

# Get the process IDs of all running processes
pids=$(ps -e -o pid --no-header)

# Loop through each process ID
for pid in $pids; do
  # Execute your bash command for each process
  #echo "Executing command for process ID: $pid"
  sudo taskset -a -p 0x03 $pid
  #taskset -p $pid
  # Replace the following line with your desired command
  # Example: kill $pid
done
