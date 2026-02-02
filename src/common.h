/**
 * Copyright 2026 Xiao Xuanwen <xxw_pc@163.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <list>
#include <ranges>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/algorithm/string/split.hpp>
#include <boost/beast/core/detail/base64.hpp>

#include <git2.h>
#include <git2/sys/transport.h>
#include <openssl/err.h>
#include <openssl/evp.h>



/**
 * libgit2 的版本号
 * 之所以在这里定义, 是因为 libgit2 只定义了几个独立的主次版本号宏, 不好使用
 */
#define LIBGIT2_NUMBER  ( ( LIBGIT2_VER_MAJOR * 10000 ) + ( LIBGIT2_VER_MINOR * 100 ) + LIBGIT2_VER_REVISION )


/**
 * 本程序运行时, 实际传入 main 函数的 argv[0] 的 basename
 */
extern const char      *grx_name;


extern const char      *git_dir;
extern const char      *remote_name;
extern const char      *remote_url;

extern git_repository  *repo;
extern git_odb         *odb;
extern git_remote      *remote;

extern int              log_indent;



inline const char * my_getenv( const char *name )
{
#ifdef __gnu_linux__
   return secure_getenv( name );
#else
   return getenv( name );
#endif
}



inline void print_oid( FILE *out, const git_oid &oid )
{
   char  buf[41];
   git_oid_tostr( buf, 41, &oid );
   fprintf( out, "%40.40s", buf );
}



inline void trace_format( const char &ch )
{
   fprintf( stderr, "%c", ch );
}


inline void trace_format( const char * const &str )
{
   fprintf( stderr, "%s", str );
}


inline void trace_format( const std::string &str )
{
   fprintf( stderr, "%s", str.c_str( ) );
}


inline void trace_format( const std::string_view &str )
{
   for ( auto c : str )
      fprintf( stderr, "%c", c );
}


inline void trace_format( const int &val )
{
   fprintf( stderr, "%d", val );
}


inline void trace_format( const unsigned int &val )
{
   fprintf( stderr, "%u", val );
}


inline void trace_format( const unsigned long &val )
{
   fprintf( stderr, "%lu", val );
}


inline void trace_format( const git_otype &type )
{
   fprintf( stderr, "%-6s", git_object_type2string( type ) );
}


inline void trace_format( const git_oid &oid )
{
   char  buf[41];
   git_oid_tostr( buf, 41, &oid );
   fprintf( stderr, "%s", buf );
}



inline void trace_args( )
{
}


inline void trace_args( const auto &arg0, const auto &...args )
{
   trace_format( arg0 );
   trace_args( args... );
}



extern bool trace_enable;



inline void trace( const auto &...args )
{
   if ( trace_enable ) [[unlikely]]
   {
      fprintf( stderr, "\033[95m" );

      trace_args( args... );

      fprintf( stderr, "\033[0m\n" );
   }
}



#define xcrypt_err( fmt, ... ) \
   ({ \
      fprintf( stderr, "\033[31mXCRYPT: " fmt "\033[0m\n", ##__VA_ARGS__ ); \
      fflush( stderr ); \
   })


[[noreturn]] void _xcrypt_abort( const char *, unsigned );

#define xcrypt_abort( fmt, ... ) \
   ({ \
      if constexpr ( fmt[0] ) \
         xcrypt_err( fmt, ##__VA_ARGS__ ); \
      _xcrypt_abort( __PRETTY_FUNCTION__, __LINE__ ); \
   })



#define ensure( cond ) \
   ({ \
      if ( !( cond ) ) \
         xcrypt_abort( "ensure failed: %s", #cond ); \
   })



#define  git_ensure( ret ) \
   if ( ret < 0 ) { \
      auto  e = giterr_last( ); \
      if ( e != nullptr )  \
         xcrypt_abort( "git error : no = %d, message = %s", e->klass, e->message ); \
   } else



#define ssl_ensure( ret ) \
   if ( ( ret ) == 0 ) \
   { \
      ERR_print_errors_fp( stderr ); \
      xcrypt_abort( "" ); \
   } else



bool get_line( FILE *, std::string & );
int  system( const std::list< std::string > & );



class Pipe
{
public:
   Pipe( const std::string &cmd )
   {
      _fp = popen( cmd.c_str( ), "r" );
      ensure( _fp != nullptr );
   }

   ~Pipe( )
   {
      if ( _fp != nullptr )
         pclose( _fp );
   }

   bool get_line( std::string &line )
   {
      return ::get_line( _fp, line );
   }

private:
   FILE        *_fp;
};



inline std::string & operator += ( std::string &str, const git_oid &oid )
{
   auto  sz = str.size( );
   str.resize( sz + 40 );
   git_oid_fmt( str.data( ) + sz, &oid );
   return str;
}



template < >
struct std::hash< git_oid >
{
   size_t operator ( ) ( const git_oid &oid ) const
   {
      return *reinterpret_cast< const size_t * >( oid.id );
   }
};



static bool operator == ( const git_oid &a, const git_oid &b )
{
   return git_oid_cmp( &a, &b ) == 0;
}



inline std::string_view to_sv( git_odb_object *obj )
{
   auto  data = static_cast< const char * >( git_odb_object_data( obj ) );
   auto  size = git_odb_object_size( obj );

   return std::string_view( data, size );
}



union alignas(16) Password
{
   struct
   {
      uint8_t  key[16];
      uint8_t  ivec[16];
   };

   uint8_t     md[32];
};

static_assert( sizeof( Password ) == 32 );

extern Password   pw;



class View
   : public std::string_view
{
public:
   View( const void *data, size_t size )
      : std::string_view( static_cast< const char * >( data ), size )
   { }
};



template < uint8_t *DATA, size_t SIZE >
class Output
{
public:
   Output( )
      : _out( reinterpret_cast< char * >( DATA ) )
   { }


   char * data( ) const
   {
      return reinterpret_cast< char * >( DATA );
   }


   char * end( ) const
   {
      return _out;
   }


   size_t size( ) const
   {
      return _out - data( );
   }


   size_t available( ) const
   {
      return SIZE - size( );
   }


   operator char * ( ) const
   {
      return _out;
   }


   char & operator []( size_t n )
   {
      return _out[n];
   }


   void append( const void *buff, size_t size )
   {
      memcpy( _out, buff, size );
      _out += size;
   }


   Output & operator << ( const char &ch )
   {
      *_out = ch;
      ++_out;
      return *this;
   }


   template< size_t N >
   Output & operator << ( const char (&str)[N] )
   {
      memcpy( _out, str, N - 1 );
      _out += N - 1;
      return *this;
   }


   Output & operator << ( const git_oid &oid )
   {
      git_oid_fmt( _out, &oid );
      _out += 40;
      return *this;
   }


   void operator += ( size_t n )
   {
      _out += n;
   }

private:
   char  *_out;
};



class Sha3Ctx
{
public:
   Sha3Ctx( )
   {
      _ctx = EVP_MD_CTX_new( );
      ensure( _ctx != nullptr );

      auto ret = EVP_DigestInit_ex( _ctx, EVP_sha3_256( ), nullptr );
      ssl_ensure( ret );
   }

   ~Sha3Ctx( )
   {
      EVP_MD_CTX_free( _ctx );
   }

   void update( const void *data, size_t size, const auto &...args )
   {
      auto  ret = EVP_DigestUpdate( _ctx, data, size );
      ssl_ensure( ret );

      if constexpr ( sizeof...( args ) > 0 )
         update( args... );
   }

   void digest( uint8_t (&md)[32] )
   {
      auto  ret = EVP_DigestFinal_ex( _ctx, md, nullptr );
      ssl_ensure( ret );
   }

private:
   EVP_MD_CTX  *_ctx;
};



inline void sha3_256( uint8_t (&md)[32], const void *data, size_t size )
{
   auto  ret = EVP_Digest( data, size, md, nullptr, EVP_sha3_256( ), nullptr );
   ssl_ensure( ret );
}



inline void sha3_256( uint8_t (&md)[32], const auto &...args ) requires ( sizeof...( args ) >= 4 )
{
   Sha3Ctx  ctx;
   ctx.update( args... );
   ctx.digest( md );
}



size_t aes_encrypt( uint8_t *, const uint8_t *, size_t );
size_t aes_decrypt( uint8_t *, const uint8_t *, size_t );



template < uint8_t *ARRAY = nullptr, size_t SIZE = 0 >
class Memory
   : public boost::noncopyable
{
public:
   Memory( size_t size )
   {
      ensure( size > 0 );

      if ( size <= SIZE )
         _memory = ARRAY;

      else
      {
         _memory = static_cast< uint8_t * >( ::malloc( size ) );
         ensure( _memory != nullptr );
      }
   }


   ~Memory( )
   {
      if ( _memory != ARRAY )
         ::free( _memory );
   }


   operator uint8_t * ( )
   {
      return _memory;
   }

private:
   uint8_t  *_memory;
};



struct Raw_oid
{
   Raw_oid & operator = ( const git_oid &oid )
   {
      memcpy( this->id, oid.id, GIT_OID_RAWSZ );

      this->id32[5] = 0;
      this->id32[6] = 0;
      this->id32[7] = 0;

      return *this;
   }

   git_oid & to( git_oid &oid ) const
   {
      git_oid_fromraw( &oid, this->id );
      return oid;
   }

private:
   union
   {
      uint8_t     id[32];
      uint32_t    id32[8];
   };
};

static_assert( sizeof( Raw_oid ) == 32 );



class GitStrArray
{
public:
   void push( std::string &&s )
   {
      _strs.emplace_back( std::move( s ) );
   }

   operator git_strarray * ( )
   {
      _vec.clear( );

      for ( auto &s : _strs )
         _vec.emplace_back( s.data( ) );

      _array.strings = _vec.data( );
      _array.count   = _vec.size( );

      return &_array;
   }

   auto & string_list( )
   {
      return _strs;
   }

private:
   std::list< std::string >   _strs;
   std::vector< char * >      _vec;
   git_strarray               _array{ };
};



using  Oid_set = std::unordered_set< git_oid >;
using  Oid_vec = std::vector< git_oid >;



void init_crypt( );
void encrypt( git_revwalk * );
void encrypt( git_oid & );
void decrypt( git_revwalk * );
void decrypt( git_oid & );


std::filesystem::path omp_path( );
void omp_load( );
std::pair< const git_oid, const git_oid > * omp_find( const git_oid & );
void omp_insert( const git_oid &, const git_oid & );
void omp_store( );



enum
{
   PROG_INIT,
   PROG_ENCRYPT,
   PROG_DECRYPT,
   PROG_ENUMERATE,
   PROG_COMPRESS,
   PROG_RECEIVE,
   PROG_UNPACK,
   PROG_WRITE,
   PROG_EXIT,
};


extern volatile uintmax_t  prog_num_1;
extern volatile uintmax_t  prog_num_2;


void progress_init( );
int  progress( unsigned, uintmax_t, uintmax_t );
void progress_end_line( );
void progress_exit( );



void repo_close( );
std::string get_secret_key_config_name( const char * );
void check_secret_key_format( const char * );
void load_remote( const char * );



int user_command( unsigned argc, char **argv );
int remote_helper( unsigned argc, char **argv );
