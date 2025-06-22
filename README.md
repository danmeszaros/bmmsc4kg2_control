# bmmsc4kg2_control

This project aims to ease the pain of controlling BlackMagic Micro Studio Camera 4K G2 using only
builtin buttons.

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
