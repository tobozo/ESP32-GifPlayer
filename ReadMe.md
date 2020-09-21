# ESP32-GifPlayer 

GIF player Demo for M5Stack, Odroid-GO, ESP32-Wrover-Kit, LoLinD32-Pro, D-Duino32-XS, and more...

This sketch will open the SD card and queue all files found in the `/gif/` folder then play them in an endless loop.

# Depends on the following libraries (all of them available from the Arduino Library Manager):
  - [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF)
  - [ESP32-Chimera-core](https://github.com/tobozo/ESP32-Chimera-core)
  - [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
  - [ESP32-Targz](https://github.com/tobozo/ESP32-targz)
  - [M5StackUpdater](https://github.com/tobozo/M5Stack-SD-Updater)

# Deploying GIFs manually:

  - Create a "gif" folder on the root of your SD Card
  - Copy your GIF files in the /gif folder
  - Run the sketch

# Deploying GIFs automatically:

  - Run any WiFi example sketch to connect your ESP32 to your WiFi router (make sure the connection is successful)
  - Edit the value of `GIF_ARCHIVE_URL` in `gifdownloader.h' to match the URL to your .tar.gz GIFs archive (must contain a /gif/ folder too)
  - Run the sketch
  - Wait for the download
  - Wait for the unpacking
  - Wait for the unzipping


loosely coded by @tobozo for @bitbank2
