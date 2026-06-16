#include <FS.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <M5Unified.h>
#include <math.h>
#include <string.h>

#include "src/cards.h"

static constexpr const char* CARD_BACK_IMAGE = "/img/card_back/CardBacks.jpg";
static constexpr int CARD_SOURCE_W = 115;
static constexpr int CARD_SOURCE_H = 201;
static constexpr int SCREEN_MARGIN = 8;
static constexpr int ANIMATION_STEPS = 8;
static constexpr int ANIMATION_DELAY_MS = 8;
static constexpr int SHUFFLE_STEPS = 10;
static constexpr int SHUFFLE_DELAY_MS = 35;
static constexpr int PLACE_STEPS = 8;
static constexpr int PLACE_DELAY_MS = 18;
static constexpr int SPREAD_STEPS = 7;
static constexpr int SPREAD_DELAY_MS = 8;
static constexpr int SELECT_STEPS = 10;
static constexpr int SELECT_DELAY_MS = 14;
static constexpr int SPREAD_ENTRY_STEPS = 8;
static constexpr int SPREAD_ENTRY_DELAY_MS = 10;
static constexpr int SPREAD_SHAKE_PASSES = 5;
static constexpr int SPREAD_SHAKE_STEPS = 3;
static constexpr int SPREAD_SHAKE_DELAY_MS = 6;
static constexpr uint32_t SPREAD_HOLD_SCROLL_MS = 90;
static constexpr float SHAKE_THRESHOLD = 2.2f;
static constexpr uint32_t SHAKE_COOLDOWN_MS = 900;
static constexpr float SPREAD_SHAKE_PEAK_THRESHOLD = 0.75f;
static constexpr float SPREAD_SHAKE_RELEASE_THRESHOLD = 0.18f;
static constexpr uint32_t SPREAD_SHAKE_WINDOW_MS = 900;
static constexpr uint32_t SPREAD_SHAKE_MIN_GAP_MS = 70;
static constexpr uint32_t SPREAD_SHAKE_COOLDOWN_MS = 1100;

enum ShakeDirection {
  SHAKE_NONE,
  SHAKE_HORIZONTAL,
  SHAKE_VERTICAL
};

bool faceUp = false;
bool showingCardName = false;
bool showingCardSpread = false;
bool frontButtonHoldConsumed = false;
bool sideButtonHoldConsumed = false;
bool suppressSpreadHoldUntilRelease = false;
bool storageReady = false;
uint32_t lastSpreadHoldScrollMs = 0;
float cardScale = 0.5f;
int cardW = 150;
int cardH = 264;
int cardX = 0;
int cardY = 0;
fs::FS* cardFS = nullptr;
M5Canvas frameCanvas(&M5.Display);
int deck[CARD_COUNT];
bool reversedDeck[CARD_COUNT];
int currentDeckPosition = 0;
char reversedImagePath[96];

const char* currentImagePath(int index, bool showFace, bool reversed = false) {
  if (!showFace) {
    return CARD_BACK_IMAGE;
  }

  if (!reversed) {
    return tarotCards[index].image;
  }

  const char* filename = strrchr(tarotCards[index].image, '/');
  if (filename == nullptr) {
    return tarotCards[index].image;
  }

  snprintf(reversedImagePath, sizeof(reversedImagePath), "/img/card_front_reversed/%s", filename + 1);
  return reversedImagePath;
}

int currentCardIndex() {
  return deck[currentDeckPosition];
}

bool currentCardReversed() {
  return faceUp && reversedDeck[currentDeckPosition];
}

void choosePortraitRotation() {
  M5.Display.setRotation(0);
  if (M5.Display.width() > M5.Display.height()) {
    M5.Display.setRotation(1);
  }
}

void updateCardLayout() {
  const int screenW = M5.Display.width();
  const int screenH = M5.Display.height();
  const int maxW = screenW - (SCREEN_MARGIN * 2);
  const int maxH = screenH - (SCREEN_MARGIN * 2);
  const float scaleW = static_cast<float>(maxW) / CARD_SOURCE_W;
  const float scaleH = static_cast<float>(maxH) / CARD_SOURCE_H;

  cardScale = min(scaleW, scaleH);
  cardW = static_cast<int>(CARD_SOURCE_W * cardScale);
  cardH = static_cast<int>(CARD_SOURCE_H * cardScale);
  cardX = (screenW - cardW) / 2;
  cardY = (screenH - cardH) / 2;
}

void drawStatus(const char* message) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(1);
  M5.Display.drawString(message, M5.Display.width() / 2, M5.Display.height() / 2);
}

void drawCardImageOn(
  M5Canvas& canvas,
  int index,
  bool showFace,
  bool reversed,
  int x,
  int y,
  float scaleX,
  float scaleY
) {
  if (!storageReady || cardFS == nullptr) {
    drawStatus("Cards not ready");
    return;
  }

  canvas.drawJpgFile(
    *cardFS,
    currentImagePath(index, showFace, reversed),
    x,
    y,
    0,
    0,
    0,
    0,
    scaleX,
    scaleY
  );
}

void drawCardOn(
  M5Canvas& canvas,
  int index,
  bool showFace,
  int xOffset,
  float scaleX = -1.0f,
  bool reversed = false
) {
  if (scaleX < 0.0f) {
    scaleX = cardScale;
  }

  const int scaledW = static_cast<int>(CARD_SOURCE_W * scaleX);
  const int x = cardX + xOffset + ((cardW - scaledW) / 2);

  drawCardImageOn(canvas, index, showFace, reversed, x, cardY, scaleX, cardScale);
}

void drawCardAt(
  M5Canvas& canvas,
  int index,
  bool showFace,
  int x,
  int y,
  float scale = -1.0f,
  bool reversed = false
) {
  if (scale < 0.0f) {
    scale = cardScale;
  }

  drawCardImageOn(canvas, index, showFace, reversed, x, y, scale, scale);
}

void drawCurrentCard() {
  showingCardName = false;
  showingCardSpread = false;
  frameCanvas.fillScreen(TFT_BLACK);
  drawCardOn(frameCanvas, currentCardIndex(), faceUp, 0, cardScale, currentCardReversed());
  frameCanvas.pushSprite(0, 0);
}

int wrappedDeckPosition(int position) {
  while (position < 0) {
    position += CARD_COUNT;
  }

  return position % CARD_COUNT;
}

float spreadScaleForRelative(float rel) {
  const float distance = fabsf(rel);
  if (distance < 0.01f) {
    return 0.55f;
  }

  if (distance < 1.01f) {
    return 0.55f - (0.13f * distance);
  }

  if (distance < 2.01f) {
    return 0.42f - (0.10f * (distance - 1.0f));
  }

  return 0.0f;
}

void drawSpreadCard(int cardIndex, float rel, bool selected) {
  const int centerX = frameCanvas.width() / 2;
  const int centerY = frameCanvas.height() / 2;
  const int spacing = 43;
  const float scale = spreadScaleForRelative(rel);

  if (scale <= 0.0f) {
    return;
  }

  const int w = static_cast<int>(CARD_SOURCE_W * scale);
  const int h = static_cast<int>(CARD_SOURCE_H * scale);
  const int x = centerX + static_cast<int>(rel * spacing) - (w / 2);
  const int y = centerY - (h / 2);

  drawCardAt(frameCanvas, cardIndex, false, x, y, scale);

  if (selected) {
    frameCanvas.drawRect(x - 3, y - 3, w + 6, h + 6, TFT_WHITE);
    frameCanvas.drawRect(x - 4, y - 4, w + 8, h + 8, TFT_DARKGREY);
  }
}

void drawCardSpreadFrame(int centerPosition) {
  frameCanvas.fillScreen(TFT_BLACK);

  for (int rel = -2; rel <= 2; ++rel) {
    if (rel == 0) {
      continue;
    }

    const int position = wrappedDeckPosition(centerPosition + rel);
    drawSpreadCard(deck[position], rel, false);
  }

  drawSpreadCard(deck[centerPosition], 0, true);
  frameCanvas.pushSprite(0, 0);
}

void drawCardSpreadScreen() {
  showingCardName = false;
  showingCardSpread = true;
  lastSpreadHoldScrollMs = millis();
  drawCardSpreadFrame(currentDeckPosition);
}

void enterCardSpreadScreen() {
  const int cardIndex = currentCardIndex();
  const int centerX = frameCanvas.width() / 2;
  const int centerY = frameCanvas.height() / 2;
  const float endScale = spreadScaleForRelative(0.0f);

  showingCardName = false;

  for (int step = 0; step <= SPREAD_ENTRY_STEPS; ++step) {
    const float t = static_cast<float>(step) / SPREAD_ENTRY_STEPS;
    const float scale = cardScale + ((endScale - cardScale) * t);
    const int w = static_cast<int>(CARD_SOURCE_W * scale);
    const int h = static_cast<int>(CARD_SOURCE_H * scale);
    const int x = centerX - (w / 2);
    const int y = centerY - (h / 2);

    frameCanvas.fillScreen(TFT_BLACK);
    drawCardAt(frameCanvas, cardIndex, false, x, y, scale);
    frameCanvas.pushSprite(0, 0);
    delay(SPREAD_ENTRY_DELAY_MS);
  }

  drawCardSpreadScreen();
}

void advanceCardSpreadRight() {
  const int oldPosition = currentDeckPosition;
  const int nextPosition = wrappedDeckPosition(currentDeckPosition + 1);

  for (int step = 0; step <= SPREAD_STEPS; ++step) {
    const float t = static_cast<float>(step) / SPREAD_STEPS;
    frameCanvas.fillScreen(TFT_BLACK);

    for (int rel = -2; rel <= 2; ++rel) {
      const int position = wrappedDeckPosition(oldPosition + rel);
      if (position == nextPosition) {
        continue;
      }

      drawSpreadCard(deck[position], static_cast<float>(rel) - t, false);
    }

    drawSpreadCard(deck[nextPosition], 1.0f - t, true);
    frameCanvas.pushSprite(0, 0);
    delay(SPREAD_DELAY_MS);
  }

  currentDeckPosition = nextPosition;
  faceUp = false;
  drawCardSpreadScreen();
}

void advanceCardSpreadRightFast(int stepCount, int delayMs) {
  const int oldPosition = currentDeckPosition;
  const int nextPosition = wrappedDeckPosition(currentDeckPosition + 1);

  for (int step = 0; step <= stepCount; ++step) {
    const float t = static_cast<float>(step) / stepCount;
    frameCanvas.fillScreen(TFT_BLACK);

    for (int rel = -2; rel <= 2; ++rel) {
      const int position = wrappedDeckPosition(oldPosition + rel);
      if (position == nextPosition) {
        continue;
      }

      drawSpreadCard(deck[position], static_cast<float>(rel) - t, false);
    }

    drawSpreadCard(deck[nextPosition], 1.0f - t, true);
    frameCanvas.pushSprite(0, 0);
    delay(delayMs);
  }

  currentDeckPosition = nextPosition;
  faceUp = false;
  drawCardSpreadFrame(currentDeckPosition);
}

void shakeScrollCardSpread() {
  for (int i = 0; i < SPREAD_SHAKE_PASSES; ++i) {
    advanceCardSpreadRightFast(SPREAD_SHAKE_STEPS, SPREAD_SHAKE_DELAY_MS);
  }

  drawCardSpreadScreen();
}

void selectCenteredSpreadCard() {
  const int cardIndex = currentCardIndex();
  const int centerX = frameCanvas.width() / 2;
  const int centerY = frameCanvas.height() / 2;
  const float startScale = spreadScaleForRelative(0.0f);

  for (int step = 0; step <= SELECT_STEPS; ++step) {
    const float t = static_cast<float>(step) / SELECT_STEPS;
    const float scale = startScale + ((cardScale - startScale) * t);
    const int w = static_cast<int>(CARD_SOURCE_W * scale);
    const int h = static_cast<int>(CARD_SOURCE_H * scale);
    const int x = centerX - (w / 2);
    const int y = centerY - (h / 2);

    frameCanvas.fillScreen(TFT_BLACK);
    drawCardAt(frameCanvas, cardIndex, false, x, y, scale);
    frameCanvas.pushSprite(0, 0);
    delay(SELECT_DELAY_MS);
  }

  faceUp = false;
  drawCurrentCard();
}

void drawCardNameScreen() {
  const char* name = tarotCards[currentCardIndex()].name;
  const int maxLineWidth = frameCanvas.width() - (SCREEN_MARGIN * 2);
  const int lineHeight = 20;
  char lines[4][28] = {};
  int lineCount = 0;
  int pos = 0;

  showingCardName = true;
  frameCanvas.fillScreen(TFT_BLACK);
  frameCanvas.setTextDatum(middle_center);
  frameCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  frameCanvas.setTextSize(2);

  while (name[pos] != '\0' && lineCount < 4) {
    char candidate[28] = {};
    char best[28] = {};
    int candidateLen = 0;
    int bestLen = 0;
    int bestEnd = pos;
    int scan = pos;

    while (name[scan] == ' ') {
      ++scan;
    }

    while (name[scan] != '\0') {
      char word[16] = {};
      int wordLen = 0;

      while (name[scan] == ' ') {
        ++scan;
      }

      while (name[scan] != '\0' && name[scan] != ' ' && wordLen < static_cast<int>(sizeof(word)) - 1) {
        word[wordLen++] = name[scan++];
      }

      if (wordLen == 0) {
        break;
      }

      char test[28] = {};
      if (candidateLen == 0) {
        snprintf(test, sizeof(test), "%s", word);
      } else {
        snprintf(test, sizeof(test), "%s %s", candidate, word);
      }

      if (frameCanvas.textWidth(test) > maxLineWidth && candidateLen > 0) {
        break;
      }

      snprintf(candidate, sizeof(candidate), "%s", test);
      candidateLen = strlen(candidate);
      snprintf(best, sizeof(best), "%s", candidate);
      bestLen = candidateLen;
      bestEnd = scan;
    }

    if (bestLen == 0) {
      break;
    }

    snprintf(lines[lineCount], sizeof(lines[lineCount]), "%s", best);
    ++lineCount;
    pos = bestEnd;
  }

  const int blockHeight = lineCount * lineHeight;
  const int firstY = (frameCanvas.height() - blockHeight) / 2 + (lineHeight / 2);

  for (int i = 0; i < lineCount; ++i) {
    frameCanvas.drawString(lines[i], frameCanvas.width() / 2, firstY + (i * lineHeight));
  }

  frameCanvas.pushSprite(0, 0);
}

void placeCardOnTop(int nextPosition, ShakeDirection direction = SHAKE_HORIZONTAL) {
  const int nextCard = deck[nextPosition];
  const int startX = direction == SHAKE_VERTICAL ? cardX : M5.Display.width() + 4;
  const int startY = direction == SHAKE_VERTICAL ? M5.Display.height() + 4 : cardY;

  for (int step = 0; step <= PLACE_STEPS; ++step) {
    const float t = static_cast<float>(step) / PLACE_STEPS;
    const int x = startX + static_cast<int>((cardX - startX) * t);
    const int y = startY + static_cast<int>((cardY - startY) * t);

    frameCanvas.fillScreen(TFT_BLACK);
    drawCardAt(frameCanvas, currentCardIndex(), false, cardX - 3, cardY + 3);
    drawCardAt(frameCanvas, nextCard, false, x, y);
    frameCanvas.pushSprite(0, 0);
    delay(PLACE_DELAY_MS);
  }

  currentDeckPosition = nextPosition;
  faceUp = false;
  drawCurrentCard();
}

void slideToDeckPosition(int nextPosition, bool currentFaceVisible, bool nextFaceVisible) {
  showingCardName = false;
  showingCardSpread = false;
  const int screenW = M5.Display.width();
  const int currentCard = currentCardIndex();
  const int nextCard = deck[nextPosition];

  for (int step = 0; step <= ANIMATION_STEPS; ++step) {
    const int currentOffset = map(step, 0, ANIMATION_STEPS, 0, -screenW);
    const int nextOffset = map(step, 0, ANIMATION_STEPS, screenW, 0);

    frameCanvas.fillScreen(TFT_BLACK);
    drawCardOn(frameCanvas, currentCard, currentFaceVisible, currentOffset, cardScale, currentFaceVisible && currentCardReversed());
    drawCardOn(frameCanvas, nextCard, nextFaceVisible, nextOffset, cardScale, nextFaceVisible && reversedDeck[nextPosition]);
    frameCanvas.pushSprite(0, 0);
    delay(ANIMATION_DELAY_MS);
  }

  currentDeckPosition = nextPosition;
  faceUp = nextFaceVisible;
  drawCurrentCard();
}

void slideToNextCard(bool currentFaceVisible, bool nextFaceVisible) {
  slideToDeckPosition((currentDeckPosition + 1) % CARD_COUNT, currentFaceVisible, nextFaceVisible);
}

void slideToNextCard() {
  slideToNextCard(faceUp, false);
}

int randomDifferentDeckPosition() {
  if (CARD_COUNT <= 1) {
    return currentDeckPosition;
  }

  int nextPosition = currentDeckPosition;
  while (nextPosition == currentDeckPosition) {
    nextPosition = random(CARD_COUNT);
  }

  return nextPosition;
}

void drawShuffleFrame(int step, ShakeDirection direction) {
  const int offsetPattern[] = {-18, 12, -8, 18, -14, 8};
  const int offset = offsetPattern[step % 6];
  const int xOffset = direction == SHAKE_VERTICAL ? 0 : offset;
  const int yOffset = direction == SHAKE_VERTICAL ? offset : 0;
  const float scaleX = cardScale * (step % 2 == 0 ? 0.94f : 1.0f);

  frameCanvas.fillScreen(TFT_BLACK);
  drawCardAt(frameCanvas, currentCardIndex(), false, cardX + xOffset, cardY + yOffset, scaleX);

  if (step % 2 == 0) {
    drawCardAt(frameCanvas, currentCardIndex(), false, cardX - (xOffset / 2), cardY - (yOffset / 2), cardScale);
  }

  frameCanvas.pushSprite(0, 0);
}

void shuffleToRandomCardBack(ShakeDirection direction) {
  const int nextPosition = randomDifferentDeckPosition();

  for (int step = 0; step < SHUFFLE_STEPS; ++step) {
    drawShuffleFrame(step, direction);
    delay(SHUFFLE_DELAY_MS);
  }

  placeCardOnTop(nextPosition, direction);
}

void flipCurrentCard() {
  showingCardName = false;
  showingCardSpread = false;
  const bool nextFaceUp = !faceUp;

  for (int step = 0; step <= ANIMATION_STEPS; ++step) {
    const float t = static_cast<float>(step) / ANIMATION_STEPS;
    const float widthScale = cardScale * (1.0f - t);
    frameCanvas.fillScreen(TFT_BLACK);
    drawCardOn(frameCanvas, currentCardIndex(), faceUp, 0, max(widthScale, 0.02f), currentCardReversed());
    frameCanvas.pushSprite(0, 0);
    delay(ANIMATION_DELAY_MS);
  }

  for (int step = 0; step <= ANIMATION_STEPS; ++step) {
    const float t = static_cast<float>(step) / ANIMATION_STEPS;
    const float widthScale = cardScale * t;
    frameCanvas.fillScreen(TFT_BLACK);
    drawCardOn(frameCanvas, currentCardIndex(), nextFaceUp, 0, max(widthScale, 0.02f), nextFaceUp && reversedDeck[currentDeckPosition]);
    frameCanvas.pushSprite(0, 0);
    delay(ANIMATION_DELAY_MS);
  }

  faceUp = nextFaceUp;
  drawCurrentCard();
}

bool rightSideButtonLongPressed() {
  return M5.BtnB.wasHold() || M5.BtnPWR.wasHold() || M5.BtnC.wasHold();
}

bool rightSideButtonReleased() {
  return M5.BtnB.wasReleased() || M5.BtnPWR.wasReleased() || M5.BtnC.wasReleased();
}

bool rightSideButtonHolding() {
  return M5.BtnB.isHolding() || M5.BtnPWR.isHolding() || M5.BtnC.isHolding();
}

bool frontBlueButtonLongPressed() {
  return M5.BtnA.wasHold();
}

bool frontBlueButtonReleased() {
  return M5.BtnA.wasReleased();
}

bool spreadShakeDetected() {
  static uint32_t lastSpreadShakeMs = 0;
  static bool hasPreviousAccel = false;
  static float previousAx = 0.0f;
  static float previousAy = 0.0f;
  static float previousAz = 0.0f;
  static bool gestureActive = false;
  static uint32_t gestureStartMs = 0;
  static uint32_t lastPeakMs = 0;
  static int swingCount = 0;
  static int lastPeakSign = 0;

  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) {
    return false;
  }

  if (!hasPreviousAccel) {
    previousAx = ax;
    previousAy = ay;
    previousAz = az;
    hasPreviousAccel = true;
    return false;
  }

  const float dx = ax - previousAx;
  const float dy = ay - previousAy;
  const float dz = az - previousAz;
  const float movement = sqrtf((dx * dx) + (dy * dy) + (dz * dz));
  const uint32_t now = millis();

  previousAx = ax;
  previousAy = ay;
  previousAz = az;

  if (now - lastSpreadShakeMs < SPREAD_SHAKE_COOLDOWN_MS) {
    return false;
  }

  if (gestureActive && now - gestureStartMs > SPREAD_SHAKE_WINDOW_MS) {
    gestureActive = false;
    swingCount = 0;
    lastPeakSign = 0;
  }

  if (movement > SPREAD_SHAKE_PEAK_THRESHOLD) {
    const float adx = fabsf(dx);
    const float ady = fabsf(dy);
    const float adz = fabsf(dz);
    int peakSign = dx >= 0.0f ? 1 : -1;

    if (ady >= adx && ady >= adz) {
      peakSign = dy >= 0.0f ? 1 : -1;
    } else if (adz >= adx && adz >= ady) {
      peakSign = dz >= 0.0f ? 1 : -1;
    }

    if (!gestureActive) {
      gestureActive = true;
      gestureStartMs = now;
      lastPeakMs = now;
      swingCount = 1;
      lastPeakSign = peakSign;
      return false;
    }

    if (now - lastPeakMs >= SPREAD_SHAKE_MIN_GAP_MS) {
      if (peakSign != lastPeakSign) {
        ++swingCount;
        lastPeakSign = peakSign;
      }

      lastPeakMs = now;
    }
  }

  if (gestureActive && swingCount >= 4 && movement < SPREAD_SHAKE_RELEASE_THRESHOLD && now - gestureStartMs <= SPREAD_SHAKE_WINDOW_MS) {
    gestureActive = false;
    swingCount = 0;
    lastPeakSign = 0;
    lastSpreadShakeMs = now;
    return true;
  }

  return false;
}

ShakeDirection detectShakeDirection() {
  static uint32_t lastShakeMs = 0;
  static bool hasPreviousAccel = false;
  static float previousAx = 0.0f;
  static float previousAy = 0.0f;

  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) {
    return SHAKE_NONE;
  }

  if (!hasPreviousAccel) {
    previousAx = ax;
    previousAy = ay;
    hasPreviousAccel = true;
    return SHAKE_NONE;
  }

  const float force = sqrtf((ax * ax) + (ay * ay) + (az * az));
  const float horizontalForce = fabsf(ay - previousAy);
  const float verticalForce = fabsf(ax - previousAx);
  const uint32_t now = millis();

  previousAx = ax;
  previousAy = ay;

  if (force > SHAKE_THRESHOLD && (lastShakeMs == 0 || now - lastShakeMs > SHAKE_COOLDOWN_MS)) {
    lastShakeMs = now;
    return horizontalForce >= verticalForce ? SHAKE_HORIZONTAL : SHAKE_VERTICAL;
  }

  return SHAKE_NONE;
}

bool storageHasCards(fs::FS& fs) {
  return fs.exists(tarotCards[0].image) && fs.exists(CARD_BACK_IMAGE);
}

bool beginCardStorage() {
  if (LittleFS.begin(false) && storageHasCards(LittleFS)) {
    cardFS = &LittleFS;
    return true;
  }

  if (SPIFFS.begin(false) && storageHasCards(SPIFFS)) {
    cardFS = &SPIFFS;
    return true;
  }

  return false;
}

void shuffleDeck() {
  for (int i = 0; i < CARD_COUNT; ++i) {
    deck[i] = i;
    reversedDeck[i] = random(2) == 0;
  }

  for (int i = CARD_COUNT - 1; i > 0; --i) {
    const int j = random(i + 1);
    const int card = deck[i];
    const bool reversed = reversedDeck[i];

    deck[i] = deck[j];
    reversedDeck[i] = reversedDeck[j];
    deck[j] = card;
    reversedDeck[j] = reversed;
  }

  currentDeckPosition = 0;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  randomSeed(micros());

  choosePortraitRotation();
  updateCardLayout();
  frameCanvas.setColorDepth(16);
  frameCanvas.createSprite(M5.Display.width(), M5.Display.height());
  M5.Display.setBrightness(180);
  drawStatus("Loading");

  storageReady = beginCardStorage();
  if (!storageReady) {
    drawStatus("No card files");
    return;
  }

  shuffleDeck();
  drawCurrentCard();
}

void loop() {
  M5.update();

  if (!storageReady) {
    delay(50);
    return;
  }

  if (!showingCardName && !showingCardSpread && !faceUp && rightSideButtonLongPressed()) {
    sideButtonHoldConsumed = true;
    suppressSpreadHoldUntilRelease = true;
    enterCardSpreadScreen();
  }

  if (showingCardSpread && !suppressSpreadHoldUntilRelease && rightSideButtonHolding()) {
    const uint32_t now = millis();
    if (now - lastSpreadHoldScrollMs >= SPREAD_HOLD_SCROLL_MS) {
      sideButtonHoldConsumed = true;
      lastSpreadHoldScrollMs = now;
      advanceCardSpreadRight();
    }
  }

  if (rightSideButtonReleased()) {
    suppressSpreadHoldUntilRelease = false;

    if (sideButtonHoldConsumed) {
      sideButtonHoldConsumed = false;
    } else if (showingCardSpread) {
      advanceCardSpreadRight();
    } else if (!showingCardName && !faceUp) {
      slideToNextCard();
    }
  }

  if (frontBlueButtonLongPressed() && faceUp && !showingCardName) {
    frontButtonHoldConsumed = true;
    drawCardNameScreen();
  }

  if (frontBlueButtonReleased()) {
    if (frontButtonHoldConsumed) {
      frontButtonHoldConsumed = false;
    } else if (showingCardSpread) {
      selectCenteredSpreadCard();
    } else if (showingCardName) {
      drawCurrentCard();
    } else {
      flipCurrentCard();
    }
  }

  if (showingCardSpread) {
    if (spreadShakeDetected()) {
      shakeScrollCardSpread();
    }
  } else {
    const ShakeDirection shakeDirection = showingCardName ? SHAKE_NONE : detectShakeDirection();
    if (shakeDirection != SHAKE_NONE && !faceUp) {
      shuffleToRandomCardBack(shakeDirection);
    }
  }

  delay(10);
}
