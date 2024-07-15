// Oleg Kotov

#include "sd_utils.h"
#include "utils.h"

#include <arduino.h>
#include <sd.h>

bool sd_init( bool print_info /* = false */ )
{
    printf( "initializing sd card...\n" );

    if ( SD.begin() == false )
    {
        printf( "> error: card mount failed\n" );
        return false;
    }

    uint8_t cardType = SD.cardType();

    if ( cardType == CARD_NONE )
    {
        printf( "> no sd card attached\n" );
        return false;
    }
    
    if ( !print_info ) return true;

    printf( "card type: " );
    
    if ( cardType == CARD_MMC )
    {
        printf( "MMC\n" );
    }
    else if ( cardType == CARD_SD )
    {
        printf( "SDSC\n" );
    } 
    else if ( cardType == CARD_SDHC )
    {
        printf( "SDHC\n" );
    }
    else
    {
        printf( "Unknown\n" );
        // return false?
    }
    
    printf( "card size: %s\n", format_bytes( SD.cardSize() ).c_str() );
    
    uint64_t free_space = SD.totalBytes() - SD.usedBytes();

    printf( "total space: %s\n", format_bytes( SD.totalBytes() ).c_str() );
    printf( "used space: %s\n", format_bytes( SD.usedBytes() ).c_str() );
    printf( "free space: %s\n", format_bytes( free_space ).c_str() );

    return true;
}

void sd_list_directory( const char* dirname, uint8_t levels /* = 0 */ )
{
    File root = SD.open( dirname );
    
    if ( !root )
    {
        Serial.println( "failed to open directory" );
        return;
    }
  
    if ( root.isDirectory() == false )
    {
        Serial.println( "not a directory" );
        return;
    }

    File file = root.openNextFile();

    while ( file )
    {
        if ( file.isDirectory() == true )
        {
            if ( String( file.name() ) == String( "System Volume Information" ) )
            {
                file = root.openNextFile();
                continue;
            }

            if ( levels )
            {
                sd_list_directory( file.path(), levels - 1 );
            }
        }
        else
        {
            Serial.print( file.path() );
            Serial.printf( "\t%s (%i bytes)\n", format_bytes( file.size() ).c_str(), file.size() );
        }

        file = root.openNextFile();
    }
}

void sd_format()
{
    File root = SD.open( "/" );
    
    if ( !root )
    {
        Serial.println( "failed to open directory" );
        return;
    }
  
    if ( root.isDirectory() == false )
    {
        Serial.println( "not a directory" );
        return;
    }

    File file = root.openNextFile();

    while ( file )
    {
        if ( file.isDirectory() == true )
        {
            if ( String( file.name() ) == String( "System Volume Information" ) )
            {
                file = root.openNextFile();
                continue;
            }

            SD.rmdir( file.path() );
        }
        else
        {
            SD.remove( file.path() );
        }

        file = root.openNextFile();
    }
}

