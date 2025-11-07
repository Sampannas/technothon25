#include <TFT_eSPI.h> // For the color TFT
#include <SPI.h>      // For SPI communication
#include <Bounce2.h>
#include <vector>     // Include vector library
#include <Arduino.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include "Audio.h" // Assumes this is the ESP8266Audio or similar library

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
int menuOffset = 0; // <<< FIXED: Added missing variable
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

// ------------------------------------
//         METADATA GENERATION
// ------------------------------------

bool generateMetadata() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return false;
  }

  File root = LittleFS.open("/music");
  if (!root) {
    Serial.println("Failed to open music directory");
    return false;
  }

  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return false;
  }

  DynamicJsonDocument doc(16384);  // Adjust size as needed
  JsonArray songsArray = doc.createNestedArray("songs");

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory() && String(file.name()).endsWith(".mp3")) {
      String filepath = String("/music/") + file.name();
      // audio.connecttoFS(LittleFS, filepath.c_str()); // This just checks connection, doesn't read tags

      JsonObject song = songsArray.createNestedObject();
      
      // --- FIXED ---
      // The Audio library does not have .getTrack()/.getArtist()/.getAlbum()
      // We will use the filename as the title and "Unknown" for others.
      String title = file.name();
      title.replace(".mp3", ""); // Clean up the title
      
      song["filename"] = file.name();
      song["title"] = title;
      song["artist"] = "Unknown Artist";
      song["album"] = "Unknown Album";
      
      // Generate a filename for album art
      String albumArtFilename = "Unknown_Album";
      albumArtFilename += ".bmp";
      song["albumArt"] = albumArtFilename;
      
      // audio.stopSong(); // No need to stop if we didn't play
    }
    file = root.openNextFile();
  }

  File metadataFile = LittleFS.open("/metadata.json", "w");
  if (!metadataFile) {
    Serial.println("Failed to create metadata file");
    return false;
  }

  if (serializeJson(doc, metadataFile) == 0) {
    Serial.println("Failed to write to metadata file");
    return false;
  }

  metadataFile.close();
  return true;
}

// ------------------------------------
//         SETUP
// ------------------------------------
void setup() {
  Serial.begin(115200);
  
  if (!SD_MMC.begin()) { // This initializes in 4-bit mode by default
    Serial.println("SD_MMC Card Mount Failed!");
    // You could draw an error to the TFT here
    tft.init();
    tft.fillScreen(TFT_RED);
    tft.drawString("SD Card Error", 20, 20);
    while (1); // Stop here
  }
  Serial.println("SD_MMC Card OK.");

  // Run metadata generation (which is now fixed to use SD_MMC)
  if (generateMetadata()) {
    Serial.println("Metadata generated successfully");
  } else {
    Serial.println("Failed to generate metadata");
  }
  loadMetadata();
  populateMenuItems();
  
  // --- Setup TFT Display ---
  tft.init();
  tft.setRotation(0); // Portrait mode (320x480)
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
  
  // Update all button states
  btnPrevUp.update();
  btnNextDown.update();
  btnSelectPlay.update();
  
  // --- Handle Mock Playback Timer ---
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
  
  // --- 30 FPS Frame Rate Control ---
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
  File file = LittleFS.open("/metadata.json", "r");
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
          requestRedraw(true);
          break;
      }
    }
  }
  
  if (btnNextDown.fell()) {
    selectedMenuIndex = (selectedMenuIndex + 1) % currentMenuItems.size();
    // Logic to scroll the viewport down
    int max_items_on_screen = 5; // Should match calculation in updateDisplay
    if (selectedMenuIndex >= menuOffset + max_items_on_screen) {
      menuOffset = selectedMenuIndex - max_items_on_screen + 1;
    }
    requestRedraw();
  }
  
  if (btnPrevUp.fell()) {
    selectedMenuIndex--;
    if (selectedMenuIndex < 0) {
      selectedMenuIndex = currentMenuItems.size() - 1;
      // Snap to end
      int max_items_on_screen = 5; // Should match calculation in updateDisplay
      menuOffset = max(0, (int)currentMenuItems.size() - max_items_on_screen);
    } else if (selectedMenuIndex < menuOffset) {
      // Logic to scroll the viewport up
      menuOffset = selectedMenuIndex;
    }
    requestRedraw();
  }
}

// This is the one and only handlePlayingInput
void handlePlayingInput() {
  // Play/Pause Toggle
  if (btnSelectPlay.fell()) {
    prevIsPlaying = isPlaying;
    isPlaying = !isPlaying;
    if (isPlaying) {
      lastTimeUpdate = millis();
    }
    requestRedraw();
  }
  
  // Next Song
  if (btnNextDown.fell()) {
    handleNextSong();
  }
  
  // Prev / Rewind / Back
  if (btnPrevUp.fell()) {
    unsigned long pressTime = millis();
    if (pressTime - lastPrevPressTime < DOUBLE_CLICK_MS) {
      // Double click - go back to browse
      previousState = currentState;
      // --- FIXED ---
      currentState = STATE_MENU; // Go back to the menu, not "BROWSING"
      isPlaying = false;
      prevSelectedSongIndex = -1;  // Reset
      requestRedraw(true);
      lastPrevPressTime = 0;
    } else {
      // Single click - rewind
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
  // --- FIXED ---
  playingSongIndex = (playingSongIndex + 1) % songs.size(); // Use songs.size()
  isPlaying = true;
  prevSongTimeSec = -1;
  currentSongTimeSec = 0;
  songDurationSec = 180 + (playingSongIndex * 15);
  lastTimeUpdate = millis();
  requestRedraw(true);  // Full redraw for new song
}

void handlePrevSong() {
  prevPlayingSongIndex = playingSongIndex;
  playingSongIndex--;
  // --- FIXED ---
  if (playingSongIndex < 0) {
    playingSongIndex = songs.size() - 1; // Use songs.size()
  }
  isPlaying = true;
  prevSongTimeSec = -1;
  currentSongTimeSec = 0;
  songDurationSec = 180 + (playingSongIndex * 15);
  lastTimeUpdate = millis();
  requestRedraw(true);  // Full redraw for new song
}

// ------------------------------------
//     DISPLAY FUNCTIONS
// ------------------------------------

// --- HELPER: Request Redraw ---
void requestRedraw(bool fullRedraw) {
  needsRedraw = true;
  if (fullRedraw) {
    needsFullRedraw = true;
  }
}

// --- HELPER: Format Time ---
String formatTime(int totalSeconds) {
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  char timeString[6];
  sprintf(timeString, "%02d:%02d", minutes, seconds);
  return String(timeString);
}

// --- DRAW: Header ---
void drawHeader(String title) {
  // Draw iPod-style header bar
  tft.fillRect(0, 0, SCREEN_WIDTH, 40, IPOD_BLUE);
  
  // Title
  tft.setTextColor(IPOD_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString(title, SCREEN_WIDTH/2, 20);
  
  // Battery icon (mock)
  drawBattery();
}

// --- DRAW: Battery ---
void drawBattery() {
  // Simple battery icon in top right
  int x = SCREEN_WIDTH - 40;
  int y = 10;
  tft.drawRect(x, y, 30, 20, IPOD_WHITE);
  tft.fillRect(x + 30, y + 6, 3, 8, IPOD_WHITE);
  tft.fillRect(x + 2, y + 2, 20, 16, IPOD_WHITE);
}

// --- DRAW: Scrollbar ---
void drawScrollbar(int currentIndex, int totalItems, int yStart, int height) {
  // Simple check if scrollbar is needed
  if (totalItems * 45 < height) return; 
  
  // Clear previous scrollbar
  tft.fillRect(SCREEN_WIDTH - 10, yStart, 8, height, IPOD_WHITE);
  
  int barHeight = height;
  int thumbHeight = max(20, barHeight / totalItems);
  int thumbY = yStart + (currentIndex * (barHeight - thumbHeight) / (totalItems - 1));
  
  // Draw scrollbar track
  tft.fillRect(SCREEN_WIDTH - 10, yStart, 8, barHeight, IPOD_LIGHT_GRAY);
  // Draw scrollbar thumb
  tft.fillRect(SCREEN_WIDTH - 10, thumbY, 8, thumbHeight, IPOD_DARK_GRAY);
}


// --- UPDATE: Progress Bar (for Playing state) ---
void updateProgressBar() {
  int progressY = 355;  // Fixed position for progress bar
  
  // Only update time if changed
  if (currentSongTimeSec != prevSongTimeSec) {
    // Clear time displays
    tft.fillRect(30, progressY, 60, 16, IPOD_WHITE);
    tft.fillRect(SCREEN_WIDTH - 90, progressY, 60, 16, IPOD_WHITE);
    
    tft.setTextColor(IPOD_BLACK);
    tft.setTextSize(1);
    
    // Current time (left)
    tft.setTextDatum(TL_DATUM);
    tft.drawString(formatTime(currentSongTimeSec), 30, progressY);
    
    // Total time (right)
    tft.setTextDatum(TR_DATUM);
    tft.drawString(formatTime(songDurationSec), SCREEN_WIDTH - 30, progressY);
        float percent = (float)currentSongTimeSec / (float)songDurationSec;
    int barWidth = SCREEN_WIDTH - 60;
    int barX = 30;
    int barY = progressY + 20;
    
    // Clear and redraw progress bar
    tft.fillRoundRect(barX, barY, barWidth, 10, 5, IPOD_LIGHT_GRAY);
    tft.drawRoundRect(barX, barY, barWidth, 10, 5, IPOD_DARK_GRAY);
    
    // Progress bar fill
    int fillWidth = (int)(percent * (barWidth - 2));
    if (fillWidth > 0) {
      tft.fillRoundRect(barX + 1, barY + 1, fillWidth, 8, 4, IPOD_BLUE);
    }
    
    prevSongTimeSec = currentSongTimeSec;
  }
}

// --- UPDATE: Play/Pause Button (for Playing state) ---
void updatePlayPauseButton() {
  int controlsY = 410;  // Fixed position for controls
  int centerX = SCREEN_WIDTH / 2;
  
  // Clear the play/pause button area
  tft.fillCircle(centerX, controlsY, 26, IPOD_WHITE);
  
  if (isPlaying) {
    // Playing - show pause icon
    tft.fillCircle(centerX, controlsY, 25, IPOD_BLUE);
    tft.fillRect(centerX - 8, controlsY - 10, 6, 20, IPOD_WHITE);
    tft.fillRect(centerX + 2, controlsY - 10, 6, 20, IPOD_WHITE);
  } else {
    // Paused - show play icon
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
  // Check if state changed (needs full redraw)
  if (currentState != previousState) {
    needsFullRedraw = true;
    previousState = currentState;
  }
  
  // Only clear screen when absolutely necessary
  if (needsFullRedraw) {
    tft.fillScreen(IPOD_WHITE);
    needsFullRedraw = false;
    
    // Reset previous values to force redraw
    prevSelectedMenuIndex = -1;
    prevSelectedSongIndex = -1;
    prevSongTimeSec = -1;
    prevIsPlaying = !isPlaying;
    prevPlayingSongIndex = -1;
  }
  
  switch (currentState) {
    case STATE_HOME: { // FIXED: Added scope brackets
      // iPod-style splash screen (only draw once)
      tft.fillScreen(IPOD_BLACK);
      
      // Draw iPod logo area
      tft.fillRoundRect(60, 140, 200, 120, 20, IPOD_WHITE);
      tft.setTextColor(IPOD_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(4);
      tft.drawString("iPod", SCREEN_WIDTH/2, 200);
      
      // Click to continue
      tft.setTextColor(IPOD_WHITE);
      tft.setTextSize(2);
      tft.drawString("Click to continue", SCREEN_WIDTH/2, 320);
      
      // Navigation hint
      tft.setTextSize(1);
      tft.drawString("MENU", SCREEN_WIDTH/2, 420);
      break;
    }
    
    // --- FIXED: Replaced entire case with correct logic ---
    case STATE_MENU: {
      String headerTitle = "Menu";
      if (currentMenu == MENU_MAIN) headerTitle = "iPod";
      else if (currentMenu == MENU_ARTISTS) headerTitle = "Artists";
      else if (currentMenu == MENU_ALBUMS) headerTitle = "Albums";
      else if (currentMenu == MENU_SONGS) headerTitle = "Songs";
      
      drawHeader(headerTitle);

      // Clear the menu list area
      tft.fillRect(0, 40, SCREEN_WIDTH, SCREEN_HEIGHT - 40, IPOD_WHITE);
      
      int y_start = 60;
      int row_height = 45;
      // Calculate max items that can fit
      int max_items_on_screen = (SCREEN_HEIGHT - y_start - 20) / row_height; 
      
      int itemsToDraw = min((int)currentMenuItems.size() - menuOffset, max_items_on_screen);
      
      for (int i = 0; i < itemsToDraw; i++) {
        int index = menuOffset + i; // The index in the full currentMenuItems list
        if (index >= currentMenuItems.size()) break;
        
        int yPos = y_start + (i * row_height); // The y-position on the screen
        
        bool selected = (index == selectedMenuIndex);
        
        // --- Logic from the (now deleted) correct drawMenuItem ---
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
        // --- End inlined logic ---
      }
      
      drawScrollbar(selectedMenuIndex, currentMenuItems.size(), y_start, max_items_on_screen * row_height);
      break;
    }
   
    case STATE_PLAYING: { // FIXED: Added scope brackets
      // Static elements (draw once)
      if (playingSongIndex != prevPlayingSongIndex) {
        drawHeader("Now Playing");
        
        // Album art section
        int artSize = 200;
        int artX = (SCREEN_WIDTH - artSize) / 2;
        int artY = 60;
        
        // Album art placeholder with shadow
        tft.fillRect(artX + 3, artY + 3, artSize, artSize, IPOD_DARK_GRAY);
        tft.fillRect(artX, artY, artSize, artSize, IPOD_LIGHT_GRAY);
        tft.drawRect(artX, artY, artSize, artSize, IPOD_DARK_GRAY);
        
        // Album art placeholder text
        tft.setTextColor(IPOD_DARK_GRAY);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.drawString("Album Art", SCREEN_WIDTH/2, artY + artSize/2);
        
        // Song info section
        int infoY = artY + artSize + 20;
        
        // --- FIXED: Use 'songs' vector ---
        // Song title
        tft.setTextColor(IPOD_BLACK);
        tft.setTextSize(2);
        tft.drawString(songs[playingSongIndex].title, SCREEN_WIDTH/2, infoY);
        
        // Artist
        tft.setTextColor(IPOD_DARK_GRAY);
        tft.setTextSize(1);
        tft.drawString(songs[playingSongIndex].artist, SCREEN_WIDTH/2, infoY + 25);
        
        // Album
        tft.drawString(songs[playingSongIndex].album, SCREEN_WIDTH/2, infoY + 40);
        
        // Draw static control buttons
        int controlsY = 410;
        int centerX = SCREEN_WIDTH / 2;
        
        // Previous button (left)
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
        
        // Next button (right)
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
        
        // Control hints at bottom
        tft.setTextDatum(BC_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(IPOD_DARK_GRAY);
        tft.drawString("PREV", centerX - 70, SCREEN_HEIGHT - 10);
        tft.drawString("PLAY/PAUSE", centerX, SCREEN_HEIGHT - 10);
        tft.drawString("NEXT", centerX + 70, SCREEN_HEIGHT - 10);
        
        prevPlayingSongIndex = playingSongIndex;
      }
      
      // Update only dynamic elements
      updateProgressBar();
      
      // Update play/pause button only if state changed
      if (isPlaying != prevIsPlaying) {
        updatePlayPauseButton();
        prevIsPlaying = isPlaying;
      }
      
      break;
    }
  }
}