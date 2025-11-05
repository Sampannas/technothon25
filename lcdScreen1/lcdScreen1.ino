#include <U8g2lib.h>
#include <Bounce2.h>
#include <Wire.h> 
#include "LittleFS.h" // <-- For reading the file system
#include <vector>      // <-- For creating a dynamic list

// --- Hardware Definitions ---
// Setup U8g2 for SSD1306 (128x64) using Hardware I2C
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Button Pins (using internal pullups)
#define BTN_PREV_UP_PIN 12
#define BTN_NEXT_DOWN_PIN 13
#define BTN_SELECT_PLAY_PIN 14

// --- Button Logic ---
Bounce btnPrevUp = Bounce();
Bounce btnNextDown = Bounce();
Bounce btnSelectPlay = Bounce();

#define DOUBLE_CLICK_MS 300 
unsigned long lastPrevPressTime = 0;

// --- Song List (Now dynamic!) ---
std::vector<String> songList;
int numSongs = 0;

// --- State Machine ---
enum PlayerState {
  STATE_SPLASH,
  STATE_BROWSING,
  STATE_PLAYING
};
PlayerState currentState = STATE_SPLASH;

// --- Player Status ---
int selectedSongIndex = 0;
int playingSongIndex = 0;
bool isPlaying = false; 

// --- Mock Playback Timer ---
int songDurationSec = 245;    // (4:05) Mock duration
int currentSongTimeSec = 0;   // Current "playback" time
unsigned long lastTimeUpdate = 0; // For 1-second timer

// --- Function Declarations ---
void updateDisplay();
void handleNextSong();
void handlePrevSong();
void loadSongList();
String formatTime(int totalSeconds);

// ------------------------------------
//          SETUP
// ------------------------------------
void setup() {
  Serial.begin(115200);

  // Tell Wire to use pins 21 (SDA) and 22 (SCL)
  Wire.begin(21, 22); 

  // Setup U8g2 Display
  u8g2.begin();

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

  // --- NEW: Load files from LittleFS ---
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  loadSongList(); // Scan the filesystem and fill the list
  numSongs = songList.size(); // Update the total count

  updateDisplay(); // Show the initial (Splash) screen
  delay(1500); // Hold the splash screen
  
  if (currentState == STATE_SPLASH) {
     currentState = STATE_BROWSING;
     updateDisplay();
  }
}

/**
 * @brief Scans the root of LittleFS and adds file names to the list.
 */
void loadSongList() {
  Serial.println("Scanning LittleFS for files...");
  File root = LittleFS.open("/");
  if (!root) {
    Serial.println("- Failed to open root directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      // Add the file name (e.g., "/song1.mp3") to our list
      songList.push_back(String(file.name()));
      Serial.print("  Found file: ");
      Serial.println(file.name());
    }
    file = root.openNextFile();
  }
  
  root.close();
  file.close();
  Serial.println("File scan complete.");
}

// ------------------------------------
//          MAIN LOOP
// ------------------------------------
void loop() {
  // Update all button states
  btnPrevUp.update();
  btnNextDown.update();
  btnSelectPlay.update();

  // --- 1. Handle Mock Playback Timer ---
  if (currentState == STATE_PLAYING && isPlaying) {
    if (millis() - lastTimeUpdate > 1000) {
      currentSongTimeSec++;
      lastTimeUpdate = millis();
      if (currentSongTimeSec > songDurationSec) {
        handleNextSong(); 
      } else {
        updateDisplay(); 
      }
    }
  }

  // Don't process button inputs if no songs were found
  if (numSongs == 0) {
    return; 
  }

  // --- 2. Handle Button Inputs Based on State ---
  switch (currentState) {
    case STATE_SPLASH:
      handleSplashInput();
      break;
    case STATE_BROWSING:
      handleBrowsingInput();
      break;
    case STATE_PLAYING:
      handlePlayingInput();
      break;
  }
}

// ------------------------------------
//      BUTTON HANDLER FUNCTIONS
// ------------------------------------

/** Handles input for the "Welcome" screen */
void handleSplashInput() {
  if (btnSelectPlay.fell()) {
    currentState = STATE_BROWSING;
    updateDisplay();
  }
}

/** Handles input for the "Choose Song" menu */
void handleBrowsingInput() {
  // Button 3: Select Song
  if (btnSelectPlay.fell()) {
    playingSongIndex = selectedSongIndex;
    currentState = STATE_PLAYING;
    isPlaying = true; 
    currentSongTimeSec = 0;
    songDurationSec = 180 + (playingSongIndex * 15); 
    lastTimeUpdate = millis();
    Serial.println("Action: Play");
    updateDisplay(); 
  }

  // Button 2: Scroll Down
  if (btnNextDown.fell()) {
    selectedSongIndex = (selectedSongIndex + 1) % numSongs;
    updateDisplay(); 
  }

  // Button 1: Scroll Up
  if (btnPrevUp.fell()) {
    selectedSongIndex--;
    if (selectedSongIndex < 0) {
      selectedSongIndex = numSongs - 1; 
    }
    updateDisplay(); 
  }
}

/** Handles input for the "Now Playing" screen */
void handlePlayingInput() {
  // Button 3: Play/Pause Toggle
  if (btnSelectPlay.fell()) {
    isPlaying = !isPlaying; 
    if (isPlaying) {
      Serial.println("Action: Resume");
      lastTimeUpdate = millis(); 
    } else {
      Serial.println("Action: Pause");
    }
    updateDisplay(); 
  }

  // Button 2: Next Song
  if (btnNextDown.fell()) {
    handleNextSong();
  }

  // Button 1: Prev / Rewind
  if (btnPrevUp.fell()) {
    unsigned long pressTime = millis();
    if (pressTime - lastPrevPressTime < DOUBLE_CLICK_MS) {
      Serial.println("Action: Previous Song");
      handlePrevSong();
      lastPrevPressTime = 0; 
    } else {
      Serial.println("Action: Rewind song");
      currentSongTimeSec = 0; 
      lastTimeUpdate = millis();
      lastPrevPressTime = pressTime;
      updateDisplay(); 
    }
  }
}

// ------------------------------------
//      ACTION HELPER FUNCTIONS
// ------------------------------------

/** Loads and plays the next song */
void handleNextSong() {
  playingSongIndex = (playingSongIndex + 1) % numSongs;
  isPlaying = true;
  currentSongTimeSec = 0;
  songDurationSec = 180 + (playingSongIndex * 15); 
  lastTimeUpdate = millis();
  updateDisplay(); 
}

/** Loads and plays the previous song */
void handlePrevSong() {
  playingSongIndex--;
  if (playingSongIndex < 0) {
    playingSongIndex = numSongs - 1;
  }
  isPlaying = true;
  currentSongTimeSec = 0;
  songDurationSec = 180 + (playingSongIndex * 15); 
  lastTimeUpdate = millis();
  updateDisplay(); 
}

// ------------------------------------
//      DISPLAY FUNCTIONS
// ------------------------------------

/**
 * @brief Formats total seconds into an "MM:SS" string.
 */
String formatTime(int totalSeconds) {
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  char timeString[6]; // "MM:SS" + null
  sprintf(timeString, "%02d:%02d", minutes, seconds);
  return String(timeString);
}


/**
 * @brief Redraws the ENTIRE LCD screen based on the current state.
 */
void updateDisplay() {
  u8g2.clearBuffer(); // Clear the internal memory

  switch (currentState) {
    case STATE_SPLASH: {
      u8g2.setFont(u8g2_font_ncenB10_tr); 
      u8g2.drawStr(10, 25, "ESP32 Player");
      u8g2.setFont(u8g2_font_7x13_tr); 
      u8g2.drawStr(28, 45, "Loading...");
      break;
    }

    case STATE_BROWSING: { 
      u8g2.setFont(u8g2_font_8x13_tr); 
      u8g2.drawStr(0, 12, "Choose Song:");
      
      // Check if we found any songs
      if (numSongs == 0) {
        u8g2.setFont(u8g2_font_profont12_tr); 
        u8g2.drawStr(10, 40, "No songs found!");
      } else {
        u8g2.drawRFrame(0, 18, 128, 18, 3); 
        u8g2.setFont(u8g2_font_profont12_tr); 
        
        // Use .c_str() to convert Arduino String to C-string
        u8g2.drawStr(4, 32, songList[selectedSongIndex].c_str());

        int nextIndex = (selectedSongIndex + 1) % numSongs;
        u8g2.drawStr(4, 52, songList[nextIndex].c_str());
      }
      break;
    } 

    case STATE_PLAYING: {
      // --- Header: "Now Playing" ---
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(2, 9, "Now Playing:");
      u8g2.drawLine(0, 11, 128, 11); // Separator line

      // --- Song Name (2 lines, centered area) ---
      u8g2.setFont(u8g2_font_7x13_tr); // Nice readable font
      char line1[19]; // ~18 chars for 128px width
      char line2[19];
      
      // Split song name across two lines
      // Use .c_str() to convert Arduino String to C-string
      const char* songName = songList[playingSongIndex].c_str();
      int nameLen = strlen(songName);
      if (nameLen <= 18) {
        // Short name - center it on one line
        strncpy(line1, songName, 18);
        line1[18] = '\0';
        u8g2.drawStr(4, 26, line1);
      } else {
        // Long name - split across two lines
        strncpy(line1, songName, 18);
        line1[18] = '\0';
        strncpy(line2, songName + 18, 18);
        line2[18] = '\0';
        u8g2.drawStr(4, 24, line1);
        u8g2.drawStr(4, 36, line2);
      }

      // --- Time Display (centered) ---
      u8g2.setFont(u8g2_font_6x10_tr);
      String timeDisplay = formatTime(currentSongTimeSec) + " / " + formatTime(songDurationSec);
      int timeWidth = timeDisplay.length() * 6; // Approximate width
      u8g2.drawStr((128 - timeWidth) / 2, 48, timeDisplay.c_str());

      // --- Progress Bar (80% width) + Play/Pause Icon ---
      float percent = (float)currentSongTimeSec / (float)songDurationSec;
      int barFullWidth = 102; // ~80% of 128px
      int barWidth = (int)(percent * barFullWidth);
      u8g2.drawFrame(2, 54, barFullWidth, 8);        // Outline
      u8g2.drawBox(2, 54, barWidth, 8);              // Fill

      // Play/Pause Icon (next to progress bar)
      u8g2.setFont(u8g2_font_9x15_tr); // Medium-large icon font
      if (isPlaying) {
        u8g2.drawStr(110, 62, ">");  // Play triangle
      } else {
        u8g2.drawStr(108, 62, "||"); // Pause bars
      }
      break;
    }
  }
  
  u8g2.sendBuffer(); // Send the completed drawing to the screen
}