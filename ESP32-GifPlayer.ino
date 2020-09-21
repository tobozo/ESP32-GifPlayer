/*\
 *
 * ESP32-GifPlayer
 * ****************
 *
 * https://github.com/tobozo/ESP32-GifPlayer
 *
 * GIF player Demo for M5Stack, Odroid-GO, ESP32-Wrover-Kit, LoLinD32-Pro,
 * D-Duino32-XS, and more...
 *
 * This sketch will open the SD card and queue all files found in the /gif
 * folder then play them in an endless loop.
 *
 * If no GIF files are found on the SD Card, it will download and unpack
 * a tar.gz archive of GIFs from a provided URL (default github)
 *
 * This demo is based on Larry Bank's AnimatedGIF library:
 *
 *   https://github.com/bitbank2/
 *
 * Library dependencies (available from the Arduino Library Manager):
 *
 * - AnimatedGIF        https://github.com/bitbank2/AnimatedGIF
 * - ESP32-Chimera-core https://github.com/tobozo/ESP32-Chimera-core
 * - LovyanGFX          https://github.com/lovyan03/LovyanGFX
 * - ESP32-Targz        https://github.com/tobozo/ESP32-targz
 * - M5StackUpdater     https://github.com/tobozo/M5Stack-SD-Updater
 *
 *
 * MIT License
 *
 * Copyright (c) 2020 tobozo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
 *
 *
\*/

//#define LGFX_ONLY // enable this for custom board profiles
//#define USE_SPIFFS // requires "Large SPIFFS" partition !!

#if defined LGFX_ONLY
  #include <LGFX_TFT_eSPI.hpp>
  #include <SD.h>
  #define TFCARD_CS_PIN 4 // CS pin for SD Card
  //static lgfx::Touch_XPT2046 touch;
  //static lgfx::Panel_ILI9341 panel;
  //static lgfx::Panel_ILI9342 panel;
  static LGFX tft;
  //#define HAS_TOUCH
#else
  #include <ESP32-Chimera-Core.h> // https://github.com/tobozo/ESP32-Chimera-Core or regular M5Stack Core
  #define tft M5.Lcd // syntax sugar
#endif

#if defined USE_SPIFFS
  #define GIFPLAYER_FS SPIFFS
  #define GIFPLAYER_FS_Begin() SPIFFS.begin(true)
#elif defined M5STACK_SD
  #define GIFPLAYER_FS M5STACK_SD
  #define GIFPLAYER_FS_Begin() M5.sd_begin()
#else
  #define GIFPLAYER_FS SD
  #define GIFPLAYER_FS_Begin() GIFPLAYER_FS.begin( TFCARD_CS_PIN )
#endif

#include <M5StackUpdater.h>

// leave empty if your ESP32 had a previous successful WiFi connection
char WiFi_SSID[32] = "";
char WiFi_PASS[32] = "";

#include "gifdownloader.h"
#include <AnimatedGIF.h>

AnimatedGIF gif;

// rule: loop GIF at least during 3s, maximum 5 times, and don't loop/animate longer than 30s per GIF
const int maxLoopIterations =     5; // stop after this amount of loops
const int maxLoopsDuration  =  3000; // ms, max cumulated time after the GIF will break loop
const int maxGifDuration    = 30000; // ms, max GIF duration

// used to center image based on GIF dimensions
static int xOffset = 0;
static int yOffset = 0;

static int totalFiles = 0; // GIF files count
static int currentFile = 0;
static int lastFile = -1;

char GifComment[256];

static File FSGifFile; // temp gif file holder
static File GifRootFolder; // directory listing

std::vector<std::string> GifFiles; // GIF files path


static void MyCustomDelay( unsigned long ms ) {
  delay( ms );
  //log_d("delay %d\n", ms);
}


static void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  //log_d("GIFOpenFile( %s )\n", fname );
  FSGifFile = GIFPLAYER_FS.open(fname);
  if (FSGifFile) {
    *pSize = FSGifFile.size();
    return (void *)&FSGifFile;
  }
  return NULL;
}


static void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  if (f != NULL)
     f->close();
}


static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
  int32_t iBytesRead;
  iBytesRead = iLen;
  File *f = static_cast<File *>(pFile->fHandle);
  // Note: If you read a file all the way to the last byte, seek() stops working
  if ((pFile->iSize - pFile->iPos) < iLen)
      iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
  if (iBytesRead <= 0)
      return 0;
  iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
  pFile->iPos = f->position();
  return iBytesRead;
}


static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{
  int i = micros();
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  i = micros() - i;
  //log_d("Seek time = %d us\n", i);
  return pFile->iPos;
}


static void TFTDraw(int x, int y, int w, int h, uint16_t* lBuf )
{
  tft.pushRect( x+xOffset, y+yOffset, w, h, lBuf );
}


// Draw a line of image directly on the LCD
void GIFDraw(GIFDRAW *pDraw)
{
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[320];
  int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > tft.width() )
      iWidth = tft.width() ;
  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y; // current line

  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) {// restore to background color
    for (x=0; x<iWidth; x++) {
      if (s[x] == pDraw->ucTransparent)
          s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) { // if transparency used
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + iWidth;
    x = 0;
    iCount = 0; // count non-transparent pixels
    while(x < iWidth) {
      c = ucTransparent-1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent) { // done, stop
          s--; // back up to treat it like transparent
        } else { // opaque
            *d++ = usPalette[c];
            iCount++;
        }
      } // while looking for opaque pixels
      if (iCount) { // any opaque pixels?
        TFTDraw( pDraw->iX+x, y, iCount, 1, (uint16_t*)usTemp );
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)
            iCount++;
        else
            s--;
      }
      if (iCount) {
        x += iCount; // skip these
        iCount = 0;
      }
    }
  } else {
    s = pDraw->pPixels;
    // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
    for (x=0; x<iWidth; x++)
      usTemp[x] = usPalette[*s++];
    TFTDraw( pDraw->iX, y, iWidth, 1, (uint16_t*)usTemp );
  }
} /* GIFDraw() */


int gifPlay( char* gifPath )
{ // 0=infinite

  gif.begin(BIG_ENDIAN_PIXELS);

  if( ! gif.open( gifPath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw ) ) {
    log_n("Could not open gif %s", gifPath );
    return maxLoopsDuration;
  }

  int frameDelay = 0; // store delay for the last frame
  int then = 0; // store overall delay
  bool showcomment = false;

  // center the GIF !!
  int w = gif.getCanvasWidth();
  int h = gif.getCanvasHeight();
  xOffset = ( tft.width()  - w )  /2;
  yOffset = ( tft.height() - h ) /2;

  if( lastFile != currentFile ) {
    log_n("Playing %s [%d,%d] with offset [%d,%d]", gifPath, w, h, xOffset, yOffset );
    lastFile = currentFile;
    showcomment = true;
  }

  while (gif.playFrame(true, &frameDelay)) {
    if( showcomment )
      if (gif.getComment(GifComment))
        log_n("GIF Comment: %s", GifComment);

    then += frameDelay;
    if( then > maxGifDuration ) { // avoid being trapped in infinite GIF's
      //log_w("Broke the GIF loop, max duration exceeded");
      break;
    }
  }

  gif.close();

  return then;
}


int getGifInventory( const char* basePath )
{
  int amount = 0;
  GifRootFolder = GIFPLAYER_FS.open(basePath);
  if(!GifRootFolder){
    log_n("Failed to open directory");
    return 0;
  }

  if(!GifRootFolder.isDirectory()){
    log_n("Not a directory");
    return 0;
  }

  File file = GifRootFolder.openNextFile();

  tft.setTextColor( TFT_WHITE, TFT_BLACK );
  tft.setTextSize( 2 );

  int textPosX = tft.width()/2 - 16;
  int textPosY = tft.height()/2 - 10;

  tft.drawString("GIF Files:", textPosX-40, textPosY-20 );

  while( file ) {
    if(!file.isDirectory()) {
      GifFiles.push_back( file.name() );
      amount++;
      tft.drawString(String(amount), textPosX, textPosY );
      file.close();
    }
    file = GifRootFolder.openNextFile();
  }
  GifRootFolder.close();
  log_n("Found %d GIF files", amount);
  return amount;
}




void setup()
{
  #if defined LGFX_ONLY
    Serial.begin(115200);
    /*
    auto p = new lgfx::Panel_ILI9341();
    p->spi_3wire = false;
    p->spi_cs    = TFT_CS;  // 14
    p->spi_dc    = TFT_DC;  // 27
    p->gpio_rst  = TFT_RST; // 33
    p->gpio_bl   = TFT_LED; // 32
    p->pwm_ch_bl = 7;

    tft.setPanel(p);

    auto t = new lgfx::Touch_XPT2046(); // sharing SPI with TFT
    t->spi_mosi = MOSI; // 23
    t->spi_miso = MISO; // 19
    t->spi_sclk = SCK;  // 18
    t->spi_cs   = TOUCH_CS; // TOUCH_CS may be custom build spefific ?
    t->spi_host = VSPI_HOST;
    t->bus_shared = true;
    t->freq     = 1600000;

    tft.touch(t);
    */

    auto p = new lgfx::Panel_ILI9342;
    p->reverse_invert = true;
    p->spi_3wire = true;
    p->spi_cs =  5;
    p->spi_dc = 15;
    p->rotation = 1;
    p->offset_rotation = 3;

    tft.setPanel(p);
    tft.init();


    //tft.begin();

  #else

    M5.begin();

  #endif

  checkSDUpdater( GIFPLAYER_FS, MENU_BIN, 1500 );

  int attempts = 0;
  int maxAttempts = 50;
  int delayBetweenAttempts = 300;
  bool isblinked = false;

  tft.setTextDatum( MC_DATUM );

  while(! GIFPLAYER_FS_Begin() ) {
    log_n("SD Card mount failed! (attempt %d of %d)", attempts, maxAttempts );
    isblinked = !isblinked;
    attempts++;
    if( isblinked ) {
      tft.setTextColor( TFT_WHITE, TFT_BLACK );
    } else {
      tft.setTextColor( TFT_BLACK, TFT_WHITE );
    }
    tft.drawString( "INSERT SD", tft.width()/2, tft.height()/2 );

    if( attempts > maxAttempts ) {
      log_n("Giving up");
      tft.setBrightness(0);
      #if defined( ARDUINO_M5Stack_Core_ESP32 ) || defined( ARDUINO_M5STACK_FIRE ) || defined( ARDUINO_ODROID_ESP32 )// || defined( ARDUINO_M5STACK_Core2 )
        #ifndef LGFX_ONLY
          M5.setWakeupButton( BUTTON_B_PIN );
          M5.powerOFF();
        #else
          // TODO: turn TFT off
          esp_deep_sleep_start();
        #endif
      #else
        // TODO: turn TFT off
        esp_deep_sleep_start();
      #endif
    }
    delay( delayBetweenAttempts );

    GIFPLAYER_FS_Begin();
  }

  log_n("SD Card mounted!");

  tft.begin();
  tft.fillScreen(TFT_BLACK);

  totalFiles = getGifInventory( "/gif" ); // scan the SD card GIF folder

  if( totalFiles == 0 ) {
     tft.fillScreen( TFT_BLACK );
     tft.drawString( "Downloading ...", tft.width()/2, tft.height()/2 );
     runWifiDownloader( GIFPLAYER_FS );
     // TODO: download and unzip https://github.com/bitbank2/AnimatedGIF/releases/download/1.0.1/Gif_Animations_By_Cyriak_Harris.tar.gz
     while(1);
  }

  tft.setTextDatum( TL_DATUM );

}



void loop()
{

  tft.clear();

  const char * fileName = GifFiles[currentFile++%totalFiles].c_str();

  int loops = maxLoopIterations; // max loops
  int durationControl = maxLoopsDuration; // force break loop after xxx ms

  while(loops-->0 && durationControl > 0 ) {
    durationControl -= gifPlay( (char*)fileName );
    gif.reset();
  }

}

