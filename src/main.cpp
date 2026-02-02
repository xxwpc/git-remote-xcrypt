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

#include "common.h"

#include <libgen.h>
#include <signal.h>



// git-remote-xcrypt 的实际运行时的名称
const char *grx_name;



static void ctrl_c( int sig )
{
   progress_exit( );
   _Exit( EXIT_FAILURE );
}



static void install_ctrl_c( )
{
   struct sigaction action;

   action.sa_handler = ctrl_c;
   sigemptyset( &action.sa_mask );
   action.sa_flags = 0;

   sigaction( SIGINT, &action, nullptr );
}



static void check_trace( )
{
   auto  en = my_getenv( "XCRYPT_TRACE" );
   if ( en == nullptr )
      return;

   auto  v = atoi( en );
   trace_enable = !!v;
}



static void init_component( )
{
   check_trace( );

   auto  ret = OpenSSL_add_all_ciphers( );
   if ( ret != 1 )
   {
      ERR_print_errors_fp( stderr );
      exit( EXIT_FAILURE );
   }

   ret = git_libgit2_init( );
   if ( ret < 0 )
   {
      git_ensure( ret );
      exit( EXIT_FAILURE );
   }

   progress_init( );
   atexit( &progress_exit );

   install_ctrl_c( );

   init_crypt( );
}



static int proc_command( unsigned argc, char **argv )
{
   trace( argc, " params:" );
   for ( unsigned i = 0; i < ( unsigned )argc; ++i )
      trace( "   ", i, ": ", argv[i] );

   git_dir = my_getenv( "GIT_DIR" );
   if ( git_dir == nullptr )
      return user_command( argc - 1, argv + 1 );

   return remote_helper( argc, argv );
}



int main( int argc, char **argv )
try
{
   grx_name = basename( argv[0] );

   if ( my_getenv( "XCRYPT_DEBUG" ) != nullptr )
      sleep( 40 );

   init_component( );

   auto  ret = proc_command( argc, argv );

   repo_close( );

   return ret;
}
catch ( const std::exception &e )
{
   xcrypt_err( "EXCEPTION: %s", e.what( ) );
   return EXIT_FAILURE;
}
