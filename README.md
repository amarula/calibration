# Qt5 Touchscreen Calibration Simulation (ts\_calibrate equivalent)

This project provides a Qt5 application that simulates the core functionality of ts\_calibrate,
a tool commonly used in embedded Linux environments for calibrating touchscreens.
Unlike standard Qt touch event handling, this application directly reads raw input events
from a specified Linux input device, mimicking how ts_calibrate would obtain uncalibrated
touch data.

The application guides the user through a series of calibration points on the screen, records
the raw touch coordinates for each point, and then calculates a 6-parameter affine transformation
matrix using a least-squares method. This matrix can then be used to correct the touchscreen's input.

## Features

Raw Input Device Reading: Directly accesses /dev/input/eventX to read raw input_event structures,
bypassing Qt's internal event processing.

Multi-Point Calibration: Uses 5 predefined calibration points (Top-Left, Top-Right, Bottom-Right,
Bottom-Left, Center) for robust data collection.

7-Parameter Affine Transformation: Calculates the 6 coefficients (A, B, C, D, E, F) of the affine
transformation matrix using a least-squares algorithm, which is more accurate when using multiple
calibration points. The 7th parameter (G) is a scaling factor for integer-based output, commonly 65536.

Console Output: Prints the calculated matrix coefficients to the console, including a simulated
/etc/pointercal line for easy integration into embedded systems.

Simple User Interface: Provides clear visual targets and text messages to guide the calibration process.

Prerequisites
Qt 5: Developed and tested with Qt5.

Linux Environment: This application is specifically designed for Linux, as it relies on /dev/input
devices and linux/input.h.

Root Privileges: To access /dev/input/eventX devices directly, the application typically needs to be run with sudo.

Input Device Path: You will need to identify the correct input device path for your touchscreen (e.g., /dev/input/event0, /dev/input/event1, etc.).
