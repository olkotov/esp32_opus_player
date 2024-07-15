// Oleg Kotov

#include "utils.h"
#include "mbedtls/md5.h"
#include <arduino.h>

String md5( const String& str )
{
    // https://esp8266.ru/forum/threads/md5-na-esp32-deljus-opytom.5506/

    // 256 - максимальный размер буфера принимаемой строки
    unsigned char content[512];
    unsigned char mbedtls_md5sum[16];
    
    for ( int i = 0; i < str.length(); ++i )
    {
        content[i] = str.charAt( i );
    }
    
    String hash;
    
    int ret = mbedtls_md5_ret( content, str.length(), mbedtls_md5sum );
    
    for ( byte i = 0; i < 16; ++i )
    {
        if ( mbedtls_md5sum[i] < 0x10 ) hash += "0";
        hash += String( mbedtls_md5sum[i], HEX );
        // Serial.printf( "%02x", mbedtls_md5sum[i] );
    }
    
    return hash;
}

String format_bytes( uint64_t bytes )
{
    // https://stackoverflow.com/a/18650828/18742597
    
    int k = 1024;
    
    // String sizes[] = { "Б", "КБ", "МБ", "ГБ", "ТБ", "ПБ", "ЭБ", "ЗБ", "ЙБ" };
    String sizes[] = { "bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
    
    int i = floor( log( bytes ) / log( k ) );
    float result = bytes / (float)pow( k, i );
    
    // $str = str_replace( '.', ',', $str );
    
    char buffer[32];
    sprintf( buffer, "%.2f %s", result, sizes[i].c_str() );

    return String( buffer );
}

String format_artists( std::vector<String>& artists )
{
	String result;

	int numArtists = artists.size();

	for ( int i = 0; i < numArtists; ++i )
	{
		result += artists[i];

		if ( i < numArtists - 1 )
		{
			result += ", ";
		}
	}

	return result;
}

String pad( int32_t n )
{
    return ( n < 10 ) ? "0" + String( n ) : String( n );
}

String format_duration( int32_t duration )
{
    int32_t hours = (int32_t)floor( duration / 3600 );

    if ( hours > 0 )
    {
        duration = duration - hours * 3600;

        int32_t minutes = (int32_t)floor( duration / 60 );
        int32_t seconds = duration - minutes * 60;

        return hours + ":" + pad( minutes ) + ":" + pad( seconds );
    }
    else
    {
        int32_t minutes = (int32_t)floor( duration / 60 );
        int32_t seconds = duration - minutes * 60;

        return pad( minutes ) + ":" + pad( seconds );
    }
}

bool isdigit( const char* str )
{
    size_t len = strlen( str );

    for ( int i = 0; i < len; ++i )
    {
        if ( !isdigit( str[i] ) ) return false;
    }

    return true;
}

bool endsWith ( const char* base, const char* str )
{
    int slen = strlen(str) - 1;
    const char *p = base + strlen(base) - 1;
    while(p > base && isspace(*p)) p--;  // rtrim
    p -= slen;
    if (p < base) return false;
    return (strncmp(p, str, slen) == 0);
}

