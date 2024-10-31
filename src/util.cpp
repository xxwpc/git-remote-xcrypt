#include "common.h"

#include <boost/process.hpp>



bool trace_enable;



[[noreturn]] void _xcrypt_abort( const char *func, unsigned line )
{
   progress_exit( );
   xcrypt_err( "Abort in function: %s, line: %u", func, line );
   abort( );
}



bool get_line( FILE *fp, std::string &line )
{
   constexpr size_t  BUF_SIZE = 80;

   char  buf[BUF_SIZE];

   bool  succ = false;

   line.clear( );

   while ( fgets( buf, BUF_SIZE, fp ) != nullptr )
   {
      succ = true;

      auto  n = strlen( buf );

      line.append( buf, n );

      if ( n < ( BUF_SIZE - 1 ) )
         break;

      if ( buf[BUF_SIZE-2] == '\n' )
         break;
   }

   if ( ( line.size( ) > 0 ) && ( line.back( ) == '\n' ) )
      line.pop_back( );

   return succ;
}



int  system( const std::list< std::string > &args )
{
   return boost::process::system( boost::process::args = args );
}
