# ARTIK SDK Documentation

## Introduction

This documentation details the usage of the ARTIK C/C++
API developed by Samsung to speed-up application development
on ARTIK hardware modules.

This API has been developed in an object-oriented fashion,
in order to provide consistency across the different languages
and bindings supported.

The following languages are supported:
 - C
 - C++
 - node.js

## Supported platforms

The API is available and has been tested for the following hardware
platforms:
 - ARTIK 520 Development platform
 - ARTIK 530 Development platform
 - ARTIK 710 Development platform
 - ARTIK 05x Development Platforms
 - ARTIK 305 Development platform

## Modules

The API is organized by modules that each contain a set of features.
A "module manager" allows applications to get a instance of a module,
configure it, then call APIs exposed by the module.

The modules that are currently available are:
 - GPIO: module for operating on hardware digital input/output pins
 - I2C: module for communicating with onboard chips over I2C bus
 - Serial: module for communicating with external hardware over UART
 - PWM: module for generating pulse width modulated signals
 - HTTP: module for sending requests to HTTP/HTTPS servers
 - Cloud: module for calling the ARTIK cloud APIs
 - ADC: module for reading Analog inputs
 - Wi-Fi: module for managing wireless networks
 - Media: module for managing audio/video streams
 - Security: module for handling security features
 - Bluetooth: module for scanning and communicating with BT devices
 - Sensors: module for reading information from sensors
 - ZigBee: module for communicating with remote ZigBee devices
 - LWM2M: module for communicating with a remote LWM2M server as a client
 - MQTT: module for connecting to a MQTT broker as a client
