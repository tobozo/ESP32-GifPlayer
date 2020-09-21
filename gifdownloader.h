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

#include <ESP32-targz.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

HTTPClient http;

// must be a tar.gz
//#define GIF_ARCHIVE_URL "https://github.com/bitbank2/AnimatedGIF/releases/download/1.0.1/Gif_Animations_By_Cyriak_Harris.tar.gz"    // 25MB ~=20mn download
#define GIF_ARCHIVE_URL "https://github.com/bitbank2/AnimatedGIF/releases/download/1.0.1/Gif_Animations_By_Cyriak_Harris_lite.tar.gz" // 1.5MB ~= 2mn download
#define GIF_ARCHIVE_PATH "/gifs.tar.gz"
#define GIF_DIR_PATH "/gif"
#define TMP_DIR_PATH "/tmp"
#define TMP_TAR_PATH TMP_DIR_PATH "/data.tar"

static bool /*yolo*/wget( const char* url, fs::FS &fs, const char* path );
static void runWifiDownloader( void * param );
static void startWifi( void * param = NULL );


static void progressBar( void * param );
static void activityIndicator( void * param );


static void runWifiDownloader( fs::FS &destFs ) {
  bool downloadfile = false;

  if( !destFs.exists( GIF_DIR_PATH ) ) {
    log_w("Folder %s does not exists, creating", GIF_DIR_PATH );
    destFs.mkdir( GIF_DIR_PATH  );
  }
  if( !destFs.exists( TMP_DIR_PATH ) ) {
    log_w("Folder %s does not exists, creating", TMP_DIR_PATH );
    destFs.mkdir( TMP_DIR_PATH );
  }

  if( destFs.exists( GIF_ARCHIVE_PATH ) ) {
    File f = destFs.open( GIF_ARCHIVE_PATH );
    size_t fileSize = f.size();
    log_w("File %s exists on filesystem [%d bytes]", GIF_ARCHIVE_PATH, fileSize );
    f.close();
    if( fileSize != 26448701 ) {
      destFs.remove( GIF_ARCHIVE_PATH );
      downloadfile = true;
    }
  } else {
    downloadfile = true;
  }
  if( downloadfile ) {
    startWifi();
    if( ! wget( GIF_ARCHIVE_URL, destFs, GIF_ARCHIVE_PATH ) ) {
      log_e("Failed to download %s", GIF_ARCHIVE_URL );
      return;
    }
    log_w("Successfully saved file as %s, now unpacking", GIF_ARCHIVE_PATH );
  } else {
    log_w("Unpacking to %s", TMP_TAR_PATH);
  }

  tft.setTextColor( TFT_WHITE, TFT_BLACK);

  tft.fillScreen( TFT_BLACK );
  tft.drawString( "  Unpacking ... ", tft.width()/2, tft.height()/2  );

  gzExpander(destFs, GIF_ARCHIVE_PATH, destFs, TMP_TAR_PATH);

  tft.fillScreen( TFT_BLACK );
  tft.drawString( "  Unzipping ... ", tft.width()/2, tft.height()/2 );

  tarExpander(destFs, TMP_TAR_PATH, destFs, "/");

  // finished !
  ESP.restart();

}


static void startWifi( void * param ) {
  WiFi.mode(WIFI_STA);
  Serial.println(WiFi.macAddress());

  if( String( WiFi_SSID ) !="" && String( WiFi_PASS ) !="" ) {
    WiFi.begin( WiFi_SSID, WiFi_PASS );
  } else {
    WiFi.begin();
  }
  while(WiFi.status() != WL_CONNECTED) {
    log_e("Not connected");
    delay(1000);
  }
  log_w("Connected!");
  if( String( WiFi_SSID ) !="" ) {
    Serial.print("Connected to ");
    Serial.println(WiFi_SSID);
  }
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("");

}


static bool /*yolo*/wget( const char* url, fs::FS &fs, const char* path ) {

  WiFiClientSecure *client = new WiFiClientSecure;
  client->setCACert( NULL ); // yolo security

  const char* UserAgent = "GifPlayerHTTPClient";

  http.setUserAgent( UserAgent );
  http.setConnectTimeout( 10000 ); // 10s timeout = 10000

  if( ! http.begin(*client, url ) ) {
    log_e("Can't open url %s", url );
    return false;
  }

  const char * headerKeys[] = {"location", "redirect"};
  const size_t numberOfHeaders = 2;
  http.collectHeaders(headerKeys, numberOfHeaders);

  log_w("URL = %s", url);

  int httpCode = http.GET();

  // file found at server
  if (httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
    String newlocation = "";
    for(int i = 0; i< http.headers(); i++) {
      String headerContent = http.header(i);
      if( headerContent !="" ) {
        newlocation = headerContent;
        Serial.printf("%s: %s\n", headerKeys[i], headerContent.c_str());
      }
    }

    http.end();
    if( newlocation != "" ) {
      log_w("Found 302/301 location header: %s", newlocation.c_str() );
      return wget( newlocation.c_str(), fs, path );
    } else {
      log_e("Empty redirect !!");
      return false;
    }
  }

  WiFiClient *stream = http.getStreamPtr();

  if( stream == nullptr ) {
    http.end();
    log_e("Connection failed!");
    return false;
  }

  File outFile = fs.open( path, FILE_WRITE );
  if( ! outFile ) {
    log_e("Can't open %s file to save url %s", path, url );
    return false;
  }

  uint8_t *wgetBuff = getGzBufferUint8();
  size_t sizeOfBuff = sizeof(wgetBuff);

  if( sizeOfBuff == 0 ) {
    log_e("bad buffer");
    while(1);
  } else {
    log_e("Buffer size : %d", sizeOfBuff );
  }

  int len = http.getSize();
  int bytesLeftToDownload = len;
  int bytesDownloaded = 0;
  float lastprogress = 0;
  bool progresstoggle = false;
  char *progressStr = new char[32];

  while(http.connected() && (len > 0 || len == -1)) {
    size_t size = stream->available();
    if(size) {
      // read up to 1024 bytes
      int c = stream->readBytes(wgetBuff, ((size > sizeOfBuff) ? sizeOfBuff : size));
      outFile.write( wgetBuff, c );
      bytesLeftToDownload -= c;
      bytesDownloaded += c;
      float progress = (((float)bytesDownloaded / (float)len) * 100.00);
      float roundedprogress = int(progress*10) / 10.0;
      float roundedactivity = int(progress*10) / 10.0;
      //Serial.printf("%.2f / %.2f / %d\n", progress, roundedprogress, bytesLeftToDownload );
      if( lastprogress != roundedprogress ) {
        sprintf( progressStr, "  %.1f%s  ", progress, "%" );
        Serial.printf("Progress: %s - %d bytes left\n", progressStr, bytesLeftToDownload );
        tft.setTextColor( TFT_BLACK, TFT_WHITE );
        tft.drawString( progressStr, tft.width()/2, tft.height()/2 + 20 );
        progresstoggle = !progresstoggle;
        if( progresstoggle ) {
          tft.setTextColor( TFT_BLACK, TFT_WHITE );
        } else {
          tft.setTextColor( TFT_WHITE, TFT_BLACK );
        }
        tft.drawString( " - - - - ", tft.width()/2, tft.height()/2 + 40 );
        lastprogress = roundedprogress;
      }
    }
  }
  outFile.close();
  return fs.exists( path );
}
