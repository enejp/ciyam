// Copyright (c) 2006-2012 CIYAM Pty. Ltd. ACN 093 704 539
// Copyright (c) 2012-2013 CIYAM Open Developers
//
// Distributed under the MIT/X11 software license, please refer to the file license.txt
// in the root project directory or http://www.opensource.org/licenses/mit-license.php.

#ifndef CIYAM_SESSION_H
#  define CIYAM_SESSION_H

#  ifndef HAS_PRECOMPILED_STD_HEADERS
#     include <string>
#  endif

#  include "config.h"
#  include "sockets.h"
#  include "threads.h"
#  ifdef SSL_SUPPORT
#     include "ssl_socket.h"
#  endif

class ciyam_session : public thread
{
   public:
#  ifdef SSL_SUPPORT
   ciyam_session( std::auto_ptr< ssl_socket >& ap_socket );
#  else
   ciyam_session( std::auto_ptr< tcp_socket >& ap_socket );
#endif

   ~ciyam_session( );

   void on_start( );

   static void increment_session_count( );
   static void decrement_session_count( );

   private:
#  ifdef SSL_SUPPORT
   std::auto_ptr< ssl_socket > ap_socket;
#  else
   std::auto_ptr< tcp_socket > ap_socket;
#  endif
};

#endif

