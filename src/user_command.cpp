#include <libgen.h>
#include <unistd.h>

#include "common.h"



static void repo_open_cur_dir( )
{
   if ( repo != nullptr )
      return;

   auto  ret = git_repository_open_ext( &repo, ".", 0, nullptr );
   git_ensure( ret );

   git_dir = strdup( git_repository_path( repo ) );

   ret = git_repository_odb( &odb, repo );
   git_ensure( ret );
}



[[noreturn]] static void usage( )
{
   printf( "usage: %s <command> [<args>...]\n\n", grx_name );
   printf( "command:\n" );
   printf( "   add          add a remote\n" );
   printf( "   clear        clear cache files\n" );
   printf( "   clone        clone an encrypted repository\n" ),
//   printf( "   decrypt      decrypt a object\n" );
//   printf( "   encrypt      encrypt a object\n" );
//   printf( "   set          modify config\n" );

   _Exit( EXIT_FAILURE );
}



/**
 * 如果用户输入的 url 不以 xcrypt:: 开头, 自动添加 xcrypt:: 前缀
 */
static std::string get_xcrypt_url( const char *url )
{
   std::string  real_url = url;

   if ( !real_url.starts_with( "xcrypt::" ) )
      real_url.insert( 0, "xcrypt::" );

   return real_url;
}



static void copy_args( std::list< std::string > &args, char **argv, size_t f, size_t t )
{
   for ( size_t i = f; i < t; ++i )
      args.emplace_back( argv[i] );
}



static void set_secret_key( const char *remote_name, const char *key )
{
   git_config  *cfg;
   git_ensure( git_repository_config( &cfg, repo ) );

   auto  name = get_secret_key_config_name( remote_name );

   git_config_set_string( cfg, name.c_str( ), key );
   git_config_free( cfg );
}



static const char * get_git_option( size_t argc, char **argv, char short_opt, const char *long_opt, const char *default_value )
{
   for ( size_t i = 0; i < argc; ++i )
   {
      if ( argv[i][0] != '-' )
         continue;

      switch ( argv[i][1] )
      {
      case 0:
         goto def_val;

      case '-':
         if ( argv[i][2] == 0 )
            goto def_val;

         if ( strcmp( argv[i] + 2, long_opt ) == 0 )
         {
            if ( ( i + 1 ) < argc )
               return argv[i+1];

            goto def_val;
         }

         break;

      default:
         if ( argv[i][1] == short_opt )
         {
            if ( argv[i][2] != 0 )
               return &argv[i][2];

            if ( ( i + 1 ) < argc )
               return argv[i+1];

            goto def_val;
         }
      }
   }

def_val:
   return default_value;
}



static void remote_refs( const char *prefix )
{
   git_reference_iterator  *itr;

   std::string  glob = prefix;
   glob += remote_name;
   glob += "/*";

   auto ret = git_reference_iterator_glob_new( &itr, repo, glob.c_str( ) );
   git_ensure( ret );

   git_reference  *ref = nullptr;
   while ( git_reference_next( &ref, itr ) == 0 )
   {
      trace( "delete ref : ", git_reference_name( ref ) );
      git_reference_delete( ref );
      git_reference_free( ref );
      git_ensure( ret );
   }

   git_reference_iterator_free( itr );
}



static int do_add( unsigned argc, char **argv )
{
   if ( argc < 4 )
   {
      printf( "%s add <name> <url> <password> [<git remote add options>]\n", grx_name );
      _Exit( EXIT_FAILURE );
   }

   // 1. 检查输入格式
   check_secret_key_format( argv[3] );

   // 2. 调用 git remote add 添加远程
   std::list< std::string >   args = { "git", "remote", "add" };

   for ( unsigned i = 4; i < argc; ++i )
      args.emplace_back( argv[i] );

   args.emplace_back( argv[1] );
   args.emplace_back( get_xcrypt_url( argv[2] ) );

   auto  ret = system( args );

   if ( ret != 0 )
      _Exit( ret );

   // 3. 设置密钥
   repo_open_cur_dir( );
   set_secret_key( argv[1], argv[3] );

   return EXIT_SUCCESS;
}



static int do_clear( unsigned argc, char **argv )
{
   if ( argc == 0 )
   {
      fprintf( stderr, "git-remote-xcrypt clear <name>\n" );
      _Exit( EXIT_FAILURE );
   }

   repo_open_cur_dir( );

   remote_name = argv[1];
   auto  ret = git_remote_lookup( &remote, repo, remote_name );
   git_ensure( ret );

   // 删除 omp
   {
      std::error_code  ec;
      auto  path = omp_path( );
      trace( "delete omp : ", path );
      std::filesystem::remove( omp_path( ), ec );
      assert( ec == std::error_code( ) );
   }

   remote_refs( "refs/remotes/" );
   remote_refs( "refs/xcrypt/remotes/" );

   exit( EXIT_SUCCESS );
}



/**
 * 克隆一个加密仓库
 */
static int do_clone( unsigned argc, char **argv )
{
   if ( argc < 4 )
      goto usage;

   // 检查参数, 不能有 -o, --origin 参数
   for ( size_t i = 4; i < argc; ++i )
   {
      auto  arg = std::string_view( argv[i] );

      if ( arg.starts_with( "-o" ) || arg.starts_with( "--origin" ) )
      {
         xcrypt_err( "Can't use option '%s'", argv[i] );
         _Exit( EXIT_FAILURE );
      }
   }

   // 查找 --, 是否输入 dir 参数
   size_t   sep;
   for ( sep = 4; sep < argc; ++sep )
   {
      if ( strcmp( argv[sep], "--" ) == 0 )
      {
         ++sep;
         break;
      }
   }

   {
      const char  *remote_name = get_git_option( argc - 3, argv + 3, 'o', "origin", "origin" );

      // 检查密钥格式
      check_secret_key_format( argv[3] );

      // 构造 git clone 命令, 先通过 --config 设置密钥
      std::list< std::string >  args = { "git", "clone", "--config" };

      std::string key_cfg = get_secret_key_config_name( remote_name );
      key_cfg += '=';
      key_cfg += argv[3];

      args.emplace_back( std::move( key_cfg ) );

      // 通过 --origin 参数指定远程仓库名
      args.emplace_back( "--origin" );
      args.emplace_back( argv[1] );

      // 复制到 seq 前的所有参数
      copy_args( args, argv, 4, sep );

      // url
      args.emplace_back( get_xcrypt_url( argv[2] ) );

      // 复制 seq 之后的参数
      copy_args( args, argv, sep, argc );

      return system( args );
   }

usage:
   printf( "usage: %s clone <name> <url> <password> [<git clone options>] [-- <dir>]\n", grx_name );
   return EXIT_FAILURE;
}



static int do_crypt( unsigned argc, char **argv, void (*crypt)( git_oid & ) )
{
   assert( argc >= 3 );

   repo_open_cur_dir( );
   load_remote( argv[1] );

   git_object  *obj;
   auto  ret = git_revparse_single( &obj, repo, argv[2] );
   git_ensure( ret );

   git_oid  oid = *git_object_id( obj );

   print_oid( stdout, oid );
   printf( "\n" );;

   crypt( oid );

   print_oid( stdout, oid );
   printf( "\n" );;

   git_object_free( obj );

   return EXIT_SUCCESS;

usage:
   printf( "usage: git-remote-xcrypt encrypt <name> <object>\n" );
   return EXIT_FAILURE;
}




static int do_decrypt( unsigned argc, char **argv )
{
   return do_crypt( argc, argv, &decrypt );
}



static int do_encrypt( unsigned argc, char **argv )
{
   return do_crypt( argc, argv, &encrypt );
}



static int do_set( unsigned argc, char **argv )
{
   int         opt;
   const char *key_ptr = nullptr;
   const char *url_ptr = nullptr;

   while ( ( opt = getopt( argc - 1, argv + 1, "p:u:" ) ) != -1 )
   {
      switch ( opt )
      {
      case 'p':
         key_ptr = optarg;
         break;

      case 'u':
         url_ptr = optarg;
         break;

      default:
         printf( "%c\n", opt );
         _Exit( 0 );
      }
   }

   if ( ( optind + 1 ) < argc )
   {
      printf( "usage\n" );
      _Exit( 0 );
   }

   repo_open_cur_dir( );

   git_ensure( git_remote_lookup( &remote, repo, argv[1] ) );

   check_secret_key_format( key_ptr );

   if ( url_ptr != nullptr )
   {
      std::string    url = url_ptr;
      if ( !url.starts_with( "xcrypt::" ) )
         url.insert( 0, "xcrypt::" );

      git_ensure( git_remote_set_url( repo, argv[1], url.c_str( ) ) );
   }

   if ( key_ptr != nullptr )
      set_secret_key( argv[1], key_ptr );

   return 0;
}



using user_command_callback = int (*)( unsigned, char ** );



static constexpr std::pair< std::string_view, user_command_callback >  user_command_table[] =
{
   { "add",       &do_add     },
   { "clear",     &do_clear   },
   { "clone",     &do_clone   },
   { "decrypt",   &do_decrypt },
   { "encrypt",   &do_encrypt },
   { "set",       &do_set     },
};



static user_command_callback get_user_command_callback( const char *command )
{
   auto  itr = std::lower_bound( std::begin( user_command_table ), std::end( user_command_table ), command,
      []( auto &item, const char * cmd )
      {
         return item.first < cmd;
      } );

   if ( itr == std::end( user_command_table ) )
      return nullptr;

   if ( itr->first != command )
      return nullptr;

   return itr->second;
}



int user_command( unsigned argc, char **argv )
{
   if ( argc > 0 )
   {
      auto  cb = get_user_command_callback( argv[0] );
      if ( cb != nullptr )
         return cb( argc, argv );

      xcrypt_err( "unknown command '%s'", argv[0] );
   }

   usage( );
}
