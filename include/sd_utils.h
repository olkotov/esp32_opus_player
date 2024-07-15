// Oleg Kotov

#pragma once

#include <stdint.h>

bool sd_init( bool print_info = false );
void sd_list_directory( const char* dirname, uint8_t levels = 0 );
void sd_format();

