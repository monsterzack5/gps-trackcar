#/bin/bash

probe-rs download --chip nRF9151_xxAA --binary-format hex ./build/merged.hex  && probe-rs reset --chip nRF9151_xxAA
