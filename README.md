# MIDI to DAC/shift register controller section

This sketch was created as part of the standalone voice board project, in this sketch I can output 32 channels of DAC from 0-5V to control synth parameters such as cutoff. resonance etc.

The shift register section can be used for switched options on the synth board, these outputs are all 0-5V switching, I was considering adding a 4th chip that is only powered at 3.3V for other sections

It is very low part count and uses the Seed XIAO RA4M1 board to talk to the MIDI interface and DAC's and shitf registers. Currently it only translates CC messages to CV and swicthed output.
But it would not be hard to change this to NRPN or Sysex to increase the resolution of the DAC outputs.
