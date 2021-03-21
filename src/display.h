#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <src/logo.h>
#include <list>

namespace {
    TFT_eSPI        tft;
    TFT_eSprite     sprite(&tft);
}

void setup_display() {
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(1);
    sprite.createSprite(tft.width(), tft.height());
}

void clear_sprite(int bg = TFT_BLACK) {
    sprite.fillSprite(bg);
    if (WiFi.status() == WL_CONNECTED) {
        sprite.drawRect(0, 0, tft.width(), tft.height(), TFT_BLUE);
    }
}

void display(const String& text, int fg = TFT_WHITE, int bg = TFT_BLACK) {
    clear_sprite(bg);
    sprite.setTextSize(1);
    bool nondigits = false;
    for (int i = 0; i < text.length(); i++) {
        char c = text.charAt(i);
        if (c < '0' || c > '9') nondigits = true;
    }
    sprite.setTextFont(nondigits ? 4 : 8);
    sprite.setTextSize(nondigits && text.length() < 10 ? 2 : 1);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(fg, bg);
    sprite.drawString(text, tft.width()/2, tft.height()/2);

    sprite.pushSprite(0, 0);
}

void display(const std::list<String>& lines, int fg = TFT_WHITE, int bg = TFT_BLACK) {
    clear_sprite(bg);
    sprite.setTextSize(1);
    sprite.setTextFont(4);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(fg, bg);

    const int line_height = 32;
    int y = tft.height()/2 - (lines.size()-1) * line_height/2;
    for (auto line : lines) {
        sprite.drawString(line, tft.width()/2, y);
        y += line_height;
    }
    sprite.pushSprite(0, 0);
}

void display_logo() {
    clear_sprite();
    sprite.setSwapBytes(true);
    sprite.pushImage(12, 30, 215, 76, OPERAME_LOGO);
    sprite.pushSprite(0, 0);
}

void panic(const String& message) {
    display(message, TFT_RED);
    delay(5000);
    ESP.restart();
}