#include <U8g2lib.h>
#include <Bounce2.h>
#include <Wire.h> 

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

// --- Song List ---
const char* songList[] = {
  "Song 1.mp3",
  "Track 2 - Artist",
  "My Favorite Tune",
  "Audiofile_004",
  "B-Side",
  "Podcast Ep. 12"
};
const int numSongs = sizeof(songList) / sizeof(songList[0]);

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

  updateDisplay(); // Show the initial (Splash) screen
  delay(1500); // Hold the splash screen
  
  if (currentState == STATE_SPLASH) {
     currentState = STATE_BROWSING;
     updateDisplay();
  }
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
    if (pressTime - lastTimeUpdate < DOUBLE_CLICK_MS) {
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
      
      u8g2.drawRFrame(0, 18, 128, 18, 3); 
      u8g2.setFont(u8g2_font_profont12_tr); 
      
      u8g2.drawStr(4, 32, songList[selectedSongIndex]);

      int nextIndex = (selectedSongIndex + 1) % numSongs;
      u8g2.drawStr(4, 52, songList[nextIndex]);
      break;
    } 

    // --- THIS IS THE MODIFIED SECTION ---
    case STATE_PLAYING: {
      // --- 1. Draw Album Art Placeholder (Left Side) ---
      // This draws a 64x64 box.
      // Replace this with u8g2.drawXBM(0, 0, 64, 64, your_art_array);
      u8g2.drawFrame(0, 0, 64, 64);
      u8g2.setFont(u8g2_font_4x6_tr); // Tiny font for placeholder
      u8g2.drawStr(12, 30, "ALBUM");
      u8g2.drawStr(16, 38, "ART");


      // --- 2. Draw Text (Right Side) ---
      int textX = 66; // Starting X for all text (right of the art)

      // Song Name (truncated to fit 64px width)
      u8g2.setFont(u8g2_font_profont11_tr); // 6px wide font
      char nameBuf[11]; // 64px / 6px/char = ~10 chars + null
      
      // Copy first 10 chars for line 1
      strncpy(nameBuf, songList[playingSongIndex], 10);
      nameBuf[10] = '\0'; 
      u8g2.drawStr(textX, 12, nameBuf);

      // (Optional) Draw line 2 if name is longer than 10 chars
      if (strlen(songList[playingSongIndex]) > 10) {
         strncpy(nameBuf, songList[playingSongIndex] + 10, 10); // Get next 10
         nameBuf[10] = '\0'; 
         u8g2.drawStr(textX, 24, nameBuf); // Draw on line 2
      }

      // Time Display
      u8g2.setFont(u8g2_font_t0_11b_tr); // Small, clear font
      String timeDisplay = formatTime(currentSongTimeSec) + "/" + formatTime(songDurationSec);
      u8g2.drawStr(textX, 40, timeDisplay.c_str());

      // Progress Bar (60px wide)
      float percent = (float)currentSongTimeSec / (float)songDurationSec;
      int barWidth = (int)(percent * 60); // 60px wide bar
      u8g2.drawFrame(textX, 48, 60, 8);      // Outline
      u8g2.drawBox(textX, 48, barWidth, 8); // Fill
      
      // Play/Pause Icon
      u8g2.setFont(u8g2_font_10x20_tr); // Big icon
      if (isPlaying) {
        u8g2.drawStr(textX + 45, 62, ">"); // Play icon
      } else {
        u8g2.drawStr(textX + 42, 62, "||"); // Pause icon
      }
      break;
    }
  }
  
  u8g2.sendBuffer(); // Send the completed drawing to the screen
}