// Copyright (c) 2000
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

#ifndef POINTERS_H
#  define POINTERS_H

#  ifndef HAS_PRECOMPILED_STD_HEADERS
#     include <functional>
#  endif

#  include "macros.h"

#  define SPECIALISE_STD_LESS_TO_DEREFERENCE

// NOTE: The following template class and specialisation permits a pointer's type to be used in
// situations where the type is a template parameter is actually a pointer (whilst being usable
// if the template parameter is not a pointer).
//
// Usage example:
// template< typename C > struct container_wrapper
// {
//    typedef typename remove_pointer< C::value_type >::type base_type;
//    base_type* construct_container_object( ) { return new base_type; }
// };
//
// The above source would create a new "string" with "container_wrapper< vector< string > >" or
// with "container_wrapper< vector< string* > >".

template< class T > struct remove_pointer
{
   typedef T type;
};

template< class T > struct remove_pointer< T* >
{
   typedef T type;
};

template< typename T > class ref_count_ptr
{
   class bool_test
   {
      public:
      bool_test( ) { }

      private:
      void operator delete( void* );
   };

   public:
   ref_count_ptr( )
    : p_instance( 0 ), p_ref_count( new int( 1 ) ) { }

   explicit ref_count_ptr( T* p_instance )
    : p_instance( p_instance ), p_ref_count( new int( 1 ) ) { }

   ref_count_ptr( const ref_count_ptr< T >& src )
   {
      copy_from( src );
   }

   template< typename D > ref_count_ptr( const ref_count_ptr< D >& src )
   {
      copy_from( src );
   }

   ref_count_ptr< T >& operator =( T* p_new_instance )
   {
      if( p_instance == p_new_instance )
         return *this;

      if( !--*p_ref_count )
      {
         delete p_instance;
         *p_ref_count = 1;
      }
      else
         p_ref_count = new int( 1 );

      p_instance = p_new_instance;
      return *this;
   }

   ref_count_ptr< T >& operator =( const ref_count_ptr< T >& src )
   {
      if( !--*p_ref_count )
      {
         delete p_instance;
         delete p_ref_count;
      }
      copy_from( src );
      return *this;
   }

   template< typename D > ref_count_ptr< T >& operator =( const ref_count_ptr< D >& src )
   {
      if( !--*p_ref_count )
      {
         delete p_instance;
         delete p_ref_count;
      }
      copy_from( src );
      return *this;
   }

   ~ref_count_ptr( )
   {
      if( !--*p_ref_count )
      {
         delete p_instance;
         delete p_ref_count;
      }
   }

#  ifdef __BORLANDC__
   operator bool_test*( ) const;
#  else
   operator bool_test*( ) const
   {
      if( !p_instance )
         return 0;
      static bool_test test;
      return &test;
   }
#  endif

   T& operator *( ) const { return *p_instance; }
   T* operator ->( ) const { return p_instance; }

   T* get( ) const { return p_instance; }
   int* get_ref( ) const { return p_ref_count; }

   private:
   template< typename D > void copy_from( const ref_count_ptr< D >& src )
   {
      p_instance = src.get( );
      p_ref_count = src.get_ref( );

      ++*p_ref_count;
   }

   T* p_instance;
   int* p_ref_count;
};

#  ifdef __BORLANDC__
template< typename T >
 ref_count_ptr< T >::operator bool_test*( ) const
{
   if( !p_instance )
      return 0;
   static bool_test test;
   return &test;
}
#  endif

template< typename T, typename U >
 inline bool operator ==( const ref_count_ptr< T >& lhs, const ref_count_ptr< U >& rhs )
{
    return lhs.get( ) == rhs.get( );
}

template< typename T, typename U >
 inline bool operator !=( const ref_count_ptr< T >& lhs, const ref_count_ptr< U >& rhs )
{
    return lhs.get( ) != rhs.get( );
}

template< typename T > inline bool operator <( const ref_count_ptr< T >& lhs, const ref_count_ptr< T >& rhs )
{
    return std::less< T* >( )( lhs.get( ), rhs.get( ) );
}

#  ifdef SPECIALISE_STD_LESS_TO_DEREFERENCE
namespace std
{

template< typename T > struct less< ref_count_ptr< T > >
{
   bool operator ( )( const ref_count_ptr< T >& lhs, const ref_count_ptr< T >& rhs ) const
   {
      return *lhs < *rhs;
   }
};

}
#  endif

#endif
