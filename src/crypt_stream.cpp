// Copyright (c) 2010-2012 CIYAM Pty. Ltd. ACN 093 704 539
// Copyright (c) 2012-2013 CIYAM Open Developers
//
// Distributed under the MIT/X11 software license, please refer to the file license.txt
// in the root project directory or http://www.opensource.org/licenses/mit-license.php.

#ifdef __BORLANDC__
#  include "precompile.h"
#endif
#pragma hdrstop

#ifndef HAS_PRECOMPILED_STD_HEADERS
#  include <memory.h>
#  include <ctime>
#  include <cstdlib>
#  include <memory>
#  include <sstream>
#  include <iostream>
#endif

#include "crypt_stream.h"

#include "md5.h"
#include "sha1.h"
#include "base64.h"
#include "utilities.h"

#ifdef SSL_SUPPORT
#  include <openssl/aes.h>
#  include <openssl/ssl.h>
// KLUDGE: Suppress the "function now deprecated" warning as it is being incorrectly issued for
// the "sgetn" I/O function (an issue at least with the VS Express 2005 version of VC8).
#  ifdef _MSC_VER
#     pragma warning (disable: 4996)
#  endif
#endif

using namespace std;

const size_t c_max_key_size = 128;
const size_t c_file_buf_size = 32768;

// NOTE: This algorithm is an XOR approach for encrypting a stream in place
// and is very quick, however, it is not considered "strong encryption" and
// therefore should not be used for encryptiong very sensitive information.
void crypt_stream( iostream& io, const char* p_key, size_t key_length )
{
   unsigned char key[ c_max_key_size ];
   unsigned char buf[ c_file_buf_size ];

   io.seekg( 0, ios::end );
   size_t length = ( size_t )io.tellg( );

   io.seekg( 0, ios::beg );

   memcpy( key, p_key, key_length );

   unsigned char datkey = 0;
   for( size_t i = 0; i < key_length; i++ )
      datkey += key[ i ];

   size_t buflen = c_file_buf_size;
   for( size_t pos = 0; pos < length; pos += buflen )
   {
      if( length - pos < c_file_buf_size )
         buflen = length - pos;

      int dir = 1;
      int key_offset = 0;

      io.seekg( pos, ios::beg );
      io.read( ( char* )buf, buflen );

      unsigned char ch;
      for( size_t offset = 0; offset < buflen; offset++ )
      {
         ch = buf[ offset ];
         ch ^= datkey;
         buf[ offset ] = ch;

         datkey += ( key[ key_offset ] % 131 );
         key[ key_offset ] ^= datkey;

         key_offset += dir;

         if( datkey % 31 == 0 )
            dir *= -1;

         if( key_offset >= ( int )key_length )
            key_offset = 0;

         if( key_offset < 0 )
            key_offset = ( int )key_length - 1;
      }

      io.seekg( pos, ios::beg );
      io.write( ( char* )buf, buflen );
   }

   memset( buf, '\0', c_file_buf_size );
   memset( key, '\0', c_max_key_size );
}

#ifdef SSL_SUPPORT
string aes_crypt( const string& s, const char* p_key, size_t key_length, crypt_op op )
{
   string output( s.length( ) + AES_BLOCK_SIZE, '\0' );
   unsigned char* p_output = ( unsigned char* )output.data( );

   // NOTE: Use an MD5 hash of the key as ckey and ivec must be 16 bytes each.
   auto_ptr< char > ap_digest( MD5( ( unsigned char* )p_key ).hex_digest( ) );

   unsigned char* p_ckey = ( unsigned char* )ap_digest.get( );
   unsigned char* p_ivec = ( unsigned char* )ap_digest.get( ) + 16;

   EVP_CIPHER_CTX ctx;
   EVP_CIPHER_CTX_init( &ctx );

   if( op == e_crypt_op_encrypt )
      EVP_EncryptInit_ex( &ctx, EVP_aes_128_cbc( ), 0, p_ckey, p_ivec );
   else
      EVP_DecryptInit_ex( &ctx, EVP_aes_128_cbc( ), 0, p_ckey, p_ivec );

   int num = 0;
   if( op == e_crypt_op_encrypt )
      EVP_EncryptUpdate( &ctx, p_output, &num, ( const unsigned char* )s.data( ), s.size( ) );
   else
      EVP_DecryptUpdate( &ctx, p_output, &num, ( const unsigned char* )s.data( ), s.size( ) );

   int tlen = 0;
   if( op == e_crypt_op_encrypt )
      EVP_EncryptFinal_ex( &ctx, p_output + num, &tlen );
   else
      EVP_DecryptFinal_ex( &ctx, p_output + num, &tlen );

   EVP_CIPHER_CTX_cleanup( &ctx );

   output.resize( num + tlen );

   return output;
}
#endif

string get_totp( int pin, int freq )
{
   // NOTE: Platforms with a 32 bit time_t will suffer from the 2038 *bug*.
   time_t tm( time( 0 ) / freq );

   if( pin )
      tm *= pin;

   sha1 hash( to_string( tm ) );

   return lower( hash.get_digest_as_string( ).substr( 0, 6 ) );
}

string password_encrypt( const string& password, const string& key, bool use_ssl )
{
   string s( password );

   srand( time( 0 ) );

#ifndef SSL_SUPPORT
   use_ssl = false;
#endif

   // NOTE: If the password is less than 20 characters then append a '\0' followed
   // by as many random characters as needed so that the length of the password is
   // disguised from non-technical user observation.
   if( !use_ssl )
   {
      char c( '\0' );
      while( s.length( ) < 20 )
      {
         s += c;
         c = rand( ) % 256;
      }
   }

   stringstream ss( s );
   ss.seekp( 0 );

   auto_ptr< char > ap_digest( MD5( ( unsigned char* )key.c_str( ) ).hex_digest( ) );

   if( !use_ssl )
   {
      crypt_stream( ss, ap_digest.get( ), 32 );
      s = base64::encode( ss.str( ) );
   }

#ifdef SSL_SUPPORT
   if( use_ssl )
      s = base64::encode( aes_crypt( ss.str( ), ap_digest.get( ), 32, e_crypt_op_encrypt ) );
#endif

   return s;
}

string password_decrypt( const string& password, const string& key, bool use_ssl )
{
   string s;
   if( password.empty( ) )
      return password;

   stringstream ss( base64::decode( password ) );

#ifndef SSL_SUPPORT
   use_ssl = false;
#endif

   auto_ptr< char > ap_digest( MD5( ( unsigned char* )key.c_str( ) ).hex_digest( ) );

   if( !use_ssl )
   {
      crypt_stream( ss, ap_digest.get( ), 32 );
      s = ss.str( );
   }

#ifdef SSL_SUPPORT
   if( use_ssl )
      s = aes_crypt( ss.str( ), ap_digest.get( ), 32, e_crypt_op_decrypt );
#endif

   return s.c_str( ); // NOTE: Remove any trailing padding from encryption.
}

