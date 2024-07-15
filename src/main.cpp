// Oleg Kotov

// TODO: config file
constexpr int button_up_pin    = 0;
constexpr int button_down_pin  = 4;
constexpr int button_left_pin  = 2;
constexpr int button_right_pin = 15;
constexpr int button_a_pin     = 12;
constexpr int button_b_pin     = 13;

#include <arduino.h>
#include <sd.h>
#include <arduino_json.h>

#include <freertos/task.h>

#include "opus.h"
#include "bluetootha2dpsource.h"

#include "utils.h"
#include "sd_utils.h"
#include "ringbuf.h"
#include "biquad.h"
#include "gui.h"
#include "button.h"

#include <vector>
#include <mutex> // try to protect a file pointer state

std::mutex file_mutex;

Button button_up    ( button_up_pin    );
Button button_down  ( button_down_pin  );
Button button_left  ( button_left_pin  );
Button button_right ( button_right_pin );
Button button_a     ( button_a_pin     );
Button button_b     ( button_b_pin     );

PlayerUI ui;

// 512 * 16 = 8192
RingBuffer ringbuf( 8192 ); // 7680

bool  m_playing = false;
bool  m_mono = false;
bool  m_bass_boost = true;
float m_balance = 0.0f;
float m_volume = 0.1f;
// m_playback_speed

Biquad filter;

BluetoothA2DPSource a2dp_source;

void deadloop();

File file;

// --- decoder stuff

size_t filesize = 0;
size_t filepos = 0;
bool eof = false;

const uint16_t byterate = 120000 / 8;

// ---

const char* audios_dir = "/";
std::vector<String> queue;
int32_t track_index = 0;

size_t get_audio_pos()
{
    std::lock_guard<std::mutex> lock( file_mutex );
    if ( !file ) return 0;
    return file.position();
}

void play_audio( const char* filename )
{
    std::lock_guard<std::mutex> lock( file_mutex );

    if ( file ) file.close();

    char filepath[256];
    snprintf( filepath, 256, "%s%s", audios_dir, filename );

    printf( "\nopen audio '%s'\n", filepath );

    char str_buffer[256];
    snprintf( str_buffer, 256, "%s/audio.snd", filepath );

    // open input file
    file = SD.open( str_buffer, FILE_READ );

    if ( !file )
    {
        printf( "unable to open file '%s'\n", str_buffer );
    }

    filesize = file.size();

    printf( "filesize: %s (%zu bytes)\n", format_bytes( filesize ).c_str(), filesize );

    uint32_t duration = filesize / byterate;

    printf( "duration: %s\n", format_duration( duration ).c_str() );

    snprintf( str_buffer, 256, "%s/info.json", filepath );

    // open input file
    File meta_file = SD.open( str_buffer, FILE_READ );

    if ( !meta_file )
    {
        printf( "unable to open file '%s'\n", str_buffer );
    }

    char data[meta_file.size()];

    meta_file.read( (uint8_t*)data, file.size() );

    JSONVar meta = JSON.parse( data );

    snprintf( str_buffer, 256, "%s - %s", (const char*)meta["artists"], (const char*)meta["title"] );

    printf( "%s\n", str_buffer );
    printf( "is_explicit: %s\n", ( (bool)meta["is_explicit"] ) ? "true" : "false" );

    // printf( "%s - %s\n", (const char*)meta["artists"], (const char*)meta["title"] ); // ?

    ui.set_title( str_buffer );

    file.seek( 328 );

    eof = false;
}

void scan_audios( const char* path, std::vector<String>& tracklist )
{
    File directory;

    if ( SD.exists( path ) == false )
    {
        printf( "directory not exists\n" );
        return;
    }

    directory = SD.open( path );

    if ( !directory )
    {
        printf( "failed to open directory\n" );
        return;
    }

    if ( directory.isDirectory() == false )
    {
        printf( "not a directory\n" );
        return;
    }

    File file = directory.openNextFile();

    Adafruit_SSD1306* display = ui.get_display();

    while( file )
    {
        if ( file.isDirectory() == true )
        {
            if ( !endsWith( file.name(), ".song" ) )
            {
                // printf( "'%s' skipping...\n", file.name() );
                file = directory.openNextFile();
                continue;
            }

            // printf( "'%s' added to queue\n", file.name() );

            // if ( isdigit( file.name() ) == false )
            // {
            //     printf( "'%s' digit error\n", file.name() );
            //     continue;
            // }

            char filepath[256];
            snprintf( filepath, 256, "%s/audio.snd", file.path() );

            if ( !SD.exists( filepath ) )
            {
                printf( "'%s' not exists\n", filepath );
                file = directory.openNextFile();
                continue;
            }

            snprintf( filepath, 256, "%s/info.json", file.path() );

            if ( !SD.exists( filepath ) )
            {
                printf( "'%s' not exists\n", filepath );
                file = directory.openNextFile();
                continue;
            }

            // tracklist.push_back( atoi( file.name() ) );
            tracklist.push_back( file.name() );
        }

        printf( "\r%i tracks found", tracklist.size() );
        // vTaskDelay( pdMS_TO_TICKS( 1 ) );

        display->clearDisplay();
        display->setCursor( 0, 0 );
        display->println( "scanning audios..." );
        display->printf( "%i tracks found", tracklist.size() );
        display->display();

        file = directory.openNextFile();
    }

    printf( "\n" );
}

static uint32_t char_to_int( uint8_t ch[4] )
{
    return ( (uint32_t)ch[0] << 24 ) | ( (uint32_t)ch[1] << 16 )
        |  ( (uint32_t)ch[2] <<  8 ) |   (uint32_t)ch[3];
}

const int frame_size_bytes = sizeof( int16_t ) * 2; // 2 channels

int32_t get_audio_block( Frame* frames, int32_t frame_count )
{
    // vTaskDelay( pdMS_TO_TICKS( 10000 ) );
    // printf( "get_audio_block priority: %i\n", uxTaskPriorityGet( NULL ) );

    if ( ringbuf.bytes_filled() < 512 ) return 0;

    size_t bytes_read = ringbuf.read( frames, frame_count * frame_size_bytes );

    // dsp
    // TODO: r128 normalization
    for ( int32_t i = 0; i < frame_count; ++i )
    {
        Frame& frame = frames[i];

        // stereo -> mono
        if ( m_mono )
        {
            int16_t mixed = ( frame.channel1 + frame.channel2 ) * 0.5;

            frame.channel1 = mixed;
            frame.channel2 = mixed;
        }

        // gain
        {
            m_volume = clamp( m_volume, 0.0f, 1.0f );

            frame.channel1 *= m_volume;
            frame.channel2 *= m_volume;
        }

        // bass boost
        if ( m_bass_boost )
        {
            // volume overflow protection
            static float amplitude_ratio = powf( 10.0f, -6.0f / 20.0f ); // -6dB gain

            frame.channel1 *= amplitude_ratio;
            frame.channel2 *= amplitude_ratio;

            // convert to float
            // process
            // convert to uint16_t

            static const float inv_div = 1.0f / 32768.0f;

            float channel1 = frame.channel1 * inv_div;
            float channel2 = frame.channel2 * inv_div;

            channel1 = clamp( channel1, -1.0f, 1.0f );
            channel2 = clamp( channel2, -1.0f, 1.0f );

            channel1 = filter.process( channel1 );
            channel2 = filter.process( channel2 );

            channel1 = clamp( channel1, -1.0f, 1.0f );
            channel2 = clamp( channel2, -1.0f, 1.0f );

            frame.channel1 = clamp( (int16_t)( channel1 * 32768 ), (int16_t)-32768, (int16_t)32767 );
            frame.channel2 = clamp( (int16_t)( channel2 * 32768 ), (int16_t)-32768, (int16_t)32767 );
        }

        // balance
        if ( m_balance != 0.0f )
        {
            m_balance = clamp( m_balance, -1.0f, 1.0f );

            float vol_l = 1.0f;
            float vol_r = 1.0f;

            if ( m_balance < 0.0f )
            {
                vol_r -= abs( m_balance );
            }
            else if ( m_balance > 0.0f )
            {
                vol_l -= abs( m_balance );
            }

            frame.channel1 *= vol_l;
            frame.channel2 *= vol_r;
        }   
    }

    return bytes_read / frame_size_bytes;
}

// ========================================

void skip_to_prev_track()
{
    track_index--;
    if ( track_index < 0 ) track_index = queue.size() - 1;

    play_audio( queue[track_index].c_str() );
}

void skip_to_next_track()
{
    track_index++;
    if ( track_index >= queue.size() ) track_index = 0;

    play_audio( queue[track_index].c_str() );
}

void volume_up()
{
    m_volume += 0.05f;
    m_volume = clamp( m_volume, 0.0f, 1.0f );

    printf( "volume: %i\%\n", int( m_volume * 100 ) );
}

void volume_down()
{
    m_volume -= 0.05f;
    m_volume = clamp( m_volume, 0.0f, 1.0f );

    printf( "volume: %i\%\n", int( m_volume * 100 ) );
}

void toggle_eq()
{
    m_bass_boost = !m_bass_boost;
    printf( "equalizer: %s\n", ( m_bass_boost ) ? "on" : "off" );
}

void toggle_playing()
{
    m_playing = !m_playing;
    printf( "playback: %s\n", ( m_playing ) ? "play" : "pause" );
}

void toggle_mono()
{
    m_mono = !m_mono;
    printf( "mono mode: %s\n", ( m_mono ) ? "enabled" : "disabled" );
}

void balance_left()
{
    m_balance -= 0.1f;
    m_balance = clamp( m_balance, -1.0f, 1.0f );

    printf( "balance: %.2f\n", m_balance );
}

void balance_right()
{
    m_balance += 0.1f;
    m_balance = clamp( m_balance, -1.0f, 1.0f );

    printf( "balance: %.2f\n", m_balance );
}

void audio_rewind()
{
    std::lock_guard<std::mutex> lock( file_mutex );

    if ( !file ) return;

    file.seek( 0 );

    printf( "audio rewind\n" );
}

void audio_seek( uint32_t time_sec )
{
    std::lock_guard<std::mutex> lock( file_mutex );

    if ( !file ) return;

    uint32_t pos = ( time_sec * byterate );    
    pos = clamp( (uint32_t)pos, (uint32_t)0, (uint32_t)file.size() );

    // correct align
    pos -= pos % 328;
    pos = clamp( (uint32_t)pos, (uint32_t)0, (uint32_t)file.size() );

    file.seek( pos );

    printf( "audio seek: %i sec, %i bytes\n", time_sec , pos );
}

// ========================================

void audio_task( void* parameters )
{
    vTaskDelay( pdMS_TO_TICKS( 250 ) );
    printf( "[audio] task started\n" );

    // uint32_t stackHWM = uxTaskGetStackHighWaterMark( NULL );

    OpusDecoder* decoder = nullptr;
    int32_t sampling_rate = 48000;
    int channels = 2;

    #define MAX_PACKET 1500
    int max_payload_bytes = MAX_PACKET;
    int max_frame_size = 960;

    int16_t* pcm_buffer = nullptr;

    // packet
    size_t num_read;
    uint8_t* data = nullptr;
    int len;

    uint64_t decoded_bytes_total = 0;

    // ---

    uint32_t freeHeap = ESP.getFreeHeap();
    printf( "- free heap: %u bytes\n", freeHeap );

    printf( "creating decoder and allocate buffers...\n" );

    // create decoder

    int status;
    decoder = opus_decoder_create( sampling_rate, channels, &status );

    if ( status != OPUS_OK )
    {
        printf( "cannot create decoder: %s\n", opus_strerror( status ) );
        deadloop();
    }

    // freeHeap = ESP.getFreeHeap();
    // printf( "free heap: %u bytes\n", freeHeap );

    // allocate buffers memory

    // 1
    data = (uint8_t*)calloc( max_payload_bytes, sizeof( uint8_t ) );

    if ( !data )
    {
        printf( "error allocate memory [data]\n" );
        deadloop();
    }

    // 2
    pcm_buffer = (int16_t*)calloc( max_frame_size * channels, sizeof( int16_t ) );

    if ( !pcm_buffer )
    {
        printf( "error allocate memory [pcm_buffer]\n" );
        deadloop();
    }

    freeHeap = ESP.getFreeHeap();
    printf( "- free heap: %u bytes\n", freeHeap );

    // ---

    // decoder_init();
    // audio_init();

    // seek
    // ( second * byterate ) - ( ( second * byterate ) % 328 )
    // 60 sec * 15000 ( 120.000 / 8 ) = 900.000

    // 900.000 % 328 = 296
    // 900.000 - 296 = 899,704

    // file.seek( 899704 );

    printf( "start decoding\n" );

    while ( true )
    {
        vTaskDelay( pdMS_TO_TICKS( 1 ) );

        // vTaskDelay( pdMS_TO_TICKS( 250 ) ); // test delay

        // vTaskDelay( pdMS_TO_TICKS( 8 ) ); // bl delay ( 8 my default )

        if ( !m_playing ) continue;

        // printf( "bytes_available: %i\n", ringbuf.bytes_available() );
        if ( ringbuf.bytes_available() < 3840 ) continue; // 3840 bytes == 960 decoded samples * 2 channels

        std::lock_guard<std::mutex> lock( file_mutex );

        if ( !file ) continue;

        if ( eof ) continue;

        // stackHWM = uxTaskGetStackHighWaterMark( NULL );

        filepos = file.position();
        // printf( "\rfilepos:  %lli", pos ); // 328 ( 4 + 4 + 320 )

        // eof
        if ( filepos == filesize )
        {
            printf( "eof\n" );
            // printf( "decoded: %lli bytes\n", decoded_bytes_total );
            // printf( "stack high water mark: %u bytes\n", stackHWM );

            // eof callback

            ringbuf.reset();

            // file_mutex.unlock(); wrong

            eof = true;

            // skip_to_next_track();
            continue;
        }

        // read data from input file
        {
            uint8_t ch[4];
            num_read = file.read( (uint8_t*)ch, 4 );

            if ( num_read != 4 )
            {
                printf( "error 1\n" );
                // deadloop();
                continue;
            }
            
            len = char_to_int( ch );

            if ( len > max_payload_bytes || len < 0 )
            {
                printf( "invalid payload length: %d\n", len );
                // deadloop();
                continue;
            }

            num_read = file.read( (uint8_t*)ch, 4 );

            if ( num_read != 4 )
            {
                printf( "error 2\n" );
                // deadloop();
                continue;
            }

            // uint32_t enc_final_range = char_to_int( ch );

            num_read = file.read( (uint8_t*)data, len );

            // printf( "len: %i\n", len );

            if ( num_read != (size_t)len )
            {
                printf( "ran out of input, expecting %d bytes got %d\n", len, (int)num_read );
                // deadloop();
                continue;
            }
        }

        int32_t decoded_samples = 0;
        int32_t decoded_bytes = 0;

        // decode data
        {
            decoded_samples = opus_decode( decoder, data, len, pcm_buffer, max_frame_size, 0 );

            if ( decoded_samples <= 0 )
            {
                printf( "\ndecode error\n" );
                // deadloop();
                continue;
            }

            decoded_bytes = decoded_samples * channels * sizeof( int16_t );
        }

        // write to ring buffer to send ble later
        {
            ringbuf.write( pcm_buffer, decoded_bytes );
        }

        decoded_bytes_total += decoded_bytes;
    }
}

void ui_task( void* parameters )
{
    vTaskDelay( pdMS_TO_TICKS( 250 ) );
    printf( "[ui] task started\n" );
  
    while ( true )
    {
        vTaskDelay( pdMS_TO_TICKS( 1 ) );

        uint32_t current_duration  = get_audio_pos() / byterate;
        uint32_t playback_duration = filesize / byterate;

        ui.draw( current_duration, playback_duration, a2dp_source.is_connected(), m_playing, m_bass_boost );
    }
}

void event_handler_task( void* parameters )
{
    vTaskDelay( pdMS_TO_TICKS( 250 ) );
    printf( "[event_handler] task started\n" );
  
    while ( true )
    {
        vTaskDelay( pdMS_TO_TICKS( 1 ) );

        if ( eof ) skip_to_next_track();

        // vTaskDelay( pdMS_TO_TICKS( 16 ) );
        // printf( "arduino_loop priority: %i\n", uxTaskPriorityGet( NULL ) );

        // char buff[64];
        // vTaskGetRunTimeStats( buff );

        // printf( "%s\n", buff );

        if ( !a2dp_source.is_connected() )
        {
            m_playing = false;
        }

        if ( button_left.click() )
        {
            skip_to_prev_track();
        }

        if ( button_right.click() )
        {
            skip_to_next_track();
        }

        if ( button_up.click() )
        {
            volume_up();
        }

        if ( button_down.click() )
        {
            volume_down();
        }

        if ( button_a.click() )
        {
            toggle_playing();
        }

        if ( button_b.click() )
        {
            // toggle_eq();
            // audio_rewind();

            uint32_t duration  = get_audio_pos() / byterate;
            duration += 15;
            audio_seek( duration );
        }
    }
}

void setup()
{
    Serial.begin( 115200 );
    Serial.println();

    uint32_t freeHeap = ESP.getFreeHeap();
    printf( "- free heap: %u bytes\n", freeHeap );

    printf( "initialization ui...\n" );

    ui.init();

    freeHeap = ESP.getFreeHeap();
    printf( "- free heap: %u bytes\n", freeHeap );

    Adafruit_SSD1306* display = ui.get_display();

    display->clearDisplay();
    display->setCursor( 0, 0 );
    display->println( "initialization..." );
    display->display();

    while ( !sd_init() )
    {
        display->clearDisplay();

        display->drawBitmap( 0, 7, floppy_icon, 16, 16, WHITE );

        display->setCursor( 22, 7 );
        display->println( "insert sd card" );

        display->setCursor( 22, 16 );
        display->println( "to continue..." );

        display->display();

        vTaskDelay( pdMS_TO_TICKS( 500 ) );
    }

    freeHeap = ESP.getFreeHeap();
    printf( "- free heap: %u bytes\n", freeHeap );

    // display->clearDisplay();
    // display->setCursor( 0, 0 );
    // display->println( "scanning audios..." );
    // display->display();

    printf( "scanning audio library...\n" );
    scan_audios( audios_dir, queue );

    // for ( const auto& song : queue )
    // {
    //     printf( "- %s\n", song.c_str() );
    // }

    // size_t track_count = queue.size();
    // printf( "%i tracks found\n", track_count );

    play_audio( queue[track_index].c_str() );

    freeHeap = ESP.getFreeHeap();
    printf( "- free heap: %u bytes\n", freeHeap );

    a2dp_source.set_nvs_init( true );
    a2dp_source.set_reset_ble( true );
    a2dp_source.set_discoverability( ESP_BT_NON_DISCOVERABLE );
    a2dp_source.set_auto_reconnect( false );

    printf( "starting A2DP source...\n" );
    a2dp_source.start( "Soundcore Motion+", get_audio_block );
    // Soundcore Motion+
    // VHM-314

    freeHeap = ESP.getFreeHeap();
    printf( "- free heap: %u bytes\n", freeHeap );

    const float sampleRate = 44100.0f;
    const float frequencyHz = 31.0f;
    const float Q = 1.0f;
    const float dbGain = 6.0f;

    const float nyquist = sampleRate * 0.5f;
    float normalizedFrequency = frequencyHz / nyquist;

    filter.setPeakingParams( normalizedFrequency, Q, dbGain );

    printf( "creating decoder and ui tasks...\n" );

    // 19
    xTaskCreateUniversal( ui_task, "ui_task", 4 * 1024, NULL, 1, NULL, 0 );
    xTaskCreateUniversal( audio_task, "audio_task", 12 * 1024, NULL, 1, NULL, ARDUINO_RUNNING_CORE );
    xTaskCreateUniversal( event_handler_task, "event_handler_task", 10 * 1024, NULL, 1, NULL, ARDUINO_RUNNING_CORE );

    freeHeap = ESP.getFreeHeap();
    printf( "- free heap: %u bytes\n", freeHeap );

    vTaskDelete( NULL );
}

void loop() {}

void deadloop()
{
    printf( "deadloop\n" );
    while ( true ) {}
}

