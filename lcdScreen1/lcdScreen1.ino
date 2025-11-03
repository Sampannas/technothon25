#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Bounce2.h>

// --- Hardware Definitions ---

// I2C LCD Screen (16 columns, 2 rows)
// Common address is 0x27, but 0x3F is also possible.
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Button Pins (using internal pullups)
#define BTN_PREV_UP_PIN 12
#define BTN_NEXT_DOWN_PIN 13
#define BTN_SELECT_PLAY_PIN 14

// --- Button Logic ---
Bounce btnPrevUp = Bounce();
Bounce btnNextDown = Bounce();
Bounce btnSelectPlay = Bounce();

// Time in milliseconds for a double-click
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
  STATE_BROWSING,
  STATE_PLAYING,
  STATE_PAUSED
};
PlayerState currentState = STATE_BROWSING;

int selectedSongIndex = 0; // For the browsing menu
int playingSongIndex = 0;  // The song currently loaded

// --- Function Declarations ---
void updateDisplay();


void setup() {
  Serial.begin(115200);

  // Setup I2C LCD
  Wire.begin(); // For ESP32, default I2C is 21 (SDA), 22 (SCL)
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("MP3 Player");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");

  // Setup Buttons
  pinMode(BTN_PREV_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_NEXT_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_SELECT_PLAY_PIN, INPUT_PULLUP);

  btnPrevUp.attach(BTN_PREV_UP_PIN);
  btnNextDown.attach(BTN_NEXT_DOWN_PIN);
  btnSelectPlay.attach(BTN_SELECT_PLAY_PIN);

  btnPrevUp.interval(25); // Debounce interval in ms
  btnNextDown.interval(25);
  btnSelectPlay.interval(25);

  delay(1000);
  updateDisplay(); // Show the initial browsing screen
}

void loop() {
  // Update all button states
  btnPrevUp.update();
  btnNextDown.update();
  btnSelectPlay.update();

  // --- Handle Button Presses ---

  // Button 3: Select / Play / Pause
  if (btnSelectPlay.fell()) {
    if (currentState == STATE_BROWSING) {
      // Start playing the selected song
      playingSongIndex = selectedSongIndex;
      currentState = STATE_PLAYING;
      Serial.println("Action: Play");
    } else if (currentState == STATE_PLAYING) {
      // Pause the song
      currentState = STATE_PAUSED;
      Serial.println("Action: Pause");
    } else if (currentState == STATE_PAUSED) {
      // Resume the song
      currentState = STATE_PLAYING;
      Serial.println("Action: Resume");
    }
    updateDisplay(); // Update screen on state change
  }

  // Button 2: Next / Down
  if (btnNextDown.fell()) {
    if (currentState == STATE_BROWSING) {
      // Scroll down the list
      selectedSongIndex = (selectedSongIndex + 1) % numSongs;
      Serial.print("Scroll Down. Selected: ");
      Serial.println(selectedSongIndex);
    } else {
      // Skip to the next track
      playingSongIndex = (playingSongIndex + 1) % numSongs;
      currentState = STATE_PLAYING; // Ensure it's playing
      Serial.print("Action: Next Song. Now playing: ");
      Serial.println(playingSongIndex);
    }
    updateDisplay();
  }

  // Button 1: Prev / Up / Rewind (with double-click)
  if (btnPrevUp.fell()) {
    if (currentState == STATE_BROWSING) {
      // Scroll up the list
      selectedSongIndex--;
      if (selectedSongIndex < 0) {
        selectedSongIndex = numSongs - 1; // Wrap around
      }
      Serial.print("Scroll Up. Selected: ");
      Serial.println(selectedSongIndex);
    } else {
      // Playing or Paused state: Handle single/double click
      unsigned long pressTime = millis();
      if (pressTime - lastPrevPressTime < DOUBLE_CLICK_MS) {
        // --- Double Click: Previous Song ---
        playingSongIndex--;
        if (playingSongIndex < 0) {
          playingSongIndex = numSongs - 1; // Wrap around
        }
        currentState = STATE_PLAYING; // Ensure it's playing
        Serial.print("Action: Previous Song. Now playing: ");
        Serial.println(playingSongIndex);
        lastPrevPressTime = 0; // Reset timer to prevent triple-click
      } else {
        // --- Single Click: Rewind to Beginning ---
        // (No state change, just an action)
        Serial.print("Action: Rewind song ");
        Serial.println(playingSongIndex);
        lastPrevPressTime = pressTime;
      }
    }
    updateDisplay();
  }
}

/**
 * @brief Updates the LCD screen based on the current player state.
 */
/**
 * @brief Updates the LCD screen based on the current player state.
 */
void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);

  switch (currentState) {
    case STATE_BROWSING: {  // <-- ADDED this curly brace
      // Show the selected song with a cursor
      lcd.print(">"); // Cursor
      lcd.print(songList[selectedSongIndex]);

      // Show the next song on the second line (if it exists)
      lcd.setCursor(0, 1);
      int nextIndex = (selectedSongIndex + 1) % numSongs; // This is now safe
      lcd.print(" "); // Indent
      lcd.print(songList[nextIndex]);
      break;
    } // <-- AND ADDED this curly brace

    case STATE_PLAYING:
      // Show "Now Playing" icon and song name
      lcd.print("Now Playing  >"); // > is a "play" icon
      lcd.setCursor(0, 1);
      lcd.print(songList[playingSongIndex]);
      break;

    case STATE_PAUSED:
      // Show "Paused" icon and song name
      lcd.print("Paused      ||"); // || is a "pause" icon
      lcd.setCursor(0, 1);
      lcd.print(songList[playingSongIndex]);
      break;
  }
}