#include <TFT_eSPI.h> // For the color TFT
// #include <SPI.h>   // <-- No longer needed for SD_MMC
#include <Bounce2.h>
#include <vector>     // Include vector library
#include <Arduino.h>
#include <SD_MMC.h>     // <-- Correct
#include <ArduinoJson.h>
#include "Audio.h" // Assumes this is the ESP8266Audio or similar library
#include "AudioTools.h"  // Arduino Audio Tools for metadata extraction
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"  // For MP3 metadata
TFT_eSPI tft = TFT_eSPI();
Audio audio;

// Button Pins
#define BTN_PREV_UP_PIN 12
#define BTN_NEXT_DOWN_PIN 13
#define BTN_SELECT_PLAY_PIN 14

// --- Button Logic ---
Bounce btnPrevUp = Bounce();
Bounce btnNextDown = Bounce();
Bounce btnSelectPlay = Bounce();

#define DOUBLE_CLICK_MS 300
unsigned long lastPrevPressTime = 0;

// --- Frame Rate Control ---
#define TARGET_FPS 30
#define FRAME_TIME_MS (1000 / TARGET_FPS)
unsigned long lastFrameTime = 0;
bool needsRedraw = true;
bool needsFullRedraw = true;  // Added for full screen redraws

// --- iPod Style Colors ---
#define IPOD_WHITE      TFT_WHITE
#define IPOD_BLACK      TFT_BLACK
#define IPOD_GRAY       tft.color565(200, 200, 200)
#define IPOD_DARK_GRAY  tft.color565(100, 100, 100)
#define IPOD_LIGHT_GRAY tft.color565(240, 240, 240)
#define IPOD_BLUE       tft.color565(0, 122, 255)
#define IPOD_SELECT     tft.color565(0, 100, 200)

// --- Screen Dimensions (Portrait) ---
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 480


// --- Metadata and Menu Structure ---
struct Song {
  String filename;
  String title;
  String artist;
  String album;
  String albumArt;
};

std::vector<Song> songs;
std::vector<String> artists;
std::vector<String> albums;

// --- Menu Items ---
enum MenuType {
  MENU_MAIN,
  MENU_ARTISTS,
  MENU_ALBUMS,
  MENU_SONGS
};

MenuType currentMenu = MENU_MAIN;
std::vector<String> currentMenuItems;

// --- State Machine ---
enum PlayerState {
  STATE_HOME,
  STATE_MENU,
  STATE_PLAYING
};

PlayerState currentState = STATE_HOME;
PlayerState previousState = STATE_HOME;

// --- Player Status ---
int selectedMenuIndex = 0;
int menuOffset = 0;
int selectedSongIndex = 0;
int playingSongIndex = 0;
bool isPlaying = false;
bool inMusicMenu = false;

// Previous values for partial updates
int prevSelectedMenuIndex = -1;
int prevSelectedSongIndex = -1;
int prevSongTimeSec = -1;
bool prevIsPlaying = false;
int prevPlayingSongIndex = -1;

// --- Mock Playback Timer ---
int songDurationSec = 245;
int currentSongTimeSec = 0;
unsigned long lastTimeUpdate = 0;

// --- Function Declarations ---
void updateDisplay();
void handleNextSong();
void handlePrevSong();
void loadMetadata();
void populateMenuItems();
void handleBackButton();
String formatTime(int totalSeconds);
void drawHeader(String title);
void drawBattery();
void drawScrollbar(int currentIndex, int totalItems, int yStart, int height);
void requestRedraw(bool fullRedraw = false);
void updateProgressBar();
void updatePlayPauseButton();
void handleHomeInput();
void handleMenuInput();
void handlePlayingInput();
void parseID3v2Tags(uint8_t* data, uint32_t size, String& title, String& artist, String& album);

// ------------------------------------
//         METADATA GENERATION
// ------------------------------------

bool generateMetadata() {
  File root = SD_MMC.open("/music");
  if (!root) {
    Serial.println("Failed to open music directory on SD_MMC");
    return false;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return false;
  }

  DynamicJsonDocument doc(32768);
  JsonArray songsArray = doc.createNestedArray("songs");
  
  int fileCount = 0;
  File file = root.openNextFile();
  
  while (file) {
    if (!file.isDirectory() && String(file.name()).endsWith(".mp3")) {
      String filepath = String("/music/") + file.name();
      
      Serial.print("Processing file ");
      Serial.print(++fileCount);
      Serial.print(": ");
      Serial.println(file.name());
      
      JsonObject song = songsArray.createNestedObject();
      
      // Default values
      String title = file.name();
      title.replace(".mp3", "");
      String artist = "Unknown Artist";
      String album = "Unknown Album";
      
      // Try simple ID3v1 extraction (safer, at end of file)
      File mp3File = SD_MMC.open(filepath, "r");
      if (mp3File && mp3File.size() > 128) {
        // Seek to ID3v1 tag location (last 128 bytes)
        mp3File.seek(mp3File.size() - 128);
        
        uint8_t id3v1[128];
        if (mp3File.read(id3v1, 128) == 128) {
          // Check for TAG identifier
          if (id3v1[0] == 'T' && id3v1[1] == 'A' && id3v1[2] == 'G') {
            // Extract fields (30 bytes each, null terminated)
            char buffer[31];
            
            // Title
            memset(buffer, 0, 31);
            memcpy(buffer, &id3v1[3], 30);
            String tempTitle = String(buffer);
            tempTitle.trim();
            if (tempTitle.length() > 0) {
              title = tempTitle;
              Serial.println("  Found title: " + title);
            }
            
            // Artist
            memset(buffer, 0, 31);
            memcpy(buffer, &id3v1[33], 30);
            String tempArtist = String(buffer);
            tempArtist.trim();
            if (tempArtist.length() > 0) {
              artist = tempArtist;
              Serial.println("  Found artist: " + artist);
            }
            
            // Album
            memset(buffer, 0, 31);
            memcpy(buffer, &id3v1[63], 30);
            String tempAlbum = String(buffer);
            tempAlbum.trim();
            if (tempAlbum.length() > 0) {
              album = tempAlbum;
              Serial.println("  Found album: " + album);
            }
          }
        }
        mp3File.close();
      }
      
      // Try to parse from filename if no metadata found
      if (artist == "Unknown Artist" && title.indexOf(" - ") > 0) {
        int dashPos = title.indexOf(" - ");
        artist = title.substring(0, dashPos);
        artist.trim();
        title = title.substring(dashPos + 3);
        title.trim();
        Serial.println("  Parsed from filename:");
        Serial.println("    Artist: " + artist);
        Serial.println("    Title: " + title);
      }
      
      song["filename"] = file.name();
      song["title"] = title;
      song["artist"] = artist;
      song["album"] = album;
      song["albumArt"] = album + ".bmp";
      
      // Yield to prevent watchdog timeout
      yield();
    }
    file = root.openNextFile();
  }
  
  root.close();
  
  Serial.print("Indexed ");
  Serial.print(fileCount);
  Serial.println(" songs");
  
  // Save metadata file
  File metadataFile = SD_MMC.open("/metadata.json", "w");
  if (!metadataFile) {
    Serial.println("Failed to create metadata file");
    return false;
  }
  
  size_t bytesWritten = serializeJson(doc, metadataFile);
  metadataFile.close();
  
  if (bytesWritten == 0) {
    Serial.println("Failed to write metadata");
    return false;
  }
  
  Serial.print("Metadata file created: ");
  Serial.print(bytesWritten);
  Serial.println(" bytes");
  
  return true;
}

// New helper function for ID3v2 parsing
void parseID3v2Tags(uint8_t* data, uint32_t size, String& title, String& artist, String& album) {
  uint32_t pos = 0;
  
  while (pos < size - 10) {
    // Read frame header
    char frameID[5] = {0};
    memcpy(frameID, &data[pos], 4);
    
    // Check for padding (null bytes)
    if (frameID[0] == 0) break;
    
    // Get frame size
    uint32_t frameSize = (data[pos + 4] << 24) |
                        (data[pos + 5] << 16) |
                        (data[pos + 6] << 8) |
                        data[pos + 7];
    
    if (frameSize == 0 || frameSize > size - pos - 10) break;
    
    // Extract text from frame (skip first byte which is encoding)
    String frameText = "";
    for (uint32_t i = 1; i < frameSize && (pos + 10 + i) < size; i++) {
      char c = data[pos + 10 + i];
      if (c >= 32 && c < 127) {  // Printable ASCII
        frameText += c;
      }
    }
    
    frameText.trim();
    
    // Check frame ID and assign values
    if (strcmp(frameID, "TIT2") == 0 && frameText.length() > 0) {
      title = frameText;
    } else if (strcmp(frameID, "TPE1") == 0 && frameText.length() > 0) {
      artist = frameText;
    } else if (strcmp(frameID, "TALB") == 0 && frameText.length() > 0) {
      album = frameText;
    }
    
    pos += 10 + frameSize;
  }
}

// ------------------------------------
//         SETUP
// ------------------------------------
void setup() {
  Serial.begin(115200);
  
  // Initialize display first for user feedback
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(IPOD_BLACK);
  tft.setTextColor(IPOD_WHITE);
  tft.setTextDatum(MC_DATUM);
  
  if (!SD_MMC.begin()) {
    Serial.println("SD_MMC Card Mount Failed!");
    tft.fillScreen(TFT_RED);
    tft.setTextSize(2);
    tft.drawString("SD Card Error", SCREEN_WIDTH/2, SCREEN_HEIGHT/2);
    while (1);
  }
  
  Serial.println("SD_MMC Card OK.");
  
  // Show indexing message
  tft.fillScreen(IPOD_BLACK);
  tft.setTextSize(2);
  tft.drawString("Indexing Music...", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 20);
  tft.setTextSize(1);
  tft.drawString("Please wait", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 20);
  
  Serial.println("Indexing music files...");
  
  if (generateMetadata()) {
    Serial.println("Metadata generated successfully");
    tft.drawString("Indexing complete!", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 40);
    delay(500);
  } else {
    Serial.println("Failed to generate metadata");
    tft.setTextColor(TFT_RED);
    tft.drawString("Indexing failed!", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 40);
    delay(2000);
  }
  
  loadMetadata();
  populateMenuItems();
  
  tft.fillScreen(IPOD_WHITE);
  
  // Setup Buttons
  pinMode(BTN_PREV_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_NEXT_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_SELECT_PLAY_PIN, INPUT_PULLUP);
  
  btnPrevUp.attach(BTN_PREV_UP_PIN);
  btnNextDown.attach(BTN_NEXT_DOWN_PIN);
  btnSelectPlay.attach(BTN_SELECT_PLAY_PIN);
  
  btnPrevUp.interval(25);
  btnNextDown.interval(25);
  btnSelectPlay.interval(25);
  
  requestRedraw(true);
}
// ------------------------------------
//         MAIN LOOP
// ------------------------------------
void loop() {
  unsigned long currentTime = millis();
  
  btnPrevUp.update();
  btnNextDown.update();
  btnSelectPlay.update();
  
  if (currentState == STATE_PLAYING && isPlaying) {
    if (currentTime - lastTimeUpdate > 1000) {
      currentSongTimeSec++;
      lastTimeUpdate = currentTime;
      
      if (currentSongTimeSec > songDurationSec) {
        handleNextSong();
      }
      requestRedraw();
    }
  }
  
  switch (currentState) {
    case STATE_HOME:
      handleHomeInput();
      break;
    case STATE_MENU:
      handleMenuInput();
      break;
    case STATE_PLAYING:
      handlePlayingInput();
      break;
  }
  
  if (needsRedraw && (currentTime - lastFrameTime >= FRAME_TIME_MS)) {
    updateDisplay();
    lastFrameTime = currentTime;
    needsRedraw = false;
  }
}

// ------------------------------------
//     METADATA & MENU LOGIC
// ------------------------------------
void loadMetadata() {
  File file = SD_MMC.open("/metadata.json", "r"); // <-- CHANGED
  if (!file) {
    Serial.println("Failed to open metadata file");
    return;
  }

  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Failed to parse metadata");
    return;
  }

  songs.clear();
  artists.clear();
  albums.clear();

  JsonArray songsArray = doc["songs"];
  for (JsonObject songObj : songsArray) {
    Song song;
    song.filename = songObj["filename"].as<String>();
    song.title = songObj["title"].as<String>();
    song.artist = songObj["artist"].as<String>();
    song.album = songObj["album"].as<String>();
    song.albumArt = songObj["albumArt"].as<String>();
    songs.push_back(song);
    
    // Check if artist/album already exists before adding
    bool artistFound = false;
    for (const String& art : artists) {
      if (art == song.artist) { artistFound = true; break; }
    }
    if (!artistFound) artists.push_back(song.artist);

    bool albumFound = false;
    for (const String& alb : albums) {
      if (alb == song.album) { albumFound = true; break; }
    }
    if (!albumFound) albums.push_back(song.album);
  }
}

void populateMenuItems() {
  currentMenuItems.clear();
  switch (currentMenu) {
    case MENU_MAIN:
      currentMenuItems = {"Music", "Artists", "Albums", "Songs", "Settings", "Back"};
      break;
    case MENU_ARTISTS:
      currentMenuItems = artists;
      currentMenuItems.push_back("Back");
      break;
    case MENU_ALBUMS:
      currentMenuItems = albums;
      currentMenuItems.push_back("Back");
      break;
    case MENU_SONGS:
      for (const Song& song : songs) {
        currentMenuItems.push_back(song.title);
      }
      currentMenuItems.push_back("Back");
      break;
  }
}

void handleBackButton() {
  switch (currentMenu) {
    case MENU_MAIN:
      currentState = STATE_HOME;
      break;
    case MENU_ARTISTS:
    case MENU_ALBUMS:
    case MENU_SONGS:
      currentMenu = MENU_MAIN;
      break;
  }
  selectedMenuIndex = 0;
  menuOffset = 0;
  populateMenuItems();
  requestRedraw(true);
}

// ------------------------------------
//     BUTTON HANDLER FUNCTIONS
// ------------------------------------
void handleHomeInput() {
  if (btnSelectPlay.fell()) {
    currentState = STATE_MENU;
    currentMenu = MENU_MAIN;
    populateMenuItems();
    requestRedraw(true);
  }
}

void handleMenuInput() {
  if (btnSelectPlay.fell()) {
    if (selectedMenuIndex == currentMenuItems.size() - 1) {
      // Back button selected
      handleBackButton();
    } else {
      switch (currentMenu) {
        case MENU_MAIN:
          switch (selectedMenuIndex) {
            case 0: // Music (default to all songs)
              currentMenu = MENU_SONGS;
              break;
            case 1: // Artists
              currentMenu = MENU_ARTISTS;
              break;
            case 2: // Albums
              currentMenu = MENU_ALBUMS;
              break;
            case 3: // Songs
              currentMenu = MENU_SONGS;
              break;
            // Add cases for other main menu items as needed
          }
          selectedMenuIndex = 0;
          menuOffset = 0;
          populateMenuItems();
          requestRedraw(true);
          break;
        case MENU_ARTISTS:
          // TODO: Filter songs by selected artist
          break;
        case MENU_ALBUMS:
          // TODO: Filter songs by selected album
          break;
        case MENU_SONGS:
          // This case assumes the list is not filtered
          // If filtered, playingSongIndex must be mapped to the main 'songs' vector index
          playingSongIndex = selectedMenuIndex;
          currentState = STATE_PLAYING;
          isPlaying = true;
          currentSongTimeSec = 0;
          prevSongTimeSec = -1;
          songDurationSec = 180 + (playingSongIndex * 15); // Mock duration
          lastTimeUpdate = millis();
          
          // --- THIS IS WHERE YOU WOULD START PLAYBACK ---
          // String fileToPlay = "/music/" + songs[playingSongIndex].filename;
          // audio.connecttoFS(SD_MMC, fileToPlay.c_str());
          // ---------------------------------------------------

          requestRedraw(true);
          break;
      }
    }
  }
  
  if (btnNextDown.fell()) {
    selectedMenuIndex = (selectedMenuIndex + 1) % currentMenuItems.size();
    int max_items_on_screen = 5; 
    if (selectedMenuIndex >= menuOffset + max_items_on_screen) {
      menuOffset = selectedMenuIndex - max_items_on_screen + 1;
    }
    requestRedraw();
  }
  
  if (btnPrevUp.fell()) {
    selectedMenuIndex--;
    if (selectedMenuIndex < 0) {
      selectedMenuIndex = currentMenuItems.size() - 1;
      int max_items_on_screen = 5; 
      menuOffset = max(0, (int)currentMenuItems.size() - max_items_on_screen);
    } else if (selectedMenuIndex < menuOffset) {
      menuOffset = selectedMenuIndex;
    }
    requestRedraw();
  }
}

void handlePlayingInput() {
  if (btnSelectPlay.fell()) {
    prevIsPlaying = isPlaying;
    isPlaying = !isPlaying;
    if (isPlaying) {
      lastTimeUpdate = millis();
      // audio.resume();
    } else {
      // audio.pause();
    }
    requestRedraw();
  }
  
  if (btnNextDown.fell()) {
    handleNextSong();
  }
  
  if (btnPrevUp.fell()) {
    unsigned long pressTime = millis();
    if (pressTime - lastPrevPressTime < DOUBLE_CLICK_MS) {
      // audio.stopSong();
      previousState = currentState;
      currentState = STATE_MENU; 
      isPlaying = false;
      prevSelectedSongIndex = -1;  
      requestRedraw(true);
      lastPrevPressTime = 0;
    } else {
      // audio.rewind();
      prevSongTimeSec = currentSongTimeSec;
      currentSongTimeSec = 0;
      lastTimeUpdate = millis();
      lastPrevPressTime = pressTime;
      requestRedraw();
    }
  }
}

// ------------------------------------
//     ACTION HELPER FUNCTIONS
// ------------------------------------
void handleNextSong() {
  prevPlayingSongIndex = playingSongIndex;
  playingSongIndex = (playingSongIndex + 1) % songs.size(); 
  isPlaying = true;
  prevSongTimeSec = -1;
  currentSongTimeSec = 0;
  songDurationSec = 180 + (playingSongIndex * 15);
  lastTimeUpdate = millis();
  
  // --- START NEXT SONG ---
  // String fileToPlay = "/music/" + songs[playingSongIndex].filename;
  // audio.connecttoFS(SD_MMC, fileToPlay.c_str());
  // -------------------------
  
  requestRedraw(true);
}

void handlePrevSong() {
  prevPlayingSongIndex = playingSongIndex;
  playingSongIndex--;
  if (playingSongIndex < 0) {
    playingSongIndex = songs.size() - 1; 
  }
  isPlaying = true;
  prevSongTimeSec = -1;
  currentSongTimeSec = 0;
  songDurationSec = 180 + (playingSongIndex * 15);
  lastTimeUpdate = millis();

  // --- START PREV SONG ---
  // String fileToPlay = "/music/" + songs[playingSongIndex].filename;
  // audio.connecttoFS(SD_MMC, fileToPlay.c_str());
  // -------------------------

  requestRedraw(true);
}

// ------------------------------------
//     DISPLAY FUNCTIONS
// ------------------------------------
void requestRedraw(bool fullRedraw) {
  needsRedraw = true;
  if (fullRedraw) {
    needsFullRedraw = true;
  }
}

String formatTime(int totalSeconds) {
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  char timeString[6];
  sprintf(timeString, "%02d:%02d", minutes, seconds);
  return String(timeString);
}

void drawHeader(String title) {
  tft.fillRect(0, 0, SCREEN_WIDTH, 40, IPOD_BLUE);
  tft.setTextColor(IPOD_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString(title, SCREEN_WIDTH/2, 20);
  drawBattery();
}

void drawBattery() {
  int x = SCREEN_WIDTH - 40;
  int y = 10;
  tft.drawRect(x, y, 30, 20, IPOD_WHITE);
  tft.fillRect(x + 30, y + 6, 3, 8, IPOD_WHITE);
  tft.fillRect(x + 2, y + 2, 20, 16, IPOD_WHITE);
}

void drawScrollbar(int currentIndex, int totalItems, int yStart, int height) {
  if (totalItems * 45 < height) return; 
  
  tft.fillRect(SCREEN_WIDTH - 10, yStart, 8, height, IPOD_WHITE);
  int barHeight = height;
  int thumbHeight = max(20, barHeight / totalItems);
  int thumbY = yStart + (currentIndex * (barHeight - thumbHeight) / (totalItems - 1));
  
  tft.fillRect(SCREEN_WIDTH - 10, yStart, 8, barHeight, IPOD_LIGHT_GRAY);
  tft.fillRect(SCREEN_WIDTH - 10, thumbY, 8, thumbHeight, IPOD_DARK_GRAY);
}

void updateProgressBar() {
  int progressY = 355;  
  
  if (currentSongTimeSec != prevSongTimeSec) {
    tft.fillRect(30, progressY, 60, 16, IPOD_WHITE);
    tft.fillRect(SCREEN_WIDTH - 90, progressY, 60, 16, IPOD_WHITE);
    
    tft.setTextColor(IPOD_BLACK);
    tft.setTextSize(1);
    
    tft.setTextDatum(TL_DATUM);
    tft.drawString(formatTime(currentSongTimeSec), 30, progressY);
    
    tft.setTextDatum(TR_DATUM);
    tft.drawString(formatTime(songDurationSec), SCREEN_WIDTH - 30, progressY);
        float percent = (float)currentSongTimeSec / (float)songDurationSec;
    int barWidth = SCREEN_WIDTH - 60;
    int barX = 30;
    int barY = progressY + 20;
    
    tft.fillRoundRect(barX, barY, barWidth, 10, 5, IPOD_LIGHT_GRAY);
    tft.drawRoundRect(barX, barY, barWidth, 10, 5, IPOD_DARK_GRAY);
    
    int fillWidth = (int)(percent * (barWidth - 2));
    if (fillWidth > 0) {
      tft.fillRoundRect(barX + 1, barY + 1, fillWidth, 8, 4, IPOD_BLUE);
    }
    
    prevSongTimeSec = currentSongTimeSec;
  }
}

void updatePlayPauseButton() {
  int controlsY = 410;
  int centerX = SCREEN_WIDTH / 2;
  
  tft.fillCircle(centerX, controlsY, 26, IPOD_WHITE);
  
  if (isPlaying) {
    tft.fillCircle(centerX, controlsY, 25, IPOD_BLUE);
    tft.fillRect(centerX - 8, controlsY - 10, 6, 20, IPOD_WHITE);
    tft.fillRect(centerX + 2, controlsY - 10, 6, 20, IPOD_WHITE);
  } else {
    tft.fillCircle(centerX, controlsY, 25, IPOD_DARK_GRAY);
    tft.fillTriangle(
      centerX - 8, controlsY - 12,
      centerX - 8, controlsY + 12,
      centerX + 10, controlsY,
      IPOD_WHITE
    );
  }
}

// ------------------------------------
//     MASTER DISPLAY FUNCTION
// ------------------------------------
void updateDisplay() {
  if (currentState != previousState) {
    needsFullRedraw = true;
    previousState = currentState;
  }
  
  if (needsFullRedraw) {
    tft.fillScreen(IPOD_WHITE);
    needsFullRedraw = false;
    
    prevSelectedMenuIndex = -1;
    prevSelectedSongIndex = -1;
    prevSongTimeSec = -1;
    prevIsPlaying = !isPlaying;
    prevPlayingSongIndex = -1;
  }
  
  switch (currentState) {
    case STATE_HOME: { 
      tft.fillScreen(IPOD_BLACK);
      tft.fillRoundRect(60, 140, 200, 120, 20, IPOD_WHITE);
      tft.setTextColor(IPOD_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(4);
      tft.drawString("iPod", SCREEN_WIDTH/2, 200);
      tft.setTextColor(IPOD_WHITE);
      tft.setTextSize(2);
      tft.drawString("Click to continue", SCREEN_WIDTH/2, 320);
      tft.setTextSize(1);
      tft.drawString("MENU", SCREEN_WIDTH/2, 420);
      break;
    }
    
    case STATE_MENU: {
      String headerTitle = "Menu";
      if (currentMenu == MENU_MAIN) headerTitle = "iPod";
      else if (currentMenu == MENU_ARTISTS) headerTitle = "Artists";
      else if (currentMenu == MENU_ALBUMS) headerTitle = "Albums";
      else if (currentMenu == MENU_SONGS) headerTitle = "Songs";
      
      drawHeader(headerTitle);

      tft.fillRect(0, 40, SCREEN_WIDTH, SCREEN_HEIGHT - 40, IPOD_WHITE);
      
      int y_start = 60;
      int row_height = 45;
      int max_items_on_screen = (SCREEN_HEIGHT - y_start - 20) / row_height; 
      int itemsToDraw = min((int)currentMenuItems.size() - menuOffset, max_items_on_screen);
      
      for (int i = 0; i < itemsToDraw; i++) {
        int index = menuOffset + i; 
        if (index >= currentMenuItems.size()) break;
        
        int yPos = y_start + (i * row_height); 
        bool selected = (index == selectedMenuIndex);
        
        tft.fillRect(10, yPos - 5, SCREEN_WIDTH - 30, row_height - 5, IPOD_WHITE);
        
        if (selected) {
          tft.fillRect(10, yPos - 5, SCREEN_WIDTH - 30, row_height - 5, IPOD_SELECT);
          tft.setTextColor(IPOD_WHITE);
        } else {
          tft.setTextColor(IPOD_BLACK);
        }
        
        tft.setTextSize(2);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(currentMenuItems[index], 20, yPos);
        
        if (selected) {
          tft.fillTriangle(
            SCREEN_WIDTH - 35, yPos + 15,
            SCREEN_WIDTH - 45, yPos + 10,
            SCREEN_WIDTH - 45, yPos + 20,
            IPOD_WHITE
          );
        }
      }
      
      drawScrollbar(selectedMenuIndex, currentMenuItems.size(), y_start, max_items_on_screen * row_height);
      break;
    }
   
    case STATE_PLAYING: {
      if (playingSongIndex != prevPlayingSongIndex) {
        drawHeader("Now Playing");
        
        int artSize = 200;
        int artX = (SCREEN_WIDTH - artSize) / 2;
        int artY = 60;
        
        tft.fillRect(artX + 3, artY + 3, artSize, artSize, IPOD_DARK_GRAY);
        tft.fillRect(artX, artY, artSize, artSize, IPOD_LIGHT_GRAY);
        tft.drawRect(artX, artY, artSize, artSize, IPOD_DARK_GRAY);
        
        tft.setTextColor(IPOD_DARK_GRAY);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.drawString("Album Art", SCREEN_WIDTH/2, artY + artSize/2);
        
        int infoY = artY + artSize + 20;
        
        tft.setTextColor(IPOD_BLACK);
        tft.setTextSize(2);
        tft.drawString(songs[playingSongIndex].title, SCREEN_WIDTH/2, infoY);
        
        tft.setTextColor(IPOD_DARK_GRAY);
        tft.setTextSize(1);
        tft.drawString(songs[playingSongIndex].artist, SCREEN_WIDTH/2, infoY + 25);
        tft.drawString(songs[playingSongIndex].album, SCREEN_WIDTH/2, infoY + 40);
        
        int controlsY = 410;
        int centerX = SCREEN_WIDTH / 2;
        
        tft.fillCircle(centerX - 70, controlsY, 20, IPOD_LIGHT_GRAY);
        tft.fillTriangle(
          centerX - 62, controlsY - 8,
          centerX - 62, controlsY + 8,
          centerX - 74, controlsY,
          IPOD_DARK_GRAY
        );
        tft.fillTriangle(
          centerX - 70, controlsY - 8,
          centerX - 70, controlsY + 8,
          centerX - 82, controlsY,
          IPOD_DARK_GRAY
        );
        
        tft.fillCircle(centerX + 70, controlsY, 20, IPOD_LIGHT_GRAY);
        tft.fillTriangle(
          centerX + 62, controlsY - 8,
          centerX + 62, controlsY + 8,
          centerX + 74, controlsY,
          IPOD_DARK_GRAY
        );
        tft.fillTriangle(
          centerX + 70, controlsY - 8,
          centerX + 70, controlsY + 8,
          centerX + 82, controlsY,
          IPOD_DARK_GRAY
        );
        
        tft.setTextDatum(BC_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(IPOD_DARK_GRAY);
        tft.drawString("PREV", centerX - 70, SCREEN_HEIGHT - 10);
        tft.drawString("PLAY/PAUSE", centerX, SCREEN_HEIGHT - 10);
        tft.drawString("NEXT", centerX + 70, SCREEN_HEIGHT - 10);
        
        prevPlayingSongIndex = playingSongIndex;
      }
      
      updateProgressBar();
      
      if (isPlaying != prevIsPlaying) {
        updatePlayPauseButton();
        prevIsPlaying = isPlaying;
      }
      
      break;
    }
  }
}