// Copyright (c) 2005
//
// CIYAM Pty. Ltd.
// ACN 093 704 539
//
// ALL RIGHTS RESERVED
//
// Permission to use this software for non-commercial purposes is hereby granted. Permission to
// distribute this software privately is granted provided that the source code is unaltered and
// complete or that any alterations and omissions have been first approved by CIYAM. Commercial
// usage of this software is not permitted without first obtaining a license for such a purpose
// from CIYAM. This software may not be publicly distributed unless written permission to do so
// has been obtained from CIYAM.
#include <cstdlib>

#include <string>
#include <fstream>
#include <iostream>

#include "fcgiapp.h"

#ifdef __BORLANDC__
#  include <dir.h>
#endif
#ifdef _MSC_VER
#  include <direct.h>
#endif
#ifdef __GNUG__
#  include <limits.h>
#  include <unistd.h>
#endif

#ifdef _WIN32
#  define PATH_MAX MAX_PATH
#  include <windows.h>
#endif

#ifdef __GNUG__
#  define _chdir chdir
#endif

#include "threads.h"
#include "utilities.h"

#ifdef _WIN32
#  define USE_MOD_FASTCGI_KLUDGE
#endif
#define USE_MULTIPLE_REQUEST_HANDLERS

using namespace std;

const int c_chunk_size = 8192;

const int c_num_handlers = 10;

const char* const c_prefix_name = "name=";
const char* const c_prefix_filename = "filename=";

#ifndef _WIN32
const char* const c_kill_script = "upload.kill.sh";
#else
const char* const c_kill_script = "upload.kill.bat";
#endif

string g_exe_path;

class pid_handler : public thread
{
   public:
   void on_start( );
};

void pid_handler::on_start( )
{
   if( file_exists( c_kill_script ) )
      file_remove( c_kill_script );

   while( true )
   {
      msleep( 1000 );

      if( !file_exists( c_kill_script ) )
      {
         ofstream outf( c_kill_script );
#ifdef _WIN32
         outf << "TASKKILL /F /PID " << get_pid( ) << '\n';
#else
         outf << "kill -9 " << get_pid( ) << '\n';
#endif
      }
   }
}

class request_handler : public thread
{
   public:
   request_handler( );

   void on_start( );

   void process_request( );

   private:
   FCGX_Stream* p_in;
   FCGX_Stream* p_out;
   FCGX_Stream* p_err;
   FCGX_ParamArray* p_env;

#ifdef USE_MULTIPLE_REQUEST_HANDLERS
   FCGX_Request request;
#else
   FCGX_ParamArray envp;
#endif
};

request_handler::request_handler( )
{
#ifdef USE_MULTIPLE_REQUEST_HANDLERS
   FCGX_InitRequest( &request, 0, 0 );
#endif
}

void request_handler::on_start( )
{
#ifdef USE_MULTIPLE_REQUEST_HANDLERS
   while( FCGX_Accept_r( &request ) >= 0 )
   {
      p_in = request.in;
      p_out = request.out;
      p_err = request.err;

      p_env = &request.envp;

      process_request( );
   }
#else
   while( FCGX_Accept( &p_in, &p_out, &p_err, &envp ) >= 0 )
   {
      p_env = &envp;

      process_request( );
   }
#endif

   delete this;
}

void request_handler::process_request( )
{
   FCGX_FPrintF( p_out, "Content-type: text/html\n" );

   char* p_param = FCGX_GetParam( "CONTENT_LENGTH", *p_env );

   FCGX_FPrintF( p_out, "\r\n\r\n<html>\n<head>\n   <title>FCGI File Upload</title>\n" );

   // NOTE: It is expected that if a parent exists it will contain a "refresh" function.
   FCGX_FPrintF( p_out, "<script type=\"text/javascript\">\n" );
   FCGX_FPrintF( p_out, "if( window.parent )\n" );
   FCGX_FPrintF( p_out, "   window.parent.refresh( );\n" );
   FCGX_FPrintF( p_out, "else\n" );
   FCGX_FPrintF( p_out, "   setTimeout( \"history.go( -1 );\", 2500 );\n" );
   FCGX_FPrintF( p_out, "</script>\n" );

   FCGX_FPrintF( p_out, "</head>\n<body>\n" );

   size_t size = 0;
   size_t length = 0;
   if( p_param )
   {
      FCGX_FPrintF( p_out, "<p>Found CONTENT_LENGTH: %s</p>\n", p_param );
      length = atoi( p_param );
   }

   string name, file_name;
   char buf[ c_chunk_size ];
   char buf2[ c_chunk_size ];
   char dbuf[ c_chunk_size * 2 ];

   string disposition, file_source;
   if( FCGX_GetLine( buf, c_chunk_size, p_in ) )
   {
      string marker( buf );
      size += marker.size( );

      string::size_type pos = marker.find_first_of( "\r\n" );
      if( pos != string::npos )
         marker.erase( pos );

      marker = "\r\n" + marker;

      FCGX_GetLine( buf, c_chunk_size, p_in );
      disposition = string( buf );
      size += disposition.size( );

      // NOTE: It is expected that the HTML file input tag will provide the file upload data in its name attribute.
      pos = disposition.find( c_prefix_name );
      if( pos != string::npos )
      {
         for( size_t i = pos + strlen( c_prefix_name ) + 1; true; i++ )
         {
            if( disposition[ i ] == '\'' || disposition[ i ] == '"' )
               break;
            name += disposition[ i ];
         }
      }

      pos = disposition.find( c_prefix_filename );
      if( pos != string::npos )
      {
         for( size_t i = pos + strlen( c_prefix_filename ) + 1; true; i++ )
         {
            if( disposition[ i ] == '\'' || disposition[ i ] == '"' )
               break;
            file_source += disposition[ i ];
         }
      }

      string ext;
      pos = file_source.find_last_of( "." );
      if( pos == string::npos )
         FCGX_FPrintF( p_out, "<p>*** unexpected file extension not found in '%s' ***</p>", file_source.c_str( ) );
      else
         ext = file_source.substr( pos );

      // NOTE: Format for upload name is [<sid>:][<dir>;]<dest>[?<limit>] where:
      // <sid> is the session id (used as a confirmation output file which will contain the file name)
      // <dir> is path below the standard child "/files" directory where the file will be created
      // <dest> is the destination file name (without extension as extension is taken from the source file)
      // <limit> is an optional maximum file size limit
      string info( name );

      string file_id;
      string session_id;

      string path( g_exe_path + "/files" );
      pos = info.find( ':' );
      if( pos != string::npos )
      {
         session_id = info.substr( 0, pos );
         info.erase( 0, pos + 1 );
      }

      string sub_path;
      pos = info.find( ';' );
      if( pos != string::npos )
      {
         sub_path += "/" + info.substr( 0, pos );
         info.erase( 0, pos + 1 );
      }

      size_t max_size = 0;

      pos = info.find( '?' );
      if( pos != string::npos )
      {
         max_size = atol( info.substr( pos + 1 ).c_str( ) );
         info.erase( pos );
      }

      file_id = info;
      file_name = path + sub_path + "/" + file_id + ext;

      FCGX_GetLine( buf, c_chunk_size, p_in );
      string content_type( buf );
      size += content_type.size( );

      FCGX_GetLine( buf, c_chunk_size, p_in );
      string line_break( buf );
      size += line_break.size( );

      size_t len2( 0 );

      ofstream outf;
      if( !ext.empty( ) )
         outf.open( file_name.c_str( ), ios::out | ios::binary );

      if( !outf )
         FCGX_FPrintF( p_out, "<p>*** unable to open file '%s' for output ***</p>", file_name.c_str( ) );

      size_t written = 0;
      bool max_size_exceeded = false;
      while( true )
      {
         int len = FCGX_GetStr( buf, c_chunk_size, p_in );

         if( len < c_chunk_size )
         {
            if( len > 0 )
            {
               if( len2 )
                  memcpy( dbuf, buf2, len2 );
               memcpy( dbuf + len2, buf, len );
            }

            string last( dbuf, len2 + len );
            pos = last.find( marker );
            if( pos == string::npos )
               FCGX_FPrintF( p_out, "<p>*** unexpected end-of-file marker not found ***</p>" );
            else
            {
               outf.write( dbuf, pos );
               written += pos;
            }

            size += len;
            break;
         }

         if( len2 > 0 )
         {
            outf.write( buf2, len2 );
            written += len2;
         }

         memcpy( buf2, buf, len );
         len2 = len;

         size += len;
         if( max_size && written > max_size )
            break;
      }

      if( max_size && written > max_size )
      {
         if( !ext.empty( ) )
            file_remove( file_name.c_str( ) );
         max_size_exceeded = true;
      }

      if( !session_id.empty( ) )
      {
         string file_info;
         file_info = path + sub_path;
         file_info += "/" + session_id;

         ofstream outf( file_info.c_str( ) );
         if( max_size_exceeded )
            outf << ">" << max_size << endl;
         else if( !ext.empty( ) )
            outf << file_name;
      }
   }

   if( length && size != length )
      FCGX_FPrintF( p_out, "<p>*** input was truncated after %ld bytes ***</p>", length );

   FCGX_FPrintF( p_out, "<p>%s</p>", disposition.c_str( ) );
   FCGX_FPrintF( p_out, "<p>Input content read was: %ld bytes</p>", size );
   FCGX_FPrintF( p_out, "<p>(wrote content to file %s...)</p>", file_name.c_str( ) );

   FCGX_FPrintF( p_out, "</body>\n</html>\n" );

#ifdef USE_MULTIPLE_REQUEST_HANDLERS
   FCGX_Finish_r( &request );
#else
   FCGX_Finish( );
#endif
}

int main( int /*argc*/, char* argv[ ] )
{
   g_exe_path = string( argv[ 0 ] );

#ifndef _WIN32
   size_t pos = g_exe_path.find_last_of( "/" );
#else
   size_t pos = g_exe_path.find_last_of( "\\" );
#endif
   if( pos != string::npos )
      g_exe_path.erase( pos );
   else
      g_exe_path.erase( );

   if( !g_exe_path.empty( ) )
      _chdir( g_exe_path.c_str( ) );

   pid_handler( ).start( );

#ifdef USE_MULTIPLE_REQUEST_HANDLERS
   FCGX_Init( );

   // KLUDGE: For some unknown reason when this FCGI interface is started automatically by Apache
   // (under Windows) it can crash, however, with the delay here this problem seems to be avoided.
   msleep( 500 );

   // FUTURE: Currently Apache's "mod_fcgid" only supports single threaded FCGI servers and under
   // Windows it is simply unable to get back a request that has been handled by any thread other
   // than the main one. Thus rather than force single threaded compilation a check is being made
   // to see if "mod_fastcgi" is in use before starting any other request handling threads.
#  ifdef USE_MOD_FASTCGI_KLUDGE
   if( has_environment_variable( "_FCGI_MUTEX_" ) ) // i.e. this var is only found in mod_fastcgi
#  endif
   {
      // Start all but one as separate threads - the main thread runs the final handler.
      for( size_t i = 1; i < c_num_handlers; i++ )
      {
         request_handler* p_request_handler = new request_handler;
         p_request_handler->start( );
      }
   }
#endif

   request_handler* p_request_handler = new request_handler;
   p_request_handler->on_start( );

   return 0;
}
