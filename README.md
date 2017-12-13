# ModMidi

ModMidi is a program that acts as an interface between the Mod Duo and the Behringer FCB1010. Here's what it does:

* Footswitches 1-5 switch between pedalboard presets
* Footswitches 6-10 load pedalboards from the current bank
* Switch 1 (Up) cycles through sets of 5 pedalboards on 6-10, if there are more than 5 pedalboards in the current bank
* Switch 2 (Down) is tap tempo
* The pedal lights & numeric display reflect the current state
* Everything stays in sync with the Mod Duo

Here's what you need:

* Mod Duo with my customized version of mod-ui (https://github.com/cdstouter/mod-ui/tree/modmidi-support) running on it
* Behringer FCB1010 with the EurekaPROM v3.2 chip set to IO mode
* A linux system with GCC & Docker installed

To compile for local testing on Ubuntu:

    $ make

To compile for the Mod Duo:

    $ docker build docker-modmidi
    $ docker run -ti -v PATH_TO_MODMIDI_REPO:/tmp/ModMidi DOCKER_IMAGE_ID
    (inside the Docker context)
    $ cd /tmp/ModMidi
    $ make

The compiled program can be run as-is. Try `ModMidi --help` for options. To install it on the Mod:

    $ scp ModMidi modmidi.service root@modduo.local:~/
    $ ssh root@modduo.local
    (now we're in the Mod)
    $ mount -o remount,rw /
    $ mv modmidi.service /etc/systemd/system
    $ systemctl enable modmidi
    $ mount -o remount,ro /
    $ systemctl start modmidi
