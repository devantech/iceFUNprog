# iceFUNprog
Programmer for Devantech iCE40 modules, iceFUN and iceWerx

To build and install the programmer:

    $ make install


To program the module:

    $ iceFUNprog blinky.bin

Note: in case the programmer doesn't seem to do anything, make sure you are not accidentally
running software which tries to use your device as a modem 
(e.g. [ModemManager](https://www.freedesktop.org/wiki/Software/ModemManager/)) as this 
interferes with this programmer.
