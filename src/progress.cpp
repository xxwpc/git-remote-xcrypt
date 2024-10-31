#include "common.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>



static std::mutex               mutex;
static std::condition_variable  cv;
static std::thread              thread;
static volatile unsigned        prog_state;
static volatile unsigned        prog_end_line = 2;
       volatile uintmax_t       prog_num_1;
       volatile uintmax_t       prog_num_2;


struct Prog_attr
{
   const char  *tip;
   bool         no_done;
};


static constexpr Prog_attr prog_attrs[] =
{
   { ""                          },
   { "Encrypting objects",  true },
   { "Decrypting objects",  true },
   { "Enumerating objects", true },
   { "Compressing objects"       },
   { "Receiving objects"         },
   { "Unpacking objects"         },
   { "Writing objects"           },
};



static void progress_thread( )
{
   unsigned   last_state = PROG_INIT;
   uintmax_t  last_num_1 = 0;
   uintmax_t  last_num_2 = 0;
   unsigned   cur_state;
   uintmax_t  cur_num_1;
   uintmax_t  cur_num_2;


   std::unique_lock< std::mutex >  lock( mutex );

   while ( true )
   {
      cv.notify_one( );
      cv.wait_for( lock, std::chrono::milliseconds( 333 ) );

      cur_state = ::prog_state;
      cur_num_1 = ::prog_num_1;
      cur_num_2 = ::prog_num_2;

      if ( cur_state != last_state )
      {
         if ( cur_state == PROG_EXIT )
            break;

         last_state    = cur_state;
         prog_end_line = 0;
      }

      else if ( prog_end_line == 2 )
         continue;

      else if ( ( prog_end_line == 0 ) && ( cur_num_1 == last_num_1 ) && ( cur_num_2 == last_num_2 ) )
         continue;

      last_num_1 = cur_num_1;
      last_num_2 = cur_num_2;

      auto  tip = prog_attrs[cur_state].tip;

      switch ( cur_state )
      {
      case PROG_INIT:
         continue;

      case PROG_ENCRYPT:
      case PROG_DECRYPT:
         fprintf( stderr, "\r%s: %" PRIuMAX ", %" PRIuMAX, tip, cur_num_1, cur_num_2 );
         break;

      default:
         if ( prog_num_2 == 0 )
            fprintf( stderr, "\r%s: %" PRIuMAX, tip, prog_num_1 );

         else
         {
            fprintf( stderr, "\r%s: %u%% (%" PRIuMAX "/%" PRIuMAX ")", tip,
               static_cast< unsigned >( prog_num_1 * 100 / prog_num_2 ), prog_num_1, prog_num_2 );
         }

         if ( ( prog_num_1 == prog_num_2 ) && ( prog_num_2 != 0 ) )
            prog_end_line = 1;

         break;
      }

      if ( prog_end_line == 1 )
      {
         fputs( "\n", stderr );
         prog_end_line = 2;
      }

      fflush( stderr );
   }

   cv.notify_one( );
}



void progress_init( )
{
   assert( !thread.joinable( ) );

   thread = std::thread( &progress_thread );
}



int progress( unsigned new_state, uint64_t new_num_1, uint64_t new_num_2 )
{
   if ( new_state != prog_state )
      progress_end_line( );

   else if ( new_num_1 != new_num_2 )
   {
      prog_num_2 = new_num_2;
      prog_num_1 = new_num_1;

      return 0;
   }

   std::unique_lock< std::mutex >  lock( mutex );

   prog_state = new_state;
   prog_num_1 = new_num_1;
   prog_num_2 = new_num_2;

   cv.notify_one( );
   cv.wait( lock );

   return 0;
}



void progress_end_line( )
{
   std::unique_lock< std::mutex >  lock( mutex );

   if ( prog_end_line == 0 )
   {
      prog_end_line = 1;

      cv.notify_one( );
      cv.wait( lock );
   }

   assert( prog_end_line == 2 );
}



void progress_exit( )
{
   if ( !thread.joinable( ) )
      return;

   progress( PROG_EXIT, 0, 0 );

   thread.join( );
}
