# bmmsc4kg2_control

project stage: proof of concept. stay tuned

This project aims to ease the pain of controlling BlackMagic Micro Studio Camera 4K G2 using only
builtin buttons. remote is powered by usb cable from the camera, no additional power supply is needed.

this is a follow up project of my first attempt. it used arduino uno with blackmagic arduino 3g-sdi shield.
major downside was its size and necessity of 12V power supply. it was powered by v-mount battery that also
supplied 12v power to the camera.

Typical usecases are:

# capturing video using external recorder

everytime you need to adjust something like aperture, gain etc, you need to first turn off Clean feed,
then do the changes and then turn the clean feed on again.

# setting video more confortably

then digging into HUD and changing things by those well hidden buttons.

# how it works:

this hardware extension runs on Raspberry Pico board with OLED display with buttons and joystick.
when the camera is turned on, raspberry starts acting as a usb ehternet device. it assigns ip address
for the camera using dhcp. camera parameters are controlled via blackmagic rest api.
