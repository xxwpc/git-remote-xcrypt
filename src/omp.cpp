#include <fstream>
#include <unordered_map>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include "common.h"



struct omp_item
{
   Raw_oid  k;
   Raw_oid  v;
};

static_assert( sizeof( omp_item ) == 64 );



static std::unordered_map< git_oid, const git_oid >   omp;
static bool                                           modified;


static std::filesystem::path omp_dir( )
{
   std::filesystem::path  path = git_dir;
   path /= "xcrypt";
   return path;
}



std::filesystem::path omp_path( )
{
   auto path = omp_dir( );
   path /= remote_name;
   path.replace_extension( "omp" );
   return path;
}



void omp_load( )
{
   auto  path = omp_path( );

   try
   {
      boost::interprocess::file_mapping   file( path.c_str( ), boost::interprocess::read_only );
      boost::interprocess::mapped_region  rgn( file, boost::interprocess::read_only );

      auto  data = static_cast< const uint8_t * >( rgn.get_address( ) );
      auto  size = rgn.get_size( );

      if ( ( size % 64 ) != ( 32 + 16 ) )
         throw std::runtime_error( "omp length error" );

      //
      Memory   buff( size - 16 );
      auto  sz = aes_decrypt( buff, data, size );
      if ( sz != ( size - 16 ) )
         throw std::runtime_error( "omp context length error" );

      //
      size -= 48;
      uint8_t  md[32];
      sha3_256( md, buff, size );
      if ( memcmp( md, buff + size, 32 ) != 0 )
         throw std::runtime_error( "omp checksum error" );

      //
      git_oid    k;
      git_oid    v;
      omp_item  *itr = reinterpret_cast< omp_item * >( static_cast< uint8_t * >( buff ) );
      omp_item  *end = itr + ( size / 64 );

      for ( ; itr < end; ++itr )
      {
         itr->k.to( k );
         itr->v.to( v );
         omp_insert( k, v );
      }
   }
   catch ( const boost::interprocess::interprocess_exception &e )
   {
      if ( e.get_error_code( ) != boost::interprocess::not_found_error )
         throw std::runtime_error( "omp format error" );
   }
}



std::pair< const git_oid, const git_oid > * omp_find( const git_oid &id )
{
   auto  itr = omp.find( id );

   if ( itr == omp.end( ) )
      return nullptr;

   assert( odb != nullptr );
   if ( git_odb_exists( odb, &itr->second ) == 0 )
      return nullptr;

   return &*itr;
}



static void omp_insert_one( const git_oid &a, const git_oid &b )
{
   auto  rst = omp.emplace( a, b );

   if ( rst.second )
   {
      modified = true;
      return;
   }

   assert( git_oid_cmp( &rst.first->second, &b ) == 0 );
}



void omp_insert( const git_oid &a, const git_oid &b )
{
   omp_insert_one( a, b );
   omp_insert_one( b, a );
}



void omp_store( )
{
   if ( !modified )
      return;

   std::error_code  ec;
   std::filesystem::create_directory( omp_dir( ), ec );

   auto  db_path  = omp_path( );
   auto  tmp_path = db_path;
   tmp_path.replace_extension( "tmp" );

   Oid_set  set;
   Memory   buff( ( omp.size( ) / 2 * 64 ) + 32 + 16 );
   auto    *ptr = reinterpret_cast< omp_item * >( static_cast< uint8_t * >( buff ) );
   auto     end = ptr + ( omp.size( ) / 2 );

   for ( auto &kv : omp )
   {
      if ( set.contains( kv.first ) || ( set.contains( kv.second ) ) )
         continue;

      set.emplace( kv.first );
      set.emplace( kv.second );

      ptr->k = kv.first;
      ptr->v = kv.second;

      if ( ++ptr >= end )
         break;
   }

   size_t  size = reinterpret_cast< uint8_t * >( ptr ) - buff;
   sha3_256( reinterpret_cast< uint8_t (&)[32] >( *ptr ), buff, size );
   auto  sz = aes_encrypt( buff, buff, size + 32 );
   assert( sz == ( size + 32 + 16 ) );

   std::ofstream  os( tmp_path.c_str( ), std::ios_base::binary | std::ios_base::trunc );
   os.write( reinterpret_cast< char * >( static_cast< uint8_t * >( buff ) ), sz );
   os.flush( );

   if ( !os )
      xcrypt_err( "save omp db failed" );

   else
   {
      os.close( );

      std::filesystem::rename( tmp_path, db_path );
   }
}
