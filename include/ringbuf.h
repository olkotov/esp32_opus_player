// Oleg Kotov

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <mutex>

// data                  write             read            end
//  |                      |                 |              |
//  ▼                      ▼                 ▼              ▼
//  ---------------------------------------------------------
//  |<------ filled ------>|<-- available -->|<-- filled -->|
//  ---------------------------------------------------------

// data                  read              write           end
//  |                      |                 |              |
//  ▼                      ▼                 ▼              ▼
//  ---------------------------------------------------------
//  |<------ avail ------->|<---- filled --->|<--- avail -->|
//  ---------------------------------------------------------

//  |<-64->|<-64->|<-64->|<-64->|<-64->|<-64->|<-64->|<-64->| // 8x64 = 512 bytes <-- capacity -->

// data                                                    end
//  |                                                       |
//  ▼                                                       ▼
//  ---------------------------------------------------------
//  |<- filled ->|<-------------- available --------------->|
//  ---------------------------------------------------------
//  |<-64->|<-64->|<-64->|<-64->|<-64->|<-64->|<-64->|<-64->|
//  ---------------------------------------------------------
//  ▲             ▲
//  |             |
// read         write



class RingBuffer
{
public:

	RingBuffer( uint16_t capacity )
	{
		m_capacity = capacity;

		init();

		// Msg( "data: %u", m_data );
		// Msg( "end:  %u", m_end_ptr );
	}

	~RingBuffer()
	{
		// assert( m_data != NULL );

		if ( m_data ) free( m_data );
		m_data = NULL;
	}

	bool init()
	{
		assert( m_data == NULL );

		// if ( m_data ) free( m_data );
		// m_data = NULL;

		m_data = (uint8_t*)malloc( m_capacity );

		if ( m_data == NULL ) return false;

		reset();

		return true;
	}

	void reset()
	{
		std::lock_guard<std::mutex> lock( m_mutex );

		m_size = 0;
		m_write_ptr = m_data;
		m_read_ptr = m_data;
		m_end_ptr = m_data + m_capacity;

		memset( m_data, 0, m_capacity );
	}

	uint16_t bytes_available() const
	{
		return m_capacity - m_size;
	}

	uint16_t bytes_filled() const
	{
		return m_size;
	}

	uint16_t capacity() const
	{
		return m_capacity;
	}

	uint16_t write( const void* buf, uint16_t len )
	{
		if ( buf == NULL ) return 0;

		uint16_t available = bytes_available();

		if ( available == 0 ) return 0;

		// double check (test)

		uint16_t available_2;

		if ( m_write_ptr < m_read_ptr )
		{
			available_2 = ( m_read_ptr - m_write_ptr );
		}
		else
		{
			available_2 = ( m_end_ptr - m_write_ptr ) + ( m_read_ptr - m_data );
		}

		// assert( available == available_2 );

		// ---

		std::lock_guard<std::mutex> lock( m_mutex );

		if ( len > available ) len = available;

		uint16_t bytes_written = 0;

		if ( m_write_ptr < m_read_ptr )
		{
			// one block

			memcpy( m_write_ptr, buf, len );
			bytes_written += len; // just double check
			m_write_ptr += len;
			if ( m_write_ptr == m_end_ptr ) m_write_ptr = m_data;
		}
		else
		{
			// two blocks

			uint16_t right_space = ( m_end_ptr - m_write_ptr );

			if ( len <= right_space )
			{
				// write to right block

				memcpy( m_write_ptr, buf, len );
				bytes_written += len; // just double check
				m_write_ptr += len;
				if ( m_write_ptr == m_end_ptr ) m_write_ptr = m_data;
			}
			else
			{
				// write to right block
				// and 
				// write to left block too
				// ----------------------

				uint16_t left_space = ( m_read_ptr - m_data );

				// write data to right block
				
				memcpy( m_write_ptr, buf, right_space );
				bytes_written += right_space; // just double check
				m_write_ptr += right_space;
				if ( m_write_ptr == m_end_ptr ) m_write_ptr = m_data;

				// write data to left block

				uint16_t remaining_len = len - right_space;
				
				memcpy ( m_write_ptr, (const char*)buf + right_space, remaining_len );
				bytes_written += remaining_len; // just double check
				m_write_ptr += remaining_len;
				if ( m_write_ptr == m_end_ptr ) m_write_ptr = m_data;
			}
		}

		m_size += bytes_written; // just double check
		// m_size += len; // look like bulletproof

		assert( bytes_written == len );

		return len;
	}

	uint16_t read( void* buf, uint16_t len )
	{
		std::lock_guard<std::mutex> lock( m_mutex );

		if ( buf == NULL ) return 0;

		uint16_t size = m_size;

		if ( size == 0 ) return 0;

		// double check (test)

		uint16_t size_2;

		if ( m_read_ptr < m_write_ptr )
		{
			size_2 = ( m_write_ptr - m_read_ptr );
		}
		else
		{
			size_2 = ( m_end_ptr - m_read_ptr ) + ( m_write_ptr - m_data );
		}

		// assert( size == size_2 );

		// ---

		if ( len > size ) len = size;

		uint16_t bytes_read = 0;

		if ( m_read_ptr < m_write_ptr )
		{
			// one block

			memcpy( buf, m_read_ptr, len );
			bytes_read += len; // just double check
			m_read_ptr += len;
			if ( m_read_ptr == m_end_ptr ) m_read_ptr = m_data;
		}
		else
		{
			// two blocks

			uint16_t right_space = ( m_end_ptr - m_read_ptr );

			if ( len <= right_space )
			{
				// read from right block

				memcpy( buf, m_read_ptr, len );
				bytes_read += len; // just double check
				m_read_ptr += len;
				if ( m_read_ptr == m_end_ptr ) m_read_ptr = m_data;
			}
			else
			{
				// read from right block
				// and 
				// read from left block too
				// ------------------------

				uint16_t left_space = ( m_write_ptr - m_data );

				// read data from right block
				
				memcpy( buf, m_read_ptr, right_space );
				bytes_read += right_space; // just double check
				m_read_ptr += right_space;
				if ( m_read_ptr == m_end_ptr ) m_read_ptr = m_data;

				// read data from left block

				uint16_t remaining_len = len - right_space;
				
				memcpy ( (char*)buf + right_space, m_read_ptr, remaining_len );
				bytes_read += remaining_len; // just double check
				m_read_ptr += remaining_len;
				if ( m_read_ptr == m_end_ptr ) m_read_ptr = m_data;
			}
		}

		m_size -= bytes_read; // just double check
		// m_size -= len; // look like bulletproof

		assert( bytes_read == len );

		return len;
	}
	
	bool empty() const
	{
		return ( m_size == 0 );
	}

	bool full() const
	{
		assert( m_size <= m_capacity );
		return ( m_size == m_capacity );
	}

	// test

	uint8_t* get_data_ptr() const
	{
		return m_data;
	}

	uint8_t* get_read_ptr() const
	{
		return m_read_ptr;
	}

	uint8_t* get_write_ptr() const
	{
		return m_write_ptr;
	}

	uint8_t* get_end_ptr() const
	{
		return m_end_ptr;
	}

private:

	std::mutex m_mutex;

	uint16_t m_capacity = 0;
	uint16_t m_size = 0;

	uint8_t* m_data = NULL;
	uint8_t* m_read_ptr = NULL;
	uint8_t* m_write_ptr = NULL;
	uint8_t* m_end_ptr = NULL;
};

