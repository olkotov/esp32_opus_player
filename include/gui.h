// Oleg Kotov

#pragma once

#include <arduino.h>
#include <wire.h>

#include <adafruit_gfx.h>
#include <adafruit_ssd1306.h>

#include "gui_icons.h"
#include "utils.h"

#include "rufont.h"

#define DISPLAY_SDA_PIN 21
#define DISPLAY_SCL_PIN 22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

#define OLED_RESET_PIN -1

enum draw_state
{
    draw_bluetooth,
    draw_playback,
    draw_menu,
    draw_error_msg
};

size_t utf8len( const char* str )
{
    size_t len = 0;

    for ( size_t i = 0; *str != 0; ++len )
    {
        int v0 = (*str & 0x80) >> 7;
        int v1 = (*str & 0x40) >> 6;
        int v2 = (*str & 0x20) >> 5;
        int v3 = (*str & 0x10) >> 4;
        str += 1 + v0 * v1 + v0 * v1 * v2 + v0 * v1 * v2 * v3;
    }

    return len;
}

int utf8len_2( const char* str )
{
    int len = 0;
    while (*str) len += (*str++ & 0xc0) != 0x80;
    return len;
}

/* Recode russian fonts from UTF-8 to Windows-1251 */
String utf8rus(String source)
{
  int i,k;
  String target;
  unsigned char n;
  char m[2] = { '0', '\0' };

  k = source.length(); i = 0;

  while (i < k) {
    n = source[i]; i++;

    if (n >= 0xC0) {
      switch (n) {
        case 0xD0: {
          n = source[i]; i++;
          if (n == 0x81) { n = 0xA8; break; }
          if (n >= 0x90 && n <= 0xBF) n = n + 0x30;
          break;
        }
        case 0xD1: {
          n = source[i]; i++;
          if (n == 0x91) { n = 0xB8; break; }
          if (n >= 0x80 && n <= 0x8F) n = n + 0x70;
          break;
        }
      }
    }
    m[0] = n; target = target + String(m);
  }
return target;
}

class PlayerUI
{
public:

    void init()
    {
        Wire.begin( DISPLAY_SDA_PIN, DISPLAY_SCL_PIN );
    
        if( m_display.begin( SSD1306_SWITCHCAPVCC, 0x3c ) == false )
        { 
            printf( "SSD1306 allocation failed\n" );
            while ( true ) {}
        }
        
        m_display.dim( true );

        // m_display.setFont( ArialRus_Plain_10 );
        m_display.cp437( true );
        m_display.setTextWrap( false );
        
        m_display.setTextSize( 1 );
        m_display.setTextColor( WHITE );

        m_display.clearDisplay(); // ?
    }

    void drawProgressBar( float progress )
    {
        int8_t pos_y = 13; // px

        uint8_t width = 105; // min - 8px

        m_display.drawPixel( 0, pos_y + 3, WHITE );
        m_display.drawFastVLine( 1, pos_y + 1, 5, WHITE );
        m_display.drawPixel( 2, pos_y + 1, WHITE );
        m_display.drawPixel( 2, pos_y + 5, WHITE );

        m_display.drawFastHLine( 3, pos_y + 0, width - 6, WHITE );
        m_display.drawFastHLine( 3, pos_y + 6, width - 6, WHITE );

        m_display.drawPixel( width - 3, pos_y + 1, WHITE );
        m_display.drawPixel( width - 3, pos_y + 5, WHITE );
        m_display.drawFastVLine( width - 2, pos_y + 1, 5, WHITE );
        m_display.drawPixel( width - 1, pos_y + 3, WHITE );

        m_display.fillRect( 2, pos_y + 1, ( width - 4 ) * progress, 5, WHITE );
    }

    void set_title( const char* title )
    {
        m_title = title;

        size_t strlen = utf8len( title );

        // printf( "title length: %i, %i\n", utf8len( title ), utf8len_2( title ) );
        printf( "title length: %i\n", strlen );

        // m_current_offset = 0;

        if ( strlen > 21 )
        {
            // we need to anim this title

            m_pos_x = 0;
            m_anim = true;

            m_title += "    "; // check
            m_title += title;

            m_next_anim_time = millis() + 4 * 1000; // 4 sec
            // m_next_slide_time = m_next_anim_time;
            // m_goal_slide_offset = -( title.length() + 4 );
            
            m_goal_slide_offset = -( ( strlen + 4 ) * 6 ) * 1000; // px * 1000, sec to ms
        }
        else
        {
            // centered title

            m_pos_x = SCREEN_WIDTH * 0.5f - ( strlen * 6 - 1 ) * 0.5f;
            m_pos_x *= 1000;
            m_anim = false;
        }
    }

    void draw_title()
    {
        if ( millis() >= m_next_anim_time && m_anim )
        {
            // m_display.drawPixel( 127, 0, WHITE );
            // m_current_offset++; // 1px

            // printf( "m_goal_slide_offset: %i\n", m_goal_slide_offset );

            static const uint32_t speed = 40;

            m_pos_x -= speed * m_delta_time;

            // float ratio = m_pos_x / m_goal_slide_offset;
            // m_pos_x = lerp( 0.0f, m_goal_slide_offset, ratio );

            // lerp( start, stop, speed )
            // width = max_width * ratio

            // m_next_slide_time = millis() + 35;
            // m_next_slide_time = millis() + 20;

            if ( m_pos_x < m_goal_slide_offset )
            {
                m_pos_x = 0;
                m_next_anim_time = millis() + 4 * 1000; // 4 sec
            }
        }

        m_display.setCursor( m_pos_x / 1000, 25 );
        m_display.println( utf8rus( m_title ) );
        // m_display.println( utf8rus( m_title.substring( m_current_offset, m_current_offset + 21 ) ) );
    }

    void clear()
    {
        m_display.clearDisplay();
    }

    void draw( uint32_t current_duration, uint32_t playback_duration, bool is_connected, bool playing, bool bass_boost )
    {
        uint32_t current_time = millis();
        m_delta_time = current_time - m_previous_time;
        m_previous_time = current_time;

        // draw all stuff here...

        float progress = ( playback_duration == 0 ) ? 0.0f : current_duration / (float)playback_duration;

        m_display.clearDisplay();

        m_display.setCursor( 0, 3 );
        m_display.println( format_duration( current_duration ) );

        // m_display.printf( "%i", m_goal_slide_offset );

        m_display.setCursor( 76, 3 );
        m_display.println( format_duration( playback_duration ) );

        // m_display.printf( "%i", m_pos_x );

        // static const float speed = 35.0f;

        // float ratio = m_pos_x / ( m_goal_slide_offset + 0.00001f );
        // m_display.printf( "%.2f", ratio );

        drawProgressBar( progress );

        // if ( is_connected ) m_display.drawPixel( 0, 0, WHITE );

        m_display.drawBitmap( 110, 1, ( playing ) ? pause_icon : play_icon, 18, 18, WHITE );

        // if ( bass_boost ) m_display.drawPixel( 127, 0, WHITE );

        // m_display.drawFastVLine( 107, 0, 32, WHITE );

        draw_title();

        m_display.display();
    }

    Adafruit_SSD1306* get_display()
    {
        return &m_display;
    }

private:

    Adafruit_SSD1306 m_display = Adafruit_SSD1306( SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN );

    String m_title;
    bool m_anim = false;
    unsigned long m_next_anim_time = 0;
    unsigned long m_next_slide_time = 0;
    // uint8_t m_current_offset = 0;
    int32_t m_pos_x = 0.0f; // px
    int32_t m_goal_slide_offset = 0; // in px now

    int32_t m_previous_time = 0.0f;
    int32_t m_delta_time = 0.0f;
};

