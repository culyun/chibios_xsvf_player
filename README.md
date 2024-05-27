# chibios_xsvf_player
Blackpill STM32F401 with USB as XSVF Player

A STM32F401 "Blackpill" Board as XSVF Programmer for CPLDs (Tested with XC9572).
Python3 Program as PC-Client, USB CDC-ACM on STM32 as Receiver. 

Debug Port on PA2, PA3 with 115200 Bd.
Protocol is a modified Moates Ostrich Protocol.

Firmware is based on ChibiOS 20 (trunk).

Upload Time for an XC9572 is about 14s vs. 16s with the Platform Cable USB (DLC10) and xc3sprog.